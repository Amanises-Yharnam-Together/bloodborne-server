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
Place CurrentPlace();
extern std::optional<PartyController> party_controller;
void InitPartyController();
bool InPartySession();
void InstallLampsAsCampfires(u64 base, u64 image_size);
std::chrono::sys_seconds LocalTimeNow();
void StartCoopTrace();
void StartRestBoardClient();

void PS4_SYSV_ABI TraceEntry(u64 tag, const GuestRegisterSnapshot* registers);
bool PS4_SYSV_ABI OverrideHostDeathRespawn(u64 tag, const GuestRegisterSnapshot* registers);
bool PS4_SYSV_ABI StayWhenTheHostLeaves(u64 tag, const GuestRegisterSnapshot* registers);

void LogGameCallChainAt(const char* what, u64 rsp);

// Seamless travel (see PartyTrip): the stage transition every trip goes through (lamps, the
// Dream's headstones, the event warps), and LeaveSession, which 01.09 calls from several places
// on the way (one is the stage change at 0x0193C689).
constexpr u64 StageTransitionEntryOffset = 0x013CDE30;
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
bool VerifySites();

} // namespace Coop

using namespace Coop;

} // namespace Core::Bloodborne
