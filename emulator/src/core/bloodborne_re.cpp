// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

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

#include <httplib.h>

namespace Core::Bloodborne {
namespace {

constexpr u64 GlobalStatePointerOffset = 0x05556678;
// GameDataMan: +0x28 "has a bloodstain", +0x30 the bloodstain record UpDateBloodMark writes.
constexpr u64 GameDataManPointerOffset = 0x0553B130;
constexpr u64 EventManPointerOffset = 0x0553B108; // SprjEventMan
constexpr u64 CurrentMapListPointerOffset = 0x0553B148;
constexpr u64 RoleMetadataTableOffset = 0x0553D750;
constexpr u64 SummonBuildRoleTableOffset = 0x0556E560;
constexpr u64 RoleMetadataStride = 0x80;
constexpr s32 RoleMetadataCount = 34;
// Event script bindings ReviveDeadPartyGuest calls.
constexpr u64 IsWhiteGhostOffset = 0x01331D80;
constexpr u64 UpDateBloodMarkOffset = 0x013308B0;
constexpr u64 RevivePlayerOffset = 0x01332B20;
// 01.09's local-player revive: refills HP and clears the death's controller locks ([+0x58]
// +0x114 bit 0 and [+0x38]+0x48 bits 0-1), then makes the player a live one (type 0).
constexpr u64 ReviveLocalCharacterOffset = 0x013179B0;
// Resolves a respawn point id to a position and rotation in the loaded blocks.
constexpr u64 ResolveRespawnPointOffset = 0x0154AFC0;
constexpr u64 LoadedBlocksPointerOffset = 0x0553B148;
// ChrResetAnimation(_, entity): sends the character's behaviour back to idle.
constexpr u64 ChrResetAnimationOffset = 0x013370F0;
constexpr u64 CharacterWarpSetOrCopyFloorOffset = 0x018BBA70;
constexpr auto BackgroundFloorCopyReturnOffsets = std::to_array<u64>({0x018BEE67, 0x018BEF34});
constexpr u64 OnlineStatePointerOffset = 0x056C7048;

std::array<std::atomic<u64>, Sites.size()> site_hits{};

} // namespace

namespace Coop {

std::atomic<u64> event_sequence{};

} // namespace Coop

namespace {

std::atomic<s32> observed_sos_area{-1};

} // namespace

namespace Coop {

std::mutex capture_mutex;
std::ofstream capture_file;

} // namespace Coop

namespace {

std::filesystem::path capture_path;

} // namespace

namespace Coop {

uintptr_t image_base{};

} // namespace Coop

namespace {

bool installed{};
thread_local u64 responder_availability_frame{};
thread_local s32 responder_availability_goods{-1};

} // namespace

namespace Coop {

thread_local HostPlacementRewriteRecord host_placement_rewrite{};

thread_local GuestPlacementHandoffRecord guest_placement_handoff{};

thread_local SummonReloadStateRecord summon_reload_state{};

thread_local DeferredSummonReloadRecord deferred_summon_reload{};

thread_local PreMatchGuestWarpRecord pre_match_guest_warp{};

thread_local HealingFountainAvailabilityRecord healing_fountain_availability{};

bool HasMemoryAccess(u64 address, size_t size, MemoryProt required) {
    if (size == 0 || address > std::numeric_limits<u64>::max() - size) {
        return false;
    }

    auto* memory = Memory::Instance();
    if (memory == nullptr || !memory->IsValidMapping(address, size)) {
        return false;
    }

    void* mapping_start{};
    void* mapping_end{};
    u32 protection{};
    const u32 required_bits = static_cast<u32>(required);
    return memory->QueryProtection(address, &mapping_start, &mapping_end, &protection) == 0 &&
           (protection & required_bits) == required_bits &&
           address >= reinterpret_cast<u64>(mapping_start) &&
           address + size <= reinterpret_cast<u64>(mapping_end);
}

} // namespace Coop

namespace {

// A guarded call (`site`) refused an object that is there (see RefusedCallLog): logged once per
// site and vtable seen, so a wrong pointer or a vtable list missing a class shows in the log.
void LogRefusedCall(std::string_view site, u64 object) {
    if (object < 0x10000) {
        return; // no object: nothing to say
    }
    const u64 vtable = TryRead<u64>(object, 0).value_or(0);
    static std::mutex mutex;
    static RefusedCallLog logged;
    {
        std::scoped_lock lock{mutex};
        if (!logged.FirstTime(site, vtable)) {
            return;
        }
    }
    LOG_WARNING(Debug, "Bloodborne refused a game call: {} got an object ({:#x}) with vtable {}",
                site, object,
                DescribeVtable(vtable, image_base, MemoryPatcher::g_eboot_image_size));
}

} // namespace

namespace Coop {

// Whether `object` is one of the game objects `vtables` names (see CharacterVtables): checked
// before the game is called with it (`site`, for the log when it is not).
bool HasVtable(u64 object, std::span<const u64> vtables, std::string_view site) {
    const auto vtable = object >= 0x10000 ? TryRead<u64>(object, 0) : std::nullopt;
    if (vtable.has_value() && IsGameVtable(*vtable, image_base, vtables)) {
        return true;
    }
    LogRefusedCall(site, object);
    return false;
}

bool HasVtable(u64 object, u64 vtable, std::string_view site) {
    return HasVtable(object, std::span<const u64>{&vtable, 1}, site);
}

bool IsCharacter(u64 object, std::string_view site) {
    return HasVtable(object, CharacterVtables, site);
}

// Whether the object's virtual at `slot` is the 01.09 function at `function` (e.g. the
// character's AddSpEffect): before a call through it, or a direct call that stands for it.
bool HasVirtual(u64 object, u64 slot, u64 function, std::string_view site) {
    const u64 vtable = object >= 0x10000 ? TryRead<u64>(object, 0).value_or(0) : 0;
    if (image_base != 0 && vtable >= 0x10000 &&
        TryRead<u64>(vtable, slot) == std::optional<u64>{image_base + function}) {
        return true;
    }
    LogRefusedCall(site, object);
    return false;
}

bool WriteTransportedSummonPlacement(u64 state, const SummonPlacementDescriptor& placement) {
    const std::array<float, 4> position{placement.x, placement.y, placement.z, 1.0F};
    const std::array<float, 4> orientation{0.0F, placement.heading, 0.0F, 0.0F};
    return WriteValue(state, 0x14C4, placement.packed_region) &&
           WriteValue(state, 0x14D0, position) && WriteValue(state, 0x14E0, orientation);
}

bool IsUsablePackedMap(u32 packed_region) {
    return packed_region != 0 && packed_region != std::numeric_limits<u32>::max();
}

bool IsSameSummonPlacement(const SummonPlacementDescriptor& left,
                           const SummonPlacementDescriptor& right) {
    return left.packed_region == right.packed_region &&
           std::bit_cast<u32>(left.x) == std::bit_cast<u32>(right.x) &&
           std::bit_cast<u32>(left.y) == std::bit_cast<u32>(right.y) &&
           std::bit_cast<u32>(left.z) == std::bit_cast<u32>(right.z) &&
           std::bit_cast<u32>(left.heading) == std::bit_cast<u32>(right.heading) &&
           left.area == right.area;
}

u64 GetLocalPlayer() {
    const u64 local_state =
        image_base != 0 ? ReadValue<u64>(image_base + CandidateLocalStatePointerOffset, 0) : 0;
    return local_state >= 0x10000 ? ReadValue<u64>(local_state, 0x60) : 0;
}

} // namespace Coop

namespace {

// The effect manager's node removal (player +0x1C8 manager, node, 1): what the bell's own
// code calls to take off 9003/9004 (0x015069BA). It unlinks the node (+0x58 next, +0x60 prev).
constexpr u64 RemoveEffectNodeOffset = 0x017E1D90;
constexpr std::array<u8, 8> RemoveEffectNodePrologue{0x55, 0x48, 0x89, 0xE5,
                                                     0x41, 0x57, 0x41, 0x56};

// Takes `effect_id` off the player (the game's thread). False when it is not on.
bool EndPlayerEffect(u64 player, s32 effect_id) {
    const u64 manager = player >= 0x10000 ? ReadValue<u64>(player, 0x1C8) : 0;
    if (manager < 0x10000 || image_base == 0 ||
        !std::ranges::equal(
            std::span<const u8>{reinterpret_cast<const u8*>(image_base + RemoveEffectNodeOffset),
                                RemoveEffectNodePrologue.size()},
            RemoveEffectNodePrologue)) {
        return false;
    }
    u64 node = ReadValue<u64>(manager, 0x08);
    for (size_t index = 0; node >= 0x10000 && index < 512; ++index) {
        if (ReadValue<s32>(node, 0x40) == effect_id) {
            using Remove = void PS4_SYSV_ABI (*)(u64 manager, u64 node, u32 now);
            reinterpret_cast<Remove>(image_base + RemoveEffectNodeOffset)(manager, node, 1);
            return true;
        }
        const u64 next = ReadValue<u64>(node, 0x58);
        if (next == node) {
            break;
        }
        node = next;
    }
    return false;
}

} // namespace

namespace Coop {

bool HasPlayerEffect(u64 player, s32 effect_id) {
    const u64 manager = player >= 0x10000 ? ReadValue<u64>(player, 0x1C8) : 0;
    if (manager < 0x10000) {
        return false;
    }

    u64 node = ReadValue<u64>(manager, 0x08);
    for (size_t index = 0; node >= 0x10000 && index < 512; ++index) {
        const u64 next = ReadValue<u64>(node, 0x58);
        const u32 flags = ReadValue<u32>(node, 0x1C);
        const u64 param = ReadValue<u64>(node, 0x48);
        if ((flags & 0x800C0003U) == 0 && param >= 0x10000 &&
            ReadValue<s32>(node, 0x40) == effect_id) {
            return true;
        }
        if (next == node) {
            break;
        }
        node = next;
    }
    return false;
}

u32 GetCurrentPackedMap() {
    const u64 map_list =
        image_base != 0 ? ReadValue<u64>(image_base + CurrentMapListPointerOffset, 0) : 0;
    if (map_list < 0x10000 || !HasMemoryAccess(map_list, 0x24, MemoryProt::CpuRead)) {
        return 0;
    }

    const s32 index = ReadValue<s32>(map_list, 0x20);
    const u64 map_array = ReadValue<u64>(map_list, 0x10);
    if (index < 0 || map_array < 0x10000 ||
        !HasMemoryAccess(map_array, 0x28, MemoryProt::CpuRead)) {
        return 0;
    }

    const s32 count = ReadValue<s32>(map_array, 0x18);
    const u64 entries = ReadValue<u64>(map_array, 0x20);
    if (index >= count || entries < 0x10000) {
        return 0;
    }

    constexpr u64 MapEntryStride = 0xA0;
    const u64 entry_offset = static_cast<u64>(index) * MapEntryStride;
    if (entry_offset > std::numeric_limits<u64>::max() - entries) {
        return 0;
    }
    const u64 entry = entries + entry_offset;
    if (entry > std::numeric_limits<u64>::max() - sizeof(u64)) {
        return 0;
    }
    return ReadValue<u32>(entry, sizeof(u64));
}

} // namespace Coop

namespace {

// Where this player is (see PlaceTracker): fed by TickPlace once per party tick, read by every
// feature instead of GetCurrentPackedMap. Only the load-time hooks, the trip machinery and the
// rig read the game's map themselves.
std::mutex place_mutex;
PlaceTracker place_tracker;
std::vector<std::function<void(const PlaceChange&)>> place_listeners;

} // namespace

namespace Coop {

Place CurrentPlace() {
    std::scoped_lock lock{place_mutex};
    return place_tracker.Current();
}

// The committed map while the player stands in the world, else 0 (in a load, before the first
// map read, in a cutscene): what the features that pause in loads read.
u32 PlayingMap() {
    const Place place = CurrentPlace();
    return place.state == PlaceState::Playing ? place.map : 0;
}

} // namespace Coop

namespace {

// `listener` runs on the game's thread after each change of place, outside the tracker's lock.
bool OnPlaceChange(std::function<void(const PlaceChange&)> listener) {
    std::scoped_lock lock{place_mutex};
    place_listeners.push_back(std::move(listener));
    return true;
}

// One line per change of place.
[[maybe_unused]] const bool place_log_listening = OnPlaceChange(
    [](const PlaceChange& change) { LOG_INFO(Debug, "{}", FormatPlaceChange(change)); });

// Each change of place into the co-op trace.
[[maybe_unused]] const bool place_trace_listening =
    OnPlaceChange([](const PlaceChange& change) { Trace("place", TracePlaceFields(change)); });

// The resets of what lives for a visit or a session (see LifecycleResets): each container
// registers its own next to it with RegisterLifecycleReset.
std::mutex lifecycle_mutex;
LifecycleResets lifecycle_resets;

} // namespace

namespace Coop {

bool RegisterLifecycleReset(std::string name, std::initializer_list<Lifecycle> events,
                            std::function<void()> reset) {
    std::scoped_lock lock{lifecycle_mutex};
    lifecycle_resets.Register(std::move(name), events, std::move(reset));
    return true;
}

void OnLifecycle(Lifecycle event) {
    std::vector<std::string> ran;
    u64 epoch = 0;
    {
        std::scoped_lock lock{lifecycle_mutex};
        ran = lifecycle_resets.Dispatch(event);
        epoch = lifecycle_resets.Epoch();
    }
    TraceLife(std::string{LifecycleName(event)}.c_str(), {{"epoch", std::to_string(epoch)}});
    LOG_INFO(Debug, "Co-op lifecycle: {}, session epoch {} (reset: {})", LifecycleName(event),
             epoch, ran.empty() ? "nothing" : fmt::format("{}", fmt::join(ran, ", ")));
}

// Auto-rejoin for a configured party (SHADPS4_BLOODBORNE_PARTY + SHADPS4_BLOODBORNE_PARTY_ROLE).
// Only ticked from the SOS-status hook on the game thread.
std::optional<PartyController> party_controller;

void InitPartyController() {
    const char* party = std::getenv("SHADPS4_BLOODBORNE_PARTY");
    const char* role = std::getenv("SHADPS4_BLOODBORNE_PARTY_ROLE");
    if (party == nullptr || !ParsePartySetting(party)) {
        return;
    }
    const std::string_view role_name = role != nullptr ? role : "";
    if (role_name != "host" && role_name != "guest") {
        LOG_WARNING(Debug, "Bloodborne party set but SHADPS4_BLOODBORNE_PARTY_ROLE is not "
                           "host or guest; auto-rejoin disabled");
        return;
    }
    party_controller.emplace(role_name == "host" ? PartyRole::Host : PartyRole::Guest);
    LOG_INFO(Debug, "Bloodborne party auto-rejoin enabled as {}", role_name);
}

} // namespace Coop

namespace {

PartyObservation ObserveParty(u64 player, s32 search_effect) {
    PartyObservation observation{};
    observation.player_valid =
        player >= 0x10000 && HasMemoryAccess(player, sizeof(u64), MemoryProt::CpuRead);
    observation.map_usable = PlayingMap() != 0;

    const u64 state = GetGlobalState();
    observation.stage_transition =
        state < 0x10000 || !HasMemoryAccess(state, 0x1521, MemoryProt::CpuRead) ||
        ReadValue<u8>(state, 0x08) != 0 || ReadValue<u8>(state, 0x1520) != 0 ||
        pending_summon_reload_map.load(std::memory_order_acquire) != 0 ||
        pending_summon_reload_state_map.load(std::memory_order_acquire) != 0;

    // The cross-map responder resume owns the bell while it is armed; stay out of its way.
    const u64 multi_play = GetMatchingState();
    observation.matching_idle =
        !IsUsablePackedMap(pending_responder_resume.target_map) && multi_play >= 0x10000 &&
        HasMemoryAccess(multi_play, 0x128, MemoryProt::CpuRead) &&
        ReadValue<s32>(multi_play, 0x124) == 0 && ReadValue<u64>(multi_play, 0x18) == 0;

    observation.search_effect_active =
        observation.player_valid && HasPlayerEffect(player, search_effect);
    observation.boss_sequence = EventFlagOn(4000) || EventFlagOn(4047);
    observation.room_for_more = party_controller &&
                                party_controller->SearchEffectId() == BeckoningBellEffect &&
                                HostHasRoomForMore();
    observation.white_phantom = observation.player_valid &&
                                HasMemoryAccess(player, 0x7C, MemoryProt::CpuRead) &&
                                ReadValue<s32>(player, 0x78) == WhiteGhostCharacterType;
    return observation;
}

// Diagnostics for guest bloodstains in the host's world (Phase 2): logs the local bloodstain
// record, the spawned stain object and the world flags that gate them whenever they change.
// The local player's own bloodstain: [[SprjEventMan +0x80] +8], the object the game's show/hide
// (0x01868A50) takes when a load shows it (0x017C23EB-0x017C25DC) and on a join (0x0193A75A).
// (Not FrpgNetMan +0xC68's: passed that, the show crashed krolebord's game, Kaniel 2026-10-11.)
u64 OwnBloodstain() {
    const u64 events = ReadValue<u64>(image_base + EventManPointerOffset, 0);
    const u64 list = events >= 0x10000 ? ReadValue<u64>(events, 0x80) : 0;
    return list >= 0x10000 ? ReadValue<u64>(list, 8) : 0;
}

// A stain object the show can take: the stain (its vtable), readable, and its marker (+0x88) and
// effect (+0x98) either absent or readable (the show frees them first).
bool LooksLikeBloodstain(u64 stain) {
    if (!HasVtable(stain, BloodstainVtable, "ShowGuestBloodstain") ||
        !HasMemoryAccess(stain, 0xC8, MemoryProt::CpuRead)) {
        return false;
    }
    for (const u64 offset : {u64{0x88}, u64{0x98}}) {
        const u64 part = ReadValue<u64>(stain, offset);
        if (part != 0 && (part < 0x10000 || !HasMemoryAccess(part, 0x98, MemoryProt::CpuRead))) {
            return false;
        }
    }
    return true;
}

// The stain's marker (+0x88) and effect (+0x98) are what 0x01868A50 makes when it shows the
// stain (and frees when it hides it): a stain recorded but never shown has neither (Kaniel
// 2026-10-11: no bloodstains in sight after the party wipe at the Tomb of Oedon).
void LogBloodstainStateChanges() {
    static std::string last;
    const u64 data = ReadValue<u64>(image_base + GameDataManPointerOffset, 0);
    const u64 record = data >= 0x10000 ? ReadValue<u64>(data, 0x30) : 0;
    const u64 stain = OwnBloodstain();
    const u64 global = GetGlobalState();
    const u64 player = GetLocalPlayer();
    const auto state =
        fmt::format("has_mark={} record_valid={} record_30={} record_38={} record_block={:#x} "
                    "record_pos=({}, {}, {}) stain={:#x} "
                    "stain_hidden={} shown_marker={:#x} shown_effect={:#x} flag1592={} "
                    "flag1770={} chr_type={}",
                    data >= 0x10000 ? ReadValue<u8>(data, 0x28) : 0xFF,
                    record >= 0x10000 ? ReadValue<u8>(record, 0x44) : 0xFF,
                    record >= 0x10000 ? ReadValue<s32>(record, 0x30) : 0,
                    record >= 0x10000 ? ReadValue<s32>(record, 0x38) : 0,
                    record >= 0x10000 ? ReadValue<u32>(record, 0x3C) : 0,
                    record >= 0x10000 ? ReadValue<float>(record, 0x18) : 0.0f,
                    record >= 0x10000 ? ReadValue<float>(record, 0x1C) : 0.0f,
                    record >= 0x10000 ? ReadValue<float>(record, 0x20) : 0.0f, stain,
                    stain >= 0x10000 ? ReadValue<u8>(stain, 0xC0) : 0xFF,
                    stain >= 0x10000 ? ReadValue<u64>(stain, 0x88) : 0,
                    stain >= 0x10000 ? ReadValue<u64>(stain, 0x98) : 0,
                    global >= 0x10000 ? ReadValue<u8>(global, 0x1592) : 0xFF,
                    global >= 0x10000 ? ReadValue<u8>(global, 0x1770) : 0xFF,
                    player >= 0x10000 ? ReadValue<s32>(player, 0x78) : -1);
    if (state != last) {
        last = state;
        LOG_INFO(Debug, "Bloodborne bloodstain state map={:#x} {}", CurrentPlace().map, state);
    }
}

constexpr u64 ShowBloodstainOffset = 0x01868A50;

// See ShowsGuestBloodstain: a guest's own stain in the host's world.
void ShowGuestBloodstain() {
    static int tries = 0;
    static u64 tried_visit = 0;
    const u64 data = ReadValue<u64>(image_base + GameDataManPointerOffset, 0);
    const u64 record = data >= 0x10000 ? ReadValue<u64>(data, 0x30) : 0;
    const u64 global = GetGlobalState();
    const u64 player = GetLocalPlayer();
    const Place place = CurrentPlace();
    const u32 map = place.state == PlaceState::Playing ? place.map : 0;
    if (data < 0x10000 || record < 0x10000 || player < 0x10000) {
        return;
    }
    if (place.visit != tried_visit) {
        tries = 0; // a load: a new visit
        tried_visit = place.visit;
    }
    const u64 stain = OwnBloodstain();
    const bool party_guest =
        party_controller && party_controller->SearchEffectId() == SmallResonantBellEffect;
    const bool loading = global < 0x10000 || TryRead<u8>(global, 0x08).value_or(1) != 0;
    if (!LooksLikeBloodstain(stain) ||
        !ShowsGuestBloodstain(party_guest, InPartySession(), loading,
                              ReadValue<u8>(data, 0x28) != 0, ReadValue<s32>(record, 0x38),
                              ReadValue<u32>(record, 0x3C), IsUsablePackedMap(map) ? map : 0,
                              ReadValue<u64>(stain, 0x88) != 0, tries)) {
        return;
    }
    ++tries;
    reinterpret_cast<void PS4_SYSV_ABI (*)(u64, bool, bool)>(image_base + ShowBloodstainOffset)(
        stain, true, false);
    LOG_INFO(Debug,
             "Bloodborne party: showed the guest's own bloodstain ({} echoes) in the host's "
             "world on map {:#x}: marker {:#x}",
             ReadValue<s32>(record, 0x38), map, ReadValue<u64>(stain, 0x88));
}

// See RestoresBloodstainMark: a party guest keeps its lost echoes through a join.
void KeepGuestBloodstainMark() {
    static bool mark_before = false;
    const u64 data = ReadValue<u64>(image_base + GameDataManPointerOffset, 0);
    const u64 record = data >= 0x10000 ? ReadValue<u64>(data, 0x30) : 0;
    if (data < 0x10000 || record < 0x10000) {
        return;
    }
    const bool mark_now = ReadValue<u8>(data, 0x28) != 0;
    const s32 echoes = ReadValue<s32>(record, 0x38);
    const u32 block = ReadValue<u32>(record, 0x3C);
    const bool party_guest =
        party_controller && party_controller->SearchEffectId() == SmallResonantBellEffect;
    if (RestoresBloodstainMark(party_guest, mark_before, mark_now, echoes, block)) {
        WriteValue(data, 0x28, u8{1});
        LOG_INFO(Debug, "Bloodborne party: kept the guest's lost echoes ({}) through the join",
                 echoes);
        return;
    }
    mark_before = mark_now;
}

void ObserveLocalAliveState();

// Diagnostics for a guest reviving in place (Phase 2): each side logs the other players'
// character type and HP (WorldChrMan +0x30 count, +0x38 entries of 0x38 bytes) when they change.
void LogOtherPlayersChanges() {
    static std::string last;
    const u64 world = ReadValue<u64>(image_base + CandidateLocalStatePointerOffset, 0);
    const s32 count = world >= 0x10000 ? ReadValue<s32>(world, 0x30) : 0;
    const u64 entries = world >= 0x10000 ? ReadValue<u64>(world, 0x38) : 0;
    std::string state;
    for (s32 index = 0; index < std::min(count, 8); ++index) {
        const u64 player = ReadValue<u64>(entries + static_cast<u64>(index) * 0x38, 0);
        if (player < 0x10000) {
            continue;
        }
        const u64 data = ReadValue<u64>(ReadValue<u64>(player, 0x3B0), 0x20);
        // The death flags too: a guest that got up invisible to the host (friend run
        // 2026-10-09) left no trace here.
        state += fmt::format(" [type={} hp={}/{} dead={:#x}]", ReadValue<s32>(player, 0x78),
                             ReadValue<s32>(data, 0x128), ReadValue<s32>(data, 0x12C),
                             ReadValue<u8>(player, 0x1E3));
    }
    if (state != last) {
        last = state;
        LOG_INFO(Debug, "Bloodborne other players:{}", state.empty() ? " none" : state);
    }
}

// Diagnostics for a guest reviving in place (Phase 2): the local player's active special
// effects, logged when the set changes, to see what a phantom's death leaves on it.
void LogLocalEffectChanges() {
    static std::string last;
    const u64 player = GetLocalPlayer();
    const u64 manager = player >= 0x10000 ? ReadValue<u64>(player, 0x1C8) : 0;
    std::string effects;
    u64 node = manager >= 0x10000 ? ReadValue<u64>(manager, 0x08) : 0;
    for (size_t index = 0; node >= 0x10000 && index < 128; ++index) {
        const u64 next = ReadValue<u64>(node, 0x58);
        if ((ReadValue<u32>(node, 0x1C) & 0x800C0003U) == 0) {
            effects += fmt::format(" {}", ReadValue<s32>(node, 0x40));
        }
        if (next == node) {
            break;
        }
        node = next;
    }
    if (effects != last) {
        last = effects;
        LOG_INFO(Debug, "Bloodborne local effects:{}", effects.empty() ? " none" : effects);
    }
}

// Diagnostics for a guest that can move and attack but not use X after a boss kill: the states
// a boss clear or a leave could leave behind, logged on change. The event manager's "returning"
// state ([SprjEventMan+0x10]+8, see ReviveCalls::clear_return_state), the player's state flags
// (+0x1E0..+0x1E7) and action state ([+0x3B0]+8: +0x1B4/+0x1B8), its character type (+0x78),
// GlobalState +0x1520 (summon warp) and +0x1592 (pickups allowed), and the leave/death flags.
void LogActionBlockingState() {
    static std::string last;
    static std::chrono::steady_clock::time_point checked{};
    const auto now = std::chrono::steady_clock::now();
    if (now - checked < std::chrono::milliseconds{500}) {
        return;
    }
    checked = now;
    const u64 player = GetLocalPlayer();
    const u64 global = GetGlobalState();
    const u64 events = ReadValue<u64>(image_base + EventManPointerOffset, 0);
    const u64 event_state = events >= 0x10000 ? ReadValue<u64>(events, 0x10) : 0;
    const u64 model = player >= 0x10000 ? ReadValue<u64>(player, 0x3B0) : 0;
    const u64 action = model >= 0x10000 ? ReadValue<u64>(model, 0x08) : 0;
    const std::string text = fmt::format(
        "returning={} flags={:#x} action={} sub={} type={} warp={} pickups={} f4000={} f4047={}",
        event_state >= 0x10000 ? ReadValue<s32>(event_state, 0x08) : -1,
        player >= 0x10000 ? ReadValue<u64>(player, 0x1E0) : 0,
        action >= 0x10000 ? ReadValue<s32>(action, 0x1B4) : -1,
        action >= 0x10000 ? ReadValue<s32>(action, 0x1B8) : -1,
        player >= 0x10000 ? ReadValue<s32>(player, 0x78) : -1,
        global >= 0x10000 ? ReadValue<u8>(global, 0x1520) : 0xFF,
        global >= 0x10000 ? ReadValue<u8>(global, 0x1592) : 0xFF, EventFlagOn(4000),
        EventFlagOn(4047));
    if (text != last) {
        last = text;
        LOG_INFO(Debug, "Bloodborne action state: {}", text);
    }
}

} // namespace

namespace Coop {

std::vector<s32> LocalEffects() {
    return EffectsOf(GetLocalPlayer());
}

std::vector<s32> EffectsOf(u64 character) {
    std::vector<s32> effects;
    const u64 manager = character >= 0x10000 ? ReadValue<u64>(character, 0x1C8) : 0;
    u64 node = manager >= 0x10000 ? ReadValue<u64>(manager, 0x08) : 0;
    for (size_t index = 0; node >= 0x10000 && index < 128; ++index) {
        const u64 next = ReadValue<u64>(node, 0x58);
        if ((ReadValue<u32>(node, 0x1C) & 0x800C0003U) == 0) {
            effects.push_back(ReadValue<s32>(node, 0x40));
        }
        if (next == node) {
            break;
        }
        node = next;
    }
    return effects;
}

} // namespace Coop

namespace {

// GlobalState +0x16F8 -> +0x14: players in the session (the count ESD function 52 checks).
constexpr u64 SessionPlayersOffset = 0x16F8;
std::jthread rest_board_client;

} // namespace

namespace Coop {

// Seamless travel: the map this member is on while in a party session (0 otherwise), posted to
// the party's map board by the same client thread, and the board it read back.
std::atomic<u32> party_map_now{};
std::mutex flag_board_mutex;

} // namespace Coop

namespace {

std::vector<std::pair<s32, bool>> flags_to_post;

} // namespace

namespace Coop {

// Kill echoes shared across maps (see SharesKillEchoes): the local kills made apart wait here
// for the board thread, and the other members' wait for the game thread to add them.
std::mutex echo_board_mutex;

} // namespace Coop

namespace {

std::vector<KillEchoes> echoes_to_post;

} // namespace

namespace Coop {

std::vector<KillEchoes> echoes_to_grant;
// Enemy deaths kept in step across loads (see VisitKills): this member's kills and visit ends
// wait for the board thread; the others' current kills (character handles) are hidden here.
std::mutex kill_board_mutex;
std::vector<std::pair<s32, bool>> kills_to_post;

} // namespace Coop

namespace {

PartyKills party_kills; // the other members' standing kills, by member

} // namespace

namespace Coop {

std::unordered_set<u32> others_kills;
// World state per map (see MapFlagBase): the host's own snapshot waiting for the board thread,
// and the latest one read back (on a guest).
// Enemy HP kept in step (see EnemyHpReport): this member's hurt enemies and the others'.
std::mutex enemy_hp_mutex;
MemberMapState local_enemy_hp;
std::vector<MemberMapState> others_enemy_hp;
MemberMapState local_map_state;
std::optional<MemberMapState> host_map_state;
MemberMapState local_shown;
std::vector<std::pair<s32, bool>> flags_to_set;

void PostWorldChanges(const FlagChanges& changes) {
    std::scoped_lock lock{flag_board_mutex};
    const auto shared = FlagsToShare(changes);
    flags_to_post.insert(flags_to_post.end(), shared.begin(), shared.end());
}

bool InPartySession() {
    const u64 multi_play = GetMatchingState();
    return party_controller.has_value() && multi_play >= 0x10000 &&
           HasMemoryAccess(multi_play, 0x128, MemoryProt::CpuRead) &&
           IsPartySession(true, ReadValue<s32>(multi_play, 0x124));
}

s32 SessionPlayers() {
    const u64 global = GetGlobalState();
    const u64 session = global >= 0x10000 ? ReadValue<u64>(global, SessionPlayersOffset) : 0;
    return session >= 0x10000 ? ReadValue<s32>(session, 0x14) : 0;
}

// A character's position, where the healing fountain update reads the player's ([+0x3B0]+0x68,
// then +0x1E0).
std::optional<LampPosition> CharacterPosition(u64 character) {
    const u64 modules = character >= 0x10000 ? ReadValue<u64>(character, 0x3B0) : 0;
    const u64 physics = modules >= 0x10000 ? ReadValue<u64>(modules, 0x68) : 0;
    if (physics < 0x10000 || !HasMemoryAccess(physics, 0x1EC, MemoryProt::CpuRead)) {
        return std::nullopt;
    }
    return LampPosition{ReadValue<float>(physics, 0x1E0), ReadValue<float>(physics, 0x1E4),
                        ReadValue<float>(physics, 0x1E8)};
}

// The local player's position.
std::optional<LampPosition> LocalPlayerPosition() {
    return CharacterPosition(GetLocalPlayer());
}
std::vector<OtherPlayer> OtherPlayers() {
    std::vector<OtherPlayer> players;
    const u64 world = ReadValue<u64>(image_base + CandidateLocalStatePointerOffset, 0);
    const s32 count = world >= 0x10000 ? ReadValue<s32>(world, 0x30) : 0;
    const u64 entries = world >= 0x10000 ? ReadValue<u64>(world, 0x38) : 0;
    const u64 local = GetLocalPlayer();
    for (s32 index = 0; entries >= 0x10000 && index < std::min(count, 8); ++index) {
        const u64 player = ReadValue<u64>(entries + static_cast<u64>(index) * 0x38, 0);
        if (player < 0x10000 || player == local ||
            !HasMemoryAccess(player, 0x3B8, MemoryProt::CpuRead)) {
            continue;
        }
        OtherPlayer other{player, ReadValue<s32>(player, 0x78)};
        const u64 modules = ReadValue<u64>(player, 0x3B0);
        const u64 data = modules >= 0x10000 ? ReadValue<u64>(modules, 0x20) : 0;
        other.hp = data >= 0x10000 ? ReadValue<s32>(data, 0x128) : 0;
        other.dead_flags = ReadValue<u8>(player, 0x1E3);
        const u64 physics = modules >= 0x10000 ? ReadValue<u64>(modules, 0x68) : 0;
        if (physics >= 0x10000 && HasMemoryAccess(physics, 0x1EC, MemoryProt::CpuRead)) {
            other.at =
                LampPosition{ReadValue<float>(physics, 0x1E0), ReadValue<float>(physics, 0x1E4),
                             ReadValue<float>(physics, 0x1E8)};
        }
        players.push_back(other);
    }
    return players;
}

} // namespace Coop

namespace {

// The co-op trace (see CoopTraceVersion): records from any thread go to a queue that a thread of
// its own writes out, so the game never waits for the disk. Off with SHADPS4_BLOODBORNE_TRACE=0.
std::atomic<bool> coop_trace_on{false};
TraceQueue coop_trace_queue{TraceQueueCap};

s64 UtcMilliseconds() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace

namespace Coop {

bool CoopTraceOn() {
    return coop_trace_on.load(std::memory_order_relaxed);
}

void Trace(std::string kind, TraceSampler::Fields fields) {
    if (CoopTraceOn()) {
        coop_trace_queue.Push(
            EncodeTraceRecord({UtcMilliseconds(), std::move(kind), std::move(fields)}));
    }
}

void TraceLife(const char* event, TraceSampler::Fields more) {
    more.insert(more.begin(), std::pair<std::string, std::string>{"event", event});
    Trace("life", std::move(more));
}

} // namespace Coop

namespace {

// A co-op frame sent or taken (see DecodeCoopFrame); `from_peer` for one taken: whether it came
// from a room peer (only those are used).
void TraceFrame(const char* kind, std::span<const u8> frame, u32 addr, u16 port,
                std::optional<bool> from_peer = std::nullopt) {
    if (!CoopTraceOn()) {
        return;
    }
    const auto decoded = DecodeCoopFrame(frame);
    if (!decoded) {
        return;
    }
    TraceSampler::Fields fields{{"type", fmt::format("{:02x}", decoded->first)},
                                {"peer", fmt::format("{:08x}:{}", addr, port)}};
    if (from_peer) {
        fields.emplace_back("from-peer", *from_peer ? "1" : "0");
    }
    fields.emplace_back("payload", TraceHex(decoded->second));
    Trace(kind, std::move(fields));
}

// A board post and its answer. A body the same as the endpoint's last one in this file is only
// noted ("same"): the HP board posts every 100 ms.
void TraceBoard(const char* kind, const std::string& endpoint, std::optional<int> status,
                std::string_view body) {
    static std::mutex mutex;
    static std::map<std::string, size_t> last_body; // kind and endpoint -> the last body's hash
    static u32 last_file = 0;
    const size_t hash = std::hash<std::string_view>{}(body);
    bool same = false;
    {
        std::scoped_lock lock{mutex};
        if (const u32 file = coop_trace_file.load(); file != last_file) {
            last_file = file;
            last_body.clear();
        }
        const auto [it, added] = last_body.try_emplace(fmt::format("{} {}", kind, endpoint), hash);
        same = !added && it->second == hash;
        it->second = hash;
    }
    TraceRecord record{0, kind, {{"endpoint", endpoint}}};
    if (status) {
        record.fields.emplace_back("status", std::to_string(*status));
    }
    if (same) {
        record.fields.emplace_back("same", "1");
    } else {
        AddTraceBody(record, body);
    }
    Trace(std::move(record.kind), std::move(record.fields));
}

// A post to one of the party's boards, traced (the request before it goes, the answer after).
httplib::Result PostToBoard(httplib::Client& client, const std::string& endpoint,
                            const httplib::Headers& headers, const std::string& body) {
    if (CoopTraceOn()) {
        TraceBoard("board-out", endpoint, std::nullopt, body);
    }
    auto response = client.Post(endpoint, headers, body, "text/plain");
    if (CoopTraceOn()) {
        if (response) {
            TraceBoard("board-in", endpoint, response->status, response->body);
        } else {
            Trace("board-in",
                  {{"endpoint", endpoint}, {"error", httplib::to_string(response.error())}});
        }
    }
    return response;
}

} // namespace

namespace Coop {

// Now in the PC's local time, as the player reads it (file names).
std::chrono::sys_seconds LocalTimeNow() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    using namespace std::chrono;
    return sys_days{year{local.tm_year + 1900} / static_cast<unsigned>(local.tm_mon + 1) /
                    static_cast<unsigned>(local.tm_mday)} +
           hours{local.tm_hour} + minutes{local.tm_min} + seconds{local.tm_sec};
}

// The trace's writer: a new file at the start and whenever one reaches TraceFileCap, the newest
// TraceFilesKept kept. Lines are flushed every quarter second, so a crash loses little.
void StartCoopTrace() {
    if (!CoopTraceEnabled(std::getenv("SHADPS4_BLOODBORNE_TRACE"))) {
        LOG_INFO(Debug, "Bloodborne co-op trace is off");
        return;
    }
    const std::filesystem::path dir = Common::FS::GetUserPath(Common::FS::PathType::LogDir);
#ifdef _WIN32
    const u32 process = static_cast<u32>(_getpid());
#else
    const u32 process = static_cast<u32>(getpid());
#endif
    const char* role = std::getenv("SHADPS4_BLOODBORNE_PARTY_ROLE");
    coop_trace_on = true;
    std::thread([dir, process, role = std::string{role != nullptr ? role : ""}] {
        std::ofstream file;
        std::uintmax_t written = 0;
        const auto open = [&] {
            file.close();
            std::error_code error;
            std::filesystem::create_directories(dir, error);
            file.open(dir / CoopTraceFileName(LocalTimeNow(), process),
                      std::ios::binary | std::ios::trunc);
            ++coop_trace_file;
            PruneCoopTraces(dir, TraceFilesKept);
            const std::string header =
                EncodeTraceRecord({UtcMilliseconds(),
                                   "trace",
                                   {{"version", std::to_string(CoopTraceVersion)},
                                    {"role", role},
                                    {"process", std::to_string(process)}}});
            file << header << '\n';
            written = header.size() + 1;
        };
        open();
        while (true) {
            std::vector<std::string> lines = coop_trace_queue.Take(std::chrono::milliseconds{250});
            if (const u64 dropped = coop_trace_queue.TakeDropped(); dropped > 0) {
                lines.push_back(EncodeTraceRecord(
                    {UtcMilliseconds(), "trace-dropped", {{"count", std::to_string(dropped)}}}));
            }
            for (const std::string& line : lines) {
                file << line << '\n';
                written += line.size() + 1;
            }
            if (!lines.empty()) {
                file.flush();
            }
            if (written >= TraceFileCap) {
                open();
            }
        }
    }).detach();
    LOG_INFO(Debug, "Bloodborne co-op trace: {}{}*.txt (the newest {} kept)",
             Common::FS::PathToUTF8String(dir / ""), CoopTracePrefix, TraceFilesKept);
}

} // namespace Coop

namespace {

// Posts this member's rest about once a second while it rests (once more when it stands up) and
// keeps the board the server answers with. Runs on its own thread; the game never waits for it.
// The signed-in user's online id, or "" before shadNet's login.
std::string LocalOnlineId() {
    int user_id = -1;
    if (Libraries::UserService::sceUserServiceGetInitialUser(&user_id) != 0 || user_id < 0) {
        return {};
    }
    auto& np = Libraries::Np::NpHandler::GetInstance();
    if (!np.IsPsnSignedIn(user_id)) {
        return {};
    }
    const auto online_id = np.GetOnlineId(user_id);
    return std::string{online_id.data, strnlen(online_id.data, sizeof(online_id.data))};
}

// The boards know a member by its online id (hex, see BoardMemberOfOnlineId): a party of three
// tells its session members apart by it. Random if shadNet never signs in.
std::string FindBoardMember(std::stop_token stop) {
    std::string member;
    for (int attempt = 0; attempt < 120 && !stop.stop_requested() && member.empty(); ++attempt) {
        if (const std::string online_id = LocalOnlineId(); !online_id.empty()) {
            member = BoardMemberOfOnlineId(online_id);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds{500});
        }
    }
    if (member.empty()) {
        std::array<u8, 8> random_bytes{};
        std::random_device random;
        for (auto& byte : random_bytes) {
            byte = static_cast<u8>(random());
        }
        member = BytesToHex(random_bytes);
    }
    return member;
}

void RunRestBoardClient(std::stop_token stop, std::string base_url, std::string party) {
    const std::string member = FindBoardMember(stop);
    httplib::Client client(base_url);
    client.set_connection_timeout(std::chrono::seconds{1});
    client.set_read_timeout(std::chrono::seconds{2});
    const httplib::Headers headers{{"X-ShadPS4-Bloodborne-Party", party}};
    LOG_INFO(Debug, "Bloodborne party rest board client for {} as member {}", base_url, member);

    std::mutex wait_mutex;
    std::condition_variable_any wait;
    s32 posted = NotResting;
    bool failing = false;
    auto next_trophy_post = std::chrono::steady_clock::now();
    auto next_echo_post = std::chrono::steady_clock::now();
    // The first post asks for nothing older than now: the board's history belongs to whatever
    // characters played before (a new character must not get it).
    s64 flag_board_since = std::numeric_limits<s64>::max();
    s64 echo_board_since = std::numeric_limits<s64>::max();
    s64 kill_board_since = std::numeric_limits<s64>::max();
    while (!stop.stop_requested()) {
        {
            std::unique_lock lock{wait_mutex};
            wait.wait_for(lock, stop, std::chrono::seconds{1}, [] { return false; });
        }
        if (const u32 map = party_map_now.load(); map != 0) {
            const auto map_response =
                PostToBoard(client, "/bloodborne/party/map", headers, EncodeMapUpdate(member, map));
            if (const auto maps = map_response && map_response->status == 200
                                      ? ParseMapBoard(map_response->body)
                                      : std::nullopt) {
                std::scoped_lock lock{map_board_mutex};
                map_board = *maps;
                map_board_member = member;
                map_board_time = std::chrono::steady_clock::now();
            }
        }
        {
            // The host posts its map's characters; a guest only reads (map 0).
            MemberMapState mine;
            {
                std::scoped_lock lock{shown_board_mutex};
                mine = local_shown;
            }
            const auto shown_response =
                PostToBoard(client, "/bloodborne/party/shown", headers,
                            EncodeMapStateUpdate(member, mine.map, mine.bits));
            if (shown_response && shown_response->status == 200) {
                const auto states = ParseMapStateBoard(shown_response->body);
                std::scoped_lock lock{shown_board_mutex};
                host_shown =
                    states && !states->empty() ? std::optional{states->front()} : std::nullopt;
            }
        }
        // Kill echoes go both ways in and out of a session: a member on its own trip (a guest
        // in the Hunter's Dream) still gets the party's kills made apart.
        if (std::chrono::steady_clock::now() >= next_echo_post) {
            MemberMapState mine;
            {
                std::scoped_lock lock{map_state_mutex};
                mine = local_map_state;
            }
            const auto map_state_response =
                PostToBoard(client, "/bloodborne/party/mapstate", headers,
                            EncodeMapStateUpdate(member, mine.map, mine.bits));
            if (const auto states = map_state_response && map_state_response->status == 200
                                        ? ParseMapStateBoard(map_state_response->body)
                                        : std::nullopt;
                states && !states->empty()) {
                std::scoped_lock lock{map_state_mutex};
                host_map_state = states->front();
            }
            std::vector<std::pair<s32, bool>> kills_out;
            {
                std::scoped_lock lock{kill_board_mutex};
                kills_out.swap(kills_to_post);
            }
            const auto kill_response =
                PostToBoard(client, "/bloodborne/party/kills", headers,
                            EncodeFlagUpdate(member, kill_board_since, kills_out));
            if (const auto kills = kill_response && kill_response->status == 200
                                       ? ParseKillBoard(kill_response->body)
                                       : std::nullopt) {
                kill_board_since = kills->latest;
                std::scoped_lock lock{kill_board_mutex};
                party_kills.Apply(kills->changes);
                others_kills = party_kills.All();
            } else {
                std::scoped_lock lock{kill_board_mutex};
                kills_to_post.insert(kills_to_post.begin(), kills_out.begin(), kills_out.end());
            }
            next_echo_post = std::chrono::steady_clock::now() + std::chrono::seconds{3};
            std::vector<KillEchoes> echoes_out;
            {
                std::scoped_lock lock{echo_board_mutex};
                echoes_out.swap(echoes_to_post);
            }
            const auto echo_response =
                PostToBoard(client, "/bloodborne/party/echoes", headers,
                            EncodeEchoUpdate(member, echo_board_since, echoes_out));
            if (const auto echoes = echo_response && echo_response->status == 200
                                        ? ParseEchoBoard(echo_response->body)
                                        : std::nullopt) {
                echo_board_since = echoes->latest;
                std::scoped_lock lock{echo_board_mutex};
                echoes_to_grant.insert(echoes_to_grant.end(), echoes->echoes.begin(),
                                       echoes->echoes.end());
            } else {
                std::scoped_lock lock{echo_board_mutex};
                echoes_to_post.insert(echoes_to_post.begin(), echoes_out.begin(), echoes_out.end());
            }
        }
        if (trophy_board_active.load() && std::chrono::steady_clock::now() >= next_trophy_post) {
            next_trophy_post = std::chrono::steady_clock::now() + std::chrono::seconds{3};
            std::vector<s32> earned;
            {
                std::scoped_lock lock{trophy_mutex};
                earned = trophies_earned_in_party;
            }
            const auto trophy_response = PostToBoard(client, "/bloodborne/party/trophy", headers,
                                                     EncodeTrophyUpdate(member, earned));
            if (const auto trophies = trophy_response && trophy_response->status == 200
                                          ? ParseTrophyBoard(trophy_response->body)
                                          : std::nullopt) {
                ReadTrophyBoard(*trophies);
            }
            std::vector<std::pair<s32, bool>> outgoing;
            {
                std::scoped_lock lock{flag_board_mutex};
                outgoing.swap(flags_to_post);
            }
            const auto flag_response =
                PostToBoard(client, "/bloodborne/party/flags", headers,
                            EncodeFlagUpdate(member, flag_board_since, outgoing));
            const auto board = flag_response && flag_response->status == 200
                                   ? ParseFlagBoard(flag_response->body)
                                   : std::nullopt;
            if (!board) {
                std::scoped_lock lock{flag_board_mutex};
                flags_to_post.insert(flags_to_post.begin(), outgoing.begin(), outgoing.end());
            } else {
                flag_board_since = board->latest;
                if (!board->changes.empty()) {
                    std::scoped_lock lock{flag_board_mutex};
                    flags_to_set.insert(flags_to_set.end(), board->changes.begin(),
                                        board->changes.end());
                }
            }
        }
        // In a session every member reads the board (to join a rest nearby); outside one only
        // a member that still has a rest on it posts, to take it off.
        const bool active = rest_board_active.load();
        const s32 lamp = active ? rest_board_lamp.load() : NotResting;
        if (!active && posted == NotResting) {
            continue;
        }
        std::optional<LampPosition> at;
        {
            std::scoped_lock lock{rest_board_position_mutex};
            at = rest_board_position;
        }
        const auto response = PostToBoard(client, "/bloodborne/party/rest", headers,
                                          EncodeLampRestUpdate(member, lamp, at));
        const auto board =
            response && response->status == 200 ? ParseLampRestBoard(response->body) : std::nullopt;
        if (!board) {
            if (!failing) {
                LOG_WARNING(Debug, "Bloodborne party rest board unreachable ({})",
                            response ? std::to_string(response->status)
                                     : httplib::to_string(response.error()));
            }
            failing = true;
            continue;
        }
        failing = false;
        posted = lamp;
        std::scoped_lock lock{rest_board_mutex};
        rest_board = *board;
        rest_board_time = std::chrono::steady_clock::now();
    }
}

} // namespace

