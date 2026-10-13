// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The co-op patch table (Sites) and its installer: code patches, hooks, checks. Split from
// bloodborne_re.cpp; the shared declarations are in bloodborne_re_internal.h.

#include "core/bloodborne_crash_dump.h"
#include "core/bloodborne_party.h"
#include "core/bloodborne_re.h"
#include "core/bloodborne_re_internal.h"
#include "core/bloodborne_write_watch.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include <fmt/ranges.h>
#include "common/logging/log.h"
#include "common/memory_patcher.h"
#include "common/path_util.h"
#include "common/singleton.h"
#include "core/cpu_patches.h"
#include "core/debugger.h"
#include "core/emulator_settings.h"
#include "core/file_format/psf.h"
#include "core/libraries/network/sockets.h"
#include "core/libraries/np/np_handler.h"
#include "core/libraries/np/np_matching2/np_matching2_signaling.h"
#include "core/libraries/np/np_trophy.h"
#include "core/libraries/system/userservice.h"
#include "core/memory.h"

namespace Core::Bloodborne {
namespace Coop {

bool negative_area_patch_installed{};
bool area_flag_patch_installed{};
bool responder_bell_area_patch_installed{};
bool responder_bell_common_patch_installed{};
bool active_bell_negative_area_patch_installed{};
bool active_bell_area_flag_patch_installed{};
bool responder_search_negative_area_patch_installed{};
bool sos_status_area_patch_installed{};
bool summon_candidate_area_patch_installed{};
bool summon_build_area_patch_installed{};
bool summon_build_world_state_patch_installed{};
bool summon_build_negative_event_patch_installed{};
bool stage_transition_keep_matching_patch_installed{};
bool world_teardown_hook_installed{};
bool ghost_death_revive_hook_installed{};
bool seamless_travel_hooks_installed{};
bool stop_request_hook_installed{};
bool host_death_respawn_hook_installed{};
bool summon_build_host_placement_hook_installed{};
bool cross_map_guest_handoff_hook_installed{};
bool summon_reload_state_hook_installed{};
bool deferred_summon_reload_hook_installed{};
bool healing_fountain_host_availability_hook_installed{};
bool channel_table_installed{};

bool EnvFlagEnabled(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' && std::string_view{value} != "0";
}

std::string BytesToHex(std::span<const u8> bytes) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const u8 byte : bytes) {
        out << std::setw(2) << static_cast<u32>(byte);
    }
    return out.str();
}

// What the co-op install did with each feature (see CoopFeatureReport), logged in one line at its
// end.
CoopFeatureReport coop_features;

} // namespace Coop

namespace {

void LogCoopFeatures() {
    if (coop_features.SkippedRequired().empty()) {
        LOG_INFO(Debug, "{}", coop_features.Summary());
    } else {
        LOG_ERROR(Debug, "{}", coop_features.Summary());
    }
}

std::span<const u8> EbootImage(u64 base, u64 image_size) {
    return {reinterpret_cast<const u8*>(base), static_cast<size_t>(image_size)};
}

// Whether every site of `feature` holds the bytes the co-op site table has for it; if not, logs
// the first that does not, with what the player loses, and reports the feature skipped.
bool CoopSitesMatch(u64 base, u64 image_size, std::string_view feature,
                    std::string_view consequence) {
    const CoopSite* site = FirstMismatchedCoopSite(EbootImage(base, image_size), feature);
    if (site == nullptr) {
        return true;
    }
    const size_t size = HexBytes(site->original).size();
    const auto actual = site->offset <= image_size && size <= image_size - site->offset
                            ? EbootImage(base, image_size).subspan(site->offset, size)
                            : std::span<const u8>{};
    LOG_ERROR(Debug, "Bloodborne {} signature mismatch at {:#x} ({}): expected={} actual={}; {}",
              feature, site->offset, site->name, site->original, BytesToHex(actual), consequence);
    coop_features.Skipped(feature, fmt::format("signature mismatch at {:#x}", site->offset));
    return false;
}

} // namespace

namespace Coop {

// The first site of `feature` the game does not hold as the table has it, as a reason.
std::string FeatureMismatch(u64 base, u64 image_size, std::string_view feature) {
    const CoopSite* site = FirstMismatchedCoopSite(EbootImage(base, image_size), feature);
    return site != nullptr ? fmt::format("signature mismatch at {:#x}", site->offset)
                           : std::string{"not found"};
}

// Why a hook at `offset` did not go in: the game's bytes there, or the hook itself.
std::string SiteFailure(u64 base, u64 image_size, u64 offset) {
    const std::span<const u8> image = EbootImage(base, image_size);
    for (const CoopSite& site : CoopSites) {
        const size_t size = HexBytes(site.original).size();
        if (site.offset == offset && site.kind == CoopSiteKind::Hook &&
            (offset > image.size() || size > image.size() - offset ||
             !MatchesCoopSite(image.subspan(offset, size), site))) {
            return fmt::format("signature mismatch at {:#x}", offset);
        }
    }
    return fmt::format("hook failed at {:#x}", offset);
}

} // namespace Coop

