// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <bit>
#include <charconv>
#include <cstring>

#include "core/bloodborne_party.h"

namespace Core::Bloodborne {

namespace {

// Must match shadNet's NormalizePartyKey so both sides agree on what a party is.
constexpr std::size_t MaxPartyKeySize = 128;

bool IsWhitespace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

} // namespace

std::optional<std::string> ParsePartySetting(std::string_view setting) {
    while (!setting.empty() && IsWhitespace(setting.front())) {
        setting.remove_prefix(1);
    }
    while (!setting.empty() && IsWhitespace(setting.back())) {
        setting.remove_suffix(1);
    }
    if (setting.empty() || setting.size() > MaxPartyKeySize) {
        return std::nullopt;
    }
    for (const char c : setting) {
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) {
            return std::nullopt;
        }
    }
    const std::size_t colon = setting.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon == setting.size() - 1) {
        return std::nullopt;
    }
    return std::string{setting};
}

bool StartSummonSearch(u64 player, s32 effect_id, AddSpEffectFn add_sp_effect) {
    if (player == 0 || add_sp_effect == nullptr) {
        return false;
    }
    add_sp_effect(player, effect_id, player, 0, 1, 0, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0);
    return true;
}

bool ShouldEndSessionOnWorldTeardown(s32 matching_state, s32 reload_state, bool reload_pending) {
    const bool in_session =
        matching_state == MatchingStateSummonedGuest || matching_state == MatchingStateHosting;
    return in_session && reload_state != SummonReloadInProgress && !reload_pending;
}

const std::vector<CodePatch>& GhostDeathKeepSessionPatches() {
    static const std::vector<u8> nop5{0x0F, 0x1F, 0x44, 0x00, 0x00};
    static const std::vector<CodePatch> patches{
        // PartyGhostDeath: SetEventFlag(4047, true) twice. 4047 marks a player on the way out;
        // left set, it would also swallow a later "host left" for the revived guest.
        {0x01381FE7, {0xE8, 0xB4, 0xC5, 0xFA, 0xFF}, nop5},
        {0x01381FF6, {0xE8, 0xA5, 0xC5, 0xFA, 0xFF}, nop5},
        // CustomLuaCallStart(4063, local player): the host's OnGameLeave drops us.
        {0x0138201B, {0xE8, 0x20, 0xCD, 0xFA, 0xFF}, nop5},
        // SetFlagInitState(2), the reset for the load into our own world.
        {0x0138202F, {0xC7, 0x00, 0x02, 0x00, 0x00, 0x00}, {0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00}},
        // PartyGhostDeath_1: skip MissionDeadFailed and OnRequestMenuEnd(PartyGhostDeath_2).
        {PartyGhostDeathMenuBranchOffset, {0x75, 0x2C}, {0xEB, 0x2C}},
    };
    return patches;
}

const std::vector<CodePatch>& GuestBloodstainPatches() {
    static const std::vector<CodePatch> patches{
        {0x0137EBC7, {0xB9, 0x30, 0x00, 0x00, 0x00}, {0xB9, 0x70, 0x00, 0x00, 0x00}},
    };
    return patches;
}

const std::vector<CodePatch>& HostDeathKeepGuestsPatches() {
    // `mov esi, 0xFCF` (start of the white phantom branch) -> `jmp HostDeadEpilogueOffset`.
    static const std::vector<CodePatch> patches{
        {0x013819C6, {0xBE, 0xCF, 0x0F, 0x00, 0x00}, {0xE9, 0x85, 0x00, 0x00, 0x00}},
        // Character death step (0x018C49F0): skip removing the session's members from a dying
        // host's world (`jne` -> `jmp` to the same target, then a nop).
        {0x018C4D1D, {0x0F, 0x85, 0x97, 0x00, 0x00, 0x00}, {0xE9, 0x98, 0x00, 0x00, 0x00, 0x90}},
    };
    return patches;
}

