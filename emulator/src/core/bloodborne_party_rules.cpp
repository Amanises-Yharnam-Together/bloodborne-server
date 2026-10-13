// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Party rules: no fog walls, harder enemies, levels, solid NPCs, shared loot. Split from
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

std::vector<KillEchoes> echoes_to_post;

} // namespace Coop

namespace {

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

} // namespace Coop

namespace {

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
std::array<std::atomic<u64>, ItemModuleChecks.size()> item_module_check_hits{};

void PS4_SYSV_ABI CountItemModuleCheck(u64 tag, const GuestRegisterSnapshot*) {
    if (tag < item_module_check_hits.size()) {
        item_module_check_hits[tag].fetch_add(1, std::memory_order_relaxed);
    }
}

} // namespace Coop

namespace {

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
    // A kill reward: the game paid this member (a post of the same kill is not owed here).
    if (amount <= 0 || caller < image_base || !SharesKillEchoes(caller - image_base, true)) {
        return;
    }
    const KillEchoes kill{amount, CurrentPlace().map};
    const bool posts = PostsKillEchoesInSession();
    std::scoped_lock lock{echo_board_mutex};
    echo_ledger.Paid(kill, SteadyMilliseconds());
    if (posts) {
        echoes_to_post.push_back(kill);
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
    if (channel_table_installed) {
        coop_features.Skipped("map item sync", "the channel table drops map item packets");
    } else if (MapItemPacketHeaderOffset + MapItemPacketHeader.size() > image_size ||
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

} // namespace Coop

} // namespace Core::Bloodborne