namespace Coop {

// Enemy HP (see SharedDamage) on its own client, every HpBoardRound and at once when this
// member's own damage changes (see HpBoardWait): on the rest board's one-second round the other
// members' hits showed late (friend run 2026-10-10), and on a 200 ms round the host's two-hit
// kills were over before the guests' bars moved (forest run 2026-10-11).
std::condition_variable_any enemy_hp_wake;
bool enemy_hp_changed = false; // enemy_hp_mutex

} // namespace Coop

namespace {

void RunHpBoardClient(std::stop_token stop, std::string base_url, std::string party) {
    const std::string member = FindBoardMember(stop);
    httplib::Client client(base_url);
    client.set_keep_alive(true);
    client.set_connection_timeout(std::chrono::seconds{1});
    client.set_read_timeout(std::chrono::seconds{1});
    const httplib::Headers headers{{"X-ShadPS4-Bloodborne-Party", party}};
    while (!stop.stop_requested()) {
        MemberMapState mine;
        {
            std::unique_lock lock{enemy_hp_mutex};
            enemy_hp_wake.wait_for(lock, stop, HpBoardWait(enemy_hp_changed),
                                   [] { return enemy_hp_changed; });
            enemy_hp_changed = false;
            mine = local_enemy_hp;
        }
        if (mine.map == 0) {
            continue;
        }
        const auto hp_response = PostToBoard(client, "/bloodborne/party/hp", headers,
                                             EncodeMapStateUpdate(member, mine.map, mine.bits));
        if (const auto states = hp_response && hp_response->status == 200
                                    ? ParseMapStateBoard(hp_response->body)
                                    : std::nullopt) {
            std::scoped_lock lock{enemy_hp_mutex};
            others_enemy_hp = *states;
        } else {
            static auto last_logged = std::chrono::steady_clock::time_point{};
            if (std::chrono::steady_clock::now() - last_logged > std::chrono::seconds{10}) {
                last_logged = std::chrono::steady_clock::now();
                LOG_INFO(Debug, "Bloodborne party: HP board post failed (status {})",
                         hp_response ? hp_response->status : -1);
            }
        }
    }
}
std::jthread hp_board_client;

} // namespace

namespace Coop {

void StartRestBoardClient() {
    const char* party_setting = std::getenv("SHADPS4_BLOODBORNE_PARTY");
    const auto party = party_setting ? ParsePartySetting(party_setting) : std::nullopt;
    const std::string base_url = EmulatorSettings.GetShadNetWebApiServer();
    if (!lamp_rest_hooks_installed || !party || !EmulatorSettings.IsShadNetEnabled() ||
        base_url.empty()) {
        return;
    }
    Libraries::Np::NpTrophy::SetTrophyUnlockObserver(NoteGameTrophyUnlock);
    rest_board_client = std::jthread(RunRestBoardClient, base_url, *party);
    hp_board_client = std::jthread(RunHpBoardClient, base_url, *party);
}

bool IsCharacterDisabled(u64 character) {
    const u64 control = ReadValue<u64>(character, 0x18);
    return control >= 0x10000 && (ReadValue<u8>(control, 0x20) & 1) != 0;
}

s32 CharacterHp(u64 character) {
    const u64 modules = ReadValue<u64>(character, 0x3B0);
    const u64 data = modules >= 0x10000 ? ReadValue<u64>(modules, 0x20) : 0;
    return data >= 0x10000 ? ReadValue<s32>(data, 0xF8) : 0;
}

} // namespace Coop

namespace {

void TickAutoplay();
void TickPartyDeath();

// The tracker's reading for this tick (the only raw map read besides the load-time hooks, the
// trip machinery and the rig), first in the tick so every feature after it sees this tick's
// place. Before TickPartyTrip: the tick that ends a trip still reads it as one (a lamp trip).
void TickPlace() {
    const u64 global = GetGlobalState();
    const u64 player = GetLocalPlayer();
    const u64 modules = player >= 0x10000 ? ReadValue<u64>(player, 0x3B0) : 0;
    const u64 data = modules >= 0x10000 ? ReadValue<u64>(modules, 0x20) : 0;
    const bool trip = PartyTripActive();
    const PlaceReading reading{
        .now_ms = SteadyMilliseconds(),
        .map = GetCurrentPackedMap(),
        .player = player >= 0x10000 ? player : 0,
        .at = LocalPlayerPosition(),
        .loading = global < 0x10000 || TryRead<u8>(global, 0x08).value_or(1) != 0,
        .trip = trip,
        .dead = data >= 0x10000 && ReadValue<s32>(data, 0xF8) <= 0,
    };
    std::optional<PlaceChange> change;
    std::vector<std::function<void(const PlaceChange&)>> listeners;
    {
        std::scoped_lock lock{place_mutex};
        change = place_tracker.Update(reading);
        if (change) {
            listeners = place_listeners;
        }
    }
    if (change) {
        for (const auto& listener : listeners) {
            listener(*change);
        }
    }
    // Into and out of a party session (not during a trip: seamless travel keeps the session
    // through the world's teardown).
    static bool was_in_session = false;
    if (!trip) {
        if (const bool in_session = InPartySession(); in_session != was_in_session) {
            was_in_session = in_session;
            OnLifecycle(in_session ? Lifecycle::Join : Lifecycle::Leave);
        }
    }
}

void TickPartyController() {
    NoteGameThreadTick();
    TickPlace();
    TickAutoplay();
    TickPartyTrip();
    TickPlayersOnThisMap();
    LogJoinTaskChanges();
    WatchSummonJoin();
    KeepGuestBloodstainMark();
    ShowGuestBloodstain();
    LogBloodstainStateChanges();
    LogLocalEffectChanges();
    if (party_controller) {
        LogActionBlockingState();
        KeepBossHpInStep();
        LogNearbyEnemyControl();
    }
    LogOtherPlayersChanges();
    ObserveLocalAliveState();
    TickPartyDeath();
    TickLampRest();
    if (!party_controller) {
        return;
    }
    ReopenClosedPartySession();
    const u64 player = GetLocalPlayer();
    const PartyObservation observation = ObserveParty(player, party_controller->SearchEffectId());
    if (party_controller->Tick(observation, PartyController::Clock::now()) !=
        PartyAction::StartSearch) {
        return;
    }
    // A bell that outlasted its search (see PartyController::Tick): the game starts a search only
    // when the effect goes on, so the old one comes off first.
    const bool ended = observation.search_effect_active &&
                       EndPlayerEffect(player, party_controller->SearchEffectId());
    if (ended) {
        LOG_INFO(Debug, "Bloodborne party: ended the host's stale bell effect {} to ring again",
                 party_controller->SearchEffectId());
    }
    const bool result = StartPlayerSummonSearch(player, party_controller->SearchEffectId());
    LOG_INFO(Debug, "Bloodborne party auto-rejoin started search effect={} map={:#x} result={}",
             party_controller->SearchEffectId(), CurrentPlace().map, result);
}
// The event system's "load wait" byte: SetLoadWait (0x013358F0) sets it, the leave handlers
// (0x013840E0 and others) set it with 4047, and only its constructor (0x01339669) clears it.
// Every event update copies it to SprjActionButtonMan +0x52 (0x0133A114): no action button.
constexpr u64 LoadWaitOffset = 0x8D;
constexpr u64 ActionButtonManPointerOffset = 0x05540048;

// The multiplayer walls (MultiplayerWallsOn) turned off in a party session the way the event
// scripts' own Lua_InvalidMultiWall does: collision and drawing off, the fog SFX invalidated.
// Event 7600 shows them again on its next multiplayer edge, so this runs about once a second.
constexpr u64 SetColiEnableOffset = 0x0132DEF0;
constexpr u64 SetDrawEnableOffset = 0x0132DEC0;
constexpr u64 InvalidSfxOffset = 0x01330870;

} // namespace

namespace Coop {

void TurnOffMultiplayerWalls(bool in_session, u32 map) {
    static std::chrono::steady_clock::time_point last{};
    const auto now = std::chrono::steady_clock::now();
    if (!in_session || now - last < std::chrono::seconds{1}) {
        return;
    }
    last = now;
    using EntitySwitch = void PS4_SYSV_ABI (*)(u64 unused, s32 entity, bool on);
    for (const MultiplayerWall& wall : MultiplayerWallsOn(map)) {
        reinterpret_cast<EntitySwitch>(image_base + SetColiEnableOffset)(0, wall.object, false);
        reinterpret_cast<EntitySwitch>(image_base + SetDrawEnableOffset)(0, wall.object, false);
        reinterpret_cast<EntitySwitch>(image_base + InvalidSfxOffset)(0, wall.sfx, true);
    }
}

} // namespace Coop

namespace {

// The loaded SpEffectParam, found through SoloParamRepository (0x05540340): its param slots hold
// a pointer whose +0x70 -> +0x70 is the PARAM file (the goods lookup at 0x01F1E3D0 reads one this
// way). Once found, row 9006 (the summoned guest's) gets a max HP rate of 1.0.
constexpr u64 SoloParamRepositoryPointerOffset = 0x05540340;

// Calls `found` with each loaded PARAM file of `type` (AtkParam_Pc and AtkParam_Npc share one)
// until it returns true; returns that file, or 0.
template <typename Found>
u64 FindLoadedParamWhere(std::string_view type, Found&& found) {
    const u64 repository = ReadValue<u64>(image_base + SoloParamRepositoryPointerOffset, 0);
    if (repository < 0x10000 || !HasMemoryAccess(repository, 0x2000, MemoryProt::CpuRead)) {
        return 0;
    }
    for (u64 slot = 0; slot < 0x2000; slot += 8) {
        const u64 holder = ReadValue<u64>(repository, slot);
        if (holder < 0x10000 || !HasMemoryAccess(holder, 0x78, MemoryProt::CpuRead)) {
            continue;
        }
        const u64 inner = ReadValue<u64>(holder, 0x70);
        if (inner < 0x10000 || !HasMemoryAccess(inner, 0x78, MemoryProt::CpuRead)) {
            continue;
        }
        const u64 param = ReadValue<u64>(inner, 0x70);
        if (param < 0x10000 || !HasMemoryAccess(param, 0x40, MemoryProt::CpuRead)) {
            continue;
        }
        if (IsParamOfType({reinterpret_cast<const u8*>(param), 0x40}, type) && found(param)) {
            return param;
        }
    }
    return 0;
}

} // namespace

namespace Coop {

u64 FindLoadedParam(std::string_view type) {
    return FindLoadedParamWhere(type, [](u64) { return true; });
}

void KeepGuestFullHp() {
    static bool done = false;
    static int attempts = 0;
    static std::chrono::steady_clock::time_point last{};
    const auto now = std::chrono::steady_clock::now();
    if (done || !party_controller || now - last < std::chrono::seconds{1}) {
        return;
    }
    last = now;
    const u64 param = FindLoadedParam("SP_EFFECT_PARAM_ST");
    if (param == 0) {
        if (++attempts == 30) {
            LOG_ERROR(Debug, "Bloodborne SpEffectParam not found in SoloParamRepository");
        }
        return;
    }
    done = true;
    const u16 rows = ReadValue<u16>(param, 0x0A);
    const u64 table_size = 0x40 + u64{rows} * 24;
    if (!HasMemoryAccess(param, table_size, MemoryProt::CpuRead)) {
        return;
    }
    const auto row =
        FindParamRow({reinterpret_cast<const u8*>(param), table_size}, PhantomHpEffect);
    if (!row || !HasMemoryAccess(param + *row, SpEffectMaxHpRateOffset + 4, MemoryProt::CpuWrite)) {
        LOG_ERROR(Debug, "Bloodborne SpEffect {} not found; summoned guests keep 70% HP",
                  PhantomHpEffect);
        return;
    }
    const float before = ReadValue<float>(param + *row, SpEffectMaxHpRateOffset);
    WriteValue(param + *row, SpEffectMaxHpRateOffset, 1.0f);
    LOG_INFO(Debug, "Bloodborne party guests keep full HP (SpEffect {} max HP rate {} -> 1)",
             PhantomHpEffect, before);
}

} // namespace Coop

namespace {

// Harder enemies (see HarderEnemies): NpcParam's HP and poise and AtkParam_Npc's damage are
// scaled in the loaded tables, which the game reads when it creates a character, so this runs
// from the title screen on (the pad reader and the save slot's opening call it). Row 271000 of
// each table (Father Gascoigne) shows whether the scaled values are still there; a table that
// was loaded again is scaled again.
const std::optional<HarderEnemies> harder_enemies = [] {
    const char* setting = std::getenv("SHADPS4_BLOODBORNE_HARDER_ENEMIES");
    return ParseHarderEnemies(setting != nullptr ? setting : "");
}();
std::mutex harder_enemies_mutex;

// A loaded PARAM file up to the end of its last row, when all of it is writable.
std::optional<std::span<u8>> WritableParam(u64 param, u64 row_size) {
    const u16 rows = ReadValue<u16>(param, 0x0A);
    const u64 entries = 0x40 + u64{rows} * 24;
    if (rows == 0 || !HasMemoryAccess(param, entries, MemoryProt::CpuRead)) {
        return std::nullopt;
    }
    u64 size = entries;
    for (u64 row = 0; row < rows; ++row) {
        size = std::max(size, ReadValue<u64>(param, 0x40 + row * 24 + 8) + row_size);
    }
    if (size > 0x1000000 || !HasMemoryAccess(param, size, MemoryProt::CpuWrite)) {
        return std::nullopt;
    }
    return std::span<u8>{reinterpret_cast<u8*>(param), size};
}

// Row 271000's {HP, poise} or {attack, 0} as this table holds it now.
std::optional<std::pair<s32, s32>> HarderEnemiesMarker(std::span<const u8> param, u64 first,
                                                       u64 second) {
    const auto row = FindParamRow(param, NpcAttackMarkerRow);
    if (!row || *row + second + 2 > param.size()) {
        return std::nullopt;
    }
    u32 one = 0;
    s16 two = 0;
    std::memcpy(&one, param.data() + *row + first, first == NpcHpOffset ? 4 : 2);
    if (second != 0) {
        std::memcpy(&two, param.data() + *row + second, sizeof(two));
    }
    return std::pair{static_cast<s32>(one), static_cast<s32>(two)};
}

} // namespace

namespace Coop {

bool EnemiesAreHarder() {
    return harder_enemies.has_value();
}

} // namespace Coop

namespace {

void MakeEnemiesHarder() {
    if (!harder_enemies) {
        return;
    }
    std::scoped_lock lock{harder_enemies_mutex};
    static std::chrono::steady_clock::time_point last{};
    static std::optional<std::pair<s32, s32>> npc_marker;
    static std::optional<std::pair<s32, s32>> attack_marker;
    static u64 npc_table = 0;
    static u64 attack_table = 0;
    const auto now = std::chrono::steady_clock::now();
    if (now - last < std::chrono::seconds{1}) {
        return;
    }
    last = now;
    constexpr u64 NpcRowSize = 0x184;
    constexpr u64 AtkRowSize = 0xA4;
    const u64 npc = FindLoadedParam("NPC_PARAM_ST");
    const u64 attacks = FindLoadedParamWhere("ATK_PARAM_ST", [](u64 param) {
        const u16 rows = ReadValue<u16>(param, 0x0A);
        const u64 entries = 0x40 + u64{rows} * 24;
        return HasMemoryAccess(param, entries, MemoryProt::CpuRead) &&
               FindParamRow({reinterpret_cast<const u8*>(param), entries}, NpcAttackMarkerRow);
    });
    const bool in_world = GetLocalPlayer() >= 0x10000;
    if (const auto table = npc != 0 ? WritableParam(npc, NpcRowSize) : std::nullopt) {
        if (npc != npc_table ||
            HarderEnemiesMarker(*table, NpcHpOffset, NpcPoiseOffset) != npc_marker) {
            const std::size_t rows = MakeNpcParamHarder(*table, *harder_enemies);
            npc_table = npc;
            npc_marker = HarderEnemiesMarker(*table, NpcHpOffset, NpcPoiseOffset);
            LOG_INFO(Debug,
                     "Bloodborne harder enemies: {} NpcParam rows x{} HP, x{} poise (Father "
                     "Gascoigne HP {}, poise {}){}",
                     rows, harder_enemies->hp, harder_enemies->poise,
                     npc_marker ? npc_marker->first : -1, npc_marker ? npc_marker->second : -1,
                     in_world ? "; enemies already on this map keep their old stats" : "");
        }
    }
    if (const auto table = attacks != 0 ? WritableParam(attacks, AtkRowSize) : std::nullopt) {
        if (attacks != attack_table ||
            HarderEnemiesMarker(*table, AtkPhysOffset, 0) != attack_marker) {
            const std::size_t rows = MakeNpcAttacksHarder(*table, harder_enemies->damage);
            attack_table = attacks;
            attack_marker = HarderEnemiesMarker(*table, AtkPhysOffset, 0);
            LOG_INFO(Debug,
                     "Bloodborne harder enemies: {} AtkParam_Npc rows x{} damage (Father "
                     "Gascoigne's first swing {}){}",
                     rows, harder_enemies->damage, attack_marker ? attack_marker->first : -1,
                     in_world ? "; enemies already on this map keep their old stats" : "");
        }
    }
    // Checked every time: a reloaded SpEffectParam gets the bonus back.
    if (const u64 effects = FindLoadedParam("SP_EFFECT_PARAM_ST"); effects != 0) {
        if (const auto table = WritableParam(effects, 0x210)) {
            if (const std::size_t rows = RemoveCoopBossHpBonus(*table); rows != 0) {
                LOG_INFO(Debug,
                         "Bloodborne harder enemies: the co-op boss HP bonus (SpEffect "
                         "7500-7503) is off in {} rows; bosses get x{} HP only",
                         rows, harder_enemies->hp);
            }
        }
    }
}

// The text of message `id` the way the game shows it (MessageLookupOffset), or null.
const char16_t* GameMessage(u64 repository, u32 category, s32 id) {
    using Lookup =
        const char16_t* PS4_SYSV_ABI (*)(u64 repository, u32 version, u32 category, s32 id);
    return reinterpret_cast<Lookup>(image_base + MessageLookupOffset)(repository, 0, category, id);
}

// See PolishedMessages: writes each replacement over the English text the game holds. Returns
// how many messages changed (0 once done, or for another language).
std::size_t PolishMessages() {
    const u64 repository = ReadValue<u64>(image_base + MsgRepositoryPointerOffset, 0);
    if (repository < 0x10000 || !HasMemoryAccess(repository, 0x10, MemoryProt::CpuRead)) {
        return 0;
    }
    std::size_t changed = 0;
    for (const PolishedMessage& message : PolishedMessages()) {
        // The network banners read the override category first, as the game does.
        const char16_t* text = nullptr;
        if (message.category == EventTextCategory) {
            text = GameMessage(repository, EventTextOverrideCategory, message.id);
        }
        if (text == nullptr) {
            text = GameMessage(repository, message.category, message.id);
        }
        const u64 address = reinterpret_cast<u64>(text);
        const std::size_t capacity = message.original.size() + 1;
        if (text == nullptr ||
            !HasMemoryAccess(address, capacity * sizeof(char16_t), MemoryProt::CpuReadWrite)) {
            continue;
        }
        const std::u16string_view current{text, message.original.size()};
        if (text[message.original.size()] != u'\0' ||
            PolishedText(message.category, message.id, current) != message.replacement) {
            continue;
        }
        std::u16string written{message.replacement};
        written.resize(capacity, u'\0');
        std::memcpy(reinterpret_cast<void*>(address), written.data(),
                    written.size() * sizeof(char16_t));
        ++changed;
    }
    return changed;
}

} // namespace

namespace Coop {

// Multiplayer UI polish for a party (Kaniel 2026-10-11): Host/Guest text and no bell icons, in
// the loaded message files and SpEffectParam. Checked every second: a reloaded file gets it
// again.
void PolishMultiplayerUi() {
    static std::chrono::steady_clock::time_point last{};
    const auto now = std::chrono::steady_clock::now();
    if (!party_controller || now - last < std::chrono::seconds{1}) {
        return;
    }
    last = now;
    if (const std::size_t messages = PolishMessages(); messages != 0) {
        LOG_INFO(Debug, "Bloodborne party UI: {} messages now say Host and Guest", messages);
    }
    if (const u64 effects = FindLoadedParam("SP_EFFECT_PARAM_ST"); effects != 0) {
        if (const auto table = WritableParam(effects, 0x210)) {
            if (const std::size_t rows = RemoveBellIcons(*table); rows != 0) {
                LOG_INFO(Debug, "Bloodborne party UI: no bell icons ({} SpEffect rows)", rows);
            }
        }
    }
}

} // namespace Coop

namespace {

// The level sync check (see LevelSyncEffect): `seta bl` has just decided whether this player is
// above the host's band; the next instructions compare it with the last answer ([r14+0x2C]) and
// add or remove effect 9200 when it changed. Clearing bl in a party session removes the effect.
constexpr u64 LevelSyncDecisionOffset = 0x01507DB5;
constexpr std::array<u8, 8> LevelSyncDecision{0x41, 0x0F, 0xB6, 0x46, 0x2C, 0x0F, 0xB6, 0xCB};

void PS4_SYSV_ABI KeepPartyGuestsLevel(u64, GuestRegisterSnapshot* registers) {
    if (!LevelSyncsGuest(InPartySession(), (registers->rbx & 0xFF) != 0)) {
        registers->rbx &= ~u64{0xFF};
    }
}

} // namespace

namespace Coop {

void InstallNoLevelSyncInParty(u64 base, u64 image_size) {
    if (!party_controller) {
        return;
    }
    if (LevelSyncDecisionOffset + LevelSyncDecision.size() > image_size ||
        !InstallCoopHook(reinterpret_cast<void*>(base + LevelSyncDecisionOffset), LevelSyncDecision,
                         0, KeepPartyGuestsLevel)) {
        LOG_ERROR(Debug, "Bloodborne level sync hook unavailable; party guests are synced to the "
                         "host's level");
        coop_features.Skipped("level sync", SiteFailure(base, image_size, LevelSyncDecisionOffset));
        return;
    }
    coop_features.On("level sync");
    LOG_INFO(Debug, "Bloodborne party guests keep their own level (no effect {})", LevelSyncEffect);
}

} // namespace Coop

namespace {

// Both NPC creation sites read the flag with `mov reg, [rax+0x145]` and test bit 7 right after.
// Pointing rax at an all-zero stand-in row for that read keeps the NPC a live character.
constexpr u64 NpcWanderCheckOffset = 0x018E4FB6;
constexpr std::array<u8, 9> NpcWanderCheck{0x8B, 0x88, 0x45, 0x01, 0x00, 0x00, 0xF6, 0xC1, 0x80};
constexpr u64 NpcWanderTypeCheckOffset = 0x018D8934;
constexpr std::array<u8, 8> NpcWanderTypeCheck{0x8B, 0x80, 0x45, 0x01, 0x00, 0x00, 0xA8, 0x80};
alignas(8) constexpr std::array<u8, NpcWanderGhostFlagOffset + 8> no_wander_npc_param{};

void PS4_SYSV_ABI KeepNpcsSolidForPartyGuests(u64, GuestRegisterSnapshot* registers) {
    if (registers->rax < 0x10000 ||
        !HasMemoryAccess(registers->rax + NpcWanderGhostFlagOffset, 4, MemoryProt::CpuRead)) {
        return;
    }
    const bool wander =
        (ReadValue<u32>(registers->rax, NpcWanderGhostFlagOffset) & NpcWanderGhostFlag) != 0;
    if (wander && !NpcTurnsIntoWanderGhost(InPartySession(), wander)) {
        registers->rax = reinterpret_cast<u64>(no_wander_npc_param.data());
    }
}

} // namespace

namespace Coop {

void InstallSolidNpcsInParty(u64 base, u64 image_size) {
    if (!party_controller) {
        return;
    }
    for (const auto& [offset, bytes] :
         {std::pair<u64, std::span<const u8>>{NpcWanderCheckOffset, NpcWanderCheck},
          std::pair<u64, std::span<const u8>>{NpcWanderTypeCheckOffset, NpcWanderTypeCheck}}) {
        if (offset + bytes.size() > image_size ||
            !InstallCoopHook(reinterpret_cast<void*>(base + offset), bytes, 0,
                             KeepNpcsSolidForPartyGuests)) {
            LOG_ERROR(Debug, "Bloodborne NPC hooks unavailable; party guests see NPCs as ghosts");
            coop_features.Skipped("solid NPCs", SiteFailure(base, image_size, offset));
            return;
        }
    }
    coop_features.On("solid NPCs");
    LOG_INFO(Debug, "Bloodborne party guests see NPCs as the host does");
}

} // namespace Coop