const std::vector<CodePatch>& BossClearKeepSessionPatches() {
    static const std::vector<u8> nop5{0x0F, 0x1F, 0x44, 0x00, 0x00};
    static const std::vector<CodePatch> patches{
        // BlockClear2_2: CustomLuaCallStart(4063, local player), the white phantom's goodbye.
        {0x013857AC, {0xE8, 0x8F, 0x95, 0xFA, 0xFF}, nop5},
        // BlockClear2_2: OnKeyTime2(4050, "BlockClear2_3Leave"), LeaveSession for every member.
        {0x01385877, {0xE8, 0x44, 0x59, 0xFA, 0xFF}, nop5},
        // BlockClear2_3: clear 4047 and 4000 for white phantoms too (`jne` -> 2-byte nop).
        {0x0138596D, {0x75, 0x1B}, {0x66, 0x90}},
        // BlockClear2_3: skip the white phantom's reset and warp home (`jne` -> `jmp`).
        {0x0138599C, {0x75, 0x46}, {0xEB, 0x46}},
    };
    return patches;
}

namespace {
u64 DecodeRel32(u8 opcode, u64 offset, std::span<const u8> bytes) {
    if (bytes.size() != 5 || bytes[0] != opcode) {
        return 0;
    }
    s32 displacement;
    std::memcpy(&displacement, bytes.data() + 1, sizeof(displacement));
    return offset + bytes.size() + static_cast<s64>(displacement);
}
} // namespace

u64 DecodeNearCallTarget(u64 offset, std::span<const u8> bytes) {
    return DecodeRel32(0xE8, offset, bytes);
}

u64 DecodeNearJumpTarget(u64 offset, std::span<const u8> bytes) {
    return DecodeRel32(0xE9, offset, bytes);
}

namespace {

bool HasReviveCalls(const GhostDeathCalls& calls) {
    return calls.set_event_flag && calls.clear_return_state && calls.request_full_recover &&
           calls.update_blood_mark && calls.revive_player && calls.revive_local_character &&
           calls.restore_alive_state && calls.chr_reset_animation && calls.warp_to_last_lamp;
}

// What both deaths share. `character_type` is set back after the local revive makes the player
// a live one (only a guest needs that).
void ReviveInPlace(const GhostDeathCalls& calls, std::optional<s32> character_type) {
    calls.set_event_flag(0, DeathEventFlag, false);
    calls.clear_return_state();
    calls.request_full_recover();
    calls.update_blood_mark();
    calls.revive_player();
    calls.revive_local_character();
    calls.restore_alive_state();
    if (character_type) {
        calls.set_chr_type(0, LocalPlayerEntity, *character_type);
    }
    calls.chr_reset_animation(0, LocalPlayerEntity);
    calls.warp_to_last_lamp();
}

} // namespace

bool ReviveDeadPartyGuest(const GhostDeathCalls& calls) {
    if (!HasReviveCalls(calls) || !calls.is_white_ghost || !calls.set_chr_type ||
        !calls.is_white_ghost()) {
        return false;
    }
    ReviveInPlace(calls, WhiteGhostCharacterType);
    return true;
}

bool ShouldReviveHostInPlace(s32 matching_state) {
    return matching_state == MatchingStateHosting || matching_state == MatchingStateSummonedGuest;
}

void ReviveDeadPartyHost(const GhostDeathCalls& calls) {
    if (HasReviveCalls(calls)) {
        ReviveInPlace(calls, std::nullopt);
    }
}

std::array<u8, 7> EncodeSignTimerStore(u8 modrm, float seconds) {
    const auto value = std::bit_cast<u32>(seconds);
    return {0xC7,
            modrm,
            0x28,
            static_cast<u8>(value),
            static_cast<u8>(value >> 8),
            static_cast<u8>(value >> 16),
            static_cast<u8>(value >> 24)};
}

bool IsLampTravelFlag(s32 flag) {
    // 7MMM01LL: map code MMM (241 = m24_01), lamp LL. Not the Hunter's Dream (210): it has no
    // lamp to travel from, and the Doll's dialog sets 721001xx.
    constexpr s32 HuntersDreamMapCode = 210;
    return flag >= 70000000 && flag < 80000000 && flag % 10000 / 100 == 1 &&
           flag / 10000 % 1000 != HuntersDreamMapCode;
}

bool LampRestsEnabled(const char* setting) {
    return setting != nullptr && std::string_view{setting} == "1";
}

bool ShouldRecreateAfterTrip(s32 kind, s32 step) {
    return (kind == RemoteHostTask || kind == RemoteGuestTask) && step == InSessionStep;
}

std::string EncodeMapUpdate(std::string_view member, u32 map) {
    std::array<char, 8> hex{};
    const auto [end, error] = std::to_chars(hex.data(), hex.data() + hex.size(), map, 16);
    return std::string{member} + ' ' + std::string{hex.data(), end};
}

