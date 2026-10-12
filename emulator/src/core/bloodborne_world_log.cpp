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

// What the lifecycle asks of the log, taken on the next kill step: the host's wipe starts a new
// log that keeps the members' kills; a new session or character one without them, and a member
// forgets its copy.
enum class LogRestart : int { None, KeepMembers, Clear };
std::atomic<int> host_restart{static_cast<int>(LogRestart::None)};
std::atomic<bool> member_reset{false};

[[maybe_unused]] const bool world_log_wipe_registered =
    RegisterLifecycleReset("world log (wipe)", {Lifecycle::Wipe}, [] {
        int none = static_cast<int>(LogRestart::None);
        host_restart.compare_exchange_strong(none, static_cast<int>(LogRestart::KeepMembers));
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

// The host's entries (or a heartbeat for none) to every room peer.
void SendEntries(const std::string& name, std::span<const WorldLogEntry> entries, s64 now_ms) {
    for (const WorldLogBatch& batch :
         BatchWorldLog(name, world_log_host.Epoch(), world_log_host.Last(), entries)) {
        SendToRoomPeers(EncodeCoopFrame(WorldLogEntriesType, EncodeWorldLogBatch(batch)));
    }
    entries_sent_ms = now_ms;
    if (!entries.empty()) {
        LOG_INFO(Debug, "Co-op world log: the host's log {:#x} took {}", world_log_host.Epoch(),
                 EntriesText(entries));
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

// The host: a new log when there is none or the lifecycle asked for one, its own kills as a
// proposal of its own, and a heartbeat every WorldLogResendMs.
void RunHostLog(const std::string& name, const WorldLogKills& mine, s64 now_ms) {
    const auto restart =
        static_cast<LogRestart>(host_restart.exchange(static_cast<int>(LogRestart::None)));
    if (!world_log_host.Started() || restart != LogRestart::None) {
        const bool keep = world_log_host.Started() && restart == LogRestart::KeepMembers;
        const u64 epoch = WorldLogEpoch(world_log_run, ++world_logs_started);
        TraceLog("host", "start", EncodeWorldLogStart(epoch, keep));
        LOG_INFO(Debug, "Co-op world log: the host starts log {:#x} ({})", epoch,
                 keep ? "a wipe: the members' kills stay" : "a new session");
        SendEntries(name, world_log_host.Start(epoch, keep), now_ms);
    }
    TakeProposal(name, {name, world_log_host.Epoch(), world_log_run, ++proposals_made, mine},
                 now_ms);
    if (now_ms - entries_sent_ms >= WorldLogResendMs) {
        SendEntries(name, {}, now_ms);
    }
}

// A member: its standing kills to the host at once on a change and every WorldLogResendMs, and a
// snapshot request when its copy fell behind.
void RunMemberLog(const std::string& name, const WorldLogKills& mine, s64 now_ms) {
    if (mine != last_proposed || now_ms - proposal_sent_ms >= WorldLogResendMs) {
        last_proposed = mine;
        proposal_sent_ms = now_ms;
        const WorldLogProposal proposal{name, world_log_member.Epoch(), world_log_run,
                                        ++proposals_made, mine};
        SendToRoomPeers(EncodeCoopFrame(WorldLogProposeType, EncodeWorldLogProposal(proposal)));
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
    return log;
}

// On the log path, under kill_board_mutex: the other members' kills from the log, with the board's
// for members outside it. The host posts the party's union to the board as its own (the fallback
// copy for a member whose link is down or who joins); a member whose copy is not fresh takes the
// board's whole.
std::unordered_set<u32> BoardKillsForTheLog(const std::string& name, s64 now_ms) {
    if (IsPartyGuestRole()) {
        if (!world_log_member.Fresh(now_ms)) {
            return party_kills.All();
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
        LOG_INFO(Debug, "Co-op world log: took the host's snapshot to seq {} ({} kills)",
                 world_log_member.Applied(), WorldLogKillCount(world_log_member.Kills()));
    }
}

void LogMemberStep(const WorldLogMember::Step& step, u64 host_last) {
    static bool behind = false;
    LogMemberStart(step);
    if (!step.applied.empty()) {
        LOG_INFO(Debug, "Co-op world log: applied {}", EntriesText(step.applied));
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
    if (type == WorldLogSnapshotRequestType && DecodeWorldLogSnapshotRequest(payload)) {
        for (const WorldLogSnapshot& part : world_log_host.Snapshot(name)) {
            SendToPeer(EncodeCoopFrame(WorldLogSnapshotType, EncodeWorldLogSnapshot(part)), addr,
                       port);
        }
    }
}

void TakeFrameAsMember(u8 type, std::span<const u8> payload, u32 addr, u16 port, s64 now_ms) {
    WorldLogMember::Step step;
    u64 host_last = 0;
    if (const auto batch =
            type == WorldLogEntriesType ? DecodeWorldLogBatch(payload) : std::nullopt) {
        step = world_log_member.Take(*batch, now_ms);
        host_last = batch->last;
    } else if (const auto part =
                   type == WorldLogSnapshotType ? DecodeWorldLogSnapshot(payload) : std::nullopt) {
        step = world_log_member.Take(*part, now_ms);
        host_last = part->last;
    } else {
        return;
    }
    if (!step.stale) {
        log_host_peer = std::pair{addr, port};
    }
    if (step.stale || step.new_epoch || step.snapshot || step.gap || !step.applied.empty()) {
        TraceLog("member", type == WorldLogEntriesType ? "entries" : "snapshot", payload);
    }
    LogMemberStep(step, host_last);
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

PartyKillsNow GatherPartyKills(const VisitKills& visit_kills, u32 map,
                               std::span<const std::pair<s32, bool>> changes) {
    const s64 now_ms = SteadyMilliseconds();
    const WorldLogKills mine = visit_kills.Standing();
    if (member_reset.exchange(false) && IsPartyGuestRole()) {
        world_log_member.Reset();
        log_host_peer.reset();
        TraceLog("member", "reset", {});
    }
    const bool log = DecideKillPath(mine, now_ms);
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