namespace {

// Diagnostics for shared loot (RE trace only): the map item module (MapItemMan, 0x017D-0x017E)
// asks "am I a summoned guest" (matching state 6) or "am I the host" (3) at these spots. Counting
// which run while a guest walks up to and picks up an item shows where the guest is held back.
struct ItemModuleCheck {
    u64 offset;
    std::array<u8, 7> bytes;
};
constexpr std::array<u8, 7> GuestStateCheck{0x83, 0xB8, 0x24, 0x01, 0x00, 0x00, 0x06};
constexpr std::array<u8, 7> GuestStateCheckRcx{0x83, 0xB9, 0x24, 0x01, 0x00, 0x00, 0x06};
constexpr std::array<u8, 7> HostStateCheck{0x83, 0xB8, 0x24, 0x01, 0x00, 0x00, 0x03};
constexpr auto ItemModuleChecks = std::to_array<ItemModuleCheck>({
    {0x017D3759, GuestStateCheck},
    {0x017D4F54, GuestStateCheck},
    {0x017D5642, GuestStateCheck},
    {0x017D6818, GuestStateCheck},
    {0x017D6AD2, GuestStateCheck},
    {0x017D83CB, GuestStateCheck},
    {0x017D853B, GuestStateCheck},
    {0x017D88BB, GuestStateCheck},
    {0x017D9AA8, GuestStateCheckRcx},
    {0x017D9E1C, GuestStateCheck},
    {0x017D9EE0, GuestStateCheckRcx},
    {0x017DD17C, GuestStateCheck},
    {0x017DDCD9, GuestStateCheck},
    {0x017DE5EC, GuestStateCheck},
    {0x017DE738, GuestStateCheck},
    {0x017DE79D, HostStateCheck},
    {0x017DE8CB, GuestStateCheck},
    {0x017DE925, HostStateCheck},
});
std::array<std::atomic<u64>, ItemModuleChecks.size()> item_module_check_hits{};

void PS4_SYSV_ABI CountItemModuleCheck(u64 tag, const GuestRegisterSnapshot*) {
    if (tag < item_module_check_hits.size()) {
        item_module_check_hits[tag].fetch_add(1, std::memory_order_relaxed);
    }
}

// Shared loot (see SoloMapItemStandIn): each hook points the checked register at the stand-in.
// In every spot the register is reloaded or overwritten right after the branch.
constexpr u64 MapItemCreateStateCheckOffset = 0x017DC3CF;   // cmp [rax+0x124], 6
constexpr u64 MapItemCreateSessionCheckOffset = 0x017DC3DC; // cmp [rdx+0x98], -1
constexpr std::array<u8, 7> MapItemCreateSessionCheck{0x83, 0xBA, 0x98, 0x00, 0x00, 0x00, 0xFF};
// A new map item gets its own handle (counter << 4 | kind in [item+0x20]) unless this player is
// a summoned guest, which waits for the host's; without one the pickup prompt never finds it.
constexpr u64 MapItemHandleStateCheckOffset = 0x017D52D2;   // cmp [rax+0x124], 6
constexpr u64 MapItemHandleSessionCheckOffset = 0x017D52DB; // cmp [r15+0x98], -1
constexpr std::array<u8, 8> MapItemHandleSessionCheck{0x41, 0x83, 0xBF, 0x98,
                                                      0x00, 0x00, 0x00, 0xFF};
constexpr std::array<u64, 2> MapItemPickupChecks{0x017D6831, 0x017D6AEF}; // [rax+0x1592], 0
// Picking up a networked map item (0x017D8570): a summoned guest only asks the host for it.
constexpr u64 MapItemTakeStateCheckOffset = 0x017D85F4; // cmp [rax+0x124], 6
constexpr std::array<u8, 7> MapItemPickupCheck{0x80, 0xB8, 0x92, 0x15, 0x00, 0x00, 0x00};

u64 SoloMapItemStandInAddress() {
    return reinterpret_cast<u64>(SoloMapItemStandIn().data());
}

void PS4_SYSV_ABI CreateMapItemsForPartyGuest(u64, GuestRegisterSnapshot* registers) {
    SetGuestPickupsBeforeMapItems();
    if (GuestHandlesMapItemsAlone(party_controller.has_value())) {
        registers->rax = SoloMapItemStandInAddress();
    }
}

void PS4_SYSV_ABI CreateMapItemsInJoinedSession(u64, GuestRegisterSnapshot* registers) {
    if (GuestHandlesMapItemsAlone(party_controller.has_value())) {
        registers->rdx = SoloMapItemStandInAddress();
    }
}

void PS4_SYSV_ABI GiveMapItemsAHandleInJoinedSession(u64, GuestRegisterSnapshot* registers) {
    if (GuestHandlesMapItemsAlone(party_controller.has_value())) {
        registers->r15 = SoloMapItemStandInAddress();
    }
}

void PS4_SYSV_ABI LetPartyGuestPickUpMapItems(u64, GuestRegisterSnapshot* registers) {
    if (GuestHandlesMapItemsAlone(party_controller.has_value())) {
        registers->rax = SoloMapItemStandInAddress();
    }
}

// The map item packet just received ([rbp-0x1A0], see MapItemPacketHeaderSeen): the hook runs
// before `mov eax, [rbp-0x1A0]; cmp ax, 1`, so an emptied header makes the loop skip it.
constexpr u64 MapItemPacketHeaderOffset = 0x017D33E1;
constexpr std::array<u8, 10> MapItemPacketHeader{0x8B, 0x85, 0x60, 0xFE, 0xFF,
                                                 0xFF, 0x66, 0x83, 0xF8, 0x01};

void PS4_SYSV_ABI IgnorePartyMapItemSync(u64, GuestRegisterSnapshot* registers) {
    const u64 header_address = registers->rbp - 0x1A0;
    const u32 header = ReadValue<u32>(header_address, 0);
    const u32 seen = MapItemPacketHeaderSeen(header, party_controller.has_value());
    if (seen != header) {
        WriteValue(header_address, 0, seen);
        static std::array<std::atomic<bool>, 8> logged{};
        const u32 kind = header >> 16;
        if (kind < logged.size() && !logged[kind].exchange(true)) {
            LOG_INFO(Debug, "Bloodborne party: ignored a map item sync packet (kind {})", kind);
        }
    }
}

// Full echoes for party guests (see RetargetRipLoad): the guest rate loads in the kill reward
// (0x018FC947), its calculation (0x018FCAA7) and the boss reward (0x01335589, a phantom's echoes
// x0.5 while more than one player is in the session) are pointed at the 1.0 at 0x04927EBC.
constexpr std::array<u64, 3> GuestEchoRateLoads{0x018FC947, 0x018FCAA7, 0x01335589};
constexpr std::array<std::array<u8, 8>, 3> GuestEchoRateLoadBytes{{
    {0xC5, 0xFA, 0x10, 0x1D, 0x71, 0xB5, 0x02, 0x03},
    {0xC5, 0xFA, 0x10, 0x1D, 0x21, 0xB4, 0x02, 0x03},
    {0xC5, 0xFA, 0x59, 0x05, 0x8B, 0x05, 0x5F, 0x03},
}};
constexpr u64 FullEchoRateOffset = 0x04927EBC;
constexpr std::array<u8, 11> AddSoulsPrologue{0x55, 0x48, 0x89, 0xE5, 0x41, 0x56,
                                              0x53, 0x48, 0x83, 0xEC, 0x10};

void PS4_SYSV_ABI NoteAddSouls(u64, const GuestRegisterSnapshot* registers) {
    const u64 caller = ReadValue<u64>(registers->rsp, 0);
    const s32 amount = static_cast<s32>(registers->rsi);
    LOG_INFO(Debug, "Bloodborne echoes: +{} (rates {}) from {:#x}", amount, registers->rcx & 0xFF,
             caller >= image_base ? caller - image_base : caller);
    if (amount > 0 && caller >= image_base &&
        SharesKillEchoes(caller - image_base, SomeoneElsewhereInSession())) {
        std::scoped_lock lock{echo_board_mutex};
        echoes_to_post.push_back(KillEchoes{amount, CurrentPlace().map});
    }
}

void GiveGuestsFullEchoes(u64 base, u64 image_size) {
    for (size_t index = 0; index < GuestEchoRateLoads.size(); ++index) {
        const u64 offset = GuestEchoRateLoads[index];
        if (offset + 8 > image_size ||
            !std::ranges::equal(std::span<const u8>{reinterpret_cast<const u8*>(base + offset), 8},
                                GuestEchoRateLoadBytes[index])) {
            LOG_ERROR(Debug, "Bloodborne guest echo rate not found at {:#x}", offset);
            coop_features.Skipped("full echoes",
                                  fmt::format("signature mismatch at {:#x}", offset));
            return;
        }
    }
    for (size_t index = 0; index < GuestEchoRateLoads.size(); ++index) {
        const u64 offset = GuestEchoRateLoads[index];
        const auto patched =
            RetargetRipLoad(GuestEchoRateLoadBytes[index], offset, FullEchoRateOffset);
        std::memcpy(reinterpret_cast<void*>(base + offset), patched.data(), patched.size());
    }
    coop_features.On("full echoes");
    LOG_INFO(Debug, "Bloodborne party guests get full echoes from kills");
    if (AddSoulsOffset + AddSoulsPrologue.size() > image_size ||
        !InstallCoopHook(reinterpret_cast<void*>(base + AddSoulsOffset), AddSoulsPrologue, 0,
                         NoteAddSouls)) {
        LOG_ERROR(Debug, "Bloodborne echo diagnostics unavailable");
        coop_features.Skipped("echo diagnostics", SiteFailure(base, image_size, AddSoulsOffset));
    } else {
        coop_features.On("echo diagnostics");
    }
}

// See MenuSeesAClient: the condition ends `or cl, al; movzx esi, cl` (al: matching state 6,
// cl: a joined session) and returns esi; cleared before that, the menu sees no client.
constexpr u64 MenuClientConditionOffset = 0x01A13FE5;
constexpr std::array<u8, 5> MenuClientCondition{0x08, 0xC1, 0x0F, 0xB6, 0xF1};

void PS4_SYSV_ABI MenusTreatPartyGuestsAsHosts(u64, GuestRegisterSnapshot* registers) {
    const bool client = (registers->rax & 0xFF) != 0 || (registers->rcx & 0xFF) != 0;
    if (!MenuSeesAClient(party_controller.has_value(), client)) {
        registers->rax &= ~u64{0xFF};
        registers->rcx &= ~u64{0xFF};
    }
}

} // namespace

namespace Coop {

void InstallSharedLoot(u64 base, u64 image_size) {
    if (!party_controller) {
        return;
    }
    GiveGuestsFullEchoes(base, image_size);
    if (MenuClientConditionOffset + MenuClientCondition.size() > image_size ||
        !InstallCoopHook(reinterpret_cast<void*>(base + MenuClientConditionOffset),
                         MenuClientCondition, 0, MenusTreatPartyGuestsAsHosts)) {
        LOG_ERROR(Debug, "Bloodborne menu client hook unavailable");
        coop_features.Skipped("menu client",
                              SiteFailure(base, image_size, MenuClientConditionOffset));
    } else {
        coop_features.On("menu client");
    }
    if (MapItemPacketHeaderOffset + MapItemPacketHeader.size() > image_size ||
        !InstallCoopHook(reinterpret_cast<void*>(base + MapItemPacketHeaderOffset),
                         MapItemPacketHeader, 0, IgnorePartyMapItemSync)) {
        LOG_ERROR(Debug, "Bloodborne map item sync hook unavailable; the host's sync clears the "
                         "guest's map items");
        coop_features.Skipped("map item sync",
                              SiteFailure(base, image_size, MapItemPacketHeaderOffset));
    } else {
        coop_features.On("map item sync");
    }
    struct LootHook {
        u64 offset;
        std::span<const u8> bytes;
        GuestRegisterEditHook edit;
    };
    const std::array<LootHook, 5> loot_hooks{{
        {MapItemCreateStateCheckOffset, GuestStateCheck, CreateMapItemsForPartyGuest},
        {MapItemCreateSessionCheckOffset, MapItemCreateSessionCheck, CreateMapItemsInJoinedSession},
        {MapItemHandleStateCheckOffset, GuestStateCheck, CreateMapItemsForPartyGuest},
        {MapItemTakeStateCheckOffset, GuestStateCheck, CreateMapItemsForPartyGuest},
        {MapItemHandleSessionCheckOffset, MapItemHandleSessionCheck,
         GiveMapItemsAHandleInJoinedSession},
    }};
    const auto hook = [base, image_size](u64 offset, std::span<const u8> bytes,
                                         GuestRegisterEditHook edit) {
        return offset + bytes.size() <= image_size &&
               InstallCoopHook(reinterpret_cast<void*>(base + offset), bytes, 0, edit);
    };
    const auto loot_failed = std::ranges::find_if(loot_hooks, [&hook](const LootHook& loot) {
        return !hook(loot.offset, loot.bytes, loot.edit);
    });
    if (loot_failed != loot_hooks.end()) {
        LOG_ERROR(Debug, "Bloodborne map item hooks unavailable; party guests see no map items");
        coop_features.Skipped("shared loot", SiteFailure(base, image_size, loot_failed->offset));
        return;
    }
    coop_features.On("shared loot");
    std::optional<u64> pickup_failed;
    for (const u64 offset : MapItemPickupChecks) {
        if (!hook(offset, MapItemPickupCheck, LetPartyGuestPickUpMapItems)) {
            LOG_ERROR(Debug, "Bloodborne map item pickup hook unavailable at {:#x}", offset);
            pickup_failed = pickup_failed.value_or(offset);
        }
    }
    if (pickup_failed) {
        coop_features.Skipped("map item pickup", SiteFailure(base, image_size, *pickup_failed));
    } else {
        coop_features.On("map item pickup");
    }
    LOG_INFO(Debug, "Bloodborne party guests see and pick up their own map items");
}

void InstallItemModuleDiagnostics(u64 base, u64 image_size) {
    if (!party_controller || !EnvFlagEnabled("SHADPS4_BLOODBORNE_RE_TRACE")) {
        return;
    }
    std::optional<u64> failed;
    for (size_t index = 0; index < ItemModuleChecks.size(); ++index) {
        const ItemModuleCheck& check = ItemModuleChecks[index];
        if (check.offset + check.bytes.size() > image_size ||
            !InstallCoopHook(reinterpret_cast<void*>(base + check.offset), check.bytes, index,
                             CountItemModuleCheck)) {
            LOG_ERROR(Debug, "Bloodborne item diagnostics: no hook at {:#x}", check.offset);
            failed = failed.value_or(check.offset);
        }
    }
    if (failed) {
        coop_features.Skipped("item diagnostics", SiteFailure(base, image_size, *failed));
    } else {
        coop_features.On("item diagnostics");
    }
}

void LogItemModuleChecks() {
    static std::array<u64, ItemModuleChecks.size()> logged{};
    static std::chrono::steady_clock::time_point last{};
    const auto now = std::chrono::steady_clock::now();
    if (now - last < std::chrono::seconds{2}) {
        return;
    }
    last = now;
    std::string text;
    for (size_t index = 0; index < ItemModuleChecks.size(); ++index) {
        const u64 hits = item_module_check_hits[index].load(std::memory_order_relaxed);
        if (hits != logged[index]) {
            text += fmt::format(" {:#x}+{}", ItemModuleChecks[index].offset, hits - logged[index]);
            logged[index] = hits;
        }
    }
    if (!text.empty()) {
        const u64 multi_play = GetMatchingState();
        const s32 state = multi_play >= 0x10000 ? ReadValue<s32>(multi_play, 0x124) : -1;
        LOG_INFO(Debug, "Bloodborne item checks (state {}):{}", state, text);
    }
}

} // namespace Coop

namespace {

// Diagnostics for the guest's missing pick-up prompt: the player's item button update
// (0x012FD880) asks MapItemMan for the nearest usable map item (0x017DA500, list +0x28,
// [rbp-0x58] = its handle), passes a busy check (0x0176B7E0), shows the prompt and, once
// pressed (0x01CD91F0 true), picks it up (0x017D8570). Inside the search, a usable item is
// examined at 0x017DA58B and one in the button's range lands at 0x017DA7CE.
constexpr u64 ItemButtonUpdateOffset = 0x012FD8CD;
constexpr std::array<u8, 10> ItemButtonUpdate{0x48, 0x8D, 0x7D, 0xC0, 0x41,
                                              0xBC, 0x04, 0x10, 0x00, 0x00};
constexpr u64 ItemButtonFoundOffset = 0x012FDC17;
constexpr std::array<u8, 8> ItemButtonFound{0x8B, 0x45, 0xA8, 0x89, 0x45, 0x9C, 0x85, 0xC0};
constexpr u64 ItemButtonNotBusyOffset = 0x012FDC84;
constexpr u64 ItemButtonPressedOffset = 0x012FDD95;
constexpr std::array<u8, 7> ItemButtonPlayerLoad{0x49, 0x8B, 0x85, 0xB0, 0x03, 0x00, 0x00};
constexpr u64 MapItemSearchExaminedOffset = 0x017DA58B;
constexpr std::array<u8, 6> MapItemSearchExamined{0xC4, 0xC1, 0x7A, 0x10, 0x46, 0x30};
constexpr u64 MapItemSearchInRangeOffset = 0x017DA7CE;
constexpr std::array<u8, 11> MapItemSearchInRange{0x41, 0x8B, 0x46, 0x20, 0x48, 0x8B,
                                                  0x8D, 0x10, 0xFF, 0xFF, 0xFF};
std::array<std::atomic<u64>, 6> item_button_counts{};
std::atomic<u32> item_button_last_handle{};

void PS4_SYSV_ABI NoteItemButton(u64 tag, const GuestRegisterSnapshot* registers) {
    if (tag >= item_button_counts.size()) {
        return;
    }
    if (tag == 1) {
        const u32 handle = ReadValue<u32>(registers->rbp - 0x58, 0);
        if (handle == 0) {
            return;
        }
        item_button_last_handle.store(handle, std::memory_order_relaxed);
    }
    item_button_counts[tag].fetch_add(1, std::memory_order_relaxed);
}

} // namespace

namespace Coop {

// On in every co-op game (was SHADPS4_BLOODBORNE_RE_TRACE only): a host could not pick up
// items after its guest's crash (friend run 2026-10-09) and the logs could not say where the
// pickup stopped. Six counting hooks, logged with the map items when they change.
void InstallItemButtonDiagnostics(u64 base, u64 image_size) {
    if (!party_controller) {
        return;
    }
    const std::array<std::pair<u64, std::span<const u8>>, 6> probes{{
        {ItemButtonUpdateOffset, ItemButtonUpdate},
        {ItemButtonFoundOffset, ItemButtonFound},
        {ItemButtonNotBusyOffset, ItemButtonPlayerLoad},
        {ItemButtonPressedOffset, ItemButtonPlayerLoad},
        {MapItemSearchExaminedOffset, MapItemSearchExamined},
        {MapItemSearchInRangeOffset, MapItemSearchInRange},
    }};
    for (u64 tag = 0; tag < probes.size(); ++tag) {
        const auto& [offset, bytes] = probes[tag];
        if (!InstallCoopHook(reinterpret_cast<void*>(base + offset), bytes, tag, NoteItemButton)) {
            LOG_ERROR(Debug, "Bloodborne item button diagnostics unavailable");
            coop_features.Skipped("item button diagnostics", SiteFailure(base, image_size, offset));
            return;
        }
    }
    coop_features.On("item button diagnostics");
}

} // namespace Coop

namespace {

// The local player's position, where the map item search reads it
// ([[[player+0x58]+8]+0x3B0]+0x68]+0x1E0).
std::optional<std::array<float, 3>> MapItemSearchPlayerPosition() {
    const u64 player = GetLocalPlayer();
    u64 node = player >= 0x10000 ? ReadValue<u64>(player, 0x58) : 0;
    for (const u64 offset : {u64{8}, u64{0x3B0}, u64{0x68}}) {
        if (node < 0x10000 || !HasMemoryAccess(node, offset + 8, MemoryProt::CpuRead)) {
            return std::nullopt;
        }
        node = ReadValue<u64>(node, offset);
    }
    if (node < 0x10000 || !HasMemoryAccess(node, 0x1EC, MemoryProt::CpuRead)) {
        return std::nullopt;
    }
    return std::array<float, 3>{ReadValue<float>(node, 0x1E0), ReadValue<float>(node, 0x1E4),
                                ReadValue<float>(node, 0x1E8)};
}

struct MapItemSpot {
    u32 handle;
    std::array<float, 3> at;
};

// The usable map items (MapItemMan's list +0x28, flag 0x40 at +0x48; see
// LogItemButtonAndMapItems) with their distance from `from`, nearest first.
std::vector<std::pair<float, MapItemSpot>> UsableMapItemsNear(const std::array<float, 3>& from) {
    std::vector<std::pair<float, MapItemSpot>> found;
    const u64 manager = ReadValue<u64>(image_base + MapItemManPointerOffset, 0);
    if (manager < 0x10000 || !HasMemoryAccess(manager, 0x40, MemoryProt::CpuRead)) {
        return found;
    }
    u64 item = ReadValue<u64>(manager, 0x28);
    for (u32 count = 0;
         item >= 0x10000 && count < 4096 && HasMemoryAccess(item, 0x58, MemoryProt::CpuRead);
         ++count, item = ReadValue<u64>(item, 0)) {
        if ((ReadValue<u16>(item, 0x48) & 0x40) == 0) {
            continue;
        }
        const std::array<float, 3> at{ReadValue<float>(item, 0x30), ReadValue<float>(item, 0x34),
                                      ReadValue<float>(item, 0x38)};
        const float dx = at[0] - from[0];
        const float dy = at[1] - from[1];
        const float dz = at[2] - from[2];
        found.push_back(
            {std::sqrt(dx * dx + dy * dy + dz * dz), MapItemSpot{ReadValue<u32>(item, 0x20), at}});
    }
    std::ranges::sort(found, {}, &std::pair<float, MapItemSpot>::first);
    return found;
}

} // namespace

namespace Coop {

void LogItemButtonAndMapItems() {
    static std::string last;
    static std::chrono::steady_clock::time_point last_time{};
    const auto now = std::chrono::steady_clock::now();
    if (now - last_time < std::chrono::seconds{3}) {
        return;
    }
    last_time = now;
    std::string lists;
    std::string nearest;
    const auto position = MapItemSearchPlayerPosition();
    if (position) {
        nearest = fmt::format(" player at ({:.1f}, {:.1f}, {:.1f});", (*position)[0],
                              (*position)[1], (*position)[2]);
    }
    const u64 manager = ReadValue<u64>(image_base + MapItemManPointerOffset, 0);
    if (manager >= 0x10000 && HasMemoryAccess(manager, 0x40, MemoryProt::CpuRead)) {
        std::vector<std::pair<float, std::string>> closest;
        for (const u64 list : {0x18, 0x28, 0x38}) {
            u32 count = 0, usable = 0, with_handle = 0;
            u64 item = ReadValue<u64>(manager, list);
            while (item >= 0x10000 && count < 4096 &&
                   HasMemoryAccess(item, 0x58, MemoryProt::CpuRead)) {
                ++count;
                const u16 flags = ReadValue<u16>(item, 0x48);
                usable += (flags & 0x40) != 0;
                with_handle += (ReadValue<u32>(item, 0x20) >> 4) != 0;
                if (position) {
                    const float x = ReadValue<float>(item, 0x30);
                    const float y = ReadValue<float>(item, 0x34);
                    const float z = ReadValue<float>(item, 0x38);
                    const float dx = x - (*position)[0];
                    const float dy = y - (*position)[1];
                    const float dz = z - (*position)[2];
                    closest.emplace_back(
                        std::sqrt(dx * dx + dy * dy + dz * dz),
                        fmt::format(" [{:#x} at ({:.1f}, {:.1f}, {:.1f}) flags {:#x}]",
                                    ReadValue<u32>(item, 0x20), x, y, z, flags));
                }
                item = ReadValue<u64>(item, 0);
            }
            lists += fmt::format(" [{:#x}: {} items, {} usable, {} with handle]", list, count,
                                 usable, with_handle);
        }
        std::ranges::sort(closest, {}, &std::pair<float, std::string>::first);
        for (size_t index = 0; index < std::min<size_t>(closest.size(), 4); ++index) {
            nearest += fmt::format(" {:.1f} m{}", closest[index].first, closest[index].second);
        }
    }
    const std::string text = fmt::format(
        "button updates {} examined {} in range {} found {} (last handle {:#x}) not busy {} "
        "pressed {};{}{}",
        item_button_counts[0].load() > 0 ? "yes" : "no", item_button_counts[4].load(),
        item_button_counts[5].load(), item_button_counts[1].load(), item_button_last_handle.load(),
        item_button_counts[2].load(), item_button_counts[3].load(), lists, nearest);
    if (text != last) {
        last = text;
        LOG_INFO(Debug, "Bloodborne map items: {}", text);
    }
}

// The other games' party death vitals and standing kills over the same link (see
// DirectVitalsType): read with the damage, used over the boards' copies.
std::mutex direct_boards_mutex;

} // namespace Coop

namespace {

DirectInbox<DirectVitals> direct_vitals_inbox;

// Which build each game runs (see PeerHelloType): this game's, from SetBuildIdentity; no hello
// goes out before it is set.
std::mutex peer_hello_mutex;
std::optional<PeerHello> own_hello;
PeerBuildCheck peer_build_check;

void NotePeerHello(PeerHello theirs, u32 addr, u16 port) {
    std::optional<std::string> warning;
    {
        std::scoped_lock lock{peer_hello_mutex};
        if (!own_hello) {
            return;
        }
        if (theirs.member.empty()) {
            theirs.member = fmt::format("peer {:08x}:{}", addr, port);
        }
        warning = peer_build_check.Take(theirs, *own_hello);
    }
    if (warning) {
        LOG_WARNING(Debug, "{}", *warning);
    }
}

} // namespace

namespace Coop {

bool TakeDirectDamage() {
    std::array<u8, 1400> buffer{};
    std::optional<std::vector<Libraries::Np::NpMatching2::RoomPeerAddress>> peers;
    bool took = false;
    for (int frames = 0; frames < 64; ++frames) {
        u32 addr = 0;
        u16 port = 0;
        const int size = Libraries::Net::P2PCoopRecvFrom(
            buffer.data(), static_cast<u32>(buffer.size()), &addr, &port);
        if (size <= 0) {
            break;
        }
        if (!peers) {
            peers = Libraries::Np::NpMatching2::GetActiveRoomPeers();
        }
        const bool from_peer = std::ranges::any_of(
            *peers, [&](const auto& peer) { return peer.addr == addr && peer.port == port; });
        const std::span<const u8> bytes{buffer.data(), static_cast<size_t>(size)};
        const auto frame = DecodeCoopFrame(bytes);
        TraceFrame("p2p-in", bytes, addr, port, from_peer);
        if (!from_peer || !frame) {
            continue;
        }
        const u64 sender = (u64{addr} << 16) | port;
        const s64 now = SteadyMilliseconds();
        if (frame->first == DirectVitalsType) {
            const std::string line(frame->second.begin(), frame->second.end());
            if (auto vitals = ParseVitalsUpdate(line)) {
                std::scoped_lock lock{direct_boards_mutex};
                direct_vitals_inbox.Take(sender, std::move(*vitals), now);
            }
            continue;
        }
        if (frame->first == DirectKillsType) {
            if (auto kills = DecodeDirectKills(frame->second)) {
                std::scoped_lock lock{direct_boards_mutex};
                direct_kills_inbox.Take(sender, std::move(*kills), now);
                took = true;
            }
            continue;
        }
        if (frame->first == PeerHelloType) {
            if (auto hello = DecodePeerHello(frame->second)) {
                NotePeerHello(std::move(*hello), addr, port);
            }
            continue;
        }
        auto state = DecodeDirectDamage(bytes);
        if (!state) {
            continue;
        }
        direct_damage_inbox.Take(sender, std::move(*state), now);
        took = true;
    }
    return took;
}

} // namespace Coop

namespace {

// A co-op frame to every room peer.
void SendToRoomPeers(const std::vector<u8>& frame) {
    for (const auto& peer : Libraries::Np::NpMatching2::GetActiveRoomPeers()) {
        Libraries::Net::P2PCoopSendTo(frame.data(), static_cast<u32>(frame.size()), peer.addr,
                                      peer.port);
        TraceFrame("p2p-out", frame, peer.addr, peer.port);
    }
}

std::string BoardMemberName();

} // namespace

namespace Coop {

// This member's standing kills to every room peer: at once when they changed, else every
// DirectDamageResendMs. Its whole set each time: a lost frame is made good by the next one.
void SendDirectKills(std::span<const u32> kills, bool changed) {
    static s64 sent_at = 0;
    const s64 now = SteadyMilliseconds();
    if (!changed && now - sent_at < DirectDamageResendMs) {
        return;
    }
    sent_at = now;
    SendToRoomPeers(EncodeCoopFrame(DirectKillsType, EncodeDirectKills(BoardMemberName(), kills)));
}

// This member's report to every room peer: at once when it changed, else every
// DirectDamageResendMs (a lost frame is made good by the next one: its counts only grow).
void SendDirectDamage(const MemberMapState& mine, bool changed) {
    static s64 sent_at = 0;
    const s64 now = SteadyMilliseconds();
    if (mine.map == 0 || (!changed && now - sent_at < DirectDamageResendMs)) {
        return;
    }
    sent_at = now;
    const std::vector<u8> frame = EncodeDirectDamage(mine.map, mine.bits);
    for (const auto& peer : Libraries::Np::NpMatching2::GetActiveRoomPeers()) {
        Libraries::Net::P2PCoopSendTo(frame.data(), static_cast<u32>(frame.size()), peer.addr,
                                      peer.port);
        TraceFrame("p2p-out", frame, peer.addr, peer.port);
    }
}

// This game's hello to every room peer: every PeerHelloResendMs, and at once when a peer it has
// not greeted joins the room's links (a new member, or one whose link came back).
void SendPeerHello() {
    static s64 checked_at = 0;
    static s64 sent_at = 0;
    static std::set<u64> greeted;
    const s64 now = SteadyMilliseconds();
    if (now - checked_at < DirectDamageResendMs) {
        return;
    }
    checked_at = now;
    PeerHello hello;
    {
        std::scoped_lock lock{peer_hello_mutex};
        if (!own_hello) {
            return;
        }
        hello = *own_hello;
    }
    const auto peers = Libraries::Np::NpMatching2::GetActiveRoomPeers();
    std::set<u64> linked;
    for (const auto& peer : peers) {
        linked.insert((u64{peer.addr} << 16) | peer.port);
    }
    const bool new_peer =
        std::ranges::any_of(linked, [&](u64 peer) { return !greeted.contains(peer); });
    for (const u64 peer : linked) {
        if (!greeted.contains(peer)) {
            TraceLife("peer-join", {{"peer", fmt::format("{:08x}:{}", peer >> 16, peer & 0xFFFF)}});
        }
    }
    for (const u64 peer : greeted) {
        if (!linked.contains(peer)) {
            TraceLife("peer-leave",
                      {{"peer", fmt::format("{:08x}:{}", peer >> 16, peer & 0xFFFF)}});
        }
    }
    greeted = std::move(linked);
    if (peers.empty() || (!new_peer && now - sent_at < PeerHelloResendMs)) {
        return;
    }
    sent_at = now;
    hello.member = BoardMemberName();
    SendToRoomPeers(EncodeCoopFrame(PeerHelloType, EncodePeerHello(hello)));
}

} // namespace Coop

namespace {

u64 OwnGameData();

// UpDateBloodMark (native 0x013D2910) takes the stain position from the player only while
// GlobalState +0x1592 is set, which it is in the player's own world. In the host's world it
// copies a stored position instead (+0x1500, where the guest was summoned from), so the stain
// went somewhere unseen. Set it for the call.
bool PS4_SYSV_ABI UpdateGuestBloodMark() {
    const u64 global = GetGlobalState();
    const bool can_flag =
        global >= 0x10000 && HasMemoryAccess(global, 0x1593, MemoryProt::CpuWrite);
    const u8 own_world = can_flag ? ReadValue<u8>(global, 0x1592) : 0;
    if (can_flag) {
        WriteValue(global, 0x1592, u8{1});
    }
    const bool result =
        reinterpret_cast<bool PS4_SYSV_ABI (*)()>(image_base + UpDateBloodMarkOffset)();
    if (can_flag) {
        WriteValue(global, 0x1592, own_world);
    }
    return result;
}

// Party death (see PartyDeathEnabled). The IsNoDead exit hook (KeepPartyMemberUp) keeps the
// local player at 1 HP where a lethal hit would kill it while it is armed (another member stands
// on its map) or down; the party tick (TickPartyDeath) puts it down, gets it up, lets it give up
// and wipes it; the vitals board client (RunVitalsBoardClient) tells the party.
#ifndef BLOODBORNE_PARTY_DEATH_DEFAULT
#define BLOODBORNE_PARTY_DEATH_DEFAULT 0 // the port's build defines it as 1
#endif
const bool party_death_enabled = PartyDeathEnabled(std::getenv("SHADPS4_BLOODBORNE_PARTY_DEATH"),
                                                   BLOODBORNE_PARTY_DEATH_DEFAULT != 0);
bool party_death_hook_installed{};
// What the game thread tells the hook: the local player's data module while party death is live
// (0 otherwise: the hook leaves everyone alone), and whether a lethal hit puts it down.
std::atomic<u64> party_death_data{};
std::atomic<bool> party_death_armed{};
std::atomic<bool> party_death_down{};
// Lethal hits the hook kept from killing the armed player: each change puts it down.
std::atomic<u32> party_death_catches{};
// The next death respawn resets the map (see DeathResetsMap).
std::atomic<bool> party_death_reset{};

} // namespace

namespace Coop {

// The lamp a death respawns the player at: resting at a lamp stores its respawn point id in
// GlobalState +0x1528 (SetReturnPointEntityId), and a death respawn resolves that id with
// 0x0154AFC0 against the loaded blocks ([0x0553B148], like Warp.RespawnTransform.Resolve). The
// same resolver finds the lamp while it is in a block the host's world has loaded; the guest is
// then moved there the way the Warp binding moves a character (vtable +0x5F8, then +0x1E1 |= 2).
// Otherwise the guest stays where it fell.
// A point on another map (see ChooseDeathRespawn: the first death's Hunter's Dream, a
// kidnapping's Gaol, a lamp elsewhere) is a trip there, as a guest's lamp trip is. So is a death
// that resets the map (party death's wipe), wherever the lamp is.
void MoveToRespawnPoint(bool arriving_from_trip) {
    const bool reset = !arriving_from_trip && party_death_reset.exchange(false);
    if (reset) {
        OnLifecycle(Lifecycle::Wipe); // kills and enemy HP, see their RegisterLifecycleReset
    }
    const u64 global = GetGlobalState();
    const s32 point = global >= 0x10000 ? ReadValue<s32>(global, 0x1528) : -1;
    const u64 blocks = ReadValue<u64>(image_base + LoadedBlocksPointerOffset, 0);
    const u64 player = GetLocalPlayer();
    if (blocks < 0x10000 || player < 0x10000) {
        return;
    }
    alignas(16) std::array<float, 4> position{};
    alignas(16) std::array<float, 4> rotation{};
    using Resolve = bool PS4_SYSV_ABI (*)(u64 blocks, float* position, float* rotation, s32 point);
    const bool in_loaded_block =
        point > 0 && reinterpret_cast<Resolve>(image_base + ResolveRespawnPointOffset)(
                         blocks, position.data(), rotation.data(), point);
    switch (ChooseDeathRespawn(point, in_loaded_block, arriving_from_trip, reset)) {
    case DeathRespawn::StayPut:
        LOG_INFO(Debug, "Bloodborne revive: respawn point {} not reachable; staying put", point);
        return;
    case DeathRespawn::Travel:
        guest_trip_point = point;
        // Right away: getting up where the player fell first looked like a second respawn.
        guest_lamp_trip_at = std::chrono::steady_clock::now() + std::chrono::milliseconds{200};
        LOG_INFO(Debug,
                 "Bloodborne revive: respawn point {} is on map {:#x}; travelling there "
                 "(reset={})",
                 point, PackedMapOfWarpPoint(point), reset);
        return;
    case DeathRespawn::MoveToPoint:
        break;
    }
    using Teleport = void PS4_SYSV_ABI (*)(u64 character, const float* position,
                                           const float* rotation, s32 mode);
    const u64 vtable = IsCharacter(player, "revive teleport") ? ReadValue<u64>(player, 0) : 0;
    const u64 teleport = vtable >= 0x10000 ? ReadValue<u64>(vtable, 0x5F8) : 0;
    if (teleport < 0x10000) {
        return;
    }
    reinterpret_cast<Teleport>(teleport)(player, position.data(), rotation.data(), 1);
    WriteValue(player, 0x1E1, static_cast<u8>(ReadValue<u8>(player, 0x1E1) | 2));
    LOG_INFO(Debug,
             "Bloodborne revive: moved to last lamp {} at ({}, {}, {}) rotation ({}, {}, {})",
             point, position[0], position[1], position[2], rotation[0], rotation[1], rotation[2]);
}

} // namespace Coop