namespace {

void WriteCoopPatches(u64 base, std::string_view feature) {
    for (const CodePatch& patch : CoopPatchesOf(feature)) {
        std::memcpy(reinterpret_cast<void*>(base + patch.offset), patch.patched.data(),
                    patch.patched.size());
    }
}

// The guest code hooks of co-op, each checked against the co-op site table first: the launcher
// warns about game patches over the sites it lists, so a hook missing from it is a bug (logged,
// and still installed).
void CheckCoopHookRegistered(const void* address, std::span<const u8> bytes) {
    const u64 offset = reinterpret_cast<u64>(address) - MemoryPatcher::g_eboot_address;
    if (!IsRegisteredCoopHook(offset, bytes)) {
        LOG_ERROR(Debug, "Bloodborne hook at {:#x} ({} bytes) is not in the co-op site table",
                  offset, bytes.size());
    }
}

} // namespace

namespace Coop {

bool InstallCoopHook(void* address, std::span<const u8> bytes, u64 tag, GuestCodeHook hook) {
    CheckCoopHookRegistered(address, bytes);
    return InstallGuestCodeHook(address, bytes, tag, hook);
}

bool InstallCoopHook(void* address, std::span<const u8> bytes, u64 tag,
                     GuestFunctionOverride hook) {
    CheckCoopHookRegistered(address, bytes);
    return InstallGuestFunctionOverride(address, bytes, tag, hook);
}

bool InstallCoopHook(void* address, std::span<const u8> bytes, u64 tag,
                     GuestRegisterEditHook hook) {
    CheckCoopHookRegistered(address, bytes);
    return InstallGuestRegisterEditHook(address, bytes, tag, hook);
}

} // namespace Coop