std::optional<std::vector<MemberMap>> ParseMapBoard(std::string_view body) {
    std::vector<MemberMap> board;
    while (!body.empty()) {
        const auto end = body.find('\n');
        const std::string_view line = body.substr(0, end);
        body = end == std::string_view::npos ? std::string_view{} : body.substr(end + 1);
        if (line.empty()) {
            continue;
        }
        const auto space = line.find(' ');
        if (space == std::string_view::npos || space == 0) {
            return std::nullopt;
        }
        const std::string_view map_text = line.substr(space + 1);
        u32 map = 0;
        const auto [parsed_end, error] =
            std::from_chars(map_text.data(), map_text.data() + map_text.size(), map, 16);
        if (error != std::errc{} || parsed_end != map_text.data() + map_text.size()) {
            return std::nullopt;
        }
        board.push_back(MemberMap{std::string{line.substr(0, space)}, map});
    }
    return board;
}

bool EveryoneElseOnMap(std::span<const MemberMap> board, std::string_view self, u32 map) {
    bool anyone_else = false;
    for (const MemberMap& entry : board) {
        if (entry.member == self) {
            continue;
        }
        if (entry.map != map) {
            return false;
        }
        anyone_else = true;
    }
    return anyone_else;
}

std::string EncodeTrophyUpdate(std::string_view member, std::span<const s32> trophies) {
    std::string body{member};
    for (const s32 trophy : trophies) {
        body += ' ' + std::to_string(trophy);
    }
    return body;
}

std::optional<std::vector<s32>> ParseTrophyBoard(std::string_view body) {
    std::vector<s32> trophies;
    while (!body.empty()) {
        const auto end = body.find('\n');
        const std::string_view line = body.substr(0, end);
        body = end == std::string_view::npos ? std::string_view{} : body.substr(end + 1);
        if (line.empty()) {
            continue;
        }
        s32 trophy = 0;
        const auto [parsed_end, error] =
            std::from_chars(line.data(), line.data() + line.size(), trophy);
        if (error != std::errc{} || parsed_end != line.data() + line.size() || trophy < 0) {
            return std::nullopt;
        }
        trophies.push_back(trophy);
    }
    return trophies;
}

std::vector<s32> TrophiesToUnlock(std::span<const s32> board, std::span<const s32> handled) {
    std::vector<s32> missing;
    for (const s32 trophy : board) {
        if (std::ranges::find(handled, trophy) == handled.end()) {
            missing.push_back(trophy);
        }
    }
    return missing;
}

std::optional<int> CharacterSlotOfSavePath(std::string_view path) {
    constexpr std::string_view prefix = "/savedata0/userdata000";
    if (path.size() != prefix.size() + 1 || !path.starts_with(prefix)) {
        return std::nullopt;
    }
    const char digit = path.back();
    if (digit < '0' || digit > '9') {
        return std::nullopt;
    }
    return digit - '0';
}

bool IsNewCharacterInSlot(std::span<const s32> confirmed, size_t confirmed_set_now) {
    return !confirmed.empty() && confirmed_set_now == 0;
}

bool IsMapPickupFlag(s32 flag) {
    const bool chalice = flag >= 52900000 && flag < 53000000;
    return flag >= 50000000 && flag < 54000000 && !chalice;
}

std::string FormatFlagList(std::span<const s32> flags) {
    std::string text;
    for (const s32 flag : flags) {
        text += std::to_string(flag) + '\n';
    }
    return text;
}

std::vector<s32> ParseFlagList(std::string_view text) {
    std::vector<s32> flags;
    while (!text.empty()) {
        const auto end = text.find('\n');
        const std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        s32 flag = 0;
        const auto [parsed_end, error] =
            std::from_chars(line.data(), line.data() + line.size(), flag);
        if (!line.empty() && error == std::errc{} && parsed_end == line.data() + line.size()) {
            flags.push_back(flag);
        }
    }
    return flags;
}