namespace {

void WarpGuestToLastLamp() {
    MoveToRespawnPoint(false);
}

AliveStateSnapshot local_alive_state;

// The local player's state flags (+0x1E0, 8 bytes) and action state ([+0x3B0]+8: +0x1B4/+0x1B8).
u64 LocalActionModule(u64 player) {
    const u64 model = ReadValue<u64>(player, 0x3B0);
    return model >= 0x10000 ? ReadValue<u64>(model, 0x08) : 0;
}

std::optional<AliveState> ReadLocalAliveState(u64 player, bool& alive) {
    alive = false;
    const u64 module = LocalActionModule(player);
    const u64 model = ReadValue<u64>(player, 0x3B0);
    const u64 data = model >= 0x10000 ? ReadValue<u64>(model, 0x20) : 0;
    if (player < 0x10000 || module < 0x10000 || data < 0x10000) {
        return std::nullopt;
    }
    const AliveState state{
        .flags = ReadValue<u64>(player, 0x1E0),
        .action = ReadValue<s32>(module, 0x1B4),
        .action_sub = ReadValue<s32>(module, 0x1B8),
    };
    alive = ReadValue<s32>(data, 0xF8) > 0 && (ReadValue<u8>(player, 0x1E3) & 1) == 0;
    return state;
}

void ObserveLocalAliveState() {
    bool alive = false;
    if (const auto state = ReadLocalAliveState(GetLocalPlayer(), alive)) {
        local_alive_state.Observe(alive, *state);
    }
}

void RestoreLocalAliveState() {
    const u64 player = GetLocalPlayer();
    bool alive = false;
    const auto before = ReadLocalAliveState(player, alive);
    const auto& saved = local_alive_state.Saved();
    if (!before || !saved) {
        LOG_ERROR(Debug, "Bloodborne revive: no alive state to restore");
        return;
    }
    WriteValue(player, 0x1E0, saved->flags);
    const u64 module = LocalActionModule(player);
    WriteValue(module, 0x1B4, saved->action);
    WriteValue(module, 0x1B8, saved->action_sub);
    LOG_INFO(Debug,
             "Bloodborne revive restored alive state flags {:#x}->{:#x} action {}->{} "
             "sub {}->{}",
             before->flags, saved->flags, before->action, saved->action, before->action_sub,
             saved->action_sub);
}

GhostDeathCalls MakeReviveCalls() {
    return GhostDeathCalls{
        .is_white_ghost =
            reinterpret_cast<bool PS4_SYSV_ABI (*)()>(image_base + IsWhiteGhostOffset),
        .set_event_flag = reinterpret_cast<bool PS4_SYSV_ABI (*)(u64, s32, bool)>(
            image_base + PartyGhostSetEventFlagOffset),
        .clear_return_state =
            [] {
                // OnEvent_4000 (0x013813D0) sets it to 4 and skips deaths while it is above 0.
                const u64 events = ReadValue<u64>(image_base + EventManPointerOffset, 0);
                const u64 state = events >= 0x10000 ? ReadValue<u64>(events, 0x10) : 0;
                if (state >= 0x10000) {
                    WriteValue(state, 0x08, s32{0});
                }
            },
        .request_full_recover =
            reinterpret_cast<bool PS4_SYSV_ABI (*)()>(image_base + RequestFullRecoverOffset),
        .update_blood_mark = UpdateGuestBloodMark,
        .revive_player = reinterpret_cast<bool PS4_SYSV_ABI (*)()>(image_base + RevivePlayerOffset),
        .revive_local_character =
            reinterpret_cast<void PS4_SYSV_ABI (*)()>(image_base + ReviveLocalCharacterOffset),
        .restore_alive_state = RestoreLocalAliveState,
        .set_chr_type =
            reinterpret_cast<void PS4_SYSV_ABI (*)(u64, s32, s32)>(image_base + SetChrTypeOffset),
        .chr_reset_animation =
            reinterpret_cast<void PS4_SYSV_ABI (*)(u64, s32)>(image_base + ChrResetAnimationOffset),
        .warp_to_last_lamp = WarpGuestToLastLamp,
    };
}

// A party guest that died gets up again where the game would show the "returned to your world"
// menu (the warp home behind it is patched out, see GhostDeathKeepSessionPatches).
void ReviveGuestAfterGhostDeath() {
    const bool revived = ReviveDeadPartyGuest(MakeReviveCalls());
    const u64 player = GetLocalPlayer();
    LOG_INFO(Debug, "Bloodborne party guest died; revived in place={} type={} flags1e3={:#x}",
             revived, player >= 0x10000 ? ReadValue<s32>(player, 0x78) : -1,
             player >= 0x10000 ? ReadValue<u8>(player, 0x1E3) : 0xFF);
}

} // namespace

namespace Coop {

// Replaces native SoloPlayDeath_2 (bloodstain, then the reload at the lamp) for a host with a
// session: it revives like a guest and the world is not reloaded.
// OnLeave_Limit (event 4046), 5 s after a white phantom lost its host (the host left, travelled,
// kicked it or the room went away): the reset and warp into the phantom's own world. A party
// member skips it (SkipsLeaveLimit): a phantom while the session goes on (with nobody left, the
// host crashed, it goes home, as a phantom never rings again), and a live player always: the
// game ran it on a host whose guest died apart from it, and the warp took the host to the title.
bool PS4_SYSV_ABI StayWhenTheHostLeaves(u64, const GuestRegisterSnapshot* registers) {
    const u64 player = GetLocalPlayer();
    if (!party_controller || player < 0x10000 ||
        !HasMemoryAccess(player, 0x7C, MemoryProt::CpuRead)) {
        return false;
    }
    const s32 type = ReadValue<s32>(player, 0x78);
    if (!SkipsLeaveLimit(type, OtherSessionMembers().size())) {
        LOG_INFO(Debug, "Bloodborne party guest lost its host and the session; going home");
        return false;
    }
    if (type != WhiteGhostCharacterType && registers != nullptr) {
        LogGameCallChainAt("leave limit", registers->rsp);
    }
    // The leave set 4047 ("on the way out"); left set, it swallows the next leave and death.
    reinterpret_cast<bool PS4_SYSV_ABI (*)(u64, s32, bool)>(
        image_base + PartyGhostSetEventFlagOffset)(0, 4047, false);
    // It also set the event system's load wait (+0x8D, the SetLoadWait binding 0x013358F0; the
    // timer passes the event system in rsi) for the reload skipped here, and only a new event
    // system clears it: Kaniel 2026-10-11, after a friend's game went away neither the host nor
    // the other guest could pick up items or talk, only fight.
    const u64 events = registers != nullptr ? registers->rsi : 0;
    const u64 buttons = ReadValue<u64>(image_base + ActionButtonManPointerOffset, 0);
    s32 load_wait = -1;
    if (events >= 0x10000 && HasMemoryAccess(events, LoadWaitOffset + 1, MemoryProt::CpuWrite)) {
        load_wait = ReadValue<u8>(events, LoadWaitOffset);
        if (load_wait == 1) {
            WriteValue(events, LoadWaitOffset, u8{0});
        }
    }
    LOG_INFO(Debug,
             "Bloodborne party member (type {}) lost a member; staying where it is map={:#x} "
             "load wait {} (action buttons {}) cleared",
             type, CurrentPlace().map, load_wait,
             buttons >= 0x10000 ? ReadValue<u8>(buttons, 0x52) : -1);
    return true;
}

bool PS4_SYSV_ABI OverrideHostDeathRespawn(u64, const GuestRegisterSnapshot*) {
    const u64 multi_play = GetMatchingState();
    const s32 matching_state =
        multi_play >= 0x10000 && HasMemoryAccess(multi_play, 0x128, MemoryProt::CpuRead)
            ? ReadValue<s32>(multi_play, 0x124)
            : -1;
    if (!ShouldReviveHostInPlace(matching_state)) {
        return false;
    }
    ReviveDeadPartyHost(MakeReviveCalls());
    LOG_INFO(Debug,
             "Bloodborne live player died in a party session (state {}); revived without a "
             "world reload",
             matching_state);
    return true;
}

} // namespace Coop

namespace {

// The local player's data module ([[player +0x3B0] +0x20]: HP +0xF8, max HP +0xFC, flags +0x200).
u64 PlayerDataModule(u64 player) {
    const u64 modules = player >= 0x10000 ? ReadValue<u64>(player, 0x3B0) : 0;
    return modules >= 0x10000 ? ReadValue<u64>(modules, 0x20) : 0;
}

// The local player's attack rating for the weapon in its right hand, as the equipment menu shows
// it: UpdateWeaponAtk (0x018FCCB0, the character) works out one per weapon slot (physical + arcane
// + fire + bolt with scaling, upgrades and gems) into the character's PlayerGameData (its vtable
// +0x1C8), right hand 1 at +0x5E4 and right hand 2 at +0x5EC; ChrAsm ([player +0x4B8]) +0x10 says
// which right-hand weapon is held. On the game thread.
constexpr u64 UpdateWeaponAtkOffset = 0x018FCCB0;
struct AttackRatings {
    std::array<s32, 4> slots{-1, -1, -1, -1}; // left 1, right 1, left 2, right 2
    s32 right_hand = -1;
    std::array<s32, 4> weapons{-1, -1, -1, -1}; // ChrAsm +0x64: the weapon ids in the same order

    s32 Held() const {
        return right_hand == 1 ? slots[3] : right_hand == 0 ? slots[1] : 0;
    }
};
AttackRatings ReadAttackRatings(u64 player) {
    AttackRatings ratings;
    const u64 vtable = IsCharacter(player, "ReadAttackRatings") ? ReadValue<u64>(player, 0) : 0;
    const u64 game_data_of = vtable >= 0x10000 ? ReadValue<u64>(vtable, 0x1C8) : 0;
    const u64 asm_data = player >= 0x10000 ? ReadValue<u64>(player, 0x4B8) : 0;
    if (game_data_of < 0x10000 || asm_data < 0x10000) {
        return ratings;
    }
    static constexpr std::array<u8, 6> UpdatePrologue{0x55, 0x48, 0x89, 0xE5, 0x41, 0x57};
    if (std::ranges::equal(
            std::span<const u8>{reinterpret_cast<const u8*>(image_base + UpdateWeaponAtkOffset),
                                UpdatePrologue.size()},
            UpdatePrologue)) {
        reinterpret_cast<void PS4_SYSV_ABI (*)(u64)>(image_base + UpdateWeaponAtkOffset)(player);
    }
    const u64 player_data = reinterpret_cast<u64 PS4_SYSV_ABI (*)(u64)>(game_data_of)(player);
    if (player_data < 0x10000 || !HasMemoryAccess(player_data, 0x5F0, MemoryProt::CpuRead)) {
        return ratings;
    }
    for (u64 slot = 0; slot < 4; ++slot) {
        ratings.slots[slot] = ReadValue<s32>(player_data, 0x5E0 + slot * 4);
        ratings.weapons[slot] = ReadValue<s32>(asm_data, 0x64 + slot * 4);
    }
    ratings.right_hand = ReadValue<s32>(asm_data, 0x10);
    return ratings;
}

// Data module +0x200 flags a downed player wears: bit 1 skips any HP loss, bit 3 a hit's damage.
constexpr u8 DownedNoDamageFlags = 0x2 | 0x8;
// WorldChrManDbg's static "Player Hide" and "Player Silence" (debug menu flags): the AI's view of
// the local player (0x01A55390, 0x01A55230) takes it as unseen and unheard, so enemies leave a
// downed player alone.
constexpr u64 PlayerHideFlagOffset = 0x0553E88E;
constexpr u64 PlayerSilenceFlagOffset = 0x0553E88F;

// The enemies' targeting systems (see ForgetsTargetNode): the enemy one's vtable and its base's.
constexpr u64 EnemyTargetingVtableOffset = 0x0531B9F0;
constexpr u64 BaseTargetingVtableOffset = 0x05366280;

// Every enemy in this world forgets the characters with these handles (see ForgetsTargetNode);
// returns how many target nodes it changed.
u32 ForgetDownedTargets(std::span<const u32> handles) {
    const u64 world = ReadValue<u64>(image_base + CandidateLocalStatePointerOffset, 0);
    if (handles.empty() || world < 0x10000) {
        return 0;
    }
    u32 forgotten = 0;
    ForEachWorldCharacter(world, [&](u64 character) {
        const u64 manager = ReadValue<u64>(character, 0x3B8);
        const u64 ai = manager >= 0x10000 && HasMemoryAccess(manager, 0xB8, MemoryProt::CpuRead)
                           ? ReadValue<u64>(manager, 0xB0)
                           : 0;
        const u64 targeting = ai >= 0x10000 && HasMemoryAccess(ai, 0x568, MemoryProt::CpuRead)
                                  ? ReadValue<u64>(ai, 0x560)
                                  : 0;
        if (targeting < 0x10000 || !HasMemoryAccess(targeting, 0x20, MemoryProt::CpuRead)) {
            return;
        }
        const u64 vtable = ReadValue<u64>(targeting, 0);
        if (vtable != image_base + EnemyTargetingVtableOffset &&
            vtable != image_base + BaseTargetingVtableOffset) {
            return;
        }
        u64 node = ReadValue<u64>(targeting, 0x18);
        for (int count = 0;
             count < 64 && node >= 0x10000 && HasMemoryAccess(node, 0x54, MemoryProt::CpuWrite);
             ++count, node = ReadValue<u64>(node, 0x08)) {
            if (ForgetsTargetNode(ReadValue<u8>(node, 0x02), ReadValue<s32>(node, 0x28),
                                  ReadValue<u32>(node, 0x50), handles) &&
                ReadValue<float>(node, 0x2C) != ForgottenTargetTimer) {
                WriteValue(node, 0x2C, ForgottenTargetTimer);
                ++forgotten;
            }
        }
    });
    return forgotten;
}

// The handles of the downed players in this world: the local player while `down`, and each other
// member the board has down on this map (the other player character nearest to where it posted).
std::vector<u32> DownedPlayerHandles(u64 player, bool down,
                                     std::span<const LampPosition> others_down) {
    std::vector<u32> handles;
    if (down && player >= 0x10000) {
        handles.push_back(ReadValue<u32>(player, 0x08));
    }
    if (others_down.empty()) {
        return handles;
    }
    const std::vector<OtherPlayer> players = OtherPlayers();
    for (const LampPosition& at : others_down) {
        const OtherPlayer* nearest = nullptr;
        float nearest_distance = 4.0f;
        for (const OtherPlayer& other : players) {
            const float distance = other.at
                                       ? std::hypot((*other.at)[0] - at[0], (*other.at)[1] - at[1],
                                                    (*other.at)[2] - at[2])
                                       : nearest_distance + 1.0f;
            if (distance <= nearest_distance) {
                nearest_distance = distance;
                nearest = &other;
            }
        }
        if (nearest != nullptr) {
            handles.push_back(ReadValue<u32>(nearest->character, 0x08));
        }
    }
    return handles;
}

// The fall damage step (0x01A35C33) takes a fall into the void as a fixed lethal height while
// [[character +0x58] +0x38] +0x48 bit 1 is set. Such a fall stays a real death: nobody could
// reach the player down there.
bool FallsIntoTheVoid(u64 data) {
    const u64 character = ReadValue<u64>(data, 0x08);
    const u64 control = character >= 0x10000 ? ReadValue<u64>(character, 0x58) : 0;
    const u64 state = control >= 0x10000 ? ReadValue<u64>(control, 0x38) : 0;
    return state >= 0x10000 && (ReadValue<u8>(state, 0x48) & 2) != 0;
}

// Diagnostics: IsNoDead's other callers that ask about the local player, once each.
void NoteOtherNoDeadCheck(u64 return_offset) {
    static std::mutex mutex;
    static std::unordered_set<u64> seen;
    std::scoped_lock lock{mutex};
    if (seen.size() < 64 && seen.insert(return_offset).second) {
        LOG_INFO(Debug, "Bloodborne party death: IsNoDead asked from {:#x} (left alone)",
                 return_offset);
    }
}

// IsNoDead's exit (see IsNoDeadExit): for the local player a lethal check answers "no dead"
// while it is armed or down, and each catch that puts it down is counted for the party tick.
void PS4_SYSV_ABI KeepPartyMemberUp(u64, GuestRegisterSnapshot* registers) {
    const u64 data = party_death_data.load(std::memory_order_relaxed);
    if (data == 0 || registers->rbx != data) {
        return;
    }
    u64 return_address = 0;
    std::memcpy(&return_address, reinterpret_cast<const void*>(registers->rbp + 8),
                sizeof(return_address));
    const u64 return_offset = return_address - image_base;
    NoDeadCheck check = ClassifyNoDeadCheck(return_offset);
    if (check == NoDeadCheck::Other) {
        NoteOtherNoDeadCheck(return_offset);
        return;
    }
    check = NoDeadCheckAt(check, ReadValue<s32>(data, 0xF8));
    if (check == NoDeadCheck::Other) {
        return; // a hit that left HP
    }
    if (check == NoDeadCheck::Lethal && FallsIntoTheVoid(data)) {
        check = NoDeadCheck::Instant;
    }
    const bool answer = (registers->r14 & 0xFF) != 0;
    const bool down = party_death_down.load(std::memory_order_relaxed);
    if (answer ||
        !KeepsPlayerAlive(check, party_death_armed.load(std::memory_order_relaxed), down, answer)) {
        return;
    }
    registers->r14 = (registers->r14 & ~u64{0xFF}) | 1;
    if (!down) {
        party_death_catches.fetch_add(1, std::memory_order_relaxed);
    }
}

// SHADPS4_BLOODBORNE_DOWNED_ANIMATION: an animation a downed player plays (none by default; for
// trying poses that lie down), undone by ChrResetAnimation when it gets up.
const s32 downed_animation = [] {
    const char* setting = std::getenv("SHADPS4_BLOODBORNE_DOWNED_ANIMATION");
    s32 animation = 0;
    if (setting != nullptr) {
        std::from_chars(setting, setting + std::strlen(setting), animation);
    }
    return animation;
}();

// What the pad reader saw (FilterPartyDeathPad): the buttons held before the downed filter, the
// attack presses so far and whether the last one was heavy (R2).
std::atomic<u32> party_pad_buttons{};
std::atomic<u32> party_attack_presses{};
std::atomic<bool> party_attack_heavy{};

// The vitals board (see RunVitalsBoardClient): what the game thread posts and what the client
// last heard back, as the member it posts as.
std::mutex vitals_mutex;
std::condition_variable_any vitals_wake;
MemberVitals vitals_mine;
std::vector<ReviveHitTotal> vitals_hits;
bool vitals_changed{};
VitalsBoard vitals_board;
std::string vitals_member;
std::optional<std::chrono::steady_clock::time_point> vitals_heard_at;
std::jthread vitals_board_client;

// What the port's overlay draws (ReadPartyPanel), as of the last party tick.
std::mutex panel_mutex;
PartyPanel panel_now;

void SetPartyPanel(PartyPanel panel) {
    std::scoped_lock lock{panel_mutex};
    panel_now = std::move(panel);
}

struct VitalsView {
    VitalsBoard board;
    std::string member;
    s64 age_ms = std::numeric_limits<s64>::max();

    bool Fresh() const {
        return age_ms <= 3'000;
    }
};

VitalsView ReadVitalsBoard() {
    VitalsView view;
    {
        std::scoped_lock lock{vitals_mutex};
        view = VitalsView{vitals_board, vitals_member};
        if (vitals_heard_at) {
            view.age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - *vitals_heard_at)
                              .count();
        }
    }
    // The peers' own lines straight over the P2P link, fresher than the board's copies.
    std::vector<DirectVitals> direct;
    {
        std::scoped_lock lock{direct_boards_mutex};
        direct = direct_vitals_inbox.Fresh(SteadyMilliseconds());
    }
    if (!direct.empty()) {
        view.board = MergeDirectVitals(view.board, direct);
        view.age_ms = std::min<s64>(view.age_ms, 0);
    }
    return view;
}

std::string BoardMemberName() {
    std::scoped_lock lock{vitals_mutex};
    return vitals_member;
}

// The net sync object (SummonSessionRulesPointerOffset, see MemberDataWait): +0x84 is 4 in a
// session, +0x68 the list of members with a remote player slot (head node; node: next +0, member
// id +0x10, slot +0x30). The slot's byte of received parts is GameDataMan [+0x18][slot].
std::optional<s32> MemberDataSlot(u64 net, u64 member_id) {
    if (net < 0x10000 || !HasMemoryAccess(net, 0x88, MemoryProt::CpuRead)) {
        return std::nullopt;
    }
    using IdEqual = bool PS4_SYSV_ABI (*)(u64 a, u64 b);
    const auto id_equal = reinterpret_cast<IdEqual>(image_base + MemberIdEqualOffset);
    const u64 head = ReadValue<u64>(net, 0x68);
    if (head < 0x10000 || !HasMemoryAccess(head, 8, MemoryProt::CpuRead)) {
        return std::nullopt;
    }
    u64 node = ReadValue<u64>(head, 0);
    for (s32 hops = 0; node >= 0x10000 && node != head && hops < 16; ++hops) {
        if (!HasMemoryAccess(node, 0x34, MemoryProt::CpuRead)) {
            break;
        }
        if (id_equal(node + 0x10, member_id)) {
            const s32 slot = ReadValue<s32>(node, 0x30);
            return slot >= 0 && slot < 4 ? std::optional{slot} : std::nullopt;
        }
        node = ReadValue<u64>(node, 0);
    }
    return std::nullopt;
}

u64 MemberDataPartsAddress(s32 slot) {
    const u64 data = ReadValue<u64>(image_base + GameDataManPointerOffset, 0);
    const u64 parts = data >= 0x10000 && HasMemoryAccess(data, 0x20, MemoryProt::CpuRead)
                          ? ReadValue<u64>(data, 0x18)
                          : 0;
    return parts >= 0x10000 && HasMemoryAccess(parts + slot, 1, MemoryProt::CpuWrite) ? parts + slot
                                                                                      : 0;
}

// The game's own request (0x01781A70 sends the same): one zero byte on channel 0x0A, which the
// member's game answers with all of its data.
void SendMemberDataRequest(u64 net, u64 member_id) {
    using Send =
        s32 PS4_SYSV_ABI (*)(u64 net, u64 member_id, u32 channel, const void* buffer, u32 size);
    const u8 zero = 0;
    reinterpret_cast<Send>(image_base + NetSyncSendOffset)(net, member_id, MemberDataRequestChannel,
                                                           &zero, 1);
}

} // namespace

namespace Coop {

// See joins_waiting_for_data: once another member has stood on this map a moment, the waiting
// members are asked for their data until all of it is in, then their characters are created.
void FinishWaitingJoins(bool in_session) {
    if (joins_waiting_for_data.empty()) {
        return;
    }
    if (!in_session) {
        joins_waiting_for_data.clear();
        return;
    }
    const VitalsView view = ReadVitalsBoard();
    const bool here = view.Fresh() && !view.member.empty() &&
                      SomeoneElseStands(view.board.members, view.member, PlayingMap());
    const s64 now = SteadyMilliseconds();
    const u64 net = ReadValue<u64>(image_base + SummonSessionRulesPointerOffset, 0);
    using SetStep = void PS4_SYSV_ABI (*)(u64 task, s32 step);
    std::vector<u64> done;
    ForEachJoinTask([&](u64 task) {
        const u64 identity = ReadValue<u64>(task, 0xC8);
        const auto waiting = joins_waiting_for_data.find(identity);
        if (waiting == joins_waiting_for_data.end()) {
            return;
        }
        const s32 step = ReadValue<s32>(task, 0x50);
        const u8 flags = ReadValue<u8>(task, 0xED);
        if (step > CreateCharacterStep) {
            done.push_back(identity);
            return;
        }
        const u64 member_id = task + 0xC8;
        const std::optional<s32> slot = MemberDataSlot(net, member_id);
        const u64 parts = slot ? MemberDataPartsAddress(*slot) : 0;
        switch (waiting->second.Tick(now, here, parts ? ReadValue<u8>(parts, 0) : 0)) {
        case MemberDataWait::Action::None:
            return;
        case MemberDataWait::Action::Request:
            if (parts) {
                WriteValue(parts, 0, static_cast<u8>(ReadValue<u8>(parts, 0) & ~MemberDataParts));
            }
            SendMemberDataRequest(net, member_id);
            LOG_INFO(Debug,
                     "Bloodborne party trip: asked a member still joining for its data (request "
                     "{}, task {:#x} step={} flags={:#x} slot={})",
                     waiting->second.Requests(), task, step, flags,
                     slot ? fmt::format("{}", *slot) : "none");
            return;
        case MemberDataWait::Action::Create:
            WriteValue(task, 0xED, static_cast<u8>(flags | 1));
            if (step < CreateCharacterStep) {
                reinterpret_cast<SetStep>(ReadValue<u64>(ReadValue<u64>(task, 0), 0x28))(
                    task, CreateCharacterStep);
            }
            done.push_back(identity);
            LOG_INFO(Debug,
                     "Bloodborne party trip: a member that was still joining sent its data; "
                     "creating its character (task {:#x} step={} flags={:#x} slot={})",
                     task, step, flags, slot.value_or(-1));
            return;
        }
    });
    for (const u64 identity : done) {
        joins_waiting_for_data.erase(identity);
    }
}

} // namespace Coop

namespace {

// See OwnDataWatch: the stats and face channel 8 carries, PlayerGameData ([GameDataMan +0x08],
// the remote slots at [GameDataMan +0x10] + 0x6A0 each have the same layout) +0x40..+0x90 (every
// 8 bytes), +0xD4, +0xE0, +0xE4 and the face (0xF0 bytes at +0x4AC).
std::optional<u64> MemberDataFingerprint(u64 data) {
    if (data < 0x10000 || !HasMemoryAccess(data, 0x5A0, MemoryProt::CpuRead)) {
        return std::nullopt;
    }
    u64 hash = 0xCBF29CE484222325ull;
    const auto mix = [&](u64 at, u64 size) {
        for (u64 index = 0; index < size; ++index) {
            hash = (hash ^ ReadValue<u8>(data, at + index)) * 0x100000001B3ull;
        }
    };
    for (u64 at = 0x40; at <= 0x90; at += 8) {
        mix(at, 4);
    }
    mix(0xD4, 4);
    mix(0xE0, 8);
    mix(0x4AC, 0xF0);
    return hash;
}

u64 OwnGameData() {
    const u64 data = ReadValue<u64>(image_base + GameDataManPointerOffset, 0);
    return data >= 0x10000 && HasMemoryAccess(data, 0x10, MemoryProt::CpuRead)
               ? ReadValue<u64>(data, 0x08)
               : 0;
}

std::optional<u64> OwnDataFingerprint() {
    return MemberDataFingerprint(OwnGameData());
}

} // namespace

namespace Coop {

// The remote player slots' stats and face, logged when they change in a session (a member's
// level up seen here; the rig's raise-stat).
void LogMemberDataChanges(bool in_session) {
    static std::array<std::optional<u64>, 4> seen;
    const u64 data = ReadValue<u64>(image_base + GameDataManPointerOffset, 0);
    const u64 slots = data >= 0x10000 && HasMemoryAccess(data, 0x20, MemoryProt::CpuRead)
                          ? ReadValue<u64>(data, 0x10)
                          : 0;
    for (s32 slot = 0; slot < 4; ++slot) {
        const auto now = in_session && slots >= 0x10000
                             ? MemberDataFingerprint(slots + static_cast<u64>(slot) * 0x6A0)
                             : std::nullopt;
        if (now && seen[slot] && *now != *seen[slot]) {
            LOG_INFO(Debug, "Bloodborne party: member slot {} stats or face changed (first {})",
                     slot, ReadValue<s32>(slots + static_cast<u64>(slot) * 0x6A0, 0x40));
        }
        seen[slot] = now;
    }
}

// A level up (or any change to the stats or face) in a session reaches the others: the game's
// own after-load broadcast of all of the player's data (BroadcastMemberDataOffset).
void ResendOwnDataOnChange(bool in_session) {
    static OwnDataWatch watch;
    const u64 net = ReadValue<u64>(image_base + SummonSessionRulesPointerOffset, 0);
    const u64 state = GetGlobalState();
    const bool can_send =
        in_session && net >= 0x10000 && HasMemoryAccess(net, 0x88, MemoryProt::CpuRead) &&
        ReadValue<s32>(net, 0x84) == 4 && ReadValue<u64>(net, 0x10) != 0 && state >= 0x10000 &&
        HasMemoryAccess(state, 0x09, MemoryProt::CpuRead) && ReadValue<u8>(state, 0x08) == 0;
    if (watch.Observe(can_send ? OwnDataFingerprint() : std::nullopt, SteadyMilliseconds())) {
        reinterpret_cast<void PS4_SYSV_ABI (*)()>(image_base + BroadcastMemberDataOffset)();
        LOG_INFO(Debug, "Bloodborne party: the player's stats or face changed; sent its data to "
                        "the session again");
    }
}

} // namespace Coop

