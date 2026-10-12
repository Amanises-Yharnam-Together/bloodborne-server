// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The world log's runtime (docs/WORLD_SYNC.md, Stage C, step 1: the kills): which path the kills
// take (WorldLogPath), the host's log (WorldLogHost) and a member's copy (WorldLogMember), their
// frames on the P2P link, and the kill board underneath as the log's fallback copy. Everything
// here runs on the game's thread (the kill step and the frames it reads); the lifecycle resets
// only set flags. Shared declarations are in bloodborne_re_internal.h.

#include "core/bloodborne_party.h"
#include "core/bloodborne_re_internal.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include "common/logging/log.h"
#include "core/libraries/np/np_matching2/np_matching2_signaling.h"

namespace Core::Bloodborne {
namespace {

const bool world_log_switch = WorldLogEnabled(std::getenv("SHADPS4_BLOODBORNE_WORLD_LOG"));
// This game's run: the upper half of the host's log epochs, and the run of its proposals.
const u32 world_log_run = [] {
    std::random_device random;
    return static_cast<u32>(random()) | 1u;
}();

WorldLogPath world_log_path;
// The shown characters (step 2) take the log only with the kills, and only while every peer has
// WorldLogShownFeature (a step 1 build keeps them on the shown board).
WorldLogPath shown_path{"shown characters", "the shown board", WorldLogShownFeature};
bool shown_on_log = false;
// A guest's copy of the host's map state from the log (map_state_mutex), see HostMapStateNow.
std::optional<MemberMapState> log_map_state;
// The shared flags and the host's map state (step 3) take the log with the kills.
std::atomic<bool> flags_on_log{false};
WorldLogFlagOutbox flag_outbox; // this game's shared flag events, until taken
s64 flags_sent_ms = 0;
FlagEventOnce flag_once; // flag_board_mutex: an event from the log or the flag board, once
WorldLogHost world_log_host;
WorldLogMember world_log_member;
u32 world_logs_started = 0;
u32 proposals_made = 0;
s64 proposal_sent_ms = 0;
s64 entries_sent_ms = 0;
WorldLogKills last_proposed;
std::map<u64, PeerHello> peer_hellos;             // by sender
std::optional<std::pair<u32, u16>> log_host_peer; // where the host's frames come from
std::unordered_set<u32> board_posted;             // the host's union as on the kill board

// What the lifecycle asks of the log, taken on the next kill step: the host starts a new log with
// no kills on its wipe (marked, see WorldLogWipeHold), a new session or character; a member
// forgets its copy on a new session or character.
enum class LogRestart : int { None, Wipe, Clear };
std::atomic<int> host_restart{static_cast<int>(LogRestart::None)};
std::atomic<bool> member_reset{false};
// When this member last heard the host's wiped log and ran its own wipe (game thread).
WorldLogWipeHold wipe_hold;

[[maybe_unused]] const bool world_log_wipe_registered =
    RegisterLifecycleReset("world log (wipe)", {Lifecycle::Wipe}, [] {
        int none = static_cast<int>(LogRestart::None);
        host_restart.compare_exchange_strong(none, static_cast<int>(LogRestart::Wipe));
    });
[[maybe_unused]] const bool world_log_session_registered = RegisterLifecycleReset(
    "world log", {Lifecycle::Join, Lifecycle::Leave, Lifecycle::NewCharacter}, [] {
        host_restart.store(static_cast<int>(LogRestart::Clear));
        member_reset.store(true);
    });

std::string LogMemberName() {
    std::scoped_lock lock{vitals_mutex};
    return vitals_member;
}

void TraceLog(std::string_view role, std::string_view op, std::span<const u8> payload) {
    if (CoopTraceOn()) {
        Trace("wlog-in", WorldLogTraceFields(role, op, payload));
    }
}

std::string EntriesText(std::span<const WorldLogEntry> entries) {
    std::string text;
    for (size_t index = 0; index < entries.size() && index < 8; ++index) {
        text += (text.empty() ? "" : ", ") + FormatWorldLogEntry(entries[index]);
    }
    return entries.size() > 8 ? fmt::format("{}, ... ({} in all)", text, entries.size()) : text;
}

// Entries for the log: each kill entry; the shown characters' (which change as the host walks)
// as a count, on at most 400 lines a game.
std::optional<std::string> LoggedEntries(std::span<const WorldLogEntry> entries) {
    std::vector<WorldLogEntry> kills;
    std::ranges::copy_if(entries, std::back_inserter(kills), [](const WorldLogEntry& entry) {
        return entry.kind != WorldLogKind::Shown && entry.kind != WorldLogKind::Unshown;
    });
    static u32 shown_lines = 0;
    const size_t shown = entries.size() - kills.size();
    if (kills.empty() && (shown == 0 || ++shown_lines > 400)) {
        return std::nullopt;
    }
    return EntriesText(kills) + (shown > 0 ? fmt::format(" ({} shown changes)", shown) : "");
}

// The host's entries (or a heartbeat for none) to every room peer.
void SendEntries(const std::string& name, std::span<const WorldLogEntry> entries, s64 now_ms) {
    for (const WorldLogBatch& batch :
         BatchWorldLog(name, world_log_host.Epoch(), world_log_host.Last(), entries)) {
        SendToRoomPeers(EncodeCoopFrame(WorldLogEntriesType, EncodeWorldLogBatch(batch)));
    }
    entries_sent_ms = now_ms;
    if (const auto text = LoggedEntries(entries)) {
        LOG_INFO(Debug, "Co-op world log: the host's log {:#x} took {}", world_log_host.Epoch(),
                 *text);
    }
}

void TakeProposal(const std::string& name, const WorldLogProposal& proposal, s64 now_ms) {
    const WorldLogHost::Taken taken = world_log_host.Propose(proposal);
    if (taken.stale || !taken.entries.empty()) {
        TraceLog("host", "propose", EncodeWorldLogProposal(proposal));
    }
    if (!taken.entries.empty()) {
        SendEntries(name, taken.entries, now_ms);
    }
}

// Flag events the log took: set in this member's world (each once, whichever copy comes first);
// the host also posts them to the flag board, the copy for a member whose link is down.
void ApplyFlagEntries(std::span<const WorldLogEntry> entries, bool board_copy, s64 now_ms) {
    std::scoped_lock lock{flag_board_mutex};
    for (const WorldLogEntry& entry : entries) {
        if (entry.kind != WorldLogKind::FlagSet && entry.kind != WorldLogKind::FlagClear) {
            continue;
        }
        const s32 flag = static_cast<s32>(entry.subject);
        const bool on = entry.kind == WorldLogKind::FlagSet;
        if (flag_once.First(flag, on, now_ms)) {
            flags_to_set.emplace_back(flag, on);
        }
        if (board_copy) {
            flags_to_post.emplace_back(flag, on);
        }
    }
}

void TakeFlagProposal(const std::string& name, const WorldLogFlagProposal& proposal, s64 now_ms) {
    const WorldLogHost::Taken taken = world_log_host.ProposeFlags(proposal, now_ms);
    if (taken.stale || !taken.entries.empty()) {
        TraceLog("host", "propose-flags", EncodeWorldLogFlagProposal(proposal));
    }
    if (!taken.entries.empty()) {
        SendEntries(name, taken.entries, now_ms);
        ApplyFlagEntries(taken.entries, true, now_ms);
    }
}

// The host: a new log when there is none or the lifecycle asked for one, its own kills as a
// proposal of its own, and a heartbeat every WorldLogResendMs.
void RunHostLog(const std::string& name, const WorldLogKills& mine, s64 now_ms) {
    const auto restart =
        static_cast<LogRestart>(host_restart.exchange(static_cast<int>(LogRestart::None)));
    if (!world_log_host.Started() || restart != LogRestart::None) {
        const bool wipe = world_log_host.Started() && restart == LogRestart::Wipe;
        const u64 epoch = WorldLogEpoch(world_log_run, ++world_logs_started);
        TraceLog("host", "start", EncodeWorldLogStart(epoch, wipe));
        LOG_INFO(Debug, "Co-op world log: the host starts log {:#x} ({}, no kills stand)", epoch,
                 wipe ? "a wipe" : "a new session");
        SendEntries(name, world_log_host.Start(epoch, wipe), now_ms);
    }
    TakeProposal(name, {name, world_log_host.Epoch(), world_log_run, ++proposals_made, mine},
                 now_ms);
    if (auto events = flag_outbox.Pending(now_ms); !events.empty()) {
        TakeFlagProposal(name, {name, world_log_host.Epoch(), world_log_run, std::move(events)},
                         now_ms);
    }
    if (now_ms - entries_sent_ms >= WorldLogResendMs) {
        SendEntries(name, {}, now_ms);
    }
}

// A member: its standing kills to the host at once on a change and every WorldLogResendMs, and a
// snapshot request when its copy fell behind.
void RunMemberLog(const std::string& name, const WorldLogKills& mine, s64 now_ms) {
    static bool held = false;
    if (const bool holds = wipe_hold.HoldsProposals(now_ms); holds != held) {
        held = holds;
        LOG_INFO(Debug, "Co-op world log: {}",
                 held ? "the host's log started over after a wipe; this member's kills wait for "
                        "its own wipe"
                      : "proposing this member's kills again");
    }
    if (!held && (mine != last_proposed || now_ms - proposal_sent_ms >= WorldLogResendMs)) {
        last_proposed = mine;
        proposal_sent_ms = now_ms;
        const WorldLogProposal proposal{name, world_log_member.Epoch(), world_log_run,
                                        ++proposals_made, mine};
        SendToRoomPeers(EncodeCoopFrame(WorldLogProposeType, EncodeWorldLogProposal(proposal)));
    }
    // Its shared flag events: at once when one came, then again for a while.
    if (flag_outbox.TakeNew() || now_ms - flags_sent_ms >= WorldLogResendMs) {
        if (auto events = flag_outbox.Pending(now_ms); !events.empty()) {
            flags_sent_ms = now_ms;
            const WorldLogFlagProposal flags{name, world_log_member.Epoch(), world_log_run,
                                             std::move(events)};
            SendToRoomPeers(
                EncodeCoopFrame(WorldLogFlagsProposeType, EncodeWorldLogFlagProposal(flags)));
        }
    }
    if (log_host_peer && world_log_member.SnapshotDue(now_ms)) {
        const WorldLogSnapshotRequest request{name, world_log_member.Epoch(),
                                              world_log_member.Applied()};
        SendToPeer(
            EncodeCoopFrame(WorldLogSnapshotRequestType, EncodeWorldLogSnapshotRequest(request)),
            log_host_peer->first, log_host_peer->second);
        LOG_INFO(Debug, "Co-op world log: asked the host for a snapshot (log {:#x}, seq {})",
                 world_log_member.Epoch(), world_log_member.Applied());
    }
}

// The kills' path changed: the board takes back what this member posted (its next post asks since
// FreshStart). Back on the board this member posts its standing kills again; on the log the host
// posts the party's union there instead (see BoardKillsForTheLog).
void SwitchKillPath(bool log, const WorldLogKills& mine) {
    std::scoped_lock lock{kill_board_mutex};
    kill_board_fresh_start = true;
    kills_to_post.clear();
    board_posted.clear();
    if (log) {
        return;
    }
    for (const auto& [map, handles] : mine) {
        for (const u32 handle : handles) {
            kills_to_post.emplace_back(static_cast<s32>(handle), true);
        }
    }
}

// The shown characters' path: on the log only with the kills. Back on the board, the host's log
// stops carrying them (its next Show on the log starts with a ShownMap).
void DecideShownPath(std::span<const WorldLogPeer> peers, bool kills_on_log, s64 now_ms) {
    const bool log = shown_path.Decide(world_log_switch, peers, now_ms) && kills_on_log;
    if (const auto line = shown_path.TakeChange()) {
        LOG_INFO(Debug, "{}", *line);
        Trace("wlog-path", {{"what", "shown"},
                            {"path", shown_path.Active() ? "log" : "board"},
                            {"why", shown_path.Why()}});
    }
    if (!log && shown_on_log) {
        world_log_host.ForgetShown();
    }
    shown_on_log = log;
}

// Which path the kills take now (see WorldLogPath), from the room peers' hellos.
bool DecideKillPath(const WorldLogKills& mine, s64 now_ms) {
    std::vector<WorldLogPeer> peers;
    for (const auto& peer : Libraries::Np::NpMatching2::GetActiveRoomPeers()) {
        const auto hello = peer_hellos.find((u64{peer.addr} << 16) | peer.port);
        const bool known = hello != peer_hellos.end();
        peers.push_back({known && !hello->second.member.empty()
                             ? hello->second.member
                             : fmt::format("peer {:08x}:{}", peer.addr, peer.port),
                         known ? std::optional{hello->second} : std::nullopt});
    }
    const bool was = world_log_path.Active();
    const bool log = world_log_path.Decide(world_log_switch, peers, now_ms);
    if (const auto line = world_log_path.TakeChange()) {
        LOG_INFO(Debug, "{}", *line);
        Trace("wlog-path", {{"path", log ? "log" : "board"}, {"why", world_log_path.Why()}});
    }
    if (log != was) {
        SwitchKillPath(log, mine);
    }
    DecideShownPath(peers, log, now_ms);
    return log;
}

// On the log path, under kill_board_mutex: the other members' kills from the log, with the board's
// for members outside it. The host posts the party's union to the board as its own (the fallback
// copy for a member whose link is down or who joins); a member whose copy is not fresh takes the
// board's whole.
std::unordered_set<u32> BoardKillsForTheLog(const std::string& name, s64 now_ms) {
    if (IsPartyGuestRole()) {
        if (!world_log_member.Fresh(now_ms)) {
            // Right after a wipe the host's entries on the board are from before it (it posts
            // every 3 s): a member back from the wipe's load killed the respawned dog by them.
            return wipe_hold.BoardStale(now_ms) ? party_kills.AllExcept(world_log_member.Host())
                                                : party_kills.All();
        }
        std::unordered_set<u32> kills = WorldLogHandles(world_log_member.Kills());
        const auto board = party_kills.AllExcept(world_log_member.Host());
        kills.insert(board.begin(), board.end());
        return kills;
    }
    std::unordered_set<u32> kills = WorldLogHandles(world_log_host.Kills());
    for (const u32 handle : board_posted) {
        if (!kills.contains(handle)) {
            kills_to_post.emplace_back(static_cast<s32>(handle), false);
        }
    }
    for (const u32 handle : kills) {
        if (!board_posted.contains(handle)) {
            kills_to_post.emplace_back(static_cast<s32>(handle), true);
        }
    }
    board_posted = kills;
    const auto board = party_kills.AllExcept(name);
    kills.insert(board.begin(), board.end());
    return kills;
}

void LogMemberStart(const WorldLogMember::Step& step) {
    if (step.new_epoch) {
        LOG_INFO(Debug, "Co-op world log: following the host's log {:#x}",
                 world_log_member.Epoch());
    }
    if (step.snapshot) {
        LOG_INFO(Debug,
                 "Co-op world log: took the host's snapshot to seq {} ({} kills, shown "
                 "characters {})",
                 world_log_member.Applied(), WorldLogKillCount(world_log_member.Kills()),
                 world_log_member.ShownKnown()
                     ? fmt::format("{} on {:#x}", world_log_member.Shown().bits.size(),
                                   world_log_member.Shown().map)
                     : "not in it");
    }
}

void LogMemberStep(const WorldLogMember::Step& step, u64 host_last) {
    static bool behind = false;
    LogMemberStart(step);
    if (const auto text = LoggedEntries(step.applied)) {
        LOG_INFO(Debug, "Co-op world log: applied {}", *text);
    }
    if (step.gap != behind && !step.stale) {
        behind = step.gap;
        LOG_INFO(Debug, "Co-op world log: {} (has seq {}, the host {})",
                 behind ? "behind the host" : "in step again", world_log_member.Applied(),
                 host_last);
    }
}

void TakeFrameAsHost(u8 type, std::span<const u8> payload, u32 addr, u16 port, s64 now_ms) {
    const std::string name = LogMemberName();
    if (!world_log_host.Started() || name.empty()) {
        return;
    }
    if (type == WorldLogProposeType) {
        if (const auto proposal = DecodeWorldLogProposal(payload)) {
            TakeProposal(name, *proposal, now_ms);
        }
        return;
    }
    if (type == WorldLogFlagsProposeType) {
        if (const auto flags = DecodeWorldLogFlagProposal(payload)) {
            TakeFlagProposal(name, *flags, now_ms);
        }
        return;
    }
    if (type == WorldLogSnapshotRequestType && DecodeWorldLogSnapshotRequest(payload)) {
        for (const WorldLogSnapshot& part : world_log_host.Snapshot(name)) {
            SendToPeer(EncodeCoopFrame(WorldLogSnapshotType, EncodeWorldLogSnapshot(part)), addr,
                       port);
        }
        // The shown characters at the same seq, always (empty while they are on the board): a
        // member of this build waits for them (WorldLogMember::ExpectShown).
        for (const WorldLogShownSnapshot& part : world_log_host.ShownSnapshot(name)) {
            SendToPeer(
                EncodeCoopFrame(WorldLogShownSnapshotType, EncodeWorldLogShownSnapshot(part)), addr,
                port);
        }
        SendToPeer(
            EncodeCoopFrame(WorldLogMapStateSnapshotType,
                            EncodeWorldLogMapStateSnapshot(world_log_host.MapStateSnapshot(name))),
            addr, port);
    }
}

// A frame the member's log takes: the step, the host's last seq, and the trace's name for it.
struct MemberTake {
    WorldLogMember::Step step;
    u64 host_last = 0;
    const char* op = "";
};
std::optional<MemberTake> TakeAsMember(u8 type, std::span<const u8> payload, s64 now_ms) {
    if (type == WorldLogEntriesType) {
        const auto batch = DecodeWorldLogBatch(payload);
        return batch ? std::optional{MemberTake{world_log_member.Take(*batch, now_ms), batch->last,
                                                "entries"}}
                     : std::nullopt;
    }
    if (type == WorldLogSnapshotType) {
        const auto part = DecodeWorldLogSnapshot(payload);
        return part ? std::optional{MemberTake{world_log_member.Take(*part, now_ms), part->last,
                                               "snapshot"}}
                    : std::nullopt;
    }
    if (type == WorldLogShownSnapshotType) {
        const auto part = DecodeWorldLogShownSnapshot(payload);
        return part ? std::optional{MemberTake{world_log_member.Take(*part, now_ms), part->last,
                                               "shown-snapshot"}}
                    : std::nullopt;
    }
    if (type == WorldLogMapStateSnapshotType) {
        const auto part = DecodeWorldLogMapStateSnapshot(payload);
        return part ? std::optional{MemberTake{world_log_member.Take(*part, now_ms), part->last,
                                               "map-state-snapshot"}}
                    : std::nullopt;
    }
    return std::nullopt;
}

// Whether the host's snapshots carry the shown characters: its hello has WorldLogShownFeature.
void ExpectShownFrom(u32 addr, u16 port) {
    const auto hello = peer_hellos.find((u64{addr} << 16) | port);
    const bool expected =
        hello != peer_hellos.end() && (hello->second.features & WorldLogShownFeature) != 0;
    if (expected != world_log_member.ExpectsShown()) {
        world_log_member.ExpectShown(expected);
        TraceLog("member", "expect-shown", std::vector<u8>{static_cast<u8>(expected ? 1 : 0)});
    }
    // Every peer runs this build: the host sends the map state part too.
    if (!world_log_member.ExpectsMapState()) {
        world_log_member.ExpectMapState(true);
        TraceLog("member", "expect-map-state", std::vector<u8>{1});
    }
}

void TakeFrameAsMember(u8 type, std::span<const u8> payload, u32 addr, u16 port, s64 now_ms) {
    if (!IsWorldLogFrameType(type) || type == WorldLogProposeType ||
        type == WorldLogSnapshotRequestType) {
        return;
    }
    ExpectShownFrom(addr, port);
    const auto taken = TakeAsMember(type, payload, now_ms);
    if (!taken) {
        return;
    }
    const WorldLogMember::Step& step = taken->step;
    if (!step.stale) {
        log_host_peer = std::pair{addr, port};
    }
    if (StartsAfterAWipe(step.applied)) {
        wipe_hold.HostWiped(now_ms);
    }
    if (step.stale || step.new_epoch || step.snapshot || step.gap || !step.applied.empty()) {
        TraceLog("member", taken->op, payload);
    }
    ApplyFlagEntries(step.applied, false, now_ms);
    LogMemberStep(step, taken->host_last);
}

// The shared flags and the map state take the log with the kills. Leaving it, this game's events
// not taken yet go to the flag board, and the host's log stops carrying its map state.
void FollowFlagsPath(bool log, s64 now_ms) {
    if (flags_on_log.exchange(log) && !log) {
        world_log_host.ForgetMapState();
        std::scoped_lock lock{flag_board_mutex};
        for (const WorldLogFlagEvent& event : flag_outbox.Pending(now_ms)) {
            flags_to_post.emplace_back(event.flag, event.on);
        }
        flag_outbox.Clear(); // its numbers go on: the host took them by number
    }
    // What a guest's load takes of the host's map flags: the log's copy while it is whole and
    // fresh.
    std::optional<MemberMapState> copy;
    if (log && IsPartyGuestRole() && world_log_member.Fresh(now_ms) &&
        world_log_member.MapStateKnown() && world_log_member.MapState().map != 0) {
        copy = MemberMapState{world_log_member.MapState().map, world_log_member.MapState().bits};
    }
    std::scoped_lock lock{map_state_mutex};
    log_map_state = std::move(copy);
}

} // namespace

namespace Coop {

bool WorldLogSwitchOn() {
    return world_log_switch;
}

void NoteWorldLogHello(u64 sender, const PeerHello& hello) {
    peer_hellos[sender] = hello;
}

void TakeWorldLogFrame(u8 type, std::span<const u8> payload, u32 addr, u16 port) {
    const s64 now_ms = SteadyMilliseconds();
    if (IsPartyGuestRole()) {
        TakeFrameAsMember(type, payload, addr, port, now_ms);
    } else {
        TakeFrameAsHost(type, payload, addr, port, now_ms);
    }
}

void ShowOnTheLog(u32 map, std::span<const CharacterShown> characters) {
    const std::string name = LogMemberName();
    if (!shown_on_log || !world_log_host.Started() || name.empty() || map == 0) {
        return;
    }
    const std::vector<WorldLogEntry> entries = world_log_host.Show(map, characters);
    if (!entries.empty()) {
        TraceLog("host", "show", EncodeWorldLogShow(map, characters));
        SendEntries(name, entries, SteadyMilliseconds());
    }
}

std::optional<MemberMapState> HostShownNow() {
    if (shown_on_log && world_log_member.Fresh(SteadyMilliseconds()) &&
        world_log_member.ShownKnown() && world_log_member.Shown().map != 0) {
        const WorldLogShown& shown = world_log_member.Shown();
        return MemberMapState{shown.map, EncodeShownCharacters(WorldLogShownCharacters(shown))};
    }
    std::scoped_lock lock{shown_board_mutex};
    return host_shown;
}

bool TakeFlagsForTheLog(std::span<const std::pair<s32, bool>> shared) {
    if (!flags_on_log.load()) {
        return false;
    }
    flag_outbox.Add(shared, SteadyMilliseconds());
    return true;
}

void FilterBoardFlags(std::vector<std::pair<s32, bool>>& changes) {
    if (!flags_on_log.load()) {
        return;
    }
    const s64 now_ms = SteadyMilliseconds();
    std::erase_if(changes, [now_ms](const std::pair<s32, bool>& change) {
        return !flag_once.First(change.first, change.second, now_ms);
    });
}

void MapStateOnTheLog(u32 map, std::span<const u8> bits) {
    const std::string name = LogMemberName();
    if (!flags_on_log.load() || !world_log_host.Started() || name.empty()) {
        return;
    }
    const std::vector<WorldLogEntry> entries = world_log_host.MapState(map, bits);
    if (!entries.empty()) {
        TraceLog("host", "map-state", EncodeWorldLogMapStateInput(map, bits));
        SendEntries(name, entries, SteadyMilliseconds());
    }
}

std::optional<MemberMapState> HostMapStateNow() {
    std::scoped_lock lock{map_state_mutex};
    return log_map_state ? log_map_state : host_map_state;
}

PartyKillsNow GatherPartyKills(const VisitKills& visit_kills, u32 map,
                               std::span<const std::pair<s32, bool>> changes, bool forgot,
                               const std::unordered_set<u32>& not_mine) {
    const s64 now_ms = SteadyMilliseconds();
    if (forgot) {
        wipe_hold.OwnWipe(now_ms);
    }
    // What this member proposes: its standing kills, but not another member's kill that died here
    // by the game's kill (it counted as this member's own in its visit).
    WorldLogKills mine = visit_kills.Standing();
    for (auto& [on_map, handles] : mine) {
        std::erase_if(handles, [&](u32 handle) { return not_mine.contains(handle); });
    }
    std::erase_if(mine, [](const auto& entry) { return entry.second.empty(); });
    if (member_reset.exchange(false) && IsPartyGuestRole()) {
        world_log_member.Reset();
        log_host_peer.reset();
        TraceLog("member", "reset", {});
    }
    const bool log = DecideKillPath(mine, now_ms);
    FollowFlagsPath(log, now_ms);
    const std::string name = LogMemberName();
    if (log && !name.empty()) {
        if (IsPartyGuestRole()) {
            RunMemberLog(name, mine, now_ms);
        } else {
            RunHostLog(name, mine, now_ms);
        }
    }
    PartyKillsNow party;
    {
        std::scoped_lock lock{kill_board_mutex};
        if (!log) {
            kills_to_post.insert(kills_to_post.end(), changes.begin(), changes.end());
        }
        party.members = log ? BoardKillsForTheLog(name, now_ms) : others_kills;
    }
    // Its own kills too (a guest pulled into the host's world sees the host's copy alive), and its
    // earlier visit's kills here that stood while another member stayed.
    party.all = party.members;
    party.all.insert(visit_kills.Killed().begin(), visit_kills.Killed().end());
    const std::vector<u32> held = visit_kills.Held(map);
    party.all.insert(held.begin(), held.end());
    // On the board path, straight over the P2P link: this member's standing kills out, the peers'
    // in (the board brings the same a moment later, and to members who load in later).
    if (!log && InPartySession()) {
        std::vector<u32> standing(visit_kills.Killed().begin(), visit_kills.Killed().end());
        standing.insert(standing.end(), held.begin(), held.end());
        SendDirectKills(standing, !changes.empty());
        std::scoped_lock lock{direct_boards_mutex};
        const std::unordered_set<u32> direct = DirectKillHandles(direct_kills_inbox.Fresh(now_ms));
        party.all.insert(direct.begin(), direct.end());
    }
    return party;
}

} // namespace Coop

} // namespace Core::Bloodborne