FlagChanges ChangedFlags(u32 block, u32 flags_per_block, std::span<const u8> before,
                         std::span<const u8> after) {
    FlagChanges changes;
    const size_t bytes = std::min(before.size(), after.size());
    for (size_t byte = 0; byte < bytes; ++byte) {
        const u8 differs = before[byte] ^ after[byte];
        if (differs == 0) {
            continue;
        }
        for (u32 bit = 0; bit < 8; ++bit) {
            const u8 mask = static_cast<u8>(0x80 >> bit);
            if ((differs & mask) == 0) {
                continue;
            }
            const s32 flag = static_cast<s32>(block * flags_per_block + byte * 8 + bit);
            ((after[byte] & mask) != 0 ? changes.set : changes.cleared).push_back(flag);
        }
    }
    return changes;
}

bool CarriesToGuestWorld(s32 flag) {
    const bool death_or_multiplayer = flag >= 4000 && flag < 5000;
    const bool trip = IsLampTravelFlag(flag) || IsHeadstoneTravelFlag(flag);
    return flag > 0 && !death_or_multiplayer && !trip && !IsMapPickupFlag(flag);
}

FlagBit FlagBitLocation(s32 flag, u32 flags_per_block) {
    const u32 within = static_cast<u32>(flag) % flags_per_block;
    return {static_cast<u32>(flag) / flags_per_block, within / 8,
            static_cast<u8>(0x80 >> (within % 8))};
}

std::vector<s32> LampFlagsKeptAtRegistration(bool party_configured, s32 base, u16 on_before) {
    std::vector<s32> kept;
    if (!party_configured || base < 0) {
        return kept;
    }
    for (s32 index = 0; index < 12; ++index) {
        if ((on_before >> index) & 1) {
            kept.push_back(base + index);
        }
    }
    return kept;
}

bool PostsToHostWorld(s32 flag) {
    return LampOfActiveFlag(flag).has_value() || LampOfActiveFlag(flag - 1).has_value();
}

std::optional<s32> LampOfActiveFlag(s32 flag) {
    constexpr s32 ActiveFlagBase = 10007810;
    const s32 offset = flag - ActiveFlagBase;
    if (offset < 0) {
        return std::nullopt;
    }
    const s32 map = offset / 10000;
    const s32 within = offset % 10000;
    if (map < 210 || map >= 400 || within % 20 != 0 || within / 20 >= 20) {
        return std::nullopt;
    }
    return map * 10000 + 1950 + within / 20;
}

bool IsBulkFlagChange(size_t changed_flags) {
    return changed_flags > 64;
}

void RecordWorldChanges(std::map<s32, bool>& world, const FlagChanges& changes) {
    for (const s32 flag : changes.set) {
        if (CarriesToGuestWorld(flag)) {
            world[flag] = true;
        }
    }
    for (const s32 flag : changes.cleared) {
        if (CarriesToGuestWorld(flag)) {
            world[flag] = false;
        }
    }
}

std::string FormatWorldChanges(const std::map<s32, bool>& world) {
    std::string text;
    for (const auto& [flag, on] : world) {
        text += std::to_string(flag) + (on ? " 1\n" : " 0\n");
    }
    return text;
}

std::map<s32, bool> ParseWorldChanges(std::string_view text) {
    std::map<s32, bool> world;
    while (!text.empty()) {
        const auto end = text.find('\n');
        const std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        const auto space = line.find(' ');
        if (space == std::string_view::npos || space + 2 != line.size()) {
            continue;
        }
        s32 flag = 0;
        const auto [parsed_end, error] = std::from_chars(line.data(), line.data() + space, flag);
        const char state = line[space + 1];
        if (error == std::errc{} && parsed_end == line.data() + space &&
            (state == '0' || state == '1')) {
            world[flag] = state == '1';
        }
    }
    return world;
}

std::string EncodeFlagUpdate(std::string_view member, s64 since,
                             std::span<const std::pair<s32, bool>> changes) {
    std::string body = std::string{member} + ' ' + std::to_string(since);
    for (const auto& [flag, on] : changes) {
        body += ' ' + std::to_string(flag) + (on ? ":1" : ":0");
    }
    return body;
}