namespace {

// Hands the client this member's vitals: posted at once when more than its position changed.
void PostVitals(const MemberVitals& mine, std::vector<ReviveHitTotal> hits) {
    std::scoped_lock lock{vitals_mutex};
    MemberVitals moved = vitals_mine;
    moved.at = mine.at;
    const bool changed = moved != mine || hits != vitals_hits;
    vitals_mine = mine;
    vitals_hits = std::move(hits);
    if (changed) {
        vitals_changed = true;
        vitals_wake.notify_all();
    }
    // Straight to the room's peers too (see DirectVitalsType), once the board named this member.
    static s64 sent_at = 0;
    const s64 now = SteadyMilliseconds();
    if (mine.vital != Vital::Off && !vitals_member.empty() &&
        (changed || now - sent_at >= DirectDamageResendMs)) {
        sent_at = now;
        MemberVitals self = mine;
        self.member = vitals_member;
        const std::string line = EncodeVitalsUpdate(self, vitals_hits);
        SendToRoomPeers(EncodeCoopFrame(
            DirectVitalsType,
            std::span<const u8>{reinterpret_cast<const u8*>(line.data()), line.size()}));
    }
}

// Posts this member's vitals to the party's vitals board on shadNet and keeps its answer: once a
// second, four times a second while anyone in the party is down (revive progress), and soon after
// a change. Out of a session it posts "off" once, then waits.
void RunVitalsBoardClient(std::stop_token stop, std::string base_url, std::string party) {
    const std::string member = FindBoardMember(stop);
    {
        std::scoped_lock lock{vitals_mutex};
        vitals_member = member;
    }
    httplib::Client client(base_url);
    client.set_connection_timeout(std::chrono::seconds{1});
    client.set_read_timeout(std::chrono::seconds{2});
    const httplib::Headers headers{{"X-ShadPS4-Bloodborne-Party", party}};
    LOG_INFO(Debug, "Bloodborne party vitals board client for {} as member {}", base_url, member);

    bool posted_on = false;
    bool failing = false;
    bool someone_down = false;
    auto last_post = std::chrono::steady_clock::now();
    while (!stop.stop_requested()) {
        MemberVitals mine;
        std::vector<ReviveHitTotal> hits;
        {
            std::unique_lock lock{vitals_mutex};
            vitals_wake.wait_until(lock, stop, last_post + std::chrono::milliseconds{100},
                                   [] { return false; });
            vitals_wake.wait_until(
                lock, stop, last_post + std::chrono::milliseconds{someone_down ? 250 : 1'000},
                [] { return vitals_changed; });
            vitals_changed = false;
            mine = vitals_mine;
            hits = vitals_hits;
        }
        if (stop.stop_requested() || (mine.vital == Vital::Off && !posted_on)) {
            last_post = std::chrono::steady_clock::now();
            continue;
        }
        mine.member = member;
        last_post = std::chrono::steady_clock::now();
        const auto response = PostToBoard(client, "/bloodborne/party/vitals", headers,
                                          EncodeVitalsUpdate(mine, hits));
        const auto board =
            response && response->status == 200 ? ParseVitalsBoard(response->body) : std::nullopt;
        if (!board) {
            if (!failing) {
                LOG_WARNING(Debug, "Bloodborne party vitals board unreachable ({})",
                            response ? std::to_string(response->status)
                                     : httplib::to_string(response.error()));
            }
            failing = true;
            continue;
        }
        failing = false;
        posted_on = mine.vital != Vital::Off;
        someone_down = std::ranges::any_of(
            board->members, [](const MemberVitals& other) { return other.vital == Vital::Down; });
        std::scoped_lock lock{vitals_mutex};
        vitals_board = *board;
        vitals_heard_at = std::chrono::steady_clock::now();
    }
}

} // namespace

namespace Coop {

void StartVitalsBoardClient() {
    const char* party_setting = std::getenv("SHADPS4_BLOODBORNE_PARTY");
    const auto party = party_setting ? ParsePartySetting(party_setting) : std::nullopt;
    const std::string base_url = EmulatorSettings.GetShadNetWebApiServer();
    if (!party_controller || !party || !EmulatorSettings.IsShadNetEnabled() || base_url.empty()) {
        return;
    }
    vitals_board_client = std::jthread(RunVitalsBoardClient, base_url, *party);
}

} // namespace Coop

namespace {

// The local player's side of party death, on the game thread.
struct PartyDeathRun {
    // Down ids start somewhere new each run, so the party's hits on an old down never count.
    DownedState downed{static_cast<u32>(std::random_device{}() % 0x40000000U) + 1};
    HoldToConfirm give_up{GiveUpHoldMs};
    ReviveHits revive_hits;
    u32 seen_catches = 0;
    u32 seen_attacks = 0;
    u32 seen_lamp_uses = 0;
    u32 handled_wipe = 0;
    u8 flags_added = 0;
    u8 hide_before = 0;
    u8 silence_before = 0;
    u32 targets_forgotten = 0; // since the last log line
};
PartyDeathRun party_death_run;

void SetPlayerFlag(u64 offset, u8 value) {
    WriteValue(image_base + offset, 0, value);
}

void GoDown(PartyDeathRun& run, u64 data, s64 now_ms) {
    const u32 down = run.downed.GoDown(now_ms, ReadValue<s32>(data, 0xFC));
    run.give_up.Restart((party_pad_buttons.load() & PadCircle) != 0);
    party_death_down.store(true);
    const u8 flags = ReadValue<u8>(data, 0x200);
    run.flags_added = DownedNoDamageFlags & static_cast<u8>(~flags);
    WriteValue(data, 0x200, static_cast<u8>(flags | DownedNoDamageFlags));
    WriteValue(data, 0xF8, s32{1});
    run.hide_before = ReadValue<u8>(image_base + PlayerHideFlagOffset, 0);
    run.silence_before = ReadValue<u8>(image_base + PlayerSilenceFlagOffset, 0);
    SetPlayerFlag(PlayerHideFlagOffset, 1);
    SetPlayerFlag(PlayerSilenceFlagOffset, 1);
    bool posed = false;
    if (downed_animation > 0) {
        using ForcePlayAnimation = bool PS4_SYSV_ABI (*)(u64 unused, s32 entity, s32 animation);
        posed = reinterpret_cast<ForcePlayAnimation>(image_base + ForcePlayAnimationOffset)(
            0, LocalPlayerEntity, downed_animation);
    }
    LOG_INFO(Debug,
             "Bloodborne party death: down {} on map {:#x}, {} damage to get up (max HP {}, "
             "pose {} played={})",
             down, CurrentPlace().map, run.downed.DamageNeeded(), ReadValue<s32>(data, 0xFC),
             downed_animation, posed);
}

// Each tick while down: no damage, 1 HP, unseen and unheard (the game may undo any of them).
void KeepDown(PartyDeathRun& run, u64 data) {
    const u8 flags = ReadValue<u8>(data, 0x200);
    if ((flags & DownedNoDamageFlags) != DownedNoDamageFlags) {
        run.flags_added |= DownedNoDamageFlags & static_cast<u8>(~flags);
        WriteValue(data, 0x200, static_cast<u8>(flags | DownedNoDamageFlags));
    }
    if (ReadValue<s32>(data, 0xF8) > 1) {
        WriteValue(data, 0xF8, s32{1});
    }
    SetPlayerFlag(PlayerHideFlagOffset, 1);
    SetPlayerFlag(PlayerSilenceFlagOffset, 1);
}

void EndDown(PartyDeathRun& run, u64 data) {
    party_death_down.store(false);
    WriteValue(data, 0x200, static_cast<u8>(ReadValue<u8>(data, 0x200) & ~run.flags_added));
    run.flags_added = 0;
    SetPlayerFlag(PlayerHideFlagOffset, run.hide_before);
    SetPlayerFlag(PlayerSilenceFlagOffset, run.silence_before);
}

void GetUp(PartyDeathRun& run, u64 data, s64 now_ms) {
    EndDown(run, data);
    const s32 hp = RevivedHp(ReadValue<s32>(data, 0xFC));
    WriteValue(data, 0xF8, hp);
    run.downed.GetUp(now_ms);
    if (downed_animation > 0) {
        reinterpret_cast<void PS4_SYSV_ABI (*)(u64, s32)>(image_base + ChrResetAnimationOffset)(
            0, LocalPlayerEntity);
    }
    LOG_INFO(Debug, "Bloodborne party death: revived from down {} with {} HP", run.downed.DownId(),
             hp);
}

// A real death from the down (a wipe, giving up, shadNet gone): the game's own kill, then the
// party death respawn (see MoveToRespawnPoint), which reloads the map if `reset`.
void DieFromDown(PartyDeathRun& run, u64 player, u64 data, bool reset, std::string_view why) {
    EndDown(run, data);
    run.downed.Died();
    party_death_reset.store(reset);
    static constexpr std::array<u8, 6> KillPrologue{0x55, 0x48, 0x89, 0xE5, 0x41, 0x57};
    const bool killed =
        IsCharacter(player, "DieFromDown kill") &&
        std::ranges::equal(
            std::span<const u8>{reinterpret_cast<const u8*>(image_base + KillCharacterOffset),
                                KillPrologue.size()},
            KillPrologue);
    if (killed) {
        reinterpret_cast<void PS4_SYSV_ABI (*)(u64 character)>(image_base +
                                                               KillCharacterOffset)(player);
    } else {
        WriteValue(data, 0xF8, s32{0});
    }
    LOG_INFO(Debug, "Bloodborne party death: {} ends down {}; dies (reset={}, game kill={})", why,
             run.downed.DownId(), reset, killed);
}

void TickPartyDeath() {
    if (!party_death_hook_installed) {
        // Without party death the vitals board still says who stands on which map: a host back
        // from a trip asks a member that was still joining for its data only once it stands here
        // (FinishWaitingJoins; rig 2026-10-11, party death off: the guest stayed unseen).
        const u64 data = PlayerDataModule(GetLocalPlayer());
        if (data >= 0x10000 && HasMemoryAccess(data, 0x100, MemoryProt::CpuRead)) {
            const bool in_session = InPartySession();
            PostVitals(MemberVitals{.map = in_session ? PlayingMap() : 0,
                                    .vital = in_session ? Vital::Up : Vital::Off,
                                    .hp = ReadValue<s32>(data, 0xF8),
                                    .max = ReadValue<s32>(data, 0xFC),
                                    .at = LocalPlayerPosition().value_or(LampPosition{})},
                       {});
        }
        return;
    }
    auto& run = party_death_run;
    const u64 player = GetLocalPlayer();
    const u64 data = PlayerDataModule(player);
    if (data < 0x10000 || !HasMemoryAccess(data, 0x204, MemoryProt::CpuWrite)) {
        party_death_data.store(0);
        SetPartyPanel({});
        return; // loading
    }
    const s64 now = SteadyMilliseconds();
    const bool in_session = InPartySession();
    const u32 map = PlayingMap();
    const VitalsView view = ReadVitalsBoard();
    const std::span<const MemberVitals> board =
        view.Fresh() ? std::span<const MemberVitals>{view.board.members}
                     : std::span<const MemberVitals>{};
    const bool armed =
        in_session && !view.member.empty() && SomeoneElseStands(board, view.member, map);
    party_death_armed.store(armed);
    party_death_data.store(in_session || run.downed.Current() == Vital::Down ? data : 0);
    bool alive = false;
    ReadLocalAliveState(player, alive);
    if (const u32 uses = lamp_uses.load(); uses != run.seen_lamp_uses) {
        run.seen_lamp_uses = uses;
        run.downed.ClearPenalty();
    }

    switch (run.downed.Current()) {
    case Vital::Off:
    case Vital::Up: {
        const u32 catches = party_death_catches.load();
        const bool caught = catches != run.seen_catches;
        run.seen_catches = catches;
        if (caught && alive) {
            GoDown(run, data, now);
            break;
        }
        if (!alive) {
            // A real death: one of the game's kills, or a lethal hit with nobody else standing.
            run.downed.Died();
            party_death_reset.store(in_session && DeathResetsMap(false, armed));
            LOG_INFO(Debug, "Bloodborne party death: died for real on map {:#x} (reset={})", map,
                     party_death_reset.load());
            break;
        }
        const u32 attacks = party_attack_presses.load();
        const bool attacked = attacks != run.seen_attacks;
        run.seen_attacks = attacks;
        const auto at = LocalPlayerPosition();
        const auto target = attacked && in_session && at
                                ? ReviveTarget(board, view.member, map, *at)
                                : std::nullopt;
        const AttackRatings ratings = target ? ReadAttackRatings(player) : AttackRatings{};
        const s32 rating = ratings.Held();
        const bool heavy = party_attack_heavy.load();
        const u32 damage = ReviveHitDamage(rating, heavy);
        if (target && run.revive_hits.Press(target->member, target->down, damage, now)) {
            LOG_INFO(Debug,
                     "Bloodborne party death: revive hit on {} (down {}) for {} damage (attack "
                     "rating {}, heavy={}; ratings {} weapons {} right hand {})",
                     target->member, target->down, damage, rating, heavy,
                     fmt::join(ratings.slots, " "), fmt::join(ratings.weapons, " "),
                     ratings.right_hand);
        }
        break;
    }
    case Vital::Down: {
        if (!alive) {
            // Killed anyway (a fall into the void, an event's kill).
            EndDown(run, data);
            run.downed.Died();
            party_death_reset.store(in_session && DeathResetsMap(false, armed));
            LOG_INFO(Debug, "Bloodborne party death: killed while down {} (reset={})",
                     run.downed.DownId(), party_death_reset.load());
            break;
        }
        KeepDown(run, data);
        const auto wipe = view.Fresh() ? WipeFor(view.board, view.member) : std::nullopt;
        const bool wiped = wipe && *wipe != run.handled_wipe;
        const bool gave_up = run.give_up.Tick((party_pad_buttons.load() & PadCircle) != 0, now);
        u32 damage = 0;
        for (const MemberVitals& entry : board) {
            if (entry.member == view.member && entry.down == run.downed.DownId()) {
                damage = entry.revive_damage;
            }
        }
        const bool revived = run.downed.Damage(damage, now);
        run.downed.Tick(now);
        switch (EndOfDown(wiped, gave_up, revived, view.age_ms)) {
        case DownEnd::None:
            break;
        case DownEnd::Revive:
            GetUp(run, data, now);
            break;
        case DownEnd::Wipe:
            run.handled_wipe = *wipe;
            DieFromDown(run, player, data, true, fmt::format("wipe {}", *wipe));
            break;
        case DownEnd::GiveUp:
            DieFromDown(run, player, data, in_session && DeathResetsMap(false, armed), "giving up");
            break;
        case DownEnd::BoardLost:
            DieFromDown(run, player, data, in_session, "the vitals board going quiet");
            break;
        }
        break;
    }
    case Vital::Dead:
        if (alive) {
            run.downed.Alive(now);
            party_death_reset.store(false);
            LOG_INFO(Debug, "Bloodborne party death: up again after a real death");
        }
        break;
    }

    // Enemies forget the downed players here (this one and the others), every tick while any is
    // down: an enemy that sees a downed guest again (Hide is the local player's only) would take it
    // as a target again.
    const std::vector<LampPosition> others_down =
        in_session ? OthersDownOnMap(board, view.member, map) : std::vector<LampPosition>{};
    const std::vector<u32> downed_handles =
        DownedPlayerHandles(player, run.downed.Current() == Vital::Down, others_down);
    if (const u32 forgotten = ForgetDownedTargets(downed_handles); forgotten > 0) {
        if (run.targets_forgotten == 0) {
            LOG_INFO(Debug,
                     "Bloodborne party death: enemies forgot {} target(s) on downed players "
                     "(self down={}, others down={})",
                     forgotten, run.downed.Current() == Vital::Down, others_down.size());
        }
        run.targets_forgotten += forgotten;
    } else if (downed_handles.empty()) {
        run.targets_forgotten = 0;
    }

    const auto at = LocalPlayerPosition();
    PostVitals(
        MemberVitals{
            .map = in_session ? map : 0,
            .vital = in_session ? run.downed.Current() : Vital::Off,
            .hp = ReadValue<s32>(data, 0xF8),
            .max = ReadValue<s32>(data, 0xFC),
            .down = run.downed.DownId(),
            .landed = run.downed.DamageLanded(),
            .needed = run.downed.DamageNeeded(),
            .at = at.value_or(LampPosition{}),
        },
        run.revive_hits.Totals(board));

    const Vital vital = run.downed.Current();
    PartyPanel panel = in_session ? BuildPartyPanel(board, view.member, map,
                                                    vital == Vital::Up ? at : std::nullopt)
                                  : PartyPanel{};
    panel.vital = in_session ? vital : Vital::Off;
    panel.landed = run.downed.DamageLanded();
    panel.needed = run.downed.DamageNeeded();
    panel.give_up = vital == Vital::Down ? run.give_up.Progress(now) : 0.0f;
    SetPartyPanel(std::move(panel));
}

} // namespace

namespace Coop {

// Party death needs a party (SHADPS4_BLOODBORNE_PARTY) and the setting.
void InstallPartyDeath(u64 base, u64 image_size) {
    if (!party_death_enabled || !party_controller) {
        return;
    }
    if (IsNoDeadExitOffset + IsNoDeadExit.size() > image_size ||
        !InstallCoopHook(reinterpret_cast<void*>(base + IsNoDeadExitOffset), IsNoDeadExit, 0,
                         KeepPartyMemberUp)) {
        LOG_ERROR(Debug, "Bloodborne party death hook unavailable; deaths stay as they are");
        coop_features.Skipped("party death", SiteFailure(base, image_size, IsNoDeadExitOffset));
        return;
    }
    party_death_hook_installed = true;
    coop_features.On("party death");
    LOG_INFO(Debug, "Bloodborne party death: a lethal hit puts a party member down while another "
                    "stands on its map (damage revives it, holding Circle gives up)");
}

void WriteHex(std::ostream& out, u64 value) {
    out << "\"0x" << std::hex << value << std::dec << '\"';
}

} // namespace Coop

namespace {

void WriteRoleSelectionMask(std::ostream& out, std::string_view field, s32 role_code) {
    if (role_code < 0 || role_code >= RoleMetadataCount || image_base == 0) {
        return;
    }
    const u64 offset =
        RoleMetadataTableOffset + static_cast<u64>(role_code) * RoleMetadataStride + sizeof(u32);
    const u64 image_size = MemoryPatcher::g_eboot_image_size;
    if (image_size < sizeof(u32) || offset > image_size - sizeof(u32)) {
        return;
    }
    out << ",\"" << field << "\":";
    WriteHex(out, ReadValue<u32>(image_base + offset, 0));
}

void WriteObjectState(std::ostream& out, u64 object) {
    if (object < 0x10000) {
        return;
    }
    out << ",\"object\":{";
    out << "\"state_54\":";
    WriteHex(out, ReadValue<u32>(object, 0x54));
    out << ",\"c8\":";
    WriteHex(out, ReadValue<u64>(object, 0xC8));
    out << ",\"d0\":";
    WriteHex(out, ReadValue<u64>(object, 0xD0));
    out << ",\"d8\":";
    WriteHex(out, ReadValue<u32>(object, 0xD8));
    out << ",\"e0\":";
    WriteHex(out, ReadValue<u64>(object, 0xE0));
    out << ",\"e8\":";
    WriteHex(out, ReadValue<u32>(object, 0xE8));
    out << ",\"ec\":";
    WriteHex(out, ReadValue<u32>(object, 0xEC));
    out << ",\"f0\":";
    WriteHex(out, ReadValue<u8>(object, 0xF0));
    out << ",\"f8\":";
    WriteHex(out, ReadValue<u32>(object, 0xF8));
    out << '}';
}

void WriteGetSosState(std::ostream& out, u64 object) {
    if (object < 0x10000) {
        return;
    }
    out << ",\"get_sos\":{";
    const u64 begin = ReadValue<u64>(object, 0xD0);
    const u64 current = ReadValue<u64>(object, 0xD8);
    const u64 end = ReadValue<u64>(object, 0xE0);
    out << "\"begin\":";
    WriteHex(out, begin);
    out << ",\"current\":";
    WriteHex(out, current);
    out << ",\"end\":";
    WriteHex(out, end);
    if (begin >= 0x10000 && current >= begin && current <= end &&
        (current - begin) % sizeof(s32) == 0) {
        const size_t count = std::min<size_t>((current - begin) / sizeof(s32), 16);
        out << ",\"summon_types\":[";
        for (size_t index = 0; index < count; ++index) {
            if (index != 0) {
                out << ',';
            }
            out << ReadValue<s32>(begin, index * sizeof(s32));
        }
        out << ']';
    }
    out << ",\"request_active\":" << static_cast<u32>(ReadValue<u8>(object, 0x120));
    out << ",\"retry_count\":" << ReadValue<u32>(object, 0x124);
    out << '}';
}

void WriteMultiplayerEffects(std::ostream& out, u64 player) {
    if (player < 0x10000) {
        return;
    }

    const u64 manager = ReadValue<u64>(player, 0x1C8);
    if (manager < 0x10000) {
        return;
    }

    out << ",\"multiplayer_effects\":{";
    out << "\"manager\":";
    WriteHex(out, manager);
    out << ",\"mask\":";
    WriteHex(out, ReadValue<u32>(manager, 0x20));
    out << ",\"entries\":[";

    bool first = true;
    u64 node = ReadValue<u64>(manager, 0x08);
    for (size_t index = 0; node >= 0x10000 && index < 512; ++index) {
        const u64 next = ReadValue<u64>(node, 0x58);
        const u32 flags = ReadValue<u32>(node, 0x1C);
        const u64 param = ReadValue<u64>(node, 0x48);
        if ((flags & 0x800C0003U) == 0 && param >= 0x10000) {
            const s32 effect_id = ReadValue<s32>(node, 0x40);
            const u16 special_state = ReadValue<u16>(param, 0x156);
            if ((special_state >= 188 && special_state <= 190) || effect_id == 9003 ||
                effect_id == 9004) {
                if (!first) {
                    out << ',';
                }
                first = false;
                out << "{\"effect_id\":" << effect_id << ",\"special_state\":" << special_state
                    << '}';
            }
        }
        if (next == node) {
            break;
        }
        node = next;
    }
    out << "]}";
}

void WriteSummonCandidate(std::ostream& out, const TraceSite& site,
                          const GuestRegisterSnapshot& registers) {
    const bool is_entry = site.offset == 0x014BA980;
    const bool is_submit = site.offset == 0x014BABCB;
    const bool is_inserted = site.offset == 0x014B8E8D;
    const bool is_filter_cycle = site.offset == 0x014B6F09;
    const bool is_manager_scan = site.offset == 0x014B6F5D;
    const bool is_filter_progress = site.offset == 0x014B6F9D || site.offset == 0x014B6FE1 ||
                                    site.offset == 0x014B7070 || site.offset == 0x014B70FF ||
                                    site.offset == 0x014B7150 || site.offset == 0x014B71B2 ||
                                    site.offset == 0x014B71F4;
    const bool is_pending_inserted = site.offset == 0x014B750B;
    const bool is_observer_result = site.offset == 0x014C0238;
    const u64 wrapper = is_entry ? registers.rsi : 0;
    const u64 storage =
        is_entry ? (wrapper >= 0x10000 ? ReadValue<u64>(wrapper, 0x08) : 0)
                 : (!is_inserted && !is_filter_cycle && !is_manager_scan && !is_filter_progress &&
                            !is_pending_inserted && !is_observer_result
                        ? registers.r15
                        : 0);

    out << ",\"summon_candidate\":{";
    out << "\"stage\":\"" << site.name << "\"";
    out << ",\"manager\":";
    WriteHex(out, is_entry              ? registers.rdi
                  : is_inserted         ? registers.rdx
                  : is_pending_inserted ? registers.r14
                                        : GetSummonManager());
    out << ",\"wrapper\":";
    WriteHex(out, wrapper);
    out << ",\"storage\":";
    WriteHex(out, storage);
    if (storage >= 0x10000) {
        out << ",\"decoded_size\":" << ReadValue<u16>(storage, 0x02);
        out << ",\"role_code\":" << static_cast<u32>(ReadValue<u8>(storage, 0x7A));
        out << ",\"field_54\":";
        WriteHex(out, ReadValue<u64>(storage, 0x54));
        out << ",\"field_d4\":";
        WriteHex(out, ReadValue<u32>(storage, 0xD4));
        out << ",\"field_dc\":";
        WriteHex(out, ReadValue<u64>(storage, 0xDC));
    }
    if (is_filter_cycle) {
        const u64 manager = GetSummonManager();
        out << ",\"manager_identity_state_register\":" << static_cast<s32>(registers.r15);
        out << ",\"local_identity_pointer\":";
        WriteHex(out, registers.r12);
        if (manager >= 0x10000) {
            out << ",\"primary_head\":";
            WriteHex(out, ReadValue<u64>(manager, 0x30));
            out << ",\"primary_count\":" << ReadValue<u64>(manager, 0x38);
        }
    } else if (is_manager_scan) {
        const u64 root = GetSummonManagerRoot();
        const u64 local_identity =
            registers.rsp >= 0x10000 ? ReadValue<u64>(registers.rsp, 0x88) : 0;
        out << ",\"primary_node\":";
        WriteHex(out, registers.r15);
        out << ",\"constructed_candidate\":";
        WriteHex(out, registers.r12);
        if (registers.r12 >= 0x10000) {
            out << ",\"role_code\":" << static_cast<s32>(ReadValue<s8>(registers.r12, 0x08));
            out << ",\"account_token\":";
            WriteHex(out, ReadValue<u64>(registers.r12, 0x0C));
            out << ",\"user_id\":" << ReadValue<u32>(registers.r12, 0x14);
            out << ",\"character_id\":";
            WriteHex(out, ReadValue<u64>(registers.r12, 0x18));
            out << ",\"candidate_identity\":[";
            WriteHex(out, ReadValue<u64>(registers.r12, 0x20));
            out << ',';
            WriteHex(out, ReadValue<u64>(registers.r12, 0x28));
            out << ']';
            out << ",\"candidate_observer\":";
            WriteHex(out, ReadValue<u64>(registers.r12, 0xF0));
            out << ",\"candidate_area\":" << ReadValue<s32>(registers.r12, 0x98);
            out << ",\"candidate_generation\":"
                << static_cast<u32>(ReadValue<u8>(registers.r12, 0xEC));
            out << ",\"candidate_available_count\":"
                << static_cast<u32>(ReadValue<u8>(registers.r12, 0xED));
            out << ",\"candidate_stale_count\":"
                << static_cast<u32>(ReadValue<u8>(registers.r12, 0xEE));
        }
        out << ",\"local_identity_pointer\":";
        WriteHex(out, local_identity);
        if (local_identity >= 0x10000) {
            out << ",\"local_identity\":[";
            WriteHex(out, ReadValue<u64>(local_identity, 0));
            out << ',';
            WriteHex(out, ReadValue<u64>(local_identity, sizeof(u64)));
            out << ']';
        }
        out << ",\"manager_root\":";
        WriteHex(out, root);
        if (root >= 0x10000) {
            out << ",\"manager_identity\":[";
            WriteHex(out, ReadValue<u64>(root, 0xA68));
            out << ',';
            WriteHex(out, ReadValue<u64>(root, 0xA70));
            out << ']';
            out << ",\"manager_identity_state\":" << ReadValue<s32>(root, 0xA78);
            out << ",\"manager_area\":" << ReadValue<s32>(root, 0xA80);
            out << ",\"manager_gate_ac0\":" << ReadValue<s32>(root, 0xAC0);
            out << ",\"manager_flags_b14\":";
            WriteHex(out, ReadValue<u8>(root, 0xB14));
        }
    } else if (is_filter_progress) {
        out << ",\"constructed_candidate\":";
        WriteHex(out, registers.r12);
    } else if (is_pending_inserted) {
        const u64 pending_head =
            registers.r14 >= 0x10000 ? ReadValue<u64>(registers.r14, 0x1A8) : 0;
        const u64 pending_node =
            pending_head >= 0x10000 ? ReadValue<u64>(pending_head, sizeof(u64)) : 0;
        const u64 pending_wrapper =
            pending_node >= 0x10000 ? ReadValue<u64>(pending_node, 0x10) : 0;
        out << ",\"pending_node\":";
        WriteHex(out, pending_node);
        out << ",\"pending_wrapper\":";
        WriteHex(out, pending_wrapper);
        out << ",\"constructed_candidate\":";
        WriteHex(out, pending_wrapper >= 0x10000 ? ReadValue<u64>(pending_wrapper, 0) : 0);
        if (registers.r14 >= 0x10000) {
            out << ",\"pending_count\":" << ReadValue<u64>(registers.r14, 0x1B0);
        }
    } else if (is_inserted) {
        out << ",\"constructed_candidate\":";
        WriteHex(out, registers.r15);
        if (registers.r15 >= 0x10000) {
            out << ",\"candidate_observer_before_registration\":";
            WriteHex(out, ReadValue<u64>(registers.r15, 0xF0));
        }
    } else if (is_observer_result) {
        out << ",\"constructed_candidate\":";
        WriteHex(out, registers.r14);
        out << ",\"interface\":";
        WriteHex(out, registers.r15);
        out << ",\"observer\":";
        WriteHex(out, registers.r12);
        if (registers.r12 >= 0x10000) {
            out << ",\"observer_callback\":";
            WriteHex(out, ReadValue<u64>(registers.r12, 0x58));
        }
        out << ",\"observer_registered\":" << static_cast<u32>(static_cast<u8>(registers.rax));
    } else if (is_submit) {
        out << ",\"constructed_candidate\":";
        WriteHex(out, registers.r14);
    }
    out << '}';

    if (!is_entry || image_base == 0 || MemoryPatcher::g_eboot_image_size < sizeof(u64) ||
        CandidateLocalStatePointerOffset > MemoryPatcher::g_eboot_image_size - sizeof(u64)) {
        return;
    }

    const u64 root = ReadValue<u64>(image_base + CandidateLocalStatePointerOffset, 0);
    const u64 player = root >= 0x10000 ? ReadValue<u64>(root, 0x60) : 0;
    out << ",\"candidate_local_state\":{";
    out << "\"root\":";
    WriteHex(out, root);
    out << ",\"player\":";
    WriteHex(out, player);
    if (player >= 0x10000) {
        const u64 effects = ReadValue<u64>(player, 0x1C8);
        out << ",\"effects\":";
        WriteHex(out, effects);
        if (effects >= 0x10000) {
            out << ",\"effect_mask\":";
            WriteHex(out, ReadValue<u32>(effects, 0x20));
        }
    }
    out << '}';
    WriteMultiplayerEffects(out, player);
}

void WriteCharacterWarp(std::ostream& out, const TraceSite& site,
                        const GuestRegisterSnapshot& registers) {
    out << ",\"character_warp\":{";
    out << "\"operation\":\"" << site.name << "\"";
    const u64 return_address = ReadValue<u64>(registers.rsp, 0);
    out << ",\"return_address\":";
    WriteHex(out, return_address);
    if (return_address >= image_base &&
        return_address - image_base < MemoryPatcher::g_eboot_image_size) {
        out << ",\"caller_offset\":\"0x" << std::hex << return_address - image_base << std::dec
            << '\"';
    }
    if (site.offset == 0x013CB3D0) {
        out << ",\"source_entity_id\":" << static_cast<s32>(registers.rdi);
        out << ",\"target_area_entity_id\":" << static_cast<s32>(registers.rsi);
        out << ",\"command_variant_argument\":" << static_cast<s32>(registers.rdx);
    } else if (site.offset == 0x013CB870) {
        out << ",\"source_entity_id\":" << static_cast<s32>(registers.rdi);
        out << ",\"target_object_entity_id\":" << static_cast<s32>(registers.rsi);
        out << ",\"target_dummypoly_id\":" << static_cast<s32>(registers.rdx);
        out << ",\"command_variant_argument\":" << static_cast<s32>(registers.rcx);
    } else if (site.offset == 0x013CCBD0) {
        out << ",\"source_entity_id\":" << static_cast<s32>(registers.rdx);
        out << ",\"target_character_entity_id\":" << static_cast<s32>(registers.rdi);
        out << ",\"target_dummypoly_id\":" << static_cast<s32>(registers.rsi);
    } else {
        out << ",\"character_object\":";
        WriteHex(out, registers.rdi);
        out << ",\"floor_object\":";
        WriteHex(out, registers.rsi);
    }
    out << '}';
}

void WriteSummonSelection(std::ostream& out, const TraceSite& site,
                          const GuestRegisterSnapshot& registers) {
    const bool is_scan = site.offset == 0x01873269;
    const bool is_match = site.offset == 0x018732DD;
    const bool is_queue_insert = site.offset == 0x01875320;
    const bool is_queued = site.offset == 0x01873605;
    const bool is_start = site.offset == 0x014BAEC0;
    const bool is_allocated = site.offset == 0x014BAFE3;
    const u64 manager =
        is_scan ? registers.rax : (is_allocated ? registers.r12 : GetSummonManager());
    const u64 status =
        is_scan && registers.rbp >= 0x6F0 + 0x10000
            ? ReadValue<u64>(registers.rbp - 0x6F0, 0)
            : ((is_match ? registers.r15
                         : (is_queue_insert ? registers.rdi : (is_queued ? registers.rbx : 0))));

    out << ",\"summon_selection\":{";
    out << "\"stage\":\"" << site.name << "\"";
    out << ",\"manager\":";
    WriteHex(out, manager);
    out << ",\"status\":";
    WriteHex(out, status);

    const u64 matching_state = GetMatchingState();
    out << ",\"matching_state\":";
    WriteHex(out, matching_state);
    if (matching_state >= 0x10000) {
        out << ",\"matching_state_124\":" << ReadValue<s32>(matching_state, 0x124);
    }

    if (is_scan || is_match) {
        out << ",\"selection_mask\":";
        WriteHex(out, static_cast<u32>(registers.r13));
    }
    if (manager >= 0x10000) {
        const u64 candidate_head = ReadValue<u64>(manager, 0x30);
        out << ",\"candidate_head\":";
        WriteHex(out, candidate_head);
        out << ",\"candidate_count\":" << ReadValue<u64>(manager, 0x38);
        const u64 pending_head = ReadValue<u64>(manager, 0x1A8);
        out << ",\"pending_head\":";
        WriteHex(out, pending_head);
        out << ",\"pending_count\":" << ReadValue<u64>(manager, 0x1B0);
        out << ",\"request_handle\":";
        WriteHex(out, ReadValue<u64>(manager, 0x138));
        if (pending_head >= 0x10000) {
            const u64 node = ReadValue<u64>(pending_head, 0);
            out << ",\"pending_first\":";
            WriteHex(out, node == pending_head ? 0 : node);
            if (node >= 0x10000 && node != pending_head) {
                const u64 item = ReadValue<u64>(node, 0x10);
                out << ",\"pending_item\":";
                WriteHex(out, item);
                if (item >= 0x10000) {
                    const u64 candidate = ReadValue<u64>(item, 0);
                    const u64 prepared = ReadValue<u64>(item, 0x08);
                    out << ",\"pending_state\":" << static_cast<u32>(ReadValue<u8>(item, 0x10));
                    out << ",\"pending_candidate\":";
                    WriteHex(out, candidate);
                    out << ",\"pending_prepared\":";
                    WriteHex(out, prepared);
                    if (candidate >= 0x10000) {
                        const s32 role_code = ReadValue<s8>(candidate, 0x08);
                        out << ",\"pending_role_code\":" << role_code;
                        WriteRoleSelectionMask(out, "pending_role_selection_mask", role_code);
                    }
                    if (prepared >= 0x10000) {
                        out << ",\"pending_prepared_role_code\":"
                            << static_cast<u32>(ReadValue<u8>(prepared, 0x22));
                    }
                }
            }
        }
    }
    if (is_match && registers.rsi >= 0x10000) {
        const u64 candidate = ReadValue<u64>(registers.rsi, 0);
        const u64 prepared = ReadValue<u64>(registers.rsi, 0x08);
        out << ",\"matched_item\":";
        WriteHex(out, registers.rsi);
        out << ",\"matched_candidate\":";
        WriteHex(out, candidate);
        out << ",\"matched_prepared\":";
        WriteHex(out, prepared);
        if (candidate >= 0x10000) {
            const s32 role_code = ReadValue<s8>(candidate, 0x08);
            out << ",\"matched_role_code\":" << role_code;
            WriteRoleSelectionMask(out, "matched_role_selection_mask", role_code);
        }
        if (prepared >= 0x10000) {
            out << ",\"matched_prepared_role_code\":"
                << static_cast<u32>(ReadValue<u8>(prepared, 0x22));
        }
    }
    if (status >= 0x10000) {
        out << ",\"queued_count\":" << ReadValue<u64>(status, 0x78);
        out << ",\"request_state_88\":" << ReadValue<s32>(status, 0x88);
        out << ",\"active_request_id\":" << ReadValue<s32>(status, 0x90);
    }
    if (is_start) {
        out << ",\"target_id\":" << static_cast<u32>(registers.rdi);
        out << ",\"session_id\":" << static_cast<u32>(registers.rsi);
        out << ",\"request_context\":";
        WriteHex(out, registers.rdx);
    } else if (is_allocated) {
        out << ",\"allocated_handle\":";
        WriteHex(out, registers.rax);
    } else if (is_queue_insert) {
        out << ",\"queue_object\":";
        WriteHex(out, registers.rsi);
        // Diagnostics for the stuck third summon (rig 2026-10-11): the queue is a map keyed by
        // the u32 at the object's start ([status+0x10], node +0x20 key, +0x28 entry); an entry
        // already there is overwritten keeping its +0x23 byte, and nothing is queued.
        const u64 object = registers.rsi;
        if (object >= 0x10000 && HasMemoryAccess(object, 0x48, MemoryProt::CpuRead)) {
            const u32 key = ReadValue<u32>(object, 0);
            out << ",\"queue_key\":" << key;
            std::string name;
            for (u64 index = 0; index < 0x24; ++index) {
                const char c = ReadValue<char>(object, 0x24 + index);
                if (c < 0x20 || c > 0x7E) {
                    break;
                }
                name += c;
            }
            out << ",\"queue_name\":\"" << name << '"';
            const u64 header = status >= 0x10000 ? ReadValue<u64>(status, 0x10) : 0;
            u64 node = header >= 0x10000 ? ReadValue<u64>(header, 0x08) : 0;
            for (int depth = 0; node >= 0x10000 && depth < 64 && ReadValue<u8>(node, 0x19) == 0;
                 ++depth) {
                const u32 node_key = ReadValue<u32>(node, 0x20);
                if (node_key == key) {
                    const u64 entry = ReadValue<u64>(node, 0x28);
                    out << ",\"queue_existing\":";
                    WriteHex(out, entry);
                    if (entry >= 0x10000) {
                        out << ",\"queue_existing_22\":" << u32{ReadValue<u8>(entry, 0x22)}
                            << ",\"queue_existing_23\":" << u32{ReadValue<u8>(entry, 0x23)};
                    }
                    break;
                }
                node = ReadValue<u64>(node, node_key < key ? 0x10 : 0x00);
            }
        }
    }
    out << '}';
}

void WriteSummonBuild(std::ostream& out, const TraceSite& site,
                      const GuestRegisterSnapshot& registers) {
    u64 prepared{};
    switch (site.offset) {
    case SummonBuildEntryOffset:
        prepared = registers.rsi;
        break;
    case 0x01874C94:
        prepared = registers.r15;
        break;
    case 0x01874E5A:
        prepared = registers.rbx;
        break;
    case 0x018750F7:
        prepared = registers.rbx;
        break;
    default:
        prepared = registers.r8;
        break;
    }

    out << ",\"summon_build\":{";
    out << "\"stage\":\"" << site.name << "\"";
    out << ",\"prepared\":";
    WriteHex(out, prepared);
    if (site.offset == SummonBuildEntryOffset) {
        if (host_placement_rewrite.prepared == prepared) {
            const auto write_placement = [&out](std::string_view name,
                                                const SummonPlacementDescriptor& placement) {
                out << ",\"" << name << "\":{";
                out << "\"packed_region\":";
                WriteHex(out, placement.packed_region);
                out << ",\"area\":" << placement.area;
                out << ",\"x\":" << placement.x;
                out << ",\"y\":" << placement.y;
                out << ",\"z\":" << placement.z;
                out << ",\"heading\":" << placement.heading << '}';
            };
            out << ",\"host_placement_rewrite\":{\"result\":\"" << host_placement_rewrite.result
                << '\"';
            write_placement("candidate", host_placement_rewrite.candidate);
            write_placement("host", host_placement_rewrite.host);
            out << '}';
        }
        out << ",\"status\":";
        WriteHex(out, registers.rdi);
        out << ",\"destructive_reject\":" << static_cast<u32>(registers.rdx & 0xFF);
        if (registers.rdi >= 0x10000) {
            out << ",\"status_extra_players\":" << ReadValue<s32>(registers.rdi, 0x1C8);
        }

        const u64 matching_state = GetMatchingState();
        out << ",\"matching_state\":";
        WriteHex(out, matching_state);
        if (matching_state >= 0x10000) {
            out << ",\"matching_state_124\":" << ReadValue<s32>(matching_state, 0x124);
        }

        const u64 session_rules = ReadValue<u64>(image_base + SummonSessionRulesPointerOffset, 0);
        out << ",\"session_rules\":";
        WriteHex(out, session_rules);
        if (session_rules >= 0x10000) {
            out << ",\"session_capacity\":" << ReadValue<s32>(session_rules, 0x0C);
        }

        const u64 global_state = ReadValue<u64>(image_base + GlobalStatePointerOffset, 0);
        const u64 population = global_state >= 0x10000 ? ReadValue<u64>(global_state, 0x16F8) : 0;
        out << ",\"global_state\":";
        WriteHex(out, global_state);
        out << ",\"online_ready\":" << static_cast<u32>(ReadValue<u8>(global_state, 0x1592));
        out << ",\"population\":";
        WriteHex(out, population);
        if (population >= 0x10000) {
            out << ",\"population_08\":" << ReadValue<s32>(population, 0x08);
            out << ",\"population_0c\":" << ReadValue<s32>(population, 0x0C);
            out << ",\"population_10\":" << ReadValue<s32>(population, 0x10);
            out << ",\"population_14\":" << ReadValue<s32>(population, 0x14);
            out << ",\"population_request_98\":" << ReadValue<s32>(population, 0x98);
        }

        const u64 local_state = ReadValue<u64>(image_base + CandidateLocalStatePointerOffset, 0);
        const u64 local_world = local_state >= 0x10000 ? ReadValue<u64>(local_state, 0x60) : 0;
        out << ",\"local_state\":";
        WriteHex(out, local_state);
        out << ",\"local_world\":";
        WriteHex(out, local_world);
        if (local_world >= 0x10000) {
            out << ",\"local_world_state\":" << ReadValue<s32>(local_world, 0x78);
        }

        const u64 manager_root = GetSummonManagerRoot();
        out << ",\"manager_root\":";
        WriteHex(out, manager_root);
        if (manager_root >= 0x10000) {
            out << ",\"manager_area\":" << ReadValue<s32>(manager_root, 0xA80);
            out << ",\"manager_flags_b14\":";
            WriteHex(out, ReadValue<u8>(manager_root, 0xB14));
        }
    }
    if (site.offset == 0x018750F7) {
        out << ",\"result\":";
        WriteHex(out, registers.rbx);
    }

    if (prepared >= 0x10000) {
        const u32 role_code = ReadValue<u8>(prepared, 0x22);
        out << ",\"role_code\":" << role_code;
        out << ",\"packed_region\":";
        WriteHex(out, ReadValue<u32>(prepared, 0x04));
        out << ",\"area\":" << ReadValue<s32>(prepared, 0x18);
        out << ",\"request_id\":" << ReadValue<s32>(prepared, 0x88);

        if (role_code < RoleMetadataCount) {
            const u64 metadata = image_base + RoleMetadataTableOffset +
                                 static_cast<u64>(role_code) * RoleMetadataStride;
            const u64 build_role =
                image_base + SummonBuildRoleTableOffset + static_cast<u64>(role_code) * 12;
            out << ",\"selection_mask\":";
            WriteHex(out, ReadValue<u32>(metadata, 0x04));
            out << ",\"role_flags\":";
            WriteHex(out, ReadValue<u8>(metadata, 0x08));
            out << ",\"capacity_class\":" << ReadValue<s32>(metadata, 0x10);
            out << ",\"role_rule\":" << static_cast<u32>(ReadValue<u8>(metadata, 0x14));
            out << ",\"delay_enabled\":" << static_cast<u32>(ReadValue<u8>(build_role, 0));
            out << ",\"delay_seconds\":" << ReadValue<float>(build_role, 0x04);
        }
    }
    out << '}';
}

void WriteMessengerState(std::ostream& out, u64 object) {
    if (object < 0x10000) {
        return;
    }
    out << ",\"messenger\":{";
    out << "\"request\":";
    WriteHex(out, ReadValue<u64>(object, 0xE0));
    out << ",\"waiting\":" << static_cast<u32>(ReadValue<u8>(object, 0xF0));
    out << ",\"flags_144\":";
    WriteHex(out, ReadValue<u8>(object, 0x144));
    out << '}';
}

bool HasActionTransition(u64 object) {
    if (object < 0x10000) {
        return false;
    }
    const u64 current = ReadValue<u64>(object, 0x10);
    const u64 previous = ReadValue<u64>(object, 0x18);
    return current != previous;
}

void WriteActionFlags(std::ostream& out, u64 object, u64 stack_pointer) {
    if (object < 0x10000 || stack_pointer < 0x10000) {
        return;
    }
    const u64 current = ReadValue<u64>(object, 0x10);
    const u64 previous = ReadValue<u64>(object, 0x18);
    const u64 return_address = ReadValue<u64>(stack_pointer, 0);
    out << ",\"action_flags\":{";
    out << "\"module\":";
    WriteHex(out, object);
    out << ",\"return_address\":";
    WriteHex(out, return_address);
    if (return_address >= image_base &&
        return_address - image_base < MemoryPatcher::g_eboot_image_size) {
        out << ",\"caller_offset\":\"0x" << std::hex << return_address - image_base << std::dec
            << '\"';
    }
    out << ",\"current\":";
    WriteHex(out, current);
    out << ",\"previous\":";
    WriteHex(out, previous);
    out << ",\"pressed\":";
    WriteHex(out, current & ~previous);
    out << ",\"released\":";
    WriteHex(out, previous & ~current);
    out << ",\"field_30\":";
    WriteHex(out, ReadValue<u64>(object, 0x30));
    out << ",\"field_38\":";
    WriteHex(out, ReadValue<u64>(object, 0x38));
    out << ",\"field_40\":";
    WriteHex(out, ReadValue<u64>(object, 0x40));
    out << ",\"field_48\":";
    WriteHex(out, ReadValue<u64>(object, 0x48));
    out << ",\"state_90\":";
    WriteHex(out, ReadValue<u32>(object, 0x90));
    out << ",\"state_94\":";
    WriteHex(out, ReadValue<u32>(object, 0x94));
    out << ",\"flags_9c\":";
    WriteHex(out, ReadValue<u8>(object, 0x9C));
    out << '}';
}

void WritePlayerItemState(std::ostream& out, u64 player) {
    out << "\"player\":";
    WriteHex(out, player);
    if (player >= 0x10000) {
        const u64 vtable = ReadValue<u64>(player, 0);
        out << ",\"vtable\":";
        WriteHex(out, vtable);
        if (vtable >= 0x10000) {
            out << ",\"vfunc_1c0\":";
            WriteHex(out, ReadValue<u64>(vtable, 0x1C0));
            out << ",\"vfunc_1c8\":";
            WriteHex(out, ReadValue<u64>(vtable, 0x1C8));
        }
        out << ",\"entity_60\":";
        WriteHex(out, ReadValue<u64>(player, 0x60));
        out << ",\"handle_90\":";
        WriteHex(out, ReadValue<u32>(player, 0x90));
        out << ",\"active_goods\":";
        WriteHex(out, ReadValue<u32>(player, 0x1C0));
        out << ",\"resolved_goods\":";
        WriteHex(out, ReadValue<u32>(player, 0x478));
        out << ",\"override_goods\":";
        WriteHex(out, ReadValue<u32>(player, 0x47C));
        out << ",\"queued_goods\":";
        WriteHex(out, ReadValue<u32>(player, 0x488));
        out << ",\"item_state\":";
        WriteHex(out, ReadValue<u32>(player, 0x490));
        WriteMultiplayerEffects(out, player);
    }
}

void WriteUseItemState(std::ostream& out, const TraceSite& site,
                       const GuestRegisterSnapshot& registers) {
    out << ",\"use_item\":{";
    WritePlayerItemState(out, registers.r13);
    out << ",\"rax\":";
    WriteHex(out, registers.rax);
    out << ",\"eax_signed\":" << static_cast<s64>(static_cast<s32>(registers.rax));
    if (site.offset == 0x018F751C && registers.rax >= 0x10000) {
        const u64 current = ReadValue<u64>(registers.rax, 0x10);
        const u64 previous = ReadValue<u64>(registers.rax, 0x18);
        out << ",\"action_current\":";
        WriteHex(out, current);
        out << ",\"action_previous\":";
        WriteHex(out, previous);
        out << ",\"action_pressed\":";
        WriteHex(out, ReadValue<u64>(registers.rax, 0x20));
    }
    if (site.offset == 0x018F7529 && registers.rax >= 0x10000) {
        out << ",\"inventory\":";
        WriteHex(out, registers.rax);
        out << ",\"inventory_selection\":[";
        for (size_t index = 0; index < 8; ++index) {
            if (index != 0) {
                out << ',';
            }
            WriteHex(out, ReadValue<u64>(registers.rax, 0x1D0 + index * sizeof(u64)));
        }
        out << ']';
    }
    out << '}';
}

bool IsTrackedGoods(s32 goods_id) {
    constexpr std::array TrackedGoods{100, 111, 200, 201, 205, 206, 225, 226};
    return std::ranges::find(TrackedGoods, goods_id) != TrackedGoods.end();
}

s32 GetUseItemExecutionGoods(const TraceSite& site, const GuestRegisterSnapshot& registers) {
    if (site.offset == 0x018C9364) {
        return registers.rbp >= 0xD8 ? ReadValue<s32>(registers.rbp - 0xD8, 0) : -1;
    }
    return static_cast<s32>(registers.rsi);
}

void WriteUseItemExecution(std::ostream& out, const TraceSite& site,
                           const GuestRegisterSnapshot& registers) {
    const bool is_result = site.offset == 0x018C9364;
    const u64 player = is_result ? registers.r14 : registers.rdi;
    const s32 goods_id = GetUseItemExecutionGoods(site, registers);
    const s32 argument = static_cast<s32>(is_result ? registers.rbx : registers.rdx);
    out << ",\"use_item_execution\":{";
    WritePlayerItemState(out, player);
    out << ",\"goods_id\":" << goods_id;
    out << ",\"argument\":" << argument;
    if (is_result) {
        out << ",\"result\":" << static_cast<u32>(static_cast<u8>(registers.r12));
    }
    out << '}';
}

void WriteGoodsActionSubmit(std::ostream& out, const GuestRegisterSnapshot& registers) {
    out << ",\"goods_action\":{";
    out << "\"player\":";
    WriteHex(out, registers.rdi);
    out << ",\"goods_id\":" << static_cast<s32>(registers.rsi);
    out << ",\"argument\":" << static_cast<s32>(registers.rdx);
    out << ",\"sp_effect_id\":" << static_cast<s32>(registers.rcx);
    out << ",\"executor_allowed\":" << static_cast<u32>(static_cast<u8>(registers.r8));
    WriteMultiplayerEffects(out, registers.rdi);
    out << '}';
}

void WriteGoodsParamLookup(std::ostream& out, const GuestRegisterSnapshot& registers) {
    const s32 goods_id = static_cast<s32>(registers.rsi);
    const u64 return_address = ReadValue<u64>(registers.rsp, 0);
    out << ",\"goods_lookup\":{";
    out << "\"goods_id\":" << goods_id;
    out << ",\"sos_area\":" << observed_sos_area.load(std::memory_order_relaxed);
    out << ",\"result_storage\":";
    WriteHex(out, registers.rdi);
    out << ",\"return_address\":";
    WriteHex(out, return_address);
    if (return_address >= image_base &&
        return_address - image_base < MemoryPatcher::g_eboot_image_size) {
        out << ",\"caller_offset\":\"0x" << std::hex << return_address - image_base << std::dec
            << '\"';
    }
    out << '}';
}

void WriteBellAvailability(std::ostream& out, const TraceSite& site,
                           const GuestRegisterSnapshot& registers) {
    const bool is_native_result = site.offset == 0x0157F962;
    const u64 selected_area = static_cast<u8>(registers.rax) != 0 ? registers.rcx : registers.rbx;
    const s32 area =
        is_native_result ? static_cast<s32>(registers.rbx) : ReadValue<s32>(selected_area, 0);
    out << ",\"bell_availability\":{";
    out << "\"area\":" << area;
    out << ",\"observed_sos_area\":" << observed_sos_area.load(std::memory_order_relaxed);
    out << ",\"player\":";
    WriteHex(out, registers.r14);
    if (is_native_result) {
        out << ",\"native_blocked\":" << static_cast<u32>(static_cast<u8>(registers.rax));
    }
    WriteMultiplayerEffects(out, registers.r14);
    out << '}';
}

void WriteResponderBellAvailability(std::ostream& out, const TraceSite& site,
                                    const GuestRegisterSnapshot& registers, s32 goods_id) {
    out << ",\"responder_bell_availability\":{";
    out << "\"stage\":\"" << site.name << "\"";
    out << ",\"goods_id\":" << goods_id;
    out << ",\"player\":";
    WriteHex(out, registers.r14);
    out << ",\"original_goods_argument\":" << static_cast<s32>(registers.rbx);
    out << ",\"resolved_goods_id\":" << static_cast<s32>(registers.r12);
    if (site.offset == 0x015800D1) {
        out << ",\"final_result\":" << static_cast<u32>(static_cast<u8>(registers.rcx));
    }
    out << '}';
    if (site.offset == 0x0157F686) {
        WriteMultiplayerEffects(out, registers.r14);
    }
}

void WriteQwords(std::ostream& out, std::string_view key, u64 address, size_t count) {
    if (address < 0x10000) {
        return;
    }
    out << ",\"" << key << "\":[";
    for (size_t index = 0; index < count; ++index) {
        if (index != 0) {
            out << ',';
        }
        WriteHex(out, ReadValue<u64>(address, index * sizeof(u64)));
    }
    out << ']';
}

void WriteFloatVector(std::ostream& out, std::string_view key, u64 address) {
    if (address < 0x10000) {
        return;
    }
    out << ",\"" << key << "\":[";
    for (size_t index = 0; index < 4; ++index) {
        if (index != 0) {
            out << ',';
        }
        const float value = ReadValue<float>(address, index * sizeof(float));
        if (std::isfinite(value)) {
            out << std::setprecision(9) << value;
        } else {
            out << "null";
        }
    }
    out << ']';
}

void WriteMapId(std::ostream& out, std::string_view key, u64 address) {
    if (address < 0x10000) {
        return;
    }
    const u32 map_id = ReadValue<u32>(address, 0);
    out << ",\"" << key << "\":";
    WriteHex(out, map_id);
    out << ",\"" << key << "_parts\":[" << ((map_id >> 24) & 0xFF) << ',' << ((map_id >> 16) & 0xFF)
        << ',' << ((map_id >> 8) & 0xFF) << ',' << (map_id & 0xFF) << ']';
}

void WriteU32(std::ostream& out, std::string_view key, u64 address) {
    if (address < 0x10000) {
        return;
    }
    out << ",\"" << key << "\":";
    WriteHex(out, ReadValue<u32>(address, 0));
}

void WritePlacementState(std::ostream& out, u64 state) {
    WriteQwords(out, "global_qwords", state, 6);
    WriteQwords(out, "sos_placement_qwords", state + 0x14A0, 5);
    WriteQwords(out, "summoned_placement_qwords", state + 0x14D0, 5);
    WriteQwords(out, "warp_qwords", state + 0x1518, 3);
    WriteQwords(out, "received_placement_qwords", state + 0x1620, 6);
}

void WriteStageWarpState(std::ostream& out, u64 state) {
    if (state < 0x10000) {
        return;
    }
    out << ",\"stage_warp_state\":{";
    out << "\"state\":";
    WriteHex(out, state);
    out << ",\"request_active\":" << static_cast<u32>(ReadValue<u8>(state, 0x08));
    const u32 map_id = ReadValue<u32>(state, 0x0C);
    out << ",\"map_id\":";
    WriteHex(out, map_id);
    out << ",\"map_parts\":[" << ((map_id >> 24) & 0xFF) << ',' << ((map_id >> 16) & 0xFF) << ','
        << ((map_id >> 8) & 0xFF) << ',' << (map_id & 0xFF) << ']';
    out << ",\"warp_info_id\":" << ReadValue<s32>(state, 0x10);
    out << ",\"mode_1538\":" << ReadValue<s32>(state, 0x1538);
    out << ",\"respawn_point_id_153c\":" << ReadValue<s32>(state, 0x153C);
    out << ",\"auxiliary_id_1540\":" << ReadValue<s32>(state, 0x1540);
    out << ",\"warp_fields_153c\":";
    WriteHex(out, ReadValue<u64>(state, 0x153C));
    out << ",\"state_1592\":" << static_cast<u32>(ReadValue<u8>(state, 0x1592));
    out << ",\"transition_16f8\":";
    WriteHex(out, ReadValue<u64>(state, 0x16F8));
    out << '}';
}

void WriteStageWarp(std::ostream& out, const TraceSite& site,
                    const GuestRegisterSnapshot& registers) {
    out << ",\"stage_warp\":{";
    if (site.offset == 0x0132E010) {
        const u32 map_id = (static_cast<u32>(registers.rsi) & 0xFF) << 24 |
                           (static_cast<u32>(registers.rdx) & 0xFF) << 16 |
                           (static_cast<u32>(registers.rcx) & 0xFF) << 8 |
                           (static_cast<u32>(registers.r8) & 0xFF);
        out << "\"source\":\"event\",\"map_id\":";
        WriteHex(out, map_id);
        out << ",\"map_parts\":[" << (registers.rsi & 0xFF) << ',' << (registers.rdx & 0xFF) << ','
            << (registers.rcx & 0xFF) << ',' << (registers.r8 & 0xFF) << ']';
        out << ",\"warp_info_id\":" << static_cast<s32>(registers.r9);
    } else if (site.offset == 0x0132E050) {
        const s32 warp_info_id = static_cast<s32>(registers.rsi);
        const s32 area = warp_info_id / 100000;
        const s32 block = (warp_info_id / 10000) % 10;
        out << "\"source\":\"bonfire_event\",\"warp_info_id\":" << warp_info_id;
        out << ",\"derived_map_parts\":[" << area << ',' << block << ",0,0]";
    } else {
        out << "\"source\":\"transition\"";
    }
    const u64 player = GetLocalPlayer();
    out << ",\"current_map\":";
    WriteHex(out, GetCurrentPackedMap()); // raw: the RE trace records the game's own memory
    out << ",\"player\":";
    WriteHex(out, player);
    out << ",\"host_effect_9001\":" << (HasPlayerEffect(player, 9001) ? "true" : "false");
    out << '}';
    WriteStageWarpState(out, GetGlobalState());
}

void WriteStageWarpDescriptor(std::ostream& out, const GuestRegisterSnapshot& registers) {
    out << ",\"stage_warp_descriptor\":{";
    out << "\"descriptor\":";
    WriteHex(out, registers.r9);
    out << ",\"mode\":" << static_cast<s32>(registers.r13);
    if (registers.r9 >= 0x10000) {
        out << ",\"request_type\":" << ReadValue<s32>(registers.r9, 0x54);
        out << ",\"request_subtype\":" << ReadValue<s32>(registers.r9, 0x58);
        out << ",\"descriptor_mode\":" << ReadValue<s32>(registers.r9, 0x98);
        out << ",\"previous_respawn_point\":" << ReadValue<s32>(registers.r9, 0x80);
        out << ",\"previous_auxiliary_id\":" << ReadValue<s32>(registers.r9, 0x84);
    }
    out << '}';
    WriteStageWarpState(out, registers.rax);
}

void WriteHealingFountainRegistration(std::ostream& out, const GuestRegisterSnapshot& registers) {
    float reaction_distance{};
    float reaction_angle{};
    std::memcpy(&reaction_distance, registers.ymm[0].data(), sizeof(reaction_distance));
    std::memcpy(&reaction_angle, registers.ymm[1].data(), sizeof(reaction_angle));

    out << ",\"healing_fountain_registration\":{";
    out << "\"manager\":";
    WriteHex(out, registers.rdi);
    out << ",\"event_flag\":" << static_cast<s32>(registers.rsi);
    out << ",\"entity_id\":" << static_cast<s32>(registers.rdx);
    out << ",\"reaction_distance\":";
    if (std::isfinite(reaction_distance)) {
        out << std::setprecision(9) << reaction_distance;
    } else {
        out << "null";
    }
    out << ",\"reaction_angle\":";
    if (std::isfinite(reaction_angle)) {
        out << std::setprecision(9) << reaction_angle;
    } else {
        out << "null";
    }
    out << ",\"initial_sword_number\":" << static_cast<s32>(registers.r8);
    out << ",\"sword_level\":" << static_cast<s32>(registers.rcx);
    out << '}';
}

void WriteHealingFountainAvailability(std::ostream& out, const GuestRegisterSnapshot& registers) {
    out << ",\"healing_fountain_availability\":{";
    out << "\"result\":\"" << healing_fountain_availability.result << '\"';
    out << ",\"object\":";
    WriteHex(out, healing_fountain_availability.object);
    out << ",\"player\":";
    WriteHex(out, healing_fountain_availability.player);
    out << ",\"host_effect_9001\":"
        << (healing_fountain_availability.host_effect ? "true" : "false");
    out << ",\"blocked_before\":"
        << (healing_fountain_availability.blocked_before ? "true" : "false");
    out << ",\"blocked_after\":"
        << (healing_fountain_availability.blocked_after ? "true" : "false");
    if (registers.r13 >= 0x10000 && HasMemoryAccess(registers.r13, 0x4C, MemoryProt::CpuRead)) {
        out << ",\"special_map_48\":" << static_cast<u32>(ReadValue<u8>(registers.r13, 0x48));
        out << ",\"multiplayer_49\":" << static_cast<u32>(ReadValue<u8>(registers.r13, 0x49));
        out << ",\"entity_missing_4a\":" << static_cast<u32>(ReadValue<u8>(registers.r13, 0x4A));
        out << ",\"native_gate_4b\":" << static_cast<u32>(ReadValue<u8>(registers.r13, 0x4B));
    }
    out << '}';
}

void WriteChairRespawnNotification(std::ostream& out, const GuestRegisterSnapshot& registers) {
    const u64 task = registers.rdi;
    const u64 manager = GetSummonManagerRoot();
    const u64 online_state =
        image_base != 0 ? ReadValue<u64>(image_base + OnlineStatePointerOffset, 0) : 0;

    out << ",\"chair_messenger_respawn_notify\":{";
    out << "\"task\":";
    WriteHex(out, task);
    if (task >= 0x10000 && HasMemoryAccess(task, 0xDC, MemoryProt::CpuRead)) {
        out << ",\"state_50\":" << ReadValue<s32>(task, 0x50);
        out << ",\"step_54\":" << ReadValue<s32>(task, 0x54);
        out << ",\"wait_active_c8\":" << static_cast<u32>(ReadValue<u8>(task, 0xC8));
        out << ",\"selected_respawn_point_cc\":" << ReadValue<s32>(task, 0xCC);
        const float timer = ReadValue<float>(task, 0xD8);
        out << ",\"timer_d8\":";
        if (std::isfinite(timer)) {
            out << std::setprecision(9) << timer;
        } else {
            out << "null";
        }
    }

    out << ",\"manager\":";
    WriteHex(out, manager);
    if (manager >= 0x10000 && HasMemoryAccess(manager, 0xA84, MemoryProt::CpuRead)) {
        out << ",\"active_08\":" << static_cast<u32>(ReadValue<u8>(manager, 0x08));
        out << ",\"state_9f4\":";
        WriteHex(out, ReadValue<u64>(manager, 0x9F4));
        out << ",\"lan_connected_9f5\":" << static_cast<u32>(ReadValue<u8>(manager, 0x9F5));
        out << ",\"lan_disconnected_9f6\":" << static_cast<u32>(ReadValue<u8>(manager, 0x9F6));
        out << ",\"signed_in_9f7\":" << static_cast<u32>(ReadValue<u8>(manager, 0x9F7));
        out << ",\"signed_out_9f8\":" << static_cast<u32>(ReadValue<u8>(manager, 0x9F8));
        out << ",\"state_9f9\":" << static_cast<u32>(ReadValue<u8>(manager, 0x9F9));
        out << ",\"state_9fa\":" << static_cast<u32>(ReadValue<u8>(manager, 0x9FA));
        out << ",\"network_flags_a05\":";
        WriteHex(out, ReadValue<u8>(manager, 0xA05));
        out << ",\"gate_a50\":" << static_cast<u32>(ReadValue<u8>(manager, 0xA50));
        out << ",\"map_a78\":";
        WriteHex(out, ReadValue<u32>(manager, 0xA78));
        out << ",\"area_a80\":" << ReadValue<s32>(manager, 0xA80);
        out << ",\"latch_d30\":" << static_cast<u32>(ReadValue<u8>(manager, 0xD30));
        out << ",\"latch_d31\":" << static_cast<u32>(ReadValue<u8>(manager, 0xD31));
    }
    out << ",\"online_state\":";
    WriteHex(out, online_state);
    if (online_state >= 0x10000 && HasMemoryAccess(online_state, 0x130, MemoryProt::CpuRead)) {
        out << ",\"online_d4\":" << ReadValue<s32>(online_state, 0xD4);
        out << ",\"online_d8\":" << static_cast<u32>(ReadValue<u8>(online_state, 0xD8));
        out << ",\"online_e0\":" << static_cast<u32>(ReadValue<u8>(online_state, 0xE0));
        out << ",\"online_120\":" << ReadValue<s32>(online_state, 0x120);
        out << ",\"online_124\":" << ReadValue<s32>(online_state, 0x124);
        out << ",\"online_128\":" << ReadValue<s32>(online_state, 0x128);
        out << ",\"online_12c\":" << ReadValue<s32>(online_state, 0x12C);
    }
    out << '}';
}

void WriteStageWarpAcknowledge(std::ostream& out, const GuestRegisterSnapshot& registers) {
    out << ",\"stage_warp_acknowledge\":{";
    out << "\"state\":";
    WriteHex(out, registers.rcx);
    const bool frame_valid = registers.rbp >= 0x198 + 0x10000;
    if (frame_valid) {
        const bool will_clear_request = ReadValue<u8>(registers.rbp - 0x198, 0) != 0;
        out << ",\"will_clear_request\":" << (will_clear_request ? "true" : "false");
    }
    out << '}';
    WriteStageWarpState(out, registers.rcx);
}

void WriteStageWarpTask(std::ostream& out, u64 task, bool include_resolved_transform) {
    if (task < 0x10000) {
        return;
    }

    const u32 map_id = ReadValue<u32>(task, 0x58);
    out << ",\"stage_warp_task\":{";
    out << "\"task\":";
    WriteHex(out, task);
    out << ",\"map_id\":";
    WriteHex(out, map_id);
    out << ",\"map_parts\":[" << ((map_id >> 24) & 0xFF) << ',' << ((map_id >> 16) & 0xFF) << ','
        << ((map_id >> 8) & 0xFF) << ',' << (map_id & 0xFF) << ']';
    out << ",\"request_type\":" << ReadValue<s32>(task, 0xA4);
    out << ",\"request_subtype\":" << ReadValue<s32>(task, 0xA8);
    out << ",\"respawn_point_id\":" << ReadValue<s32>(task, 0xD0);
    out << ",\"auxiliary_id\":" << ReadValue<s32>(task, 0xD4);
    out << ",\"mode\":" << ReadValue<s32>(task, 0xE8);
    out << '}';
    if (include_resolved_transform) {
        WriteFloatVector(out, "resolved_position", task + 0xF0);
        const float facing = ReadValue<float>(task, 0x100);
        out << ",\"resolved_facing\":";
        if (std::isfinite(facing)) {
            out << std::setprecision(9) << facing;
        } else {
            out << "null";
        }
    }
}

void WriteRespawnTransformResolve(std::ostream& out, const GuestRegisterSnapshot& registers) {
    out << ",\"respawn_transform_resolve\":{";
    out << "\"map_resource\":";
    WriteHex(out, registers.rdi);
    out << ",\"position_output\":";
    WriteHex(out, registers.rsi);
    out << ",\"orientation_output\":";
    WriteHex(out, registers.rdx);
    out << ",\"respawn_point_id\":" << static_cast<s32>(registers.rcx);
    out << '}';
}

} // namespace

namespace Coop {

u64 GetGlobalState() {
    const u64 image_size = MemoryPatcher::g_eboot_image_size;
    if (image_base == 0 || image_size < sizeof(u64) ||
        GlobalStatePointerOffset > image_size - sizeof(u64)) {
        return 0;
    }
    return ReadValue<u64>(image_base + GlobalStatePointerOffset, 0);
}

} // namespace Coop

namespace {

void WriteWorldStateValidation(std::ostream& out, const TraceSite& site,
                               const GuestRegisterSnapshot& registers) {
    s32 area = -1;
    switch (site.offset) {
    case 0x018C5365:
        area = ReadValue<s32>(registers.rbp - 0x38, 0);
        break;
    case 0x018CF97A:
        area = ReadValue<s32>(registers.rbp - 0x1A8, 0);
        break;
    case 0x0191A980:
    case 0x0191AA71:
        area = static_cast<s32>(registers.rbx);
        break;
    case 0x01AC6557:
    case 0x01AC661E:
        area = ReadValue<s32>(registers.rbx, 0);
        break;
    case 0x01FEB611:
    case 0x01FEBD01:
    case 0x01FEC381:
        area = ReadValue<s32>(registers.rbp - 0x1B8, 0);
        break;
    }

    const u64 return_address = ReadValue<u64>(registers.rbp, sizeof(u64));
    out << ",\"world_state_validation\":{";
    out << "\"call_offset\":\"0x" << std::hex << site.offset << std::dec << '"';
    out << ",\"result_low8\":" << static_cast<u32>(static_cast<u8>(registers.rax));
    out << ",\"area\":" << area;
    out << ",\"manager_area\":";
    const u64 root = GetSummonManagerRoot();
    out << (root >= 0x10000 ? ReadValue<s32>(root, 0xA80) : -1);
    out << ",\"return_address\":";
    WriteHex(out, return_address);
    if (return_address >= image_base &&
        return_address - image_base < MemoryPatcher::g_eboot_image_size) {
        out << ",\"caller_offset\":\"0x" << std::hex << return_address - image_base << std::dec
            << '"';
    }
    out << '}';
}

} // namespace

namespace Coop {

void PS4_SYSV_ABI TraceEntry(u64 tag, const GuestRegisterSnapshot* registers) {
    if (tag >= Sites.size() || registers == nullptr) {
        return;
    }

    const auto& site = Sites[tag];
    if (summon_build_host_placement_hook_installed && site.offset == SummonBuildEntryOffset) {
        ApplyCrossMapSummonHostPlacement(*registers);
    }
    if (cross_map_guest_handoff_hook_installed && site.offset == CrossMapGuestHandoffOffset) {
        ApplyCrossMapSummonGuestPlacement(*registers);
    }
    if (summon_reload_state_hook_installed && site.offset == StageWarpDescriptorFinalizeOffset) {
        ApplySummonReloadStateAtDescriptor(*registers);
    }
    if (deferred_summon_reload_hook_installed && site.offset == SosStatusUpdateOffset) {
        RefreshSeamlessLocalPlacement();
        ApplyPreMatchCrossMapGuestWarp();
        ApplyDeferredSummonReload();
        ResumePreMatchCrossMapResponder();
        TickPartyController();
    }
    if (healing_fountain_host_availability_hook_installed &&
        site.offset == HealingFountainAvailabilityOffset) {
        ApplyHealingFountainHostAvailability(*registers);
    }
    if (world_teardown_hook_installed && site.offset == WorldTeardownMatchingOffset) {
        EndSessionOnWorldTeardown();
    }
    if (ghost_death_revive_hook_installed && site.offset == GhostDeathReviveOffset) {
        ReviveGuestAfterGhostDeath();
    }
    if (site.kind == TraceKind::ActionFlags && !HasActionTransition(registers->rdi)) {
        return;
    }
    if (site.kind == TraceKind::GoodsParamLookup &&
        !IsTrackedGoods(static_cast<s32>(registers->rsi))) {
        return;
    }
    if (site.offset == CharacterWarpSetOrCopyFloorOffset) {
        const u64 return_address = ReadValue<u64>(registers->rsp, 0);
        if (return_address >= image_base) {
            const u64 return_offset = return_address - image_base;
            if (std::ranges::contains(BackgroundFloorCopyReturnOffsets, return_offset)) {
                return;
            }
        }
    }
    if ((site.kind == TraceKind::UseItemExecution || site.kind == TraceKind::GoodsActionSubmit) &&
        !IsTrackedGoods(GetUseItemExecutionGoods(site, *registers))) {
        return;
    }
    if (site.kind == TraceKind::SosValidityCode) {
        observed_sos_area.store(static_cast<s32>(registers->rax), std::memory_order_relaxed);
    }
    s32 responder_goods = -1;
    if (site.kind == TraceKind::ResponderBellAvailability) {
        if (site.offset == 0x0157F686) {
            responder_goods = static_cast<s32>(registers->r12);
            if (responder_goods != 205 && responder_goods != 225) {
                return;
            }
            responder_availability_frame = registers->rbp;
            responder_availability_goods = responder_goods;
        } else {
            if (responder_availability_frame != registers->rbp) {
                return;
            }
            responder_goods = responder_availability_goods;
            if (site.offset == 0x015800D1) {
                responder_availability_frame = 0;
                responder_availability_goods = -1;
            }
        }
    }

    const u64 hit = site_hits[tag].fetch_add(1, std::memory_order_relaxed) + 1;
    const u64 dense_capture_limit =
        site.kind == TraceKind::ActionFlags || site.kind == TraceKind::GoodsParamLookup ? 2048 : 64;
    if (hit > dense_capture_limit && hit % 300 != 0) {
        return;
    }

    const u64 sequence = event_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
    try {
        std::scoped_lock lock{capture_mutex};
        if (!capture_file) {
            return;
        }
        capture_file << "{\"type\":\"entry\",\"seq\":" << sequence << ",\"time_ms\":" << timestamp
                     << ",\"site\":\"" << site.name << "\",\"offset\":\"0x" << std::hex
                     << site.offset << std::dec << "\",\"hit\":" << hit << ",\"registers\":{";
        capture_file << "\"rax\":";
        WriteHex(capture_file, registers->rax);
        capture_file << ",\"rdi\":";
        WriteHex(capture_file, registers->rdi);
        capture_file << ",\"rsi\":";
        WriteHex(capture_file, registers->rsi);
        capture_file << ",\"rdx\":";
        WriteHex(capture_file, registers->rdx);
        capture_file << ",\"rcx\":";
        WriteHex(capture_file, registers->rcx);
        capture_file << ",\"r8\":";
        WriteHex(capture_file, registers->r8);
        capture_file << ",\"r9\":";
        WriteHex(capture_file, registers->r9);
        capture_file << ",\"rsp\":";
        WriteHex(capture_file, registers->rsp);
        capture_file << '}';

        switch (site.kind) {
        case TraceKind::SosCondition:
            WriteQwords(capture_file, "condition_qwords", registers->rdi, 8);
            break;
        case TraceKind::SosValidityDispatch: {
            capture_file << ",\"validity_dispatch\":{";
            capture_file << "\"player\":";
            WriteHex(capture_file, registers->rbx);
            const u64 vtable = ReadValue<u64>(registers->rbx, 0);
            const u64 function = ReadValue<u64>(vtable, 0x628);
            capture_file << ",\"vtable\":";
            WriteHex(capture_file, vtable);
            capture_file << ",\"function\":";
            WriteHex(capture_file, function);
            if (function >= image_base &&
                function - image_base < MemoryPatcher::g_eboot_image_size) {
                capture_file << ",\"function_offset\":\"0x" << std::hex << function - image_base
                             << std::dec << '\"';
            }
            capture_file << '}';
            break;
        }
        case TraceKind::SosValidityCode:
            capture_file << ",\"validity_code\":{";
            capture_file << "\"previous\":" << static_cast<s64>(static_cast<s32>(registers->r15));
            capture_file << ",\"current\":" << static_cast<s64>(static_cast<s32>(registers->rax));
            capture_file << ",\"object\":";
            WriteHex(capture_file, registers->r14);
            capture_file << ",\"source\":";
            WriteHex(capture_file, registers->rbx);
            capture_file << '}';
            WriteQwords(capture_file, "condition_qwords", registers->r14, 6);
            break;
        case TraceKind::SosEvent: {
            const u64 state = GetGlobalState();
            capture_file << ",\"sos_event\":{";
            capture_file << "\"global_state\":";
            WriteHex(capture_file, state);
            if (state >= 0x10000) {
                capture_file << ",\"warp_1520\":";
                WriteHex(capture_file, ReadValue<u8>(state, 0x1520));
                capture_file << ",\"menu_1544\":";
                WriteHex(capture_file, ReadValue<u8>(state, 0x1544));
                capture_file << ",\"transition_1547\":";
                WriteHex(capture_file, ReadValue<u8>(state, 0x1547));
                capture_file << ",\"active_1578\":";
                WriteHex(capture_file, ReadValue<u8>(state, 0x1578));
            }
            capture_file << '}';
            WritePlacementState(capture_file, state);
            break;
        }
        case TraceKind::MatchingEvent:
            WriteQwords(capture_file, "event_qwords", registers->rdx, 6);
            break;
        case TraceKind::MatchingSignal:
            break;
        case TraceKind::MatchingLeaveDecision: {
            const u64 return_address = ReadValue<u64>(registers->rsp, 0);
            const u64 reload_state =
                ReadValue<u64>(image_base + SummonSessionRulesPointerOffset, 0);
            capture_file << ",\"matching_leave_decision\":{";
            const bool multi_play_site = site.offset == MultiPlayStageUidPolicyOffset ||
                                         site.offset == MultiPlayStopRequestOffset;
            const bool stop_task_site = site.offset == MatchingStopTaskEnqueueOffset;
            const u64 object = registers->rdi;
            capture_file << "\"object\":";
            WriteHex(capture_file, object);
            capture_file << ",\"return_address\":";
            WriteHex(capture_file, return_address);
            capture_file << ",\"caller_offset\":";
            WriteHex(capture_file, return_address >= image_base ? return_address - image_base : 0);
            capture_file << ",\"reload_state\":";
            WriteHex(capture_file, reload_state);
            capture_file << ",\"reload_phase\":"
                         << (reload_state >= 0x10000 ? ReadValue<s32>(reload_state, 0x84) : -1);
            capture_file << ",\"current_map\":";
            WriteHex(capture_file, GetCurrentPackedMap()); // raw: the game's own memory
            if (multi_play_site && object >= 0x10000 &&
                HasMemoryAccess(object, 0x288, MemoryProt::CpuRead)) {
                const u64 matching_controller = ReadValue<u64>(object, 0x18);
                capture_file << ",\"multi_play_state\":" << ReadValue<s32>(object, 0x124);
                capture_file << ",\"multi_play_aux_state\":" << ReadValue<s32>(object, 0x120);
                capture_file << ",\"matching_controller\":";
                WriteHex(capture_file, matching_controller);
                capture_file << ",\"stop_completion_task\":";
                WriteHex(capture_file, ReadValue<u64>(object, 0x280));

                if (site.offset == MultiPlayStageUidPolicyOffset) {
                    const u64 local_state_root =
                        ReadValue<u64>(image_base + CandidateLocalStatePointerOffset, 0);
                    const u64 local_state =
                        local_state_root >= 0x10000 &&
                                HasMemoryAccess(local_state_root, 0x68, MemoryProt::CpuRead)
                            ? ReadValue<u64>(local_state_root, 0x60)
                            : 0;
                    const u64 stage_manager =
                        registers->rbp >= 0x148 + 0x10000 &&
                                HasMemoryAccess(registers->rbp - 0x148, sizeof(u64),
                                                MemoryProt::CpuRead)
                            ? ReadValue<u64>(registers->rbp - 0x148, 0)
                            : 0;
                    capture_file << ",\"stage_uid\":";
                    WriteHex(capture_file, static_cast<u32>(registers->r13));
                    capture_file << ",\"cached_stage_uid\":";
                    WriteHex(capture_file, local_state >= 0x10000 &&
                                                   HasMemoryAccess(local_state + 0x3F8, sizeof(u32),
                                                                   MemoryProt::CpuRead)
                                               ? ReadValue<u32>(local_state, 0x3F8)
                                               : 0);
                    capture_file << ",\"stage_manager\":";
                    WriteHex(capture_file, stage_manager);
                }
            } else if (stop_task_site && object >= 0x10000 &&
                       HasMemoryAccess(object, 0x408, MemoryProt::CpuRead)) {
                capture_file << ",\"stop_reason\":";
                WriteHex(capture_file, static_cast<u32>(registers->rsi));
                capture_file << ",\"matching_state\":" << ReadValue<s32>(object, 0x118);
                capture_file << ",\"controller_operation\":" << ReadValue<s32>(object, 0x3C0);
                capture_file << ",\"room_id\":";
                WriteHex(capture_file, ReadValue<u64>(object, 0x400));
            } else if (object >= 0x10000 && HasMemoryAccess(object, 0x460, MemoryProt::CpuRead)) {
                capture_file << ",\"vtable\":";
                WriteHex(capture_file, ReadValue<u64>(object, 0));
                capture_file << ",\"matching_state\":" << ReadValue<s32>(object, 0x118);
                capture_file << ",\"room_id\":";
                WriteHex(capture_file, ReadValue<u64>(object, 0x400));
            }
            capture_file << ",\"frame_chain\":[";
            u64 frame = registers->rbp;
            for (size_t index = 0; index < 8 && frame >= 0x10000 &&
                                   HasMemoryAccess(frame, 2 * sizeof(u64), MemoryProt::CpuRead);
                 ++index) {
                if (index != 0) {
                    capture_file << ',';
                }
                const u64 frame_return = ReadValue<u64>(frame, sizeof(u64));
                WriteHex(capture_file, frame_return);
                const u64 next = ReadValue<u64>(frame, 0);
                if (next <= frame || next - frame > 0x100000) {
                    break;
                }
                frame = next;
            }
            capture_file << ']';
            capture_file << '}';
            if (site.offset == 0x00CC5390) {
                WriteQwords(capture_file, "matching_leave_object_400", object + 0x400, 12);
            }
            break;
        }
        case TraceKind::SummonRequest:
        case TraceKind::MultiplayerInsert:
            WriteObjectState(capture_file, registers->rdi);
            break;
        case TraceKind::SummonCandidate:
            WriteSummonCandidate(capture_file, site, *registers);
            break;
        case TraceKind::SummonSelection:
            WriteSummonSelection(capture_file, site, *registers);
            break;
        case TraceKind::SummonBuild:
            WriteSummonBuild(capture_file, site, *registers);
            break;
        case TraceKind::GetSosRequest:
            WriteGetSosState(capture_file, registers->rdi);
            break;
        case TraceKind::GetSosArea:
            capture_file << ",\"get_sos_area\":{";
            capture_file << "\"player\":";
            WriteHex(capture_file, registers->r13);
            capture_file << ",\"source\":";
            WriteHex(capture_file, registers->rdx);
            if (registers->rdx >= 0x10000) {
                capture_file << ",\"value\":"
                             << static_cast<s64>(ReadValue<s32>(registers->rdx, 0));
            }
            capture_file << '}';
            break;
        case TraceKind::GetSosInsert:
            capture_file << ",\"get_sos_insert\":{";
            capture_file << "\"event_code\":" << static_cast<s64>(static_cast<s32>(registers->rsi));
            capture_file << ",\"observed_sos_area\":"
                         << observed_sos_area.load(std::memory_order_relaxed);
            capture_file << '}';
            WriteGetSosState(capture_file, registers->rdi);
            break;
        case TraceKind::SosStatus:
            capture_file << ",\"sos_status\":{";
            capture_file << "\"object\":";
            WriteHex(capture_file, registers->rbx);
            capture_file << ",\"observed_sos_area\":"
                         << observed_sos_area.load(std::memory_order_relaxed);
            if (site.name == "SosStatus.NativeResult") {
                capture_file << ",\"primary_present\":" << ReadValue<s32>(registers->rbp - 0x9C, 0);
                capture_file << ",\"primary_value\":" << ReadValue<s32>(registers->rbp - 0x90, 0);
                capture_file << ",\"secondary_present\":"
                             << ReadValue<s32>(registers->rbp - 0x84, 0);
                capture_file << ",\"secondary_value\":" << ReadValue<s32>(registers->rbp - 0x78, 0);
                capture_file << ",\"event_bits\":"
                             << static_cast<u32>(ReadValue<u8>(registers->rbp - 0x6C, 0));
            }
            capture_file << '}';
            if (site.offset == SosStatusUpdateOffset) {
                capture_file << ",\"pre_match_guest_warp\":{";
                capture_file << "\"result\":\"" << pre_match_guest_warp.result << '"';
                capture_file << ",\"state\":";
                WriteHex(capture_file, pre_match_guest_warp.state);
                capture_file << ",\"current_map\":";
                WriteHex(capture_file, pre_match_guest_warp.current_map);
                capture_file << ",\"target_map\":";
                WriteHex(capture_file, pre_match_guest_warp.target_map);
                capture_file << ",\"selected_map\":";
                WriteHex(capture_file, pre_match_guest_warp.selected_map);
                capture_file << ",\"multi_play_state\":" << pre_match_guest_warp.multi_play_state;
                capture_file << ",\"placement_written\":"
                             << (pre_match_guest_warp.placement_written ? "true" : "false");
                capture_file << ",\"select_result\":"
                             << (pre_match_guest_warp.select_result ? "true" : "false");
                capture_file << ",\"responder_resume_armed\":"
                             << (pre_match_guest_warp.responder_resume_armed ? "true" : "false");
                capture_file << ",\"transition_result\":" << pre_match_guest_warp.transition_result;
                capture_file << '}';
                capture_file << ",\"responder_resume\":{";
                capture_file << "\"result\":\"" << responder_resume.result << '"';
                capture_file << ",\"target_map\":";
                WriteHex(capture_file, responder_resume.target_map);
                capture_file << ",\"current_map\":";
                WriteHex(capture_file, responder_resume.current_map);
                capture_file << ",\"target_area\":" << responder_resume.target_area;
                capture_file << ",\"current_area\":" << responder_resume.current_area;
                capture_file << ",\"state\":";
                WriteHex(capture_file, responder_resume.state);
                capture_file << ",\"multi_play\":";
                WriteHex(capture_file, responder_resume.multi_play);
                capture_file << ",\"multi_play_state\":" << responder_resume.multi_play_state;
                capture_file << ",\"matching_controller\":";
                WriteHex(capture_file, responder_resume.matching_controller);
                capture_file << ",\"player\":";
                WriteHex(capture_file, responder_resume.player);
                capture_file << ",\"ready_observations\":" << responder_resume.ready_observations;
                capture_file << ",\"attempts\":" << responder_resume.attempts;
                capture_file << ",\"effect_active\":"
                             << (responder_resume.effect_active ? "true" : "false");
                capture_file << ",\"native_result\":"
                             << (responder_resume.native_result ? "true" : "false") << '}';
                capture_file << ",\"deferred_summon_reload\":{";
                capture_file << "\"result\":\"" << deferred_summon_reload.result << '"';
                capture_file << ",\"target_map\":";
                WriteHex(capture_file, deferred_summon_reload.target_map);
                capture_file << ",\"current_map\":";
                WriteHex(capture_file, deferred_summon_reload.current_map);
                capture_file << ",\"summon_reload_started\":"
                             << (deferred_summon_reload.summon_reload_started ? "true" : "false");
                capture_file << '}';
            }
            break;
        case TraceKind::SosMessenger:
            WriteMessengerState(capture_file, registers->rdi);
            break;
        case TraceKind::ActionFlags:
            WriteActionFlags(capture_file, registers->rdi, registers->rsp);
            break;
        case TraceKind::UseItem:
            WriteUseItemState(capture_file, site, *registers);
            break;
        case TraceKind::UseItemExecution:
            WriteUseItemExecution(capture_file, site, *registers);
            break;
        case TraceKind::GoodsActionSubmit:
            WriteGoodsActionSubmit(capture_file, *registers);
            break;
        case TraceKind::GoodsParamLookup:
            WriteGoodsParamLookup(capture_file, *registers);
            break;
        case TraceKind::BellAvailability:
            WriteBellAvailability(capture_file, site, *registers);
            break;
        case TraceKind::ResponderBellAvailability:
            WriteResponderBellAvailability(capture_file, site, *registers, responder_goods);
            break;
        case TraceKind::NetworkAreaRegion:
            capture_file << ",\"network_area_region\":{";
            capture_file << "\"object\":";
            WriteHex(capture_file, registers->r14);
            capture_file << ",\"sos_area\":" << observed_sos_area.load(std::memory_order_relaxed);
            capture_file << ",\"previous_region\":"
                         << static_cast<s64>(ReadValue<s32>(registers->r14, 0x44));
            capture_file << ",\"selected_region\":"
                         << static_cast<s64>(static_cast<s32>(registers->r15));
            capture_file << '}';
            break;
        case TraceKind::CrossMapGuestHandoff:
            capture_file << ",\"cross_map_guest_handoff\":{";
            capture_file << "\"result\":\"" << guest_placement_handoff.result << '"';
            capture_file << ",\"object\":";
            WriteHex(capture_file, guest_placement_handoff.object);
            capture_file << ",\"state\":";
            WriteHex(capture_file, guest_placement_handoff.state);
            capture_file << ",\"current_map\":";
            WriteHex(capture_file, guest_placement_handoff.current_map);
            capture_file << ",\"received_map\":";
            WriteHex(capture_file, guest_placement_handoff.received_map);
            capture_file << ",\"selected_map\":";
            WriteHex(capture_file, guest_placement_handoff.selected_map);
            capture_file << ",\"transported_host\":"
                         << (guest_placement_handoff.transported_host ? "true" : "false");
            capture_file << ",\"placement_refreshed\":"
                         << (guest_placement_handoff.placement_refreshed ? "true" : "false");
            capture_file << ",\"select_result\":"
                         << (guest_placement_handoff.select_result ? "true" : "false");
            capture_file << ",\"summon_reload_armed\":"
                         << (guest_placement_handoff.summon_reload_armed ? "true" : "false");
            capture_file << ",\"transition_result\":" << guest_placement_handoff.transition_result;
            capture_file << '}';
            WritePlacementState(capture_file, guest_placement_handoff.state);
            break;
        case TraceKind::SosPlacement:
            WriteQwords(capture_file, "position_qwords", registers->rax + 0x14A0, 2);
            WriteQwords(capture_file, "orientation_qwords", registers->rax + 0x14B0, 2);
            capture_file << ",\"map_id\":";
            WriteHex(capture_file, static_cast<u32>(registers->rcx));
            break;
        case TraceKind::GlobalState:
            WritePlacementState(capture_file, registers->rax);
            break;
        case TraceKind::ReceivedPlacementConditional:
            WriteFloatVector(capture_file, "received_position", registers->rdi);
            WriteFloatVector(capture_file, "received_orientation", registers->rsi);
            WriteU32(capture_file, "matching_key", registers->rdx);
            WriteMapId(capture_file, "received_map_id", registers->rcx);
            WritePlacementState(capture_file, registers->r8);
            break;
        case TraceKind::ReceivedPlacementSet:
            WriteFloatVector(capture_file, "received_position", registers->rdi);
            WriteFloatVector(capture_file, "received_orientation", registers->rsi);
            WriteU32(capture_file, "matching_key", registers->rdx);
            WriteMapId(capture_file, "received_map_id", registers->rcx);
            WritePlacementState(capture_file, registers->rax);
            break;
        case TraceKind::PlacementSelect:
            capture_file << (site.offset == 0x01332CD0 ? ",\"selected_map_id\":"
                                                       : ",\"default_map_id\":");
            WriteHex(capture_file, static_cast<u32>(registers->rax));
            WritePlacementState(capture_file, registers->rcx);
            break;
        case TraceKind::WarpCommand:
            WriteQwords(capture_file, "parameter_qwords", registers->rsi, 12);
            break;
        case TraceKind::CharacterWarp:
            WriteCharacterWarp(capture_file, site, *registers);
            break;
        case TraceKind::WarpResolved:
            WriteFloatVector(capture_file, "position", registers->rdi);
            WriteFloatVector(capture_file, "orientation", registers->rsi);
            WriteFloatVector(capture_file, "camera_orientation", registers->rdx);
            WriteMapId(capture_file, "map_id", registers->rcx);
            break;
        case TraceKind::WarpLocalApply:
            WriteMapId(capture_file, "map_id", registers->rsi);
            WriteFloatVector(capture_file, "position", registers->rdx);
            WriteFloatVector(capture_file, "orientation", registers->rcx);
            break;
        case TraceKind::StageWarp:
            WriteStageWarp(capture_file, site, *registers);
            break;
        case TraceKind::RespawnPointWarp:
            capture_file << ",\"respawn_point_warp\":{";
            capture_file << "\"warp_info_id\":" << static_cast<s32>(registers->rdi) << '}';
            WriteStageWarpState(capture_file, GetGlobalState());
            break;
        case TraceKind::StageWarpDescriptor:
            WriteStageWarpDescriptor(capture_file, *registers);
            if (site.offset == StageWarpDescriptorFinalizeOffset) {
                capture_file << ",\"summon_reload_state\":{";
                capture_file << "\"result\":\"" << summon_reload_state.result << '"';
                capture_file << ",\"target_map\":";
                WriteHex(capture_file, summon_reload_state.target_map);
                capture_file << ",\"descriptor_map\":";
                WriteHex(capture_file, summon_reload_state.descriptor_map);
                capture_file << ",\"reload_state\":";
                WriteHex(capture_file, summon_reload_state.reload_state);
                capture_file << ",\"state_before\":" << summon_reload_state.state_before;
                capture_file << ",\"state_after\":" << summon_reload_state.state_after;
                capture_file << ",\"summon_reload_started\":"
                             << (summon_reload_state.summon_reload_started ? "true" : "false");
                capture_file << ",\"state_setter_used\":"
                             << (summon_reload_state.state_setter_used ? "true" : "false");
                capture_file << ",\"placement_refreshed\":"
                             << (summon_reload_state.placement_refreshed ? "true" : "false") << '}';
            }
            break;
        case TraceKind::StageWarpAcknowledge:
            WriteStageWarpAcknowledge(capture_file, *registers);
            break;
        case TraceKind::StageWarpRespawnResolve:
            WriteStageWarpTask(capture_file, registers->r13, false);
            break;
        case TraceKind::StageWarpTransformResolve:
            WriteRespawnTransformResolve(capture_file, *registers);
            break;
        case TraceKind::StageWarpPlacement:
            WriteStageWarpTask(capture_file, registers->rdi, true);
            break;
        case TraceKind::SummonPoint:
            WriteQwords(capture_file, "query_qwords", registers->rsi, 8);
            break;
        case TraceKind::MapReload:
            WritePlacementState(capture_file, GetGlobalState());
            break;
        case TraceKind::HealingFountainRegistration:
            WriteHealingFountainRegistration(capture_file, *registers);
            break;
        case TraceKind::HealingFountainAvailability:
            WriteHealingFountainAvailability(capture_file, *registers);
            break;
        case TraceKind::ChairRespawnNotification:
            WriteChairRespawnNotification(capture_file, *registers);
            break;
        case TraceKind::WorldStateValidation:
            WriteWorldStateValidation(capture_file, site, *registers);
            break;
        }
        capture_file << "}\n";
        capture_file.flush();

        if (hit <= 8 || hit % 300 == 0) {
            LOG_INFO(Debug,
                     "Bloodborne RE entry {} hit={} rdi={:#x} rsi={:#x} rdx={:#x} capture={}",
                     site.name, hit, registers->rdi, registers->rsi, registers->rdx,
                     Common::FS::PathToUTF8String(capture_path));
        }
    } catch (...) {
        // An observer must never unwind into guest code.
    }
}

} // namespace Coop

namespace {

// Autoplay (SHADPS4_BLOODBORNE_AUTOPLAY=<script>, see ParseAutoplayScript): tests with no one at
// the pad. The pad reader (AutoplayPad) presses Cross on the title screen until the player is in
// the world; the party tick then runs the script's commands one after the other, logging each
// ("Bloodborne autoplay: step 3/9 travel 2102962 ok"), and "Bloodborne autoplay: done (ok)" or
// "done (failed)" at the end, which the rig's test driver waits for.
struct AutoplayRun {
    enum class Phase { Off, Title, World, Done };
    Phase phase = Phase::Off;
    std::vector<AutoplayCommand> script;
    size_t next = 0;
    std::chrono::steady_clock::time_point started{};
    std::chrono::steady_clock::time_point step_started{};
    bool step_begun = false;
    std::optional<std::chrono::steady_clock::time_point> hold_until;
    u32 travel_map = 0;
    std::optional<double> travel_fixed_at; // seconds into the step it was moved to the lamp
    u32 attack_handle = 0;
    std::optional<u32> attack_placed; // the attack cycle the player was last put in place for
    s32 noted_max_hp = 0;             // note-max-hp: the player's max HP then
    u32 pickup_handle = 0;            // pickup: the map item it went for
    s32 hit_from_hp = 0;              // hit: the target's HP when it began
};
std::mutex autoplay_mutex;
AutoplayRun autoplay_run;
std::atomic<u32> autoplay_buttons{0};
std::atomic<bool> autoplay_godmode{false};
std::atomic<u32> autoplay_sticks{0x80808080}; // lx, ly, rx, ry
std::once_flag autoplay_loaded;

void LoadAutoplay() {
    const char* path = std::getenv("SHADPS4_BLOODBORNE_AUTOPLAY");
    if (path == nullptr || *path == '\0') {
        return;
    }
    std::ifstream file(path);
    const std::string text{std::istreambuf_iterator<char>{file}, {}};
    std::scoped_lock lock{autoplay_mutex};
    autoplay_run.script = ParseAutoplayScript(text);
    autoplay_run.phase = AutoplayRun::Phase::Title;
    autoplay_run.started = std::chrono::steady_clock::now();
    LOG_INFO(Debug, "Bloodborne autoplay: {} steps from {}", autoplay_run.script.size(), path);
}

// The rig's checks and reports read the game's own map (GetCurrentPackedMap), not the place
// tracker: they test the game, the tracker included; only comparisons with a board's maps use
// the tracker's, as the boards do.
bool AutoplayInWorld() {
    const u64 global = GetGlobalState();
    return GetLocalPlayer() >= 0x10000 && IsUsablePackedMap(GetCurrentPackedMap()) &&
           global >= 0x10000 && HasMemoryAccess(global, 0x09, MemoryProt::CpuRead) &&
           ReadValue<u8>(global, 0x08) == 0 && LocalPlayerPosition().has_value();
}

size_t AutoplaySessionMembers() {
    const u64 session = ReadValue<u64>(image_base + SessionManagerPointerOffset, 0);
    if (session < 0x10000 || !HasMemoryAccess(session, 0x128, MemoryProt::CpuRead)) {
        return 1;
    }
    const u64 begin = ReadValue<u64>(session, 0xF8);
    const u64 end = ReadValue<u64>(session, 0x100);
    return begin >= 0x10000 && end > begin && end - begin <= 0x20 * 8 ? (end - begin) / 0x20 : 1;
}

size_t AutoplayMembersOnMyMap() {
    const u32 map = PlayingMap();
    std::scoped_lock lock{map_board_mutex};
    if (std::chrono::steady_clock::now() - map_board_time > std::chrono::seconds{3}) {
        return 0;
    }
    return static_cast<size_t>(
        std::ranges::count_if(map_board, [&](const MemberMap& entry) { return entry.map == map; }));
}

struct AutoplayCharacter {
    u64 character = 0;
    u64 data = 0;
    float distance = 0.0f;
};

// The character a command names: by handle, or the nearest living one within 30 m (0).
std::atomic<u32> autoplay_marked{0};

std::optional<AutoplayCharacter> FindAutoplayCharacter(u32 handle) {
    if (handle == AutoplayMarked) {
        handle = autoplay_marked.load();
        if (handle == 0) {
            return std::nullopt;
        }
    }
    if (handle == AutoplaySelf) {
        const u64 player = GetLocalPlayer();
        const u64 modules = player >= 0x10000 ? ReadValue<u64>(player, 0x3B0) : 0;
        const u64 data = modules >= 0x10000 ? ReadValue<u64>(modules, 0x20) : 0;
        return data >= 0x10000 ? std::optional{AutoplayCharacter{player, data, 0.0f}}
                               : std::nullopt;
    }
    const u64 world = ReadValue<u64>(image_base + CandidateLocalStatePointerOffset, 0);
    const auto me = LocalPlayerPosition();
    if (world < 0x10000 || !me) {
        return std::nullopt;
    }
    std::optional<AutoplayCharacter> found;
    ForEachWorldCharacter(world, [&](u64 character) {
        if (character == GetLocalPlayer()) {
            return;
        }
        const u64 modules = ReadValue<u64>(character, 0x3B0);
        const u64 data = modules >= 0x10000 ? ReadValue<u64>(modules, 0x20) : 0;
        const u64 physics = modules >= 0x10000 ? ReadValue<u64>(modules, 0x68) : 0;
        if (data < 0x10000 || physics < 0x10000) {
            return;
        }
        const float dx = ReadValue<float>(physics, 0x1E0) - (*me)[0];
        const float dy = ReadValue<float>(physics, 0x1E4) - (*me)[1];
        const float dz = ReadValue<float>(physics, 0x1E8) - (*me)[2];
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (handle != 0) {
            if (ReadValue<u32>(character, 0x08) == handle) {
                found = AutoplayCharacter{character, data, distance};
            }
            return;
        }
        if (ReadValue<s32>(data, 0xF8) > 0 && distance < 30.0f &&
            (!found || distance < found->distance)) {
            found = AutoplayCharacter{character, data, distance};
        }
    });
    return found;
}

void LogAutoplaySnapshot() {
    const auto at = LocalPlayerPosition();
    std::string enemies;
    const u64 world = ReadValue<u64>(image_base + CandidateLocalStatePointerOffset, 0);
    std::vector<std::pair<float, std::string>> close_by;
    if (world >= 0x10000 && at) {
        ForEachWorldCharacter(world, [&](u64 character) {
            if (character == GetLocalPlayer()) {
                return;
            }
            const u64 modules = ReadValue<u64>(character, 0x3B0);
            const u64 data = modules >= 0x10000 ? ReadValue<u64>(modules, 0x20) : 0;
            const u64 physics = modules >= 0x10000 ? ReadValue<u64>(modules, 0x68) : 0;
            if (data < 0x10000 || physics < 0x10000) {
                return;
            }
            const float dx = ReadValue<float>(physics, 0x1E0) - (*at)[0];
            const float dy = ReadValue<float>(physics, 0x1E4) - (*at)[1];
            const float dz = ReadValue<float>(physics, 0x1E8) - (*at)[2];
            const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (distance < 30.0f) {
                close_by.emplace_back(
                    distance,
                    fmt::format("{:#x} hp {}/{} dead {:#x} at {:.1f} m ({:.1f}, {:.1f}, {:.1f})",
                                ReadValue<u32>(character, 0x08), ReadValue<s32>(data, 0xF8),
                                ReadValue<s32>(data, 0xFC), ReadValue<u8>(character, 0x1E3),
                                distance, ReadValue<float>(physics, 0x1E0),
                                ReadValue<float>(physics, 0x1E4),
                                ReadValue<float>(physics, 0x1E8)));
            }
        });
    }
    std::ranges::sort(close_by);
    for (size_t index = 0; index < close_by.size() && index < 6; ++index) {
        enemies += " [" + close_by[index].second + "]";
    }
    const auto self = FindAutoplayCharacter(AutoplaySelf);
    // Raw, as all of the rig's checks (see AutoplayInWorld).
    LOG_INFO(Debug,
             "Bloodborne autoplay snapshot: map {:#x} at ({:.1f}, {:.1f}, {:.1f}) heading {:.2f} "
             "hp {}/{} session {} members on map {} in party session {} nearby:{}",
             GetCurrentPackedMap(), at ? (*at)[0] : 0.0f, at ? (*at)[1] : 0.0f,
             at ? (*at)[2] : 0.0f, LocalPlayerHeading().value_or(0.0f),
             self ? ReadValue<s32>(self->data, 0xF8) : 0,
             self ? ReadValue<s32>(self->data, 0xFC) : 0, AutoplaySessionMembers(),
             AutoplayMembersOnMyMap(), InPartySession(), enemies.empty() ? " none" : enemies);
}

// census [<meters>]: every character that close (default 60 m), nearest first, with its HP, death
// flags (+0x1E3), story hide (+0x28) and explicit disable ([+0x18]+0x20 bit 0): which world
// shows or killed what (the clinic's beast, Iosefka).
// A character's backread level ([+0x18] +0x10 now, +0x14 wanted; 4 full, 0 none: the state
// machine 0x018BD320) and its draw flags ([+0x58] +0x114, bit 0x40 cleared by the enable switch),
// for the census: which copy of a character is drawn in one world and not the other.
s32 BackreadLevel(u64 character, u64 offset) {
    const u64 control = ReadValue<u64>(character, 0x18);
    return control >= 0x10000 ? ReadValue<s32>(control, offset) : -1;
}

u32 DrawFlags(u64 character) {
    const u64 model = ReadValue<u64>(character, 0x58);
    return model >= 0x10000 && HasMemoryAccess(model, 0x116, MemoryProt::CpuRead)
               ? ReadValue<u16>(model, 0x114)
               : 0xFFFFFFFF;
}

void LogAutoplayCensus(float meters) {
    const auto at = LocalPlayerPosition();
    const u64 world = ReadValue<u64>(image_base + CandidateLocalStatePointerOffset, 0);
    if (world < 0x10000 || !at) {
        LOG_INFO(Debug, "Bloodborne autoplay census: no world");
        return;
    }
    std::vector<std::pair<float, std::string>> close_by;
    ForEachWorldCharacter(world, [&](u64 character) {
        if (character == GetLocalPlayer()) {
            return;
        }
        const u64 modules = ReadValue<u64>(character, 0x3B0);
        const u64 data = modules >= 0x10000 ? ReadValue<u64>(modules, 0x20) : 0;
        const u64 physics = modules >= 0x10000 ? ReadValue<u64>(modules, 0x68) : 0;
        if (data < 0x10000 || physics < 0x10000) {
            return;
        }
        const float x = ReadValue<float>(physics, 0x1E0);
        const float y = ReadValue<float>(physics, 0x1E4);
        const float z = ReadValue<float>(physics, 0x1E8);
        const float dx = x - (*at)[0];
        const float dy = y - (*at)[1];
        const float dz = z - (*at)[2];
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (distance < meters) {
            close_by.emplace_back(
                distance,
                fmt::format("{:#x} hp {}/{} dead {:#x} hidden {} disabled {} at {:.1f} m "
                            "({:.1f}, {:.1f}, {:.1f}) backread {}/{} draw {:#x}",
                            ReadValue<u32>(character, 0x08), ReadValue<s32>(data, 0xF8),
                            ReadValue<s32>(data, 0xFC), ReadValue<u8>(character, 0x1E3),
                            ReadValue<u8>(character, 0x28), IsCharacterDisabled(character) ? 1 : 0,
                            distance, x, y, z, BackreadLevel(character, 0x10),
                            BackreadLevel(character, 0x14), DrawFlags(character)));
        }
    });
    std::ranges::sort(close_by);
    // Raw, as all of the rig's checks (see AutoplayInWorld).
    LOG_INFO(Debug,
             "Bloodborne autoplay census: map {:#x}, {} characters within {:.0f} m of "
             "({:.1f}, {:.1f}, {:.1f})",
             GetCurrentPackedMap(), close_by.size(), meters, (*at)[0], (*at)[1], (*at)[2]);
    for (const auto& [distance, line] : close_by) {
        LOG_INFO(Debug, "Bloodborne autoplay census: {}", line);
    }
}

std::string AutoplayText(const AutoplayCommand& command) {
    std::string text = command.verb;
    for (const std::string& arg : command.args) {
        text += ' ' + arg;
    }
    return text;
}

// Runs one script command for this tick: true once it is done, `failure` set if it failed.
bool RunAutoplayStep(const AutoplayCommand& command, bool begun, double elapsed,
                     std::string& failure) {
    const auto arg = [&](size_t index) -> std::string_view {
        return index < command.args.size() ? std::string_view{command.args[index]}
                                           : std::string_view{};
    };
    const auto number = [&](size_t index, double fallback) {
        const std::string_view text = arg(index);
        double value = fallback;
        if (!text.empty()) {
            std::from_chars(text.data(), text.data() + text.size(), value);
        }
        return value;
    };
    const auto waited_out = [&](double limit) {
        if (elapsed > limit) {
            failure = fmt::format("timed out after {:.0f} s", elapsed);
        }
        return false;
    };
    const std::string& verb = command.verb;
    if (verb == "wait") {
        return elapsed >= number(0, 1.0);
    }
    if (verb == "wait-session") {
        return AutoplaySessionMembers() >= static_cast<size_t>(number(0, 2)) ||
               waited_out(number(1, 180));
    }
    // wait-solo [<timeout>]: until the session is gone (a member quit or crashed).
    if (verb == "wait-solo") {
        return AutoplaySessionMembers() <= 1 || waited_out(number(0, 180));
    }
    if (verb == "wait-members") {
        return AutoplayMembersOnMyMap() >= static_cast<size_t>(number(0, 2)) ||
               waited_out(number(1, 180));
    }
    if (verb == "wait-map") {
        const auto map = ParseAutoplayTarget(arg(0));
        // Raw, as all of the rig's checks (see AutoplayInWorld).
        return (map && AutoplayInWorld() && GetCurrentPackedMap() == *map) ||
               waited_out(number(1, 180));
    }
    // wait-vital <up|down|dead> [<timeout>]: until this player's party death state is that.
    // wait-member-vital <vital> [<timeout>]: until another member's is, on the vitals board.
    if (verb == "wait-vital" || verb == "wait-member-vital") {
        const auto vital = ParseVital(arg(0));
        if (!vital) {
            failure = "no such vital";
            return false;
        }
        if (verb == "wait-vital") {
            return party_death_run.downed.Current() == *vital || waited_out(number(1, 60));
        }
        const VitalsView view = ReadVitalsBoard();
        return (view.Fresh() && std::ranges::any_of(view.board.members,
                                                    [&](const MemberVitals& member) {
                                                        return member.member != view.member &&
                                                               member.vital == *vital;
                                                    })) ||
               waited_out(number(1, 60));
    }
    // ensure-map <map> <warp point>: travel there unless already on that map (a test's start).
    if (verb == "ensure-map" && !begun) {
        const auto map = ParseAutoplayTarget(arg(0));
        if (!map) {
            failure = "no such map";
            return false;
        }
        // Already there, unless falling through the void (a save made after a bad warp).
        const auto at = LocalPlayerPosition();
        // Raw, as all of the rig's checks (see AutoplayInWorld).
        if (GetCurrentPackedMap() == *map && at && (*at)[1] > -500.0f) {
            return true;
        }
    }
    if (verb == "travel" || verb == "ensure-map") {
        const bool ensure = verb == "ensure-map";
        const s32 point = static_cast<s32>(number(ensure ? 1 : 0, HuntersDreamWarpPoint));
        if (!begun) {
            const u64 global = GetGlobalState();
            if (global < 0x10000) {
                failure = "no game state";
                return false;
            }
            WriteValue(global, 0x1528, point);
            WriteValue(global, 0x1520, u8{0});
            using WarpNextStageBonfire = void PS4_SYSV_ABI (*)(u64 unused, s32 warp_point);
            reinterpret_cast<WarpNextStageBonfire>(image_base + WarpNextStageBonfireOffset)(0,
                                                                                            point);
            // As a guest's own lamp trip in a session (TickGuestLampTrip) does; out of one it is
            // the game's own travel, as a player's at a lamp.
            if (IsPartyGuestRole() && InPartySession()) {
                guest_trip_in_flight = true;
            }
            std::scoped_lock lock{autoplay_mutex};
            autoplay_run.travel_map = PackedMapOfWarpPoint(point);
            autoplay_run.travel_fixed_at.reset();
            return false;
        }
        // Raw, as all of the rig's checks (see AutoplayInWorld).
        const bool arrived = elapsed > 5.0 && AutoplayInWorld() && !PartyTripActive() &&
                             GetCurrentPackedMap() == autoplay_run.travel_map;
        if (arrived && !autoplay_run.travel_fixed_at) {
            // The warp alone leaves the player at (0, 0, 0), above the void: to the lamp, as a
            // guest's trip does (guest_trip_fixup_at). The load put the save's last lamp back as
            // the respawn point, so the lamp travelled to goes in again first.
            if (const u64 global = GetGlobalState(); global >= 0x10000) {
                WriteValue(global, 0x1528, point);
            }
            MoveToRespawnPoint(true);
            std::scoped_lock lock{autoplay_mutex};
            autoplay_run.travel_fixed_at = elapsed;
            return false;
        }
        if (arrived && elapsed > *autoplay_run.travel_fixed_at + 2.0) {
            const auto at = LocalPlayerPosition();
            if (!at || (*at)[1] < -500.0f) {
                failure = "arrived in the void";
                return false;
            }
            return true;
        }
        return waited_out(number(ensure ? 2 : 1, 180));
    }
    if (verb == "hurt" || verb == "kill") {
        const auto target = ParseAutoplayTarget(arg(0));
        const auto found = target ? FindAutoplayCharacter(*target) : std::nullopt;
        if (!found || !HasMemoryAccess(found->data, 0xFC, MemoryProt::CpuWrite)) {
            failure = "no such character";
            return false;
        }
        const s32 hp = ReadValue<s32>(found->data, 0xF8);
        const s32 target_hp = verb == "kill" ? 0 : std::min(hp, static_cast<s32>(number(1, 1)));
        // A kill goes through the game's own (writing 0 HP leaves a standing corpse); checked
        // against its prologue first.
        static constexpr std::array<u8, 6> KillPrologue{0x55, 0x48, 0x89, 0xE5, 0x41, 0x57};
        if (verb == "kill" && IsCharacter(found->character, "autoplay kill") &&
            std::ranges::equal(
                std::span<const u8>{reinterpret_cast<const u8*>(image_base + KillCharacterOffset),
                                    KillPrologue.size()},
                KillPrologue)) {
            using Kill = void PS4_SYSV_ABI (*)(u64 character);
            reinterpret_cast<Kill>(image_base + KillCharacterOffset)(found->character);
        } else {
            WriteValue(found->data, 0xF8, target_hp);
            // It stands for this player's hits (see own_damage), which the HP board shares.
            std::scoped_lock lock{own_hits_mutex};
            own_damage.Hit(ReadValue<u32>(found->character, 0x08), true, hp - target_hp);
        }
        LOG_INFO(Debug, "Bloodborne autoplay: {} {:#x} hp {} -> {} at {:.1f} m", verb,
                 ReadValue<u32>(found->character, 0x08), hp, target_hp, found->distance);
        return true;
    }
    // mark <target>: the character later commands call "marked" (the nearest one now, say).
    if (verb == "mark") {
        const auto target = ParseAutoplayTarget(arg(0));
        const auto found = target ? FindAutoplayCharacter(*target) : std::nullopt;
        if (!found) {
            failure = "no such character";
            return false;
        }
        autoplay_marked.store(ReadValue<u32>(found->character, 0x08));
        LOG_INFO(Debug, "Bloodborne autoplay: marked {:#x} hp {} at {:.1f} m",
                 autoplay_marked.load(), ReadValue<s32>(found->data, 0xF8), found->distance);
        return true;
    }
    // approach <target> [<dx> [<dz> [<heading>]]]: stand there, beside it, turned to <heading>.
    if (verb == "approach") {
        const auto target = ParseAutoplayTarget(arg(0));
        const auto found = target ? FindAutoplayCharacter(*target) : std::nullopt;
        const u64 modules = found ? ReadValue<u64>(found->character, 0x3B0) : 0;
        const u64 physics = modules >= 0x10000 ? ReadValue<u64>(modules, 0x68) : 0;
        if (physics < 0x10000) {
            failure = "no such character";
            return false;
        }
        TeleportLocalPlayer({ReadValue<float>(physics, 0x1E0) + static_cast<float>(number(1, 1.5)),
                             ReadValue<float>(physics, 0x1E4) + 0.2f,
                             ReadValue<float>(physics, 0x1E8) + static_cast<float>(number(2, 0))},
                            static_cast<float>(number(3, 0)));
        return true;
    }
    // attack <target> [<timeout>]: one HP left, then light attacks from each side in turn until
    // it dies (AttackStanceFor): a real hit, so the kill reward and the death run as in play.
    if (verb == "attack") {
        if (!begun) {
            const auto target = ParseAutoplayTarget(arg(0));
            const auto found = target ? FindAutoplayCharacter(*target) : std::nullopt;
            if (!found || !HasMemoryAccess(found->data, 0xFC, MemoryProt::CpuWrite)) {
                failure = "no such character";
                return false;
            }
            const s32 hp = ReadValue<s32>(found->data, 0xF8);
            WriteValue(found->data, 0xF8, std::min(hp, s32{1}));
            autoplay_run.attack_handle = ReadValue<u32>(found->character, 0x08);
            autoplay_run.attack_placed.reset();
            LOG_INFO(Debug, "Bloodborne autoplay: attacking {:#x} (hp {} -> 1) at {:.1f} m",
                     autoplay_run.attack_handle, hp, found->distance);
        }
        // By its handle from here on: "nearest" would be the next enemy once this one is dead.
        const auto found = FindAutoplayCharacter(autoplay_run.attack_handle);
        const s32 hp = found ? ReadValue<s32>(found->data, 0xF8) : 0;
        if (hp <= 0) {
            autoplay_buttons.store(0);
            LOG_INFO(Debug, "Bloodborne autoplay: {:#x} {} in attack cycle {}",
                     autoplay_run.attack_handle, found ? "died" : "is gone",
                     autoplay_run.attack_placed.value_or(0));
            return true;
        }
        const u32 cycle = AttackCycleAt(elapsed);
        const u64 modules = ReadValue<u64>(found->character, 0x3B0);
        const u64 physics = modules >= 0x10000 ? ReadValue<u64>(modules, 0x68) : 0;
        if (autoplay_run.attack_placed != cycle && physics >= 0x10000) {
            if (autoplay_run.attack_placed) {
                LOG_INFO(Debug, "Bloodborne autoplay: attack cycle {} missed (heading {:.2f})",
                         *autoplay_run.attack_placed, LocalPlayerHeading().value_or(0.0f));
            }
            const float target_x = ReadValue<float>(physics, 0x1E0);
            const float target_z = ReadValue<float>(physics, 0x1E8);
            const AttackStance stance = AttackStanceFor(target_x, target_z, cycle);
            TeleportLocalPlayer({stance.x, ReadValue<float>(physics, 0x1E4) + 0.2f, stance.z},
                                HeadingTowards(stance.x, stance.z, target_x, target_z));
            autoplay_run.attack_placed = cycle;
        }
        autoplay_buttons.store(AttackHoldsR1(elapsed) ? AutoplayButton("r1") : 0);
        if (elapsed > number(1, 40)) {
            failure = fmt::format("still alive after {:.0f} s: hp {}", elapsed, hp);
        }
        return false;
    }
    // hit <target> [<timeout>]: light attacks from each side in turn (as attack, without taking
    // its HP down first) until one lands: a real hit that the game sends to the other players.
    if (verb == "hit") {
        if (!begun) {
            const auto target = ParseAutoplayTarget(arg(0));
            const auto found = target ? FindAutoplayCharacter(*target) : std::nullopt;
            if (!found) {
                failure = "no such character";
                return false;
            }
            autoplay_run.attack_handle = ReadValue<u32>(found->character, 0x08);
            autoplay_run.attack_placed.reset();
            autoplay_run.hit_from_hp = ReadValue<s32>(found->data, 0xF8);
            LOG_INFO(Debug, "Bloodborne autoplay: hitting {:#x} (hp {}) at {:.1f} m",
                     autoplay_run.attack_handle, autoplay_run.hit_from_hp, found->distance);
        }
        const auto found = FindAutoplayCharacter(autoplay_run.attack_handle);
        if (!found) {
            autoplay_buttons.store(0);
            failure = "the target is gone";
            return false;
        }
        const s32 hp = ReadValue<s32>(found->data, 0xF8);
        if (hp < autoplay_run.hit_from_hp) {
            autoplay_buttons.store(0);
            LOG_INFO(Debug, "Bloodborne autoplay: hit {:#x}: hp {} -> {} in attack cycle {}",
                     autoplay_run.attack_handle, autoplay_run.hit_from_hp, hp,
                     autoplay_run.attack_placed.value_or(0));
            return true;
        }
        const u32 cycle = AttackCycleAt(elapsed);
        const u64 modules = ReadValue<u64>(found->character, 0x3B0);
        const u64 physics = modules >= 0x10000 ? ReadValue<u64>(modules, 0x68) : 0;
        if (autoplay_run.attack_placed != cycle && physics >= 0x10000) {
            const float target_x = ReadValue<float>(physics, 0x1E0);
            const float target_z = ReadValue<float>(physics, 0x1E8);
            const AttackStance stance = AttackStanceFor(target_x, target_z, cycle);
            TeleportLocalPlayer({stance.x, ReadValue<float>(physics, 0x1E4) + 0.2f, stance.z},
                                HeadingTowards(stance.x, stance.z, target_x, target_z));
            autoplay_run.attack_placed = cycle;
        }
        autoplay_buttons.store(AttackHoldsR1(elapsed) ? AutoplayButton("r1") : 0);
        if (elapsed > number(1, 30)) {
            autoplay_buttons.store(0);
            failure = fmt::format("no hit landed in {:.0f} s: hp {}", elapsed, hp);
        }
        return false;
    }
    if (verb == "teleport") {
        TeleportLocalPlayer({static_cast<float>(number(0, 0)), static_cast<float>(number(1, 0)),
                             static_cast<float>(number(2, 0))},
                            static_cast<float>(number(3, 0)));
        return true;
    }
    // pickup [<meters>] [<timeout>]: onto the nearest usable map item within reach (30 m), then
    // X for 0.3 s of every 1.5 s until the item leaves the usable list: a real pickup through the
    // item button (the host could not pick up items in the Forbidden Woods, 2026-10-09).
    if (verb == "pickup") {
        const auto position = MapItemSearchPlayerPosition();
        const auto items =
            position ? UsableMapItemsNear(*position) : std::vector<std::pair<float, MapItemSpot>>{};
        if (!begun) {
            if (items.empty() || items.front().first > number(0, 30)) {
                failure = "no usable map item near";
                return false;
            }
            const MapItemSpot& spot = items.front().second;
            autoplay_run.pickup_handle = spot.handle;
            TeleportLocalPlayer({spot.at[0], spot.at[1] + 0.2f, spot.at[2]});
            LOG_INFO(Debug, "Bloodborne autoplay: picking up map item {:#x} {:.1f} m away",
                     spot.handle, items.front().first);
        }
        const bool there = std::ranges::any_of(items, [](const auto& item) {
            return item.second.handle == autoplay_run.pickup_handle;
        });
        if (!there) {
            autoplay_buttons.store(0);
            LOG_INFO(Debug, "Bloodborne autoplay: map item {:#x} picked up after {:.1f} s",
                     autoplay_run.pickup_handle, elapsed);
            return true;
        }
        autoplay_buttons.store(
            elapsed > 1.0 && std::fmod(elapsed, 1.5) < 0.3 ? AutoplayButton("cross") : 0);
        if (elapsed > number(1, 20)) {
            autoplay_buttons.store(0);
            failure = fmt::format(
                "map item {:#x} still there after {:.0f} s (item button found {} not busy {} "
                "pressed {})",
                autoplay_run.pickup_handle, elapsed, item_button_counts[1].load(),
                item_button_counts[2].load(), item_button_counts[3].load());
        }
        return false;
    }
    // rise <metres>: straight up from where the player stands, to fall back down.
    if (verb == "rise") {
        const auto at = LocalPlayerPosition();
        if (!at) {
            failure = "not in the world";
            return false;
        }
        TeleportLocalPlayer({(*at)[0], (*at)[1] + static_cast<float>(number(0, 8)), (*at)[2]},
                            LocalPlayerHeading().value_or(0.0f));
        return true;
    }
    // approach-downed: a metre from the nearest downed party member on this map, where the
    // vitals board has it.
    if (verb == "approach-downed") {
        const VitalsView view = ReadVitalsBoard();
        const auto me = LocalPlayerPosition();
        const auto distance = [&](const LampPosition& at) {
            return std::hypot(at[0] - (*me)[0], at[1] - (*me)[1], at[2] - (*me)[2]);
        };
        std::optional<LampPosition> nearest;
        for (const MemberVitals& member : view.board.members) {
            if (me && view.Fresh() && member.member != view.member && member.vital == Vital::Down &&
                member.map == PlayingMap() &&
                (!nearest || distance(member.at) < distance(*nearest))) {
                nearest = member.at;
            }
        }
        if (!nearest) {
            failure = "nobody down on this map";
            return false;
        }
        TeleportLocalPlayer({(*nearest)[0] + 1.0f, (*nearest)[1] + 0.2f, (*nearest)[2]});
        return true;
    }
    // revive-downed [<timeout>]: strikes the nearest downed party member on this map until nobody
    // is down (how many hits depends on the weapon): every 1.5 s a metre from it, then R1.
    if (verb == "revive-downed") {
        const VitalsView view = ReadVitalsBoard();
        const auto me = LocalPlayerPosition();
        std::optional<LampPosition> downed;
        for (const MemberVitals& member : view.board.members) {
            if (view.Fresh() && member.member != view.member && member.vital == Vital::Down &&
                member.map == PlayingMap()) {
                downed = member.at;
            }
        }
        if (!downed || !me) {
            autoplay_buttons.store(0);
            if (!begun) {
                failure = "nobody down on this map";
                return false;
            }
            LOG_INFO(Debug, "Bloodborne autoplay: nobody down after {:.1f} s of revive hits",
                     elapsed);
            return true;
        }
        const double phase = std::fmod(elapsed, 1.5);
        if (phase < 0.1) {
            TeleportLocalPlayer({(*downed)[0] + 1.0f, (*downed)[1] + 0.2f, (*downed)[2]});
        }
        autoplay_buttons.store(phase >= 0.6 && phase < 0.75 ? AutoplayButton("r1") : 0);
        if (elapsed > number(0, 60)) {
            autoplay_buttons.store(0);
            failure = fmt::format("still down after {:.0f} s of revive hits", elapsed);
        }
        return false;
    }
    if (verb == "press" || verb == "stick") {
        u32 buttons = 0;
        u32 sticks = 0x80808080;
        if (verb == "press") {
            buttons = AutoplayButton(arg(0));
            if (buttons == 0) {
                failure = "no such button";
                return false;
            }
        } else {
            const u32 x = static_cast<u32>(std::clamp(number(0, 128), 0.0, 255.0));
            const u32 y = static_cast<u32>(std::clamp(number(1, 128), 0.0, 255.0));
            sticks = x | (y << 8) | 0x80800000;
        }
        autoplay_buttons.store(buttons);
        autoplay_sticks.store(sticks);
        std::scoped_lock lock{autoplay_mutex};
        autoplay_run.hold_until =
            std::chrono::steady_clock::now() +
            std::chrono::milliseconds{static_cast<s64>(number(verb == "press" ? 1 : 2, 150))};
        return false;
    }
    if (verb == "set-flag") {
        using SetFlag = void PS4_SYSV_ABI (*)(u64, s32, bool);
        reinterpret_cast<SetFlag>(image_base + PartyGhostSetEventFlagOffset)(
            0, static_cast<s32>(number(0, 0)), number(1, 1) != 0);
        return true;
    }
    if (verb == "expect-flag" || verb == "wait-flag") {
        const bool on = EventFlagOn(static_cast<s32>(number(0, 0)));
        if (on == (number(1, 1) != 0)) {
            return true;
        }
        if (verb == "expect-flag") {
            failure = fmt::format("flag is {}", on ? 1 : 0);
            return false;
        }
        return waited_out(number(2, 180));
    }
    // godmode [off]: the player's HP back to its max on every party tick (tests that stand
    // around near enemies), until switched off.
    if (verb == "godmode") {
        autoplay_godmode.store(arg(0) != "off");
        return true;
    }
    if (verb == "snapshot") {
        LogAutoplaySnapshot();
        return true;
    }
    if (verb == "census") {
        LogAutoplayCensus(static_cast<float>(number(0, 60)));
        return true;
    }
    // shot <name> [<timeout>]: a picture of the port's next frame (AutoplayShotPath), once its
    // presenter took the request. The emulator has no request file: skipped. A picture not taken
    // in time (10 s) is noted and the script goes on: it shows the HUD, it tests nothing.
    if (verb == "shot") {
        static const char* request = std::getenv("BB_SHOT_TRIGGER");
        if (request == nullptr) {
            LOG_INFO(Debug, "Bloodborne autoplay: shot {} skipped (nothing takes pictures)",
                     arg(0));
            return true;
        }
        if (!begun) {
            const auto path = AutoplayShotPath(request, arg(0));
            if (!path) {
                failure = "no such shot name";
                return false;
            }
            std::error_code error;
            std::filesystem::create_directories(std::filesystem::path{*path}.parent_path(), error);
            // Written whole, then moved into place: the presenter never reads half a path.
            const std::string partial = std::string{request} + ".part";
            std::ofstream{partial, std::ios::binary | std::ios::trunc} << *path;
            std::filesystem::rename(partial, request, error);
            if (error) {
                failure = fmt::format("no shot request: {}", error.message());
                return false;
            }
            LOG_INFO(Debug, "Bloodborne autoplay: shot {} asked for", *path);
        }
        std::error_code error;
        if (!std::filesystem::exists(request, error)) {
            return true;
        }
        if (elapsed > number(1, 10)) {
            LOG_WARNING(Debug, "Bloodborne autoplay: shot {} not taken in {:.0f} s", arg(0),
                        elapsed);
            return true;
        }
        return false;
    }
    if (verb == "log") {
        std::string text;
        for (const std::string& word : command.args) {
            text += (text.empty() ? "" : " ") + word;
        }
        LOG_INFO(Debug, "Bloodborne autoplay log: {}", text);
        return true;
    }
    if (verb == "expect-hp") {
        const auto target = ParseAutoplayTarget(arg(0));
        const auto found = target ? FindAutoplayCharacter(*target) : std::nullopt;
        if (!found) {
            failure = "no such character";
            return false;
        }
        const s32 hp = ReadValue<s32>(found->data, 0xF8);
        const auto holds = CompareAutoplay(hp, arg(1), static_cast<s64>(number(2, 0)));
        if (!holds || !*holds) {
            failure = fmt::format("hp is {}", hp);
            return false;
        }
        return true;
    }
    // dump <target>: a character's first 0x800 bytes, its control block (+0x18) and its module
    // pointers (+0x3B0) with each module's first 0x100 bytes, logged in hex, to diff one character
    // between the host's and a guest's world (Iosefka shown on a guest with the event hide byte
    // set, 2026-10-10).
    if (verb == "dump") {
        const auto target = ParseAutoplayTarget(arg(0));
        const auto found = target ? FindAutoplayCharacter(*target) : std::nullopt;
        if (!found || !HasMemoryAccess(found->character, 0x400, MemoryProt::CpuRead)) {
            failure = "no such character";
            return false;
        }
        const u32 handle = ReadValue<u32>(found->character, 0x08);
        const auto dump = [&](const char* what, u64 base, u64 size) {
            if (base < 0x10000 || !HasMemoryAccess(base, size, MemoryProt::CpuRead)) {
                return;
            }
            for (u64 offset = 0; offset < size; offset += 0x20) {
                std::string row;
                for (u64 byte = 0; byte < 0x20; ++byte) {
                    row += fmt::format("{:02x}", ReadValue<u8>(base, offset + byte));
                }
                LOG_INFO(Debug, "Bloodborne autoplay dump {:#x} {} +{:03x}: {}", handle, what,
                         offset, row);
            }
        };
        dump("chr", found->character, 0x800);
        dump("ctrl", ReadValue<u64>(found->character, 0x18), 0x100);
        // The model (draw flags at +0x114): how a phantom is drawn.
        dump("model", ReadValue<u64>(found->character, 0x58), 0x400);
        const u64 modules = ReadValue<u64>(found->character, 0x3B0);
        dump("modules", modules, 0x100);
        // Each module's first bytes: what draws the character is in one of them.
        for (u64 index = 0; modules >= 0x10000 && index < 0x20; ++index) {
            dump(fmt::format("module{:02x}", index).c_str(), ReadValue<u64>(modules, index * 8),
                 0x100);
        }
        return true;
    }
    // raise-stat <index> <amount>: one of the player's stats (PlayerGameData +0x40 + 8 * index,
    // what channel 8 sends) raised, as a level up does (see OwnDataWatch).
    if (verb == "raise-stat") {
        const u64 mine = OwnGameData();
        const u64 at = 0x40 + 8 * static_cast<u64>(number(0, 0));
        if (mine < 0x10000 || at > 0x90 || !HasMemoryAccess(mine + at, 4, MemoryProt::CpuWrite)) {
            failure = "no player data";
            return false;
        }
        const s32 before = ReadValue<s32>(mine, at);
        WriteValue(mine, at, static_cast<s32>(before + number(1, 1)));
        LOG_INFO(Debug, "Bloodborne autoplay raise-stat: +{:#x} {} -> {}", at, before,
                 ReadValue<s32>(mine, at));
        return true;
    }
    // peek <target> <offset>, poke <target> <offset> <value> [1] (hex): a character's u32 (or byte)
    // at an offset, logged or written (reverse engineering on the rig: a field's meaning tried in
    // play).
    if (verb == "peek" || verb == "poke") {
        const auto target = ParseAutoplayTarget(arg(0));
        const auto found = target ? FindAutoplayCharacter(*target) : std::nullopt;
        u32 offset = 0;
        u32 value = 0;
        const std::string_view offset_text = arg(1);
        const std::string_view value_text = arg(2);
        std::from_chars(offset_text.data(), offset_text.data() + offset_text.size(), offset, 16);
        std::from_chars(value_text.data(), value_text.data() + value_text.size(), value, 16);
        if (!found || offset > 0x2000 ||
            !HasMemoryAccess(found->character + offset, 4, MemoryProt::CpuRead)) {
            failure = "no such character";
            return false;
        }
        const u32 before = ReadValue<u32>(found->character, offset);
        if (verb == "poke" && arg(3) == "1") {
            WriteValue(found->character, offset, static_cast<u8>(value)); // one byte
        } else if (verb == "poke") {
            WriteValue(found->character, offset, value);
        }
        LOG_INFO(Debug, "Bloodborne autoplay {}: {:#x} +{:#x} = {:#x}{}", verb,
                 ReadValue<u32>(found->character, 0x08), offset, before,
                 verb == "poke" ? fmt::format(" -> {:#x}", value) : "");
        return true;
    }
    // expect-hidden <handle> <0|1> [<timeout>]: the character hidden by the story (its event
    // disable byte, +0x28) or not, within the time given (default: now).
    if (verb == "expect-hidden") {
        const auto target = ParseAutoplayTarget(arg(0));
        const auto found = target ? FindAutoplayCharacter(*target) : std::nullopt;
        if (!found) {
            failure = "no such character";
            return false;
        }
        const bool hidden = ReadValue<u8>(found->character, 0x28) != 0;
        if (hidden == (number(1, 1) != 0)) {
            return true;
        }
        if (elapsed >= number(2, 0)) {
            failure = fmt::format("hidden is {}", hidden ? 1 : 0);
        }
        return false;
    }
    // expect-disabled <handle> <0|1> [<timeout>]: the character's enable switch off (2004:5,
    // IsCharacterDisabled) or on, within the time given (default: now).
    if (verb == "expect-disabled") {
        const auto target = ParseAutoplayTarget(arg(0));
        const auto found = target ? FindAutoplayCharacter(*target) : std::nullopt;
        if (!found) {
            failure = "no such character";
            return false;
        }
        const bool disabled = IsCharacterDisabled(found->character);
        if (disabled == (number(1, 1) != 0)) {
            return true;
        }
        if (elapsed >= number(2, 0)) {
            failure = fmt::format("disabled is {}", disabled ? 1 : 0);
        }
        return false;
    }
    // note-max-hp: the player's max HP now, for a later expect-max-hp self == noted.
    if (verb == "note-max-hp") {
        const auto self = FindAutoplayCharacter(AutoplaySelf);
        if (!self) {
            failure = "no player";
            return false;
        }
        autoplay_run.noted_max_hp = ReadValue<s32>(self->data, 0xFC);
        LOG_INFO(Debug, "Bloodborne autoplay: max hp {}", autoplay_run.noted_max_hp);
        return true;
    }
    // expect-max-hp <target> <op> <value|noted>: its max HP (a party guest's is not cut to 70%).
    if (verb == "expect-max-hp") {
        const auto target = ParseAutoplayTarget(arg(0));
        const auto found = target ? FindAutoplayCharacter(*target) : std::nullopt;
        if (!found) {
            failure = "no such character";
            return false;
        }
        const s32 max = ReadValue<s32>(found->data, 0xFC);
        const s64 limit =
            arg(2) == "noted" ? autoplay_run.noted_max_hp : static_cast<s64>(number(2, 0));
        const auto holds = CompareAutoplay(max, arg(1), limit);
        if (!holds || !*holds) {
            failure = fmt::format("max hp is {}", max);
            return false;
        }
        return true;
    }
    // expect-effect <id> <0|1>: whether the player has that SpEffect (9200: the level sync).
    if (verb == "expect-effect") {
        const auto effects = LocalEffects();
        const bool has =
            std::ranges::find(effects, static_cast<s32>(number(0, 0))) != effects.end();
        if (has != (number(1, 1) != 0)) {
            failure = fmt::format("effect {} is {}", arg(0), has ? "on" : "off");
            return false;
        }
        return true;
    }
    if (verb == "expect-session") {
        const size_t members = AutoplaySessionMembers();
        if (members != static_cast<size_t>(number(0, 1))) {
            failure = fmt::format("{} in the session", members);
            return false;
        }
        return true;
    }
    // expect-others-near <n> <meters>: at least n other players stand alive within <meters> of
    // the player (a guest that got up invisible to the host: its copy dead, or left where it died).
    if (verb == "expect-others-near") {
        const auto here = LocalPlayerPosition();
        const double limit = number(1, 10);
        size_t close_by = 0;
        std::string seen;
        for (const OtherPlayer& other : OtherPlayers()) {
            const double distance = here && other.at ? std::hypot((*other.at)[0] - (*here)[0],
                                                                  (*other.at)[1] - (*here)[1],
                                                                  (*other.at)[2] - (*here)[2])
                                                     : -1.0;
            seen += fmt::format(" [type={} hp={} dead={:#x} at {:.1f} m]", other.type, other.hp,
                                other.dead_flags, distance);
            close_by += other.hp > 0 && (other.dead_flags & 0x05) == 0 && distance >= 0.0 &&
                        distance <= limit;
        }
        if (close_by < static_cast<size_t>(number(0, 1))) {
            failure = fmt::format("{} alive within {} m:{}", close_by, limit,
                                  seen.empty() ? " none" : seen);
            return false;
        }
        return true;
    }
    if (verb == "expect-map") {
        const auto map = ParseAutoplayTarget(arg(0));
        // Raw, as all of the rig's checks (see AutoplayInWorld).
        if (!map || GetCurrentPackedMap() != *map) {
            failure = fmt::format("on map {:#x}", GetCurrentPackedMap());
            return false;
        }
        return true;
    }
    if (verb == "done") {
        return true;
    }
    failure = "unknown command";
    return false;
}

// From the party tick (the game thread), once the player is in the world.
void TickAutoplay() {
    if (autoplay_godmode.load()) {
        if (const auto self = FindAutoplayCharacter(AutoplaySelf);
            self && ReadValue<s32>(self->data, 0xF8) > 0 &&
            HasMemoryAccess(self->data, 0x100, MemoryProt::CpuWrite)) {
            WriteValue(self->data, 0xF8, ReadValue<s32>(self->data, 0xFC));
        }
    }
    AutoplayCommand command;
    size_t index = 0;
    size_t total = 0;
    bool begun = false;
    double elapsed = 0.0;
    const auto now = std::chrono::steady_clock::now();
    {
        std::scoped_lock lock{autoplay_mutex};
        if (autoplay_run.phase != AutoplayRun::Phase::World) {
            return;
        }
        if (autoplay_run.hold_until) {
            if (now < *autoplay_run.hold_until) {
                return;
            }
            autoplay_run.hold_until.reset();
            autoplay_buttons.store(0);
            autoplay_sticks.store(0x80808080);
            LOG_INFO(Debug, "Bloodborne autoplay: step {}/{} {} ok", autoplay_run.next + 1,
                     autoplay_run.script.size(),
                     AutoplayText(autoplay_run.script[autoplay_run.next]));
            ++autoplay_run.next;
            autoplay_run.step_started = now;
            autoplay_run.step_begun = false;
            return;
        }
        if (autoplay_run.next >= autoplay_run.script.size() ||
            autoplay_run.script[autoplay_run.next].verb == "done") {
            autoplay_run.phase = AutoplayRun::Phase::Done;
            LOG_INFO(Debug, "Bloodborne autoplay: done (ok)");
            return;
        }
        command = autoplay_run.script[autoplay_run.next];
        index = autoplay_run.next;
        total = autoplay_run.script.size();
        begun = autoplay_run.step_begun;
        autoplay_run.step_begun = true;
        elapsed = std::chrono::duration<double>(now - autoplay_run.step_started).count();
    }
    std::string failure;
    const bool finished = RunAutoplayStep(command, begun, elapsed, failure);
    std::scoped_lock lock{autoplay_mutex};
    if (!failure.empty()) {
        LOG_ERROR(Debug, "Bloodborne autoplay: step {}/{} {} FAILED: {}", index + 1, total,
                  AutoplayText(command), failure);
        LOG_INFO(Debug, "Bloodborne autoplay: done (failed)");
        autoplay_run.phase = AutoplayRun::Phase::Done;
        autoplay_buttons.store(0);
        autoplay_sticks.store(0x80808080);
        return;
    }
    if (finished && !autoplay_run.hold_until) {
        LOG_INFO(Debug, "Bloodborne autoplay: step {}/{} {} ok (t={:.1f})", index + 1, total,
                 AutoplayText(command), SteadyMilliseconds() / 1000.0);
        ++autoplay_run.next;
        autoplay_run.step_started = std::chrono::steady_clock::now();
        autoplay_run.step_begun = false;
    }
}

// From the pad reader: on the title screen, Cross now and then until the player is in the world.
void TickAutoplayTitle() {
    std::scoped_lock lock{autoplay_mutex};
    if (autoplay_run.phase != AutoplayRun::Phase::Title) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (AutoplayInWorld()) {
        autoplay_run.phase = AutoplayRun::Phase::World;
        autoplay_run.step_started = now;
        autoplay_run.step_begun = false;
        autoplay_buttons.store(0);
        // Raw, as all of the rig's checks (see AutoplayInWorld).
        LOG_INFO(Debug, "Bloodborne autoplay: in the world on map {:#x}", GetCurrentPackedMap());
        return;
    }
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - autoplay_run.started);
    autoplay_buttons.store(TitlePressesCross(static_cast<u64>(ms.count())) ? AutoplayButton("cross")
                                                                           : 0);
}

} // namespace

