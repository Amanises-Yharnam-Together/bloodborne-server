// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The co-op runtime's internals that its files share (bloodborne_re.cpp and
// the bloodborne_*.cpp split from it): types, constants and templates, and the
// functions and variables one file defines for the others, in namespace Coop.
// Only those files include it.

#pragma once

#include "core/bloodborne_crash_dump.h"
#include "core/bloodborne_party.h"
#include "core/bloodborne_re.h"
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

#include <httplib.h>
#include "core/bloodborne_re_sites.h"

namespace Core::Bloodborne {
namespace Coop {
extern std::atomic<u64> event_sequence;
extern std::mutex capture_mutex;
extern std::ofstream capture_file;
extern uintptr_t image_base;
extern bool negative_area_patch_installed;
extern bool area_flag_patch_installed;
extern bool responder_bell_area_patch_installed;
extern bool responder_bell_common_patch_installed;
extern bool active_bell_negative_area_patch_installed;
extern bool active_bell_area_flag_patch_installed;
extern bool responder_search_negative_area_patch_installed;
extern bool sos_status_area_patch_installed;
extern bool summon_candidate_area_patch_installed;
extern bool summon_build_area_patch_installed;
extern bool summon_build_world_state_patch_installed;
extern bool summon_build_negative_event_patch_installed;
extern bool stage_transition_keep_matching_patch_installed;
extern bool world_teardown_hook_installed;
extern bool ghost_death_revive_hook_installed;
extern bool seamless_travel_hooks_installed;
extern bool stop_request_hook_installed;
extern bool host_death_respawn_hook_installed;
extern bool summon_build_host_placement_hook_installed;
extern bool cross_map_guest_handoff_hook_installed;
extern bool summon_reload_state_hook_installed;
extern bool deferred_summon_reload_hook_installed;
extern bool healing_fountain_host_availability_hook_installed;

struct SummonPlacementDescriptor {
    u32 packed_region{};
    float x{};
    float y{};
    float z{};
    float heading{};
    s32 area{};
};
static_assert(sizeof(SummonPlacementDescriptor) == 0x18);

struct HostPlacementRewriteRecord {
    std::string_view result{"not_attempted"};
    u64 prepared{};
    SummonPlacementDescriptor candidate{};
    SummonPlacementDescriptor host{};
};
extern thread_local HostPlacementRewriteRecord host_placement_rewrite;
extern std::atomic<u32> pending_summon_reload_map;
extern std::atomic<u32> pending_summon_reload_state_map;

struct GuestPlacementHandoffRecord {
    std::string_view result{"not_attempted"};
    u64 object{};
    u64 state{};
    u32 current_map{};
    u32 received_map{};
    u32 selected_map{};
    bool transported_host{};
    bool placement_refreshed{};
    bool select_result{};
    bool summon_reload_armed{};
    s32 transition_result{};
};
extern thread_local GuestPlacementHandoffRecord guest_placement_handoff;

struct SummonReloadStateRecord {
    std::string_view result{"not_pending"};
    u32 target_map{};
    u32 descriptor_map{};
    u64 reload_state{};
    s32 state_before{};
    s32 state_after{};
    bool summon_reload_started{};
    bool state_setter_used{};
    bool placement_refreshed{};
};
extern thread_local SummonReloadStateRecord summon_reload_state;

struct DeferredSummonReloadRecord {
    std::string_view result{"not_pending"};
    u32 target_map{};
    u32 current_map{};
    bool summon_reload_started{};
};
extern thread_local DeferredSummonReloadRecord deferred_summon_reload;

struct PreMatchGuestWarpRecord {
    std::string_view result{"not_pending"};
    u64 state{};
    u32 current_map{};
    u32 target_map{};
    u32 selected_map{};
    s32 multi_play_state{-1};
    bool placement_written{};
    bool select_result{};
    bool responder_resume_armed{};
    s32 transition_result{};
};
extern thread_local PreMatchGuestWarpRecord pre_match_guest_warp;

struct PendingResponderResume {
    u32 target_map{};
    s32 target_area{};
    u32 ready_observations{};
    u32 attempts{};
    bool dispatched{};
    std::chrono::steady_clock::time_point dispatched_at{};
};
extern thread_local PendingResponderResume pending_responder_resume;

struct ResponderResumeRecord {
    std::string_view result{"not_pending"};
    u32 target_map{};
    u32 current_map{};
    s32 target_area{};
    s32 current_area{};
    u64 state{};
    u64 multi_play{};
    s32 multi_play_state{-1};
    u64 matching_controller{};
    u64 player{};
    u32 ready_observations{};
    u32 attempts{};
    bool effect_active{};
    bool native_result{};
};
extern thread_local ResponderResumeRecord responder_resume;

struct HealingFountainAvailabilityRecord {
    std::string_view result{"not_attempted"};
    u64 object{};
    u64 player{};
    bool blocked_before{};
    bool host_effect{};
    bool blocked_after{};
};
extern thread_local HealingFountainAvailabilityRecord healing_fountain_availability;
bool EnvFlagEnabled(const char* name);
std::string BytesToHex(std::span<const u8> bytes);
extern CoopFeatureReport coop_features;
std::string FeatureMismatch(u64 base, u64 image_size, std::string_view feature);
std::string SiteFailure(u64 base, u64 image_size, u64 offset);
bool InstallCoopHook(void* address, std::span<const u8> bytes, u64 tag, GuestCodeHook hook);

bool InstallCoopHook(void* address, std::span<const u8> bytes, u64 tag, GuestFunctionOverride hook);

bool InstallCoopHook(void* address, std::span<const u8> bytes, u64 tag, GuestRegisterEditHook hook);
bool HasMemoryAccess(u64 address, size_t size, MemoryProt required);

// The value at address + offset, or nothing when that memory is not readable (for reads where 0
// means something, which ReadValue would give then).
template <typename T>
std::optional<T> TryRead(u64 address, size_t offset) {
    T value{};
    if (address > std::numeric_limits<u64>::max() - offset) {
        return std::nullopt;
    }
    const u64 source = address + offset;
    if (source > std::numeric_limits<u64>::max() - sizeof(value)) {
        return std::nullopt;
    }

    if (!HasMemoryAccess(source, sizeof(value), MemoryProt::CpuRead)) {
        return std::nullopt;
    }

    std::memcpy(&value, reinterpret_cast<const void*>(source), sizeof(value));
    return value;
}

template <typename T>
T ReadValue(u64 address, size_t offset) {
    return TryRead<T>(address, offset).value_or(T{});
}
bool HasVirtual(u64 object, u64 slot, u64 function, std::string_view site);

template <typename T>
bool WriteValue(u64 address, size_t offset, const T& value) {
    if (address > std::numeric_limits<u64>::max() - offset) {
        return false;
    }
    const u64 destination = address + offset;
    if (!HasMemoryAccess(destination, sizeof(value), MemoryProt::CpuWrite)) {
        return false;
    }

    std::memcpy(reinterpret_cast<void*>(destination), &value, sizeof(value));
    return true;
}
bool WriteTransportedSummonPlacement(u64 state, const SummonPlacementDescriptor& placement);
bool IsUsablePackedMap(u32 packed_region);

bool IsSameSummonPlacement(const SummonPlacementDescriptor& left,
                           const SummonPlacementDescriptor& right);

u64 GetSummonManagerRoot();
u64 GetGlobalState();
u64 GetMatchingState();
u64 GetLocalPlayer();
bool HasPlayerEffect(u64 player, s32 effect_id);
u32 GetCurrentPackedMap();
Place CurrentPlace();
void RefreshSeamlessLocalPlacement();
void ApplyCrossMapSummonHostPlacement(const GuestRegisterSnapshot& registers);

bool IsPartyGuestRole();
void ApplyCrossMapSummonGuestPlacement(const GuestRegisterSnapshot& registers);
void ApplySummonReloadStateAtDescriptor(const GuestRegisterSnapshot& registers);
void ApplyDeferredSummonReload();
void ApplyPreMatchCrossMapGuestWarp();
bool StartPlayerSummonSearch(u64 player, s32 effect_id);
void ResumePreMatchCrossMapResponder();
extern std::optional<PartyController> party_controller;
void InitPartyController();

bool HostHasRoomForMore();
bool InPartySession();

std::vector<s32> EffectsOf(u64 character);
std::vector<s32> LocalEffects();
extern std::mutex map_state_mutex;
extern std::optional<MemberMapState> host_map_state;
extern std::mutex map_board_mutex;
extern std::vector<MemberMap> map_board;
extern std::chrono::steady_clock::time_point map_board_time;
s32 SessionPlayers();
void InstallLampsAsCampfires(u64 base, u64 image_size);
std::chrono::sys_seconds LocalTimeNow();
void StartCoopTrace();
void StartRestBoardClient();
void LogJoinTaskChanges();

void ReopenClosedPartySession();
void WatchSummonJoin();

void PS4_SYSV_ABI TraceEntry(u64 tag, const GuestRegisterSnapshot* registers);
bool PS4_SYSV_ABI OverrideHostDeathRespawn(u64 tag, const GuestRegisterSnapshot* registers);
bool PS4_SYSV_ABI StayWhenTheHostLeaves(u64 tag, const GuestRegisterSnapshot* registers);

void LogGameCallChainAt(const char* what, u64 rsp);

// Seamless travel (see PartyTrip): the stage transition every trip goes through (lamps, the
// Dream's headstones, the event warps), and LeaveSession, which 01.09 calls from several places
// on the way (one is the stage change at 0x0193C689).
constexpr u64 StageTransitionEntryOffset = 0x013CDE30;
void SaveRemoteJoinTasks();
extern std::vector<s32> effects_before_trip;

// CSMultiPlayMan (0x05540230) keeps the join tasks in a vector at +0x10..+0x18 (see
// ShouldRecreateAfterTrip). The task's SetStep virtual (vtable +0x28) requests the next step.
constexpr u64 MultiPlayManPointerOffset = 0x05540230;

template <typename Visit>
void ForEachJoinTask(Visit&& visit) {
    const u64 manager = ReadValue<u64>(image_base + MultiPlayManPointerOffset, 0);
    if (manager < 0x10000 || !HasMemoryAccess(manager, 0x20, MemoryProt::CpuRead)) {
        return;
    }
    const u64 begin = ReadValue<u64>(manager, 0x10);
    const u64 end = ReadValue<u64>(manager, 0x18);
    if (begin < 0x10000 || end <= begin || end - begin > 8 * 16 ||
        !HasMemoryAccess(begin, end - begin, MemoryProt::CpuRead)) {
        return;
    }
    for (u64 entry = begin; entry < end; entry += 8) {
        const u64 task = ReadValue<u64>(entry, 0);
        if (task >= 0x10000 && HasMemoryAccess(task, 0xF0, MemoryProt::CpuRead)) {
            visit(task);
        }
    }
}

// What the game does when a member joins (0x0178C20C): allocate 0x100 bytes from the allocator
// at 0x05540438 (vtable +0x58), construct the task (0x01E4F9F0: task, member id, kind) and add it
// to CSMultiPlayMan +0x08 (0x01E55A70). Member ids are 0x20 bytes: SprjSessionManager
// (0x05540290) lists the session's at +0xF8..+0x100 and holds the local one at +0xD0; the game
// compares them with 0x00CA1C70.
constexpr u64 SessionManagerPointerOffset = 0x05540290;
constexpr u64 MemberIdEqualOffset = 0x00CA1C70;
extern std::vector<SavedJoinTask> saved_join_tasks;
extern std::map<u64, MemberDataWait> joins_waiting_for_data;
void RecreateRemotePlayers(std::span<const u64> only);
std::optional<u32> HostMapFromItsPost();
std::vector<u64> OtherSessionMembers();

std::optional<std::string> SessionMemberBoardName(u64 member,
                                                  const std::vector<std::string>& online_ids);
extern bool restore_search_pending;
void RestoreSearchAfterTrip(bool in_session);
void PlayGuestAsHost(bool in_session);
void InstallNoLevelSyncInParty(u64 base, u64 image_size);
void InstallSolidNpcsInParty(u64 base, u64 image_size);
void InstallSharedLoot(u64 base, u64 image_size);
void InstallItemModuleDiagnostics(u64 base, u64 image_size);
void InstallItemButtonDiagnostics(u64 base, u64 image_size);
void InstallLampsStayLitAtRegistration(u64 base, u64 image_size);
void InstallKeepLocalFlags(u64 base, u64 image_size);
void InstallDeferredBossItems(u64 base, u64 image_size);
void InstallOwnHitCount(u64 base, u64 image_size);
void InstallSeamlessTravel(u64 base, u64 image_size);
void StartVitalsBoardClient();
void InstallPartyDeath(u64 base, u64 image_size);
void ApplyHealingFountainHostAvailability(const GuestRegisterSnapshot& registers);
u64 GetSummonManager();
void WriteHex(std::ostream& out, u64 value);
bool VerifySites();

} // namespace Coop

using namespace Coop;

} // namespace Core::Bloodborne