std::optional<FlagBoardAnswer> ParseFlagBoard(std::string_view body) {
    FlagBoardAnswer answer;
    const auto first_end = body.find('\n');
    const std::string_view first = body.substr(0, first_end);
    const auto [latest_end, latest_error] =
        std::from_chars(first.data(), first.data() + first.size(), answer.latest);
    if (latest_error != std::errc{} || latest_end != first.data() + first.size()) {
        return std::nullopt;
    }
    body = first_end == std::string_view::npos ? std::string_view{} : body.substr(first_end + 1);
    while (!body.empty()) {
        const auto end = body.find('\n');
        const std::string_view line = body.substr(0, end);
        body = end == std::string_view::npos ? std::string_view{} : body.substr(end + 1);
        if (line.empty()) {
            continue;
        }
        const auto space = line.find(' ');
        s32 flag = 0;
        const auto [flag_end, flag_error] =
            std::from_chars(line.data(), line.data() + std::min(space, line.size()), flag);
        if (space == std::string_view::npos || flag_error != std::errc{} ||
            flag_end != line.data() + space || line.size() != space + 2 ||
            (line[space + 1] != '0' && line[space + 1] != '1')) {
            return std::nullopt;
        }
        answer.changes.emplace_back(flag, line[space + 1] == '1');
    }
    return answer;
}

std::vector<s32> EffectsToRestoreAfterTrip(std::span<const s32> before,
                                           std::span<const s32> after) {
    std::vector<s32> missing;
    for (const s32 effect : before) {
        // The bells' effects; 9001 (in a session as host) the game puts back itself.
        constexpr std::array<s32, 4> BellEffects{9000, 9003, 9005, 9006};
        if (std::ranges::find(BellEffects, effect) != BellEffects.end() &&
            std::ranges::find(after, effect) == after.end()) {
            missing.push_back(effect);
        }
    }
    return missing;
}

bool ShouldRestoreSearchAfterTrip(bool in_party_session, bool search_effect_active) {
    return in_party_session && !search_effect_active;
}

s32 LocalTaskKind(bool hosting) {
    return hosting ? 3 : 2;
}

s32 RemoteTaskKind(bool hosting) {
    return hosting ? RemoteGuestTask : RemoteHostTask;
}

bool TalksAsHost(bool in_party_session, s32 character_type) {
    return in_party_session && character_type == WhiteGhostCharacterType;
}

bool SettingIsOn(const char* setting) {
    return setting != nullptr && std::string_view{setting} == "1";
}

bool ShouldPlayGuestAsHost(bool switched_on, bool in_party_session, s32 character_type) {
    return switched_on && in_party_session && character_type == WhiteGhostCharacterType;
}

bool RestoresBloodstainMark(bool party_guest, bool mark_before, bool mark_now, s32 echoes,
                            u32 block) {
    return party_guest && mark_before && !mark_now && echoes > 0 && block != 0xFFFFFFFF;
}

bool LevelSyncsGuest(bool in_party_session, bool above_host_band) {
    return above_host_band && !in_party_session;
}

bool NpcTurnsIntoWanderGhost(bool in_party_session, bool param_says_wander_ghost) {
    return param_says_wander_ghost && !in_party_session;
}

const std::array<u8, GuestItemPickupAllowedOffset + 8>& SoloMapItemStandIn() {
    static const auto stand_in = [] {
        std::array<u8, GuestItemPickupAllowedOffset + 8> bytes{};
        constexpr s32 no_session = -1;
        std::memcpy(bytes.data() + JoinedSessionOffset, &no_session, sizeof(no_session));
        bytes[GuestItemPickupAllowedOffset] = 1;
        return bytes;
    }();
    return stand_in;
}

bool DefersItemLotAward(bool party_configured, bool summoned_guest, u64 return_offset) {
    return party_configured && summoned_guest && return_offset == ClientItemLotAwardReturn;
}

bool MenuSeesAClient(bool party_configured, bool summoned_client) {
    return summoned_client && !party_configured;
}

bool GuestHandlesMapItemsAlone(bool party_configured) {
    return party_configured;
}

std::array<u8, 8> RetargetRipLoad(std::span<const u8, 8> original, u64 address, u64 target) {
    std::array<u8, 8> patched{};
    std::copy(original.begin(), original.end(), patched.begin());
    const s32 displacement =
        static_cast<s32>(static_cast<s64>(target) - static_cast<s64>(address + patched.size()));
    std::memcpy(patched.data() + 4, &displacement, sizeof(displacement));
    return patched;
}

u32 MapItemPacketHeaderSeen(u32 header, bool party_configured) {
    return party_configured ? 0 : header;
}

u32 PackedMapOfWarpPoint(s32 warp_point) {
    const u32 area = static_cast<u32>(warp_point / 100000);
    const u32 block = static_cast<u32>(warp_point / 10000 % 10);
    return area << 24 | (block & 0xFF) << 16;
}