bool AutoplayPad(std::uint32_t& buttons, std::array<std::uint8_t, 4>& sticks) {
    MakeEnemiesHarder();
    std::call_once(autoplay_loaded, LoadAutoplay);
    if (autoplay_run.phase == AutoplayRun::Phase::Off) {
        return false;
    }
    TickAutoplayTitle();
    buttons = autoplay_buttons.load();
    const u32 packed = autoplay_sticks.load();
    sticks = {static_cast<u8>(packed), static_cast<u8>(packed >> 8), static_cast<u8>(packed >> 16),
              static_cast<u8>(packed >> 24)};
    return true;
}

void FilterPartyDeathPad(std::uint32_t& buttons, std::array<std::uint8_t, 4>& sticks,
                         std::uint8_t& l2, std::uint8_t& r2) {
    if (!party_death_hook_installed) {
        return;
    }
    const u32 before = party_pad_buttons.exchange(buttons, std::memory_order_relaxed);
    if (AttackPressed(before, buttons)) {
        party_attack_heavy.store(HeavyAttackPressed(before, buttons), std::memory_order_relaxed);
        party_attack_presses.fetch_add(1, std::memory_order_relaxed);
    }
    if (!party_death_down.load(std::memory_order_relaxed)) {
        return;
    }
    const PadInput kept = DownedPad({.buttons = buttons, .sticks = sticks, .l2 = l2, .r2 = r2});
    buttons = kept.buttons;
    sticks = kept.sticks;
    l2 = kept.l2;
    r2 = kept.r2;
}