namespace {

bool MatchesImage(u64 base, u64 image_size, const CodePatch& patch) {
    return patch.offset <= image_size && patch.original.size() <= image_size - patch.offset &&
           std::ranges::equal(std::span<const u8>{reinterpret_cast<const u8*>(base + patch.offset),
                                                  patch.original.size()},
                              patch.original);
}

// Deaths that keep the party: a dead guest revives in the host's world instead of being sent
// home. Optional and all-or-nothing: the byte patches alone would leave the guest dead.
void InstallGhostDeathRevive(u64 base, u64 image_size) {
    const auto& patches = GhostDeathKeepSessionPatches();
    for (const auto& patch : patches) {
        if (!MatchesImage(base, image_size, patch)) {
            LOG_ERROR(Debug,
                      "Bloodborne ghost-death signature mismatch at {:#x}; deaths still send "
                      "guests home",
                      patch.offset);
            coop_features.Skipped("ghost death",
                                  fmt::format("signature mismatch at {:#x}", patch.offset));
            return;
        }
    }
    const auto site = std::ranges::find_if(
        Sites, [](const TraceSite& site) { return site.offset == GhostDeathReviveOffset; });
    if (site == Sites.end() ||
        !InstallCoopHook(reinterpret_cast<void*>(base + GhostDeathReviveOffset),
                         std::span<const u8>{site->prologue.data(), site->prologue_size},
                         static_cast<size_t>(std::distance(Sites.begin(), site)), TraceEntry)) {
        LOG_ERROR(Debug, "Failed to install Bloodborne ghost-death revive hook at {:#x}",
                  GhostDeathReviveOffset);
        coop_features.Skipped("ghost death", SiteFailure(base, image_size, GhostDeathReviveOffset));
        return;
    }
    ghost_death_revive_hook_installed = true;
    WriteCoopPatches(base, "ghost death");
    coop_features.On("ghost death");
    // Optional: without it the guest's stain is still dropped, it just can't pick it up.
    if (CoopSitesMatch(base, image_size, "guest bloodstain",
                       "a guest cannot pick up its own bloodstain")) {
        WriteCoopPatches(base, "guest bloodstain");
        coop_features.On("guest bloodstain");
    }
    LOG_INFO(Debug, "Bloodborne ghost-death patch: party guests revive in the host's world");
}

// See NoEchoTheftPatches: no enemy steals a bloodstain, in or out of a party.
void InstallNoEchoTheft(u64 base, u64 image_size) {
    if (!CoopSitesMatch(base, image_size, "no echo theft", "enemies steal bloodstains")) {
        return;
    }
    WriteCoopPatches(base, "no echo theft");
    coop_features.On("no echo theft");
    LOG_INFO(Debug, "Bloodborne no echo theft: enemies leave bloodstains where they are");
}

// A host death with a party revives the host like a guest (no world reload) and the guests skip
// HostDead's warp home. All-or-nothing: guests that stay with a host that reloads its world
// would be stranded.
void InstallHostDeathKeepsGuests(u64 base, u64 image_size) {
    if (!world_teardown_hook_installed) {
        LOG_ERROR(Debug, "Bloodborne host-death patch unavailable; host deaths send guests home");
        coop_features.Skipped("host death", "needs world teardown");
        return;
    }
    if (!CoopSitesMatch(base, image_size, "host death", "host deaths send guests home")) {
        return;
    }
    const auto site = std::ranges::find_if(
        Sites, [](const TraceSite& site) { return site.offset == SoloPlayDeathRespawnOffset; });
    if (site == Sites.end() ||
        !InstallCoopHook(reinterpret_cast<void*>(base + SoloPlayDeathRespawnOffset),
                         std::span<const u8>{site->prologue.data(), site->prologue_size},
                         static_cast<size_t>(std::distance(Sites.begin(), site)),
                         OverrideHostDeathRespawn)) {
        LOG_ERROR(Debug, "Failed to install Bloodborne host-death respawn hook at {:#x}",
                  SoloPlayDeathRespawnOffset);
        coop_features.Skipped("host death",
                              SiteFailure(base, image_size, SoloPlayDeathRespawnOffset));
        return;
    }
    host_death_respawn_hook_installed = true;
    WriteCoopPatches(base, "host death");
    coop_features.On("host death");
    LOG_INFO(Debug, "Bloodborne host-death patch: a host with a party revives without a reload");
}

// A boss kill keeps the party together: nobody leaves the session and the guest is not warped
// home. All-or-nothing: half of it would leave a guest in a session its host already left.
void InstallBossClearKeepsSession(u64 base, u64 image_size) {
    if (!CoopSitesMatch(base, image_size, "boss clear", "boss kills end sessions")) {
        return;
    }
    WriteCoopPatches(base, "boss clear");
    coop_features.On("boss clear");
    LOG_INFO(Debug, "Bloodborne boss-clear patch: a boss kill keeps the session");
}

// A party joins in any area, whatever the player's own world has opened (see JoinAnywherePatches).
void InstallJoinAnywhere(u64 base, u64 image_size) {
    if (!party_controller) {
        return;
    }
    if (!CoopSitesMatch(base, image_size, "join anywhere", "closed areas stay closed")) {
        return;
    }
    WriteCoopPatches(base, "join anywhere");
    coop_features.On("join anywhere");
    LOG_INFO(Debug, "Bloodborne join-anywhere patch: a party joins in any area");
}

// See DeathFlagSites: logs a big character's death and the game's call chain to it.
void PS4_SYSV_ABI TraceCharacterDeath(u64 tag, GuestRegisterSnapshot* registers) {
    const auto& sites = DeathFlagSites();
    if (tag >= sites.size()) {
        return;
    }
    const DeathFlagSite& site = sites[tag];
    const u64 character = site.character == DeathRegister::Rbx   ? registers->rbx
                          : site.character == DeathRegister::Rcx ? registers->rcx
                                                                 : registers->r15;
    const u64 modules = ReadValue<u64>(character, 0x3B0);
    const u64 data = modules >= 0x10000 ? ReadValue<u64>(modules, 0x20) : 0;
    const s32 max_hp = data >= 0x10000 ? ReadValue<s32>(data, 0xFC) : 0;
    if (!TracesDeathOf(max_hp)) {
        return;
    }
    LOG_INFO(Debug,
             "Bloodborne boss death: {:#x} ({}) hp {}/{} flags {:#x} map {:#x} in session {}",
             ReadValue<u32>(character, 0x08), site.name, ReadValue<s32>(data, 0xF8), max_hp,
             ReadValue<u64>(character, 0x1E0), CurrentPlace().map, InPartySession());
    LogGameCallChainAt("boss death", registers->rsp);
}

void InstallBossDeathTracer(u64 base, u64 image_size) {
    if (!party_controller) {
        return;
    }
    const auto& sites = DeathFlagSites();
    for (u64 index = 0; index < sites.size(); ++index) {
        const DeathFlagSite& site = sites[index];
        if (site.offset + site.instruction.size() > image_size ||
            !InstallCoopHook(reinterpret_cast<void*>(base + site.offset), site.instruction, index,
                             TraceCharacterDeath)) {
            LOG_ERROR(Debug, "Bloodborne boss-death tracer unavailable at {:#x}", site.offset);
            coop_features.Skipped("boss death tracer", SiteFailure(base, image_size, site.offset));
            return;
        }
    }
    coop_features.On("boss death tracer");
    LOG_INFO(Debug, "Bloodborne boss-death tracer: big characters' deaths are logged with their "
                    "call chain");
}

// See PacketTraceMode: the game's own packets on the co-op trace, nothing changed.
PacketTraceMode packet_trace_mode = PacketTraceMode::Off;

// A member as the trace names it: its online id where its identity keeps it (the SceNpId, +0x36),
// else the identity's address. Looked up once per identity.
std::string TracedPeer(u64 identity) {
    static std::mutex mutex;
    static std::unordered_map<u64, std::string> names;
    std::scoped_lock lock{mutex};
    const auto [it, added] = names.try_emplace(identity);
    if (added) {
        std::optional<std::string> id;
        if (identity >= 0x10000 && HasMemoryAccess(identity + 0x36, 17, MemoryProt::CpuRead)) {
            id = NpOnlineIdAt({reinterpret_cast<const u8*>(identity + 0x36), 17});
        }
        it->second = id.value_or(fmt::format("{:x}", identity));
    }
    return it->second;
}

void TraceNativePacket(bool sent, u32 channel, u64 member_id, u64 payload, u32 size) {
    NativePacketRecord record{sent, channel, size, TracedPeer(ReadValue<u64>(member_id, 0)),
                              GetCurrentPackedMap()};
    const u64 net_sync = ReadValue<u64>(image_base + SummonSessionRulesPointerOffset, 0);
    const u64 session = GetMatchingState();
    record.sync_state = net_sync >= 0x10000 ? ReadValue<s32>(net_sync, 0x84) : -1;
    record.matching_state = session >= 0x10000 ? ReadValue<s32>(session, MatchingStateOffset) : -1;
    const std::size_t shown = TracedPayloadBytes(packet_trace_mode, channel, size);
    if (shown > 0 && payload >= 0x10000 && HasMemoryAccess(payload, shown, MemoryProt::CpuRead)) {
        const auto* bytes = reinterpret_cast<const u8*>(payload);
        record.payload.assign(bytes, bytes + shown);
    }
    Trace(NativePacketTraceKind(sent), NativePacketFields(record));
}

// 0x0178970D in the send (0x017894E0), after its checks, as it hands the packet to the
// transport: rcx the packet (the static buffer 0x055640F0), rdx the member id.
void PS4_SYSV_ABI TracePacketSent(u64, const GuestRegisterSnapshot* registers) {
    const u64 packet = registers->rcx;
    if (packet < 0x10000 || !HasMemoryAccess(packet, 8, MemoryProt::CpuRead)) {
        return;
    }
    const u32 size = ReadValue<u32>(packet, 4);
    TraceNativePacket(true, ReadValue<u32>(packet, 0), registers->rdx, packet + 8, size);
}

// A whole packet queued for its member (0x01790C20, rbx the member's queues, their member id at
// +0x08): reliable ones (0x01790E0E) once reassembled, channel rax, the node {u32 size, payload*}
// at +0x68; unreliable ones (0x01790FCA) channel [rbp - 0x40], node [rbp - 0x38].
void PS4_SYSV_ABI TracePacketReceived(u64 tag, const GuestRegisterSnapshot* registers) {
    const bool reliable = tag == 0;
    const u64 queues = registers->rbx;
    const u32 channel =
        reliable ? static_cast<u32>(registers->rax) : ReadValue<u32>(registers->rbp - 0x40, 0);
    const u64 node =
        reliable ? ReadValue<u64>(queues, 0x68) : ReadValue<u64>(registers->rbp - 0x38, 0);
    if (node < 0x10000) {
        return;
    }
    TraceNativePacket(false, channel, queues + 0x08, ReadValue<u64>(node, 0x08),
                      ReadValue<u32>(node, 0x00));
}

void InstallNativePacketTrace(u64 base, u64 image_size) {
    packet_trace_mode = ParsePacketTraceMode(std::getenv("SHADPS4_BLOODBORNE_PACKET_TRACE"));
    if (packet_trace_mode == PacketTraceMode::Off || !CoopTraceOn()) {
        return;
    }
    if (!CoopSitesMatch(base, image_size, "native packet trace", "game packets are not traced")) {
        return;
    }
    const bool installed =
        InstallCoopHook(reinterpret_cast<void*>(base + PacketSendTraceOffset), PacketSendTrace, 0,
                        TracePacketSent) &&
        InstallCoopHook(reinterpret_cast<void*>(base + ReliablePacketQueuedOffset),
                        ReliablePacketQueued, 0, TracePacketReceived) &&
        InstallCoopHook(reinterpret_cast<void*>(base + UnreliablePacketQueuedOffset),
                        UnreliablePacketQueued, 1, TracePacketReceived);
    if (!installed) {
        coop_features.Skipped("native packet trace", "a hook did not go in");
        return;
    }
    coop_features.On("native packet trace");
    LOG_INFO(Debug, "Bloodborne native packet trace: on ({})",
             packet_trace_mode == PacketTraceMode::Full ? "every payload" : "unknown payloads");
}

// See ChannelPolicy: the game's packets decided per channel at the send and at both receive paths.
// Members are found on the map board by their online id; a board this old knows no map.
constexpr auto ChannelTableBoardAge = std::chrono::seconds{5};

// The map board's map of a member (`member_id` the game's {identity*, lock}), or nothing.
std::optional<u32> BoardMapOf(u64 member_id) {
    const u64 identity = member_id >= 0x10000 ? ReadValue<u64>(member_id, 0) : 0;
    if (identity < 0x10000 || !HasMemoryAccess(identity + 0x36, 17, MemoryProt::CpuRead)) {
        return std::nullopt;
    }
    const auto online_id = NpOnlineIdAt({reinterpret_cast<const u8*>(identity + 0x36), 17});
    if (!online_id) {
        return std::nullopt;
    }
    const std::string member = BoardMemberOfOnlineId(*online_id);
    std::scoped_lock lock{map_board_mutex};
    if (std::chrono::steady_clock::now() - map_board_time > ChannelTableBoardAge) {
        return std::nullopt;
    }
    const auto entry = std::ranges::find(map_board, member, &MemberMap::member);
    return entry != map_board.end() ? std::optional{entry->map} : std::nullopt;
}

// Whether the game keeps a packet; a dropped one is logged once per channel and way, and put on
// the co-op trace ("net-drop") while the packet trace runs.
bool KeepsPacket(u32 channel, PacketWay way, u64 member_id) {
    if (channel >= NativeChannelCount) {
        return true;
    }
    const ChannelPolicy policy = NativeChannelPolicy(channel);
    if (policy == ChannelPolicy::Pass) {
        return true;
    }
    const u32 own_map = PlayingMap();
    const std::optional<u32> peer_map =
        policy == ChannelPolicy::SameMap ? BoardMapOf(member_id) : std::nullopt;
    if (KeepsNativePacket(channel, way, own_map, peer_map)) {
        return true;
    }
    static std::array<std::array<std::atomic<bool>, NativeChannelCount>, 2> logged{};
    if (!logged[static_cast<size_t>(way)][channel].exchange(true)) {
        LOG_INFO(Debug, "Bloodborne channel table: first {} packet on channel {:#x} dropped ({})",
                 way == PacketWay::Out ? "sent" : "received", channel, ChannelPolicyName(policy));
    }
    if (packet_trace_mode != PacketTraceMode::Off) {
        Trace("net-drop", {{"ch", fmt::format("{:02x}", channel)},
                           {"way", way == PacketWay::Out ? "out" : "in"},
                           {"policy", std::string{ChannelPolicyName(policy)}},
                           {"map", fmt::format("{:x}", own_map)},
                           {"peer-map", peer_map ? fmt::format("{:x}", *peer_map) : "-"}});
    }
    return false;
}

// The send's entry (0x017894E0: rdi the net sync, rsi the member id, edx the channel): true
// returns 0, "not sent", as the game's own checks do.
bool PS4_SYSV_ABI DecidePacketSend(u64, const GuestRegisterSnapshot* registers) {
    return !KeepsPacket(static_cast<u32>(registers->rdx), PacketWay::Out, registers->rsi);
}

// The receive's channel checks in 0x01790C20 (r8 / rbx the member's queues, their member id at
// +0x08): reliable (tag 0) before `movsxd rax, [rbx + 0x64]`, so the channel is rewritten there;
// unreliable (tag 1) after `mov eax, [rdx]`, so eax is. DroppedNativeChannel makes the game's own
// `cmp ..., 0x43; ja` drop it.
void PS4_SYSV_ABI DecidePacketReceived(u64 tag, GuestRegisterSnapshot* registers) {
    if (tag == 0) {
        const u64 queues = registers->rbx;
        const u32 channel = ReadValue<u32>(queues, 0x64);
        if (!KeepsPacket(channel, PacketWay::In, queues + 0x08)) {
            WriteValue(queues, 0x64, DroppedNativeChannel);
        }
        return;
    }
    const u32 channel = static_cast<u32>(registers->rax);
    if (!KeepsPacket(channel, PacketWay::In, registers->r8 + 0x08)) {
        registers->rax = DroppedNativeChannel;
    }
}

void InstallChannelTable(u64 base, u64 image_size) {
#ifdef BLOODBORNE_NATIVE_PORT
    const bool wanted = party_controller.has_value() &&
                        ChannelTableEnabled(std::getenv("SHADPS4_BLOODBORNE_CHANNEL_TABLE"));
#else
    // The port only (docs/SERVER_COOP_PLAN.md); shadPS4 keeps the old path.
    const bool wanted = false;
#endif
    if (!wanted) {
        return;
    }
    if (!CoopSitesMatch(base, image_size, "channel table", "packets keep the old path")) {
        return;
    }
    const bool installed =
        InstallCoopHook(reinterpret_cast<void*>(base + PacketSendOffset), PacketSendPrologue, 0,
                        DecidePacketSend) &&
        InstallCoopHook(reinterpret_cast<void*>(base + ReliableChannelCheckOffset),
                        ReliableChannelCheck, 0, DecidePacketReceived) &&
        InstallCoopHook(reinterpret_cast<void*>(base + UnreliableChannelCheckOffset),
                        UnreliableChannelCheck, 1, DecidePacketReceived);
    if (!installed) {
        coop_features.Skipped("channel table", "a hook did not go in");
        return;
    }
    channel_table_installed = true;
    coop_features.On("channel table");
    LOG_INFO(Debug, "Bloodborne channel table: on (map items, the host's door corrections and the "
                    "boss kick dropped; players, enemies, events and objects only on one map)");
}

// See SaveBackups: a thread of its own copies the saves a minute after the start, then every few
// minutes, and keeps the newest few.
void StartSaveBackups() {
    const char* setting = std::getenv("SHADPS4_BLOODBORNE_SAVE_BACKUPS");
    const auto backups = ParseSaveBackups(setting != nullptr ? setting : "");
    if (!backups) {
        LOG_INFO(Debug, "Bloodborne save backups are off");
        return;
    }
    const std::filesystem::path user_dir = Common::FS::GetUserPath(Common::FS::PathType::UserDir);
    std::thread([user_dir, backups = *backups] {
        std::this_thread::sleep_for(std::chrono::minutes{1});
        while (true) {
            if (const auto made = BackUpSaves(user_dir, SaveBackupName(LocalTimeNow()))) {
                LOG_INFO(Debug, "Bloodborne save backup: {} ({} older ones removed)",
                         Common::FS::PathToUTF8String(*made),
                         PruneSaveBackups(user_dir, backups.kept));
            }
            std::this_thread::sleep_for(backups.every);
        }
    }).detach();
    LOG_INFO(Debug, "Bloodborne save backups: every {} min into {}, the newest {} kept",
             backups->every.count(), Common::FS::PathToUTF8String(user_dir / SaveBackupsFolder),
             backups->kept);
}

// The game frees a released Scaleform movie player 5 frames later (0x01F60620's delayed delete),
// and the port's render thread can still have a draw of it queued then (New Game crashed on
// GXRenderThread at eboot+0x1F591B0, Kaniel 2026-10-13): 60 frames.
void InstallMoviePlayerDeleteDelay(u64 base, u64 image_size) {
    if (!CoopSitesMatch(base, image_size, "movie player delete delay",
                        "movie players are freed after 5 frames")) {
        return;
    }
    WriteCoopPatches(base, "movie player delete delay");
    coop_features.On("movie player delete delay");
}

// See QuietSummonBannerPatches: a party's joins show one banner, not two.
void InstallQuietSummonBanners(u64 base, u64 image_size) {
    if (!party_controller) {
        return;
    }
    if (!CoopSitesMatch(base, image_size, "quiet summon banners", "joins keep the chime banner")) {
        return;
    }
    WriteCoopPatches(base, "quiet summon banners");
    coop_features.On("quiet summon banners");
    LOG_INFO(Debug, "Bloodborne summon banner patch: no chime banner before a join's own");
}

// See PartySummonSlotHoldConstant: a joined guest's summon stops holding a slot after 15 s, so
// the host searches for the next member at once instead of 180 s later.
void InstallShortSummonSlotHold(u64 base, u64 image_size) {
    if (!party_controller) {
        return;
    }
    if (!CoopSitesMatch(base, image_size, "summon slot hold",
                        "a third member waits up to 180 s to join")) {
        return;
    }
    WriteCoopPatches(base, "summon slot hold");
    coop_features.On("summon slot hold");
    LOG_INFO(Debug, "Bloodborne summon slot patch: a summon in flight holds its slot for {} s",
             PartySummonSlotHoldSeconds);
}

constexpr u64 LeaveLimitOffset = 0x0138A4A0;
constexpr std::array<u8, 6> LeaveLimitPrologue{0x55, 0x48, 0x89, 0xE5, 0x53, 0x50};

void InstallGuestStaysWhenTheHostLeaves(u64 base, u64 image_size) {
    if (!party_controller) {
        return;
    }
    if (LeaveLimitOffset + LeaveLimitPrologue.size() > image_size ||
        !InstallCoopHook(reinterpret_cast<void*>(base + LeaveLimitOffset), LeaveLimitPrologue, 0,
                         StayWhenTheHostLeaves)) {
        LOG_ERROR(Debug, "Bloodborne leave-limit hook unavailable; a guest whose host leaves "
                         "goes home");
        coop_features.Skipped("host leaves", SiteFailure(base, image_size, LeaveLimitOffset));
        return;
    }
    coop_features.On("host leaves");
    LOG_INFO(Debug, "Bloodborne party guests stay where they are when their host leaves");
}

// Faster sign posts for party joins. Optional: on a mismatch the game keeps its own timers.
void PatchSignTimers(u64 base, u64 image_size) {
    for (const auto& site : SignTimerSites) {
        const auto expected = EncodeSignTimerStore(site.modrm, site.original_seconds);
        if (site.offset > image_size || expected.size() > image_size - site.offset ||
            !IsSignTimerStore(std::span<const u8>{reinterpret_cast<const u8*>(base + site.offset),
                                                  expected.size()},
                              site)) {
            const u8* found = reinterpret_cast<const u8*>(base + site.offset);
            LOG_ERROR(Debug,
                      "Bloodborne sign timer signature mismatch at {:#x} (found {}); "
                      "keeping the game's sign timers",
                      site.offset,
                      BytesToHex(std::span<const u8>{found, std::min<size_t>(expected.size(), 8)}));
            coop_features.Skipped("sign timers",
                                  fmt::format("signature mismatch at {:#x}", site.offset));
            return;
        }
    }
    for (const auto& site : SignTimerSites) {
        const auto patched = EncodeSignTimerStore(site.modrm, site.patched_seconds);
        std::memcpy(reinterpret_cast<void*>(base + site.offset), patched.data(), patched.size());
    }
    coop_features.On("sign timers");
    LOG_INFO(Debug, "Bloodborne sign timers patched: first post after {} s, re-post every {} s",
             FirstSignPostDelaySeconds, SignRepostSeconds);
}

} // namespace