bool IsHeadstoneTravelFlag(s32 flag) {
    return flag >= 72102000 && flag < 72104000;
}

s32 HeadstoneWarpPoint(s32 flag) {
    return flag % 10000 / 10 * 10000 + 2950 + flag % 10;
}

bool GuestMakesItsOwnLampTrip(bool in_party_session, PartyRole role) {
    return in_party_session && role == PartyRole::Guest;
}

std::vector<MultiplayerWall> MultiplayerWallsOn(u32 packed_map) {
    // InitializeEvent(_, 7600, object, sfx) in m22-m35's event files (01.09).
    static constexpr std::array<s32, 18> Walls{
        2201999, 2301999, 2401999, 2401998, 2401997, 2401996, 2401995, 2411999, 2411998,
        2411997, 2421999, 2421998, 2701999, 2701998, 2801999, 2801998, 3201999, 3501990,
    };
    std::vector<MultiplayerWall> walls;
    for (const s32 object : Walls) {
        if (PackedMapOfWarpPoint(object) == packed_map) {
            walls.push_back(MultiplayerWall{object, object + 2000});
        }
    }
    return walls;
}

bool IsParamOfType(std::span<const u8> param, std::string_view type) {
    if (param.size() < 0x0C + type.size() + 1) {
        return false;
    }
    return std::equal(type.begin(), type.end(), param.begin() + 0x0C) &&
           param[0x0C + type.size()] == 0;
}

std::optional<u64> FindParamRow(std::span<const u8> param, s32 id) {
    if (param.size() < 0x40) {
        return std::nullopt;
    }
    u16 rows = 0;
    std::memcpy(&rows, param.data() + 0x0A, sizeof(rows));
    if (param.size() < 0x40 + std::size_t{rows} * 24) {
        return std::nullopt;
    }
    std::size_t low = 0;
    std::size_t high = rows;
    while (low < high) {
        const std::size_t middle = (low + high) / 2;
        s32 row_id = 0;
        std::memcpy(&row_id, param.data() + 0x40 + middle * 24, sizeof(row_id));
        if (row_id == id) {
            u64 offset = 0;
            std::memcpy(&offset, param.data() + 0x40 + middle * 24 + 8, sizeof(offset));
            return offset;
        }
        if (row_id < id) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return std::nullopt;
}

bool IsPartySession(bool party_configured, s32 matching_state) {
    return party_configured &&
           (matching_state == MatchingStateHosting || matching_state == MatchingStateSummonedGuest);
}

void LampRestState::Rest(s32 rested_lamp, const LampPosition& at) {
    lamp = rested_lamp;
    rested_at = at;
    joined = false;
}

LampUse LampRestState::Use(s32 used_lamp, const LampPosition& at) {
    if (lamp == used_lamp) {
        lamp.reset();
        return LampUse::Travel;
    }
    Rest(used_lamp, at);
    return LampUse::Rest;
}

namespace {
float DistanceSquared(const LampPosition& a, const LampPosition& b) {
    float sum = 0.0f;
    for (std::size_t axis = 0; axis < a.size(); ++axis) {
        sum += (a[axis] - b[axis]) * (a[axis] - b[axis]);
    }
    return sum;
}

std::string FormatCoordinate(float value) {
    std::array<char, 32> text{};
    const auto [end, error] = std::to_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} ? std::string{text.data(), end} : std::string{"0"};
}

std::optional<float> ParseCoordinate(std::string_view text) {
    float value = 0.0f;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}
} // namespace

bool LampRestState::JoinNearby(std::span<const LampRest> board,
                               const std::optional<LampPosition>& player) {
    if (lamp || !player) {
        return false;
    }
    for (const LampRest& rest : board) {
        if (rest.lamp != NotResting && rest.at &&
            DistanceSquared(*rest.at, *player) <= JoinDistance * JoinDistance) {
            Rest(rest.lamp, *rest.at);
            joined = true;
            return true;
        }
    }
    return false;
}

std::optional<LampPosition> LampRestState::JoinablePosition() const {
    return lamp && !joined ? std::optional{rested_at} : std::nullopt;
}

u64 RestoreScriptFlagsAfterRespawn(u64 flags_before, u64 flags_after, s32 hp_before) {
    if (hp_before <= 0) {
        return flags_after;
    }
    return (flags_after & ~ScriptCharacterFlags) | (flags_before & ScriptCharacterFlags);
}