bool ReadPartyPanel(PartyPanel& panel) {
    std::scoped_lock lock{panel_mutex};
    panel = panel_now;
    return !panel.members.empty() || panel.vital == Vital::Down;
}

void LogGameCallChain(const char* what) {
    if (image_base == 0) {
        return;
    }
    // The HLE function runs on the guest thread's stack, so the game frames sit above this one.
    volatile u64 marker = 0;
    const u64 start = reinterpret_cast<u64>(&marker);
    std::vector<u64> words;
    for (u64 address = start; address < start + 0x4000; address += 8) {
        if (!HasMemoryAccess(address, 8, MemoryProt::CpuRead)) {
            break;
        }
        words.push_back(*reinterpret_cast<const u64*>(address));
    }
    LOG_INFO(Debug, "Bloodborne {} from the game:{}", what,
             FormatGameAddresses(words, image_base, MemoryPatcher::g_eboot_image_size, 24));
}

void SetBuildIdentity(std::string_view revision, u64 event_files) {
    std::scoped_lock lock{peer_hello_mutex};
    own_hello = PeerHello{.revision = std::string{revision}, .event_files = event_files};
}

void NoteSaveFileOpened(std::string_view guest_path) {
    if (const auto slot = CharacterSlotOfSavePath(guest_path)) {
        MakeEnemiesHarder();
        if (character_slot.exchange(*slot) != *slot) {
            LOG_INFO(Debug, "Bloodborne party: character save slot {}", *slot);
            OnLifecycle(Lifecycle::NewCharacter);
        }
    }
}