namespace Coop {

bool VerifySites() {
    const auto image_size = MemoryPatcher::g_eboot_image_size;
    for (const auto& site : Sites) {
        if ((summon_build_host_placement_hook_installed && site.offset == SummonBuildEntryOffset) ||
            (cross_map_guest_handoff_hook_installed && site.offset == CrossMapGuestHandoffOffset) ||
            (summon_reload_state_hook_installed &&
             site.offset == StageWarpDescriptorFinalizeOffset) ||
            (deferred_summon_reload_hook_installed && site.offset == SosStatusUpdateOffset) ||
            (healing_fountain_host_availability_hook_installed &&
             site.offset == HealingFountainAvailabilityOffset) ||
            (world_teardown_hook_installed && site.offset == WorldTeardownMatchingOffset) ||
            (ghost_death_revive_hook_installed && site.offset == GhostDeathReviveOffset) ||
            (host_death_respawn_hook_installed && site.offset == SoloPlayDeathRespawnOffset) ||
            (seamless_travel_hooks_installed && site.offset == StageTransitionEntryOffset) ||
            (stop_request_hook_installed && site.offset == MultiPlayStopRequestOffset)) {
            continue;
        }
        const auto expected = std::span<const u8>{site.prologue.data(), site.prologue_size};
        if (site.offset > image_size || expected.size() > image_size - site.offset) {
            LOG_ERROR(Debug, "Bloodborne RE site {} lies outside the eboot image", site.name);
            return false;
        }
        const auto actual = std::span<const u8>{
            reinterpret_cast<const u8*>(image_base + site.offset), expected.size()};
        if (!std::ranges::equal(actual, expected)) {
            LOG_ERROR(Debug,
                      "Bloodborne RE signature mismatch at {} ({:#x}): expected={} actual={}",
                      site.name, site.offset, BytesToHex(expected), BytesToHex(actual));
            return false;
        }
    }
    return true;
}

} // namespace Coop