bool RespawnsAtPartyRest(s32 hp, bool alive_this_visit) {
    return hp <= 0 && alive_this_visit;
}

bool KeepDisabledAfterRespawn(bool disabled_before, s32 hp_before) {
    return disabled_before && hp_before > 0;
}

std::optional<s32> LampRestState::Tick(const std::optional<LampPosition>& player,
                                       bool in_party_session) {
    if (!in_party_session) {
        lamp.reset();
    } else if (lamp && player) {
        if (DistanceSquared(*player, rested_at) > LeaveDistance * LeaveDistance) {
            lamp.reset();
        }
    }
    return lamp;
}

std::string EncodeLampRestUpdate(std::string_view member, s32 lamp,
                                 const std::optional<LampPosition>& at) {
    std::string update = std::string{member} + ' ' + std::to_string(lamp);
    if (lamp != NotResting && at) {
        for (const float coordinate : *at) {
            update += ' ' + FormatCoordinate(coordinate);
        }
    }
    return update;
}

std::optional<std::vector<LampRest>> ParseLampRestBoard(std::string_view body) {
    std::vector<LampRest> board;
    while (!body.empty()) {
        const std::size_t end = body.find('\n');
        std::string_view line = body.substr(0, end);
        body.remove_prefix(end == std::string_view::npos ? body.size() : end + 1);
        std::vector<std::string_view> fields;
        while (!line.empty()) {
            const std::size_t space = line.find(' ');
            fields.push_back(line.substr(0, space));
            line.remove_prefix(space == std::string_view::npos ? line.size() : space + 1);
        }
        if (fields.empty()) {
            continue;
        }
        if ((fields.size() != 2 && fields.size() != 5) || fields[0].empty()) {
            return std::nullopt;
        }
        LampRest rest{.member = std::string{fields[0]}};
        const auto [lamp_end, error] =
            std::from_chars(fields[1].data(), fields[1].data() + fields[1].size(), rest.lamp);
        if (error != std::errc{} || lamp_end != fields[1].data() + fields[1].size()) {
            return std::nullopt;
        }
        if (fields.size() == 5) {
            LampPosition at{};
            for (std::size_t axis = 0; axis < at.size(); ++axis) {
                const auto coordinate = ParseCoordinate(fields[2 + axis]);
                if (!coordinate) {
                    return std::nullopt;
                }
                at[axis] = *coordinate;
            }
            rest.at = at;
        }
        board.push_back(std::move(rest));
    }
    return board;
}

bool LampRestCoordinator::Tick(std::optional<s32> resting_lamp, s32 session_members,
                               std::span<const LampRest> board) {
    bool everyone_resting = false;
    if (resting_lamp && session_members > 1) {
        const auto resting_here = std::ranges::count_if(
            board, [&](const LampRest& rest) { return rest.lamp == *resting_lamp; });
        everyone_resting = resting_here >= session_members;
    }
    const bool refresh = everyone_resting && !everyone_was_resting;
    everyone_was_resting = everyone_resting;
    return refresh;
}

bool RefreshAtLamp(const LampRestCalls& calls) {
    if (!calls.request_full_recover || !calls.clamp_restock_item || !calls.restock_from_storage ||
        !calls.request_world_respawn) {
        return false;
    }
    calls.request_full_recover();
    calls.clamp_restock_item();
    calls.restock_from_storage();
    calls.request_world_respawn();
    return true;
}

PartyAction PartyController::Tick(const PartyObservation& observation, Clock::time_point now) {
    if (!observation.player_valid || !observation.map_usable || observation.stage_transition) {
        stable_ticks = 0;
        return PartyAction::Wait;
    }
    if (!observation.matching_idle || observation.search_effect_active ||
        observation.white_phantom || observation.waiting_to_meet) {
        // In (or joining) a session, or our search already took: the next time we are back home
        // alone is a fresh start, so forget the old cooldown.
        stable_ticks = 0;
        last_search.reset();
        return PartyAction::Wait;
    }
    if (++stable_ticks < StableTicksBeforeSearch) {
        return PartyAction::Wait;
    }
    if (last_search && now - *last_search < RetryDelay) {
        return PartyAction::Wait;
    }
    last_search = now;
    stable_ticks = 0;
    return PartyAction::StartSearch;
}

} // namespace Core::Bloodborne