void InstallReverseEngineeringTrace() {
    if (installed || !EnvFlagEnabled("SHADPS4_BLOODBORNE_RE_TRACE")) {
        return;
    }
    if (MemoryPatcher::g_game_serial != "CUSA03173") {
        LOG_WARNING(Debug, "Bloodborne RE trace ignored for title {}",
                    MemoryPatcher::g_game_serial);
        return;
    }

    const auto* param_sfo = Common::Singleton<PSF>::Instance();
    const std::string_view app_version = param_sfo->GetString("APP_VER").value_or("Unknown");
    if (app_version != "01.09") {
        LOG_ERROR(Debug, "Bloodborne RE trace requires CUSA03173 01.09; loaded version is {}",
                  app_version);
        return;
    }

    image_base = MemoryPatcher::g_eboot_address;
    if (image_base == 0 || !VerifySites()) {
        LOG_ERROR(Debug, "Bloodborne RE trace was not installed");
        return;
    }

    const auto capture_dir =
        Common::FS::GetUserPath(Common::FS::PathType::CapturesDir) / "bloodborne-re";
    std::error_code error;
    std::filesystem::create_directories(capture_dir, error);
    if (error) {
        LOG_ERROR(Debug, "Could not create Bloodborne RE capture directory {}: {}",
                  Common::FS::PathToUTF8String(capture_dir), error.message());
        return;
    }

    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
    const int process_id = Debugger::GetCurrentPid();
    capture_path = capture_dir / ("cusa03173-109-p" + std::to_string(process_id) + '-' +
                                  std::to_string(timestamp) + ".jsonl");
    capture_file.open(capture_path, std::ios::out | std::ios::trunc);
    if (!capture_file) {
        LOG_ERROR(Debug, "Could not open Bloodborne RE capture {}",
                  Common::FS::PathToUTF8String(capture_path));
        return;
    }
    capture_file << "{\"type\":\"header\",\"title\":\"CUSA03173\","
                    "\"app_version\":\"01.09\",\"image_base\":\"0x"
                 << std::hex << image_base << std::dec << "\",\"process_id\":" << process_id
                 << ",\"site_count\":" << Sites.size() << ",\"negative_area_patch\":"
                 << (negative_area_patch_installed ? "true" : "false")
                 << ",\"area_flag_patch\":" << (area_flag_patch_installed ? "true" : "false")
                 << ",\"responder_bell_area_patch\":"
                 << (responder_bell_area_patch_installed ? "true" : "false")
                 << ",\"responder_bell_common_patch\":"
                 << (responder_bell_common_patch_installed ? "true" : "false")
                 << ",\"active_bell_negative_area_patch\":"
                 << (active_bell_negative_area_patch_installed ? "true" : "false")
                 << ",\"active_bell_area_flag_patch\":"
                 << (active_bell_area_flag_patch_installed ? "true" : "false")
                 << ",\"responder_search_negative_area_patch\":"
                 << (responder_search_negative_area_patch_installed ? "true" : "false")
                 << ",\"sos_status_area_patch\":"
                 << (sos_status_area_patch_installed ? "true" : "false")
                 << ",\"summon_candidate_area_patch\":"
                 << (summon_candidate_area_patch_installed ? "true" : "false")
                 << ",\"summon_build_area_patch\":"
                 << (summon_build_area_patch_installed ? "true" : "false")
                 << ",\"summon_build_world_state_patch\":"
                 << (summon_build_world_state_patch_installed ? "true" : "false")
                 << ",\"summon_build_negative_event_patch\":"
                 << (summon_build_negative_event_patch_installed ? "true" : "false")
                 << ",\"stage_transition_keep_matching_patch\":"
                 << (stage_transition_keep_matching_patch_installed ? "true" : "false")
                 << ",\"summon_build_host_placement_hook\":"
                 << (summon_build_host_placement_hook_installed ? "true" : "false")
                 << ",\"cross_map_guest_handoff_hook\":"
                 << (cross_map_guest_handoff_hook_installed ? "true" : "false")
                 << ",\"summon_reload_state_hook\":"
                 << (summon_reload_state_hook_installed ? "true" : "false")
                 << ",\"deferred_summon_reload_hook\":"
                 << (deferred_summon_reload_hook_installed ? "true" : "false")
                 << ",\"healing_fountain_host_availability_hook\":"
                 << (healing_fountain_host_availability_hook_installed ? "true" : "false") << "}\n";
    capture_file.flush();

    size_t hook_count = 0;
    for (size_t index = 0; index < Sites.size(); ++index) {
        const auto& site = Sites[index];
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
            ++hook_count;
            continue;
        }
        const auto expected = std::span<const u8>{site.prologue.data(), site.prologue_size};
        if (!InstallGuestCodeHook(reinterpret_cast<void*>(image_base + site.offset), expected,
                                  index, TraceEntry)) {
            LOG_ERROR(Debug, "Failed to install Bloodborne RE hook {} at {:#x}", site.name,
                      site.offset);
            continue;
        }
        ++hook_count;
    }

    installed = hook_count == Sites.size();
    LOG_INFO(Debug, "Bloodborne RE trace installed {}/{} hooks; capture={}", hook_count,
             Sites.size(), Common::FS::PathToUTF8String(capture_path));
}

} // namespace Core::Bloodborne