namespace {

// Whether the seamless core is in already (its byte patches and its hooks).
bool SeamlessCoreInstalled() {
    return negative_area_patch_installed && area_flag_patch_installed &&
           responder_bell_area_patch_installed && responder_bell_common_patch_installed &&
           active_bell_negative_area_patch_installed && active_bell_area_flag_patch_installed &&
           responder_search_negative_area_patch_installed && sos_status_area_patch_installed &&
           summon_candidate_area_patch_installed && summon_build_area_patch_installed &&
           summon_build_world_state_patch_installed &&
           summon_build_negative_event_patch_installed &&
           summon_build_host_placement_hook_installed && cross_map_guest_handoff_hook_installed &&
           summon_reload_state_hook_installed && deferred_summon_reload_hook_installed &&
           healing_fountain_host_availability_hook_installed;
}

// Whether this game is the one the seamless patches are for: Bloodborne CUSA03173 at 01.09.
bool IsSeamlessCoopGame() {
    if (MemoryPatcher::g_game_serial != "CUSA03173") {
        return false;
    }
    const auto* param_sfo = Common::Singleton<PSF>::Instance();
    const std::string_view app_version = param_sfo->GetString("APP_VER").value_or("Unknown");
    if (app_version != "01.09") {
        LOG_ERROR(Debug,
                  "Bloodborne seamless patches require CUSA03173 01.09; loaded version is {}",
                  app_version);
        return false;
    }
    return true;
}

// The RE tracer's site at `offset` (Sites), or null.
const TraceSite* FindSite(u64 offset) {
    const auto site =
        std::ranges::find_if(Sites, [&](const TraceSite& site) { return site.offset == offset; });
    return site == Sites.end() ? nullptr : &*site;
}

// One of the seamless core's hooks: where, its site, its installed flag and its name for the log.
struct CoreHook {
    u64 offset;
    const TraceSite* site;
    bool* installed;
    std::string_view what;
};

// The core's hooks, all or none: one that fails (no room for its trampoline) takes the others
// out again, so the game is left as it was.
bool InstallCoreHooks(u64 base, u64 image_size, std::span<const CoreHook> core_hooks) {
    for (size_t index = 0; index < core_hooks.size(); ++index) {
        const CoreHook& hook = core_hooks[index];
        const auto expected =
            std::span<const u8>{hook.site->prologue.data(), hook.site->prologue_size};
        if (InstallCoopHook(reinterpret_cast<void*>(base + hook.offset), expected,
                            static_cast<size_t>(hook.site - Sites.data()), TraceEntry)) {
            *hook.installed = true;
            continue;
        }
        LOG_ERROR(Debug, "Failed to install Bloodborne {} hook at {:#x}", hook.what, hook.offset);
        for (size_t installed = 0; installed < index; ++installed) {
            RemoveGuestCodeHook(reinterpret_cast<void*>(base + core_hooks[installed].offset));
            *core_hooks[installed].installed = false;
        }
        coop_features.Skipped(
            "seamless core",
            fmt::format("{}; rolled back", SiteFailure(base, image_size, hook.offset)));
        LogCoopFeatures();
        return false;
    }
    return true;
}

// Optional: without it a guest quitting to the title stays in the room, but every other patch
// still applies.
void InstallWorldTeardownHook(u64 base, u64 image_size, const TraceSite& world_teardown) {
    const auto world_teardown_expected =
        std::span<const u8>{world_teardown.prologue.data(), world_teardown.prologue_size};
    world_teardown_hook_installed = InstallCoopHook(
        reinterpret_cast<void*>(base + WorldTeardownMatchingOffset), world_teardown_expected,
        static_cast<size_t>(&world_teardown - Sites.data()), TraceEntry);
    if (!world_teardown_hook_installed) {
        LOG_ERROR(Debug, "Failed to install Bloodborne world-teardown hook at {:#x}",
                  WorldTeardownMatchingOffset);
        coop_features.Skipped("world teardown",
                              SiteFailure(base, image_size, WorldTeardownMatchingOffset));
    } else {
        coop_features.On("world teardown");
    }
}

// The features on top of the core, each on its own (see coop_features), and the services they
// run.
void InstallCoopFeatures(u64 base, u64 image_size) {
    InstallGhostDeathRevive(base, image_size);
    InstallNoEchoTheft(base, image_size);
    InstallHostDeathKeepsGuests(base, image_size);
    InstallBossClearKeepsSession(base, image_size);
    InstallJoinAnywhere(base, image_size);
    InstallQuietSummonBanners(base, image_size);
    InstallMoviePlayerDeleteDelay(base, image_size);
    InstallShortSummonSlotHold(base, image_size);
    InstallBossDeathTracer(base, image_size);
    InstallGuestStaysWhenTheHostLeaves(base, image_size);
    InstallSeamlessTravel(base, image_size);
    InstallLampsAsCampfires(base, image_size);
    InstallNoLevelSyncInParty(base, image_size);
    InstallSolidNpcsInParty(base, image_size);
    // Before the shared loot: the table takes over its map item packets.
    InstallChannelTable(base, image_size);
    InstallSharedLoot(base, image_size);
    InstallLampsStayLitAtRegistration(base, image_size);
    InstallDeferredBossItems(base, image_size);
    InstallKeepLocalFlags(base, image_size);
    InstallItemModuleDiagnostics(base, image_size);
    InstallItemButtonDiagnostics(base, image_size);
    InstallPartyDeath(base, image_size);
    InstallOwnHitCount(base, image_size);
    StartSaveBackups();
    StartCoopTrace();
    InstallNativePacketTrace(base, image_size);
    StartRestBoardClient();
    StartVitalsBoardClient();
    PatchSignTimers(base, image_size);
}

// The core's byte patches, written.
void WriteSeamlessCoreBytePatches(u64 base) {
    WriteCoopPatches(base, "seamless core");
    negative_area_patch_installed = true;
    area_flag_patch_installed = true;
    responder_bell_area_patch_installed = true;
    responder_bell_common_patch_installed = true;
    active_bell_negative_area_patch_installed = true;
    active_bell_area_flag_patch_installed = true;
    responder_search_negative_area_patch_installed = true;
    sos_status_area_patch_installed = true;
    summon_candidate_area_patch_installed = true;
    summon_build_area_patch_installed = true;
    summon_build_world_state_patch_installed = true;
    summon_build_negative_event_patch_installed = true;
    stage_transition_keep_matching_patch_installed = true;
}

} // namespace

void InstallSeamlessCoopPatches() {
    if (SeamlessCoreInstalled() || !EnvFlagEnabled("SHADPS4_BLOODBORNE_SEAMLESS_COOP")) {
        return;
    }
    if (!IsSeamlessCoopGame()) {
        return;
    }

    const auto base = MemoryPatcher::g_eboot_address;
    const auto image_size = MemoryPatcher::g_eboot_image_size;
    coop_features = {};
    const TraceSite* summon_build_entry = FindSite(SummonBuildEntryOffset);
    const TraceSite* cross_map_guest_handoff = FindSite(CrossMapGuestHandoffOffset);
    const TraceSite* deferred_summon_reload = FindSite(SosStatusUpdateOffset);
    const TraceSite* summon_reload_state = FindSite(StageWarpDescriptorFinalizeOffset);
    const TraceSite* healing_fountain_host_availability =
        FindSite(HealingFountainAvailabilityOffset);
    const TraceSite* world_teardown = FindSite(WorldTeardownMatchingOffset);
    if (!summon_build_entry || !cross_map_guest_handoff || !deferred_summon_reload ||
        !summon_reload_state || !healing_fountain_host_availability || !world_teardown) {
        LOG_ERROR(Debug, "Bloodborne seamless functional hook site is missing");
        return;
    }
    if (base == 0) {
        LOG_ERROR(Debug, "Bloodborne seamless patches lie outside the eboot image");
        return;
    }

    // All of the core or none of it. Every site is checked before anything is written: its byte
    // patches, its hooks and the game functions the cross-map summon calls (the co-op site table,
    // bloodborne_patch_sites.h).
    if (!CoopSitesMatch(base, image_size, "seamless core", "no seamless co-op")) {
        LogCoopFeatures();
        return;
    }
    image_base = base;
    // Then the hooks (InstallCoreHooks: all or none).
    const std::array<CoreHook, 5> core_hooks{{
        {SummonBuildEntryOffset, summon_build_entry, &summon_build_host_placement_hook_installed,
         "cross-map host-placement"},
        {CrossMapGuestHandoffOffset, cross_map_guest_handoff,
         &cross_map_guest_handoff_hook_installed, "cross-map guest-handoff"},
        {StageWarpDescriptorFinalizeOffset, summon_reload_state,
         &summon_reload_state_hook_installed, "summon-reload state"},
        {SosStatusUpdateOffset, deferred_summon_reload, &deferred_summon_reload_hook_installed,
         "deferred summon-reload"},
        {HealingFountainAvailabilityOffset, healing_fountain_host_availability,
         &healing_fountain_host_availability_hook_installed, "healing-fountain host-availability"},
    }};
    if (!InstallCoreHooks(base, image_size, core_hooks)) {
        return;
    }
    coop_features.On("seamless core");
    InitPartyController();
    InstallWorldTeardownHook(base, image_size, *world_teardown);

    InstallCoopFeatures(base, image_size);

    WriteSeamlessCoreBytePatches(base);
    LOG_INFO(Debug,
             "Bloodborne seamless patches enabled Beckoning, Small Resonant, and Sinister Bell "
             "use, active search, cross-map candidate selection, and native summon construction "
             "with host-placement transport, guest stage handoff, retained matching during "
             "stage transitions, deferred native reload, and host lantern actions in negative "
             "and area-complete SOS regions ({} byte patches, {} hooks)",
             CoopPatchesOf("seamless core").size(), core_hooks.size());
    LogCoopFeatures();
}

} // namespace Core::Bloodborne
