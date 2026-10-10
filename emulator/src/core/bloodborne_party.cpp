// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstring>

#include <fmt/format.h>

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
        // BlockClear2: the white phantom's SetLoadWait (event state +0x8D), which turns the
        // action buttons off (SprjActionButtonMan +0x52) until the trip home it no longer takes.
        {0x01384D03,
         {0x41, 0xC6, 0x86, 0x8D, 0x00, 0x00, 0x00, 0x01},
         {0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00}},
        // SoloBlockClear (BlockClear2_1 outside a friends party, for a live player): return
        // after the boss flags instead of StopRequest and the tail jump to LeaveSession.
        {0x01385A4A,
         {0x48, 0x8B, 0x05, 0x27, 0x0C, 0x1D, 0x04, 0x48, 0x8B, 0x98, 0xF8, 0x16, 0x00, 0x00},
         {0x48, 0x83, 0xC4, 0x08, 0x5B, 0x41, 0x5E, 0x41, 0x5F, 0x5D, 0xC3, 0x0F, 0x1F, 0x00}},
    };
    return patches;
}

bool StartsTripOnWorldReload(u64 caller_offset, bool trip_active, bool in_party_session) {
    return IsWorldReloadStopRequest(caller_offset) && !trip_active && in_party_session;
}

bool IsWorldReloadStopRequest(u64 caller_offset) {
    constexpr u64 WorldLoadStopRequestReturn = 0x0193C65C;
    return caller_offset == WorldLoadStopRequestReturn;
}

const std::vector<CodePatch>& JoinAnywherePatches() {
    static const std::vector<CodePatch> patches{
        // `call EventFlagIsSet` -> `xor eax, eax` + a 3-byte nop; the `xor al, 1` after it
        // then says the area is open.
        {0x0191A8B5, {0xE8, 0x36, 0xF3, 0xA7, 0xFF}, {0x31, 0xC0, 0x0F, 0x1F, 0x00}},
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
    // 7MMM01LL: map code MMM (241 = m24_01), lamp LL. Only the maps after the Hunter's Dream (210)
    // up to the last ones (m36): the Dream has no lamp to travel from (the Doll's dialog sets
    // 721001xx), and the flags of no map are the talks' own (Gilbert's talk sets 70000140, many
    // NPCs 799001xx).
    constexpr s32 HuntersDreamMapCode = 210;
    constexpr s32 PastTheLastMapCode = 400;
    const s32 map_code = flag / 10000 % 1000;
    return flag >= 70000000 && flag < 80000000 && flag % 10000 / 100 == 1 &&
           map_code > HuntersDreamMapCode && map_code < PastTheLastMapCode;
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

bool NoOneElseOnMap(std::span<const MemberMap> board, std::string_view self, u32 map) {
    if (map == 0) {
        return false;
    }
    return std::ranges::none_of(
        board, [&](const MemberMap& entry) { return entry.member != self && entry.map == map; });
}

namespace {
bool IsOnlineIdChar(u8 c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' ||
           c == '_';
}
} // namespace

std::string BoardMemberOfOnlineId(std::string_view online_id) {
    std::string member;
    for (const char c : online_id) {
        member += fmt::format("{:02x}", static_cast<u8>(c));
    }
    return member;
}

std::string OnlineIdOfBoardMember(std::string_view member) {
    const auto nibble = [](char c) -> s32 {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        return -1;
    };
    if (member.empty() || member.size() % 2 != 0) {
        return {};
    }
    std::string online_id;
    for (size_t index = 0; index < member.size(); index += 2) {
        const s32 high = nibble(member[index]);
        const s32 low = nibble(member[index + 1]);
        if (high < 0 || low < 0 || !IsOnlineIdChar(static_cast<u8>(high * 16 + low))) {
            return {};
        }
        online_id += static_cast<char>(high * 16 + low);
    }
    return online_id;
}

std::optional<std::string> FindOnlineIdIn(std::span<const u8> bytes,
                                          std::span<const std::string> candidates) {
    std::optional<std::string> found;
    for (const std::string& candidate : candidates) {
        if (candidate.empty() || candidate.size() >= bytes.size()) {
            continue;
        }
        for (size_t at = 0; at + candidate.size() < bytes.size(); ++at) {
            if (!std::equal(candidate.begin(), candidate.end(), bytes.begin() + at) ||
                bytes[at + candidate.size()] != 0 || (at > 0 && IsOnlineIdChar(bytes[at - 1]))) {
                continue;
            }
            if (found && *found != candidate) {
                return std::nullopt; // two members' names: not this member's object
            }
            found = candidate;
            break;
        }
    }
    return found;
}

RecreatePlan PlanRecreate(std::span<const std::optional<std::string>> members,
                          std::span<const MemberMap> board, std::string_view self, u32 map,
                          const std::set<std::string>& handled) {
    RecreatePlan plan;
    if (std::ranges::any_of(members, [](const auto& member) { return !member.has_value(); })) {
        if (EveryoneElseOnMap(board, self, map)) {
            for (size_t index = 0; index < members.size(); ++index) {
                plan.now.push_back(index);
            }
            plan.done = true;
        }
        return plan;
    }
    plan.done = true;
    for (size_t index = 0; index < members.size(); ++index) {
        const std::string& name = *members[index];
        if (handled.contains(name)) {
            continue;
        }
        const auto entry = std::ranges::find_if(
            board, [&](const MemberMap& member) { return member.member == name; });
        if (entry != board.end() && entry->map == map) {
            plan.now.push_back(index);
        } else {
            plan.done = false;
        }
    }
    return plan;
}

bool HostIsHere(std::optional<u32> host_map, std::span<const MemberMap> board,
                std::string_view self, u32 map) {
    return host_map && *host_map == map && map != 0 &&
           std::ranges::any_of(board, [&](const MemberMap& entry) {
               return entry.member != self && entry.map == map;
           });
}

bool HostAway(std::optional<u32> host_map, std::span<const MemberMap> board, std::string_view self,
              u32 map) {
    return host_map ? !HostIsHere(host_map, board, self, map) : NoOneElseOnMap(board, self, map);
}

bool SomeoneElsewhere(std::span<const MemberMap> board, std::string_view self, u32 map) {
    return std::ranges::any_of(
        board, [&](const MemberMap& entry) { return entry.member != self && entry.map != map; });
}

bool RoomForMore(bool hosting, size_t in_session, size_t party_online) {
    constexpr size_t MaxPlayers = 3; // the host and two cooperators
    return hosting && in_session < MaxPlayers && party_online > in_session;
}

std::optional<size_t> PickSavedJoinTask(std::span<const SavedJoinTask> saved, s32 kind,
                                        u64 identity) {
    std::optional<size_t> only;
    size_t of_kind = 0;
    for (size_t index = 0; index < saved.size(); ++index) {
        if (saved[index].kind != kind) {
            continue;
        }
        if (saved[index].identity == identity) {
            return index;
        }
        ++of_kind;
        only = index;
    }
    return of_kind == 1 ? only : std::nullopt;
}

std::vector<SavedJoinTask> MergeSavedJoinTasks(std::span<const SavedJoinTask> before,
                                               std::span<const SavedJoinTask> found) {
    std::vector<SavedJoinTask> merged{found.begin(), found.end()};
    for (const SavedJoinTask& task : before) {
        if (std::ranges::none_of(
                found, [&](const SavedJoinTask& now) { return now.identity == task.identity; })) {
            merged.push_back(task);
        }
    }
    return merged;
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
    const bool boss_fog = IsBossFogEntryFlag(flag) || IsBossFogEntryFlag(flag - 1);
    // The bell's bookkeeping (common events 9181-9198), not progress.
    const bool bell = flag >= 9180 && flag < 9200;
    return flag > 0 && !death_or_multiplayer && !trip && !boss_fog && !bell &&
           !IsMapPickupFlag(flag);
}

std::optional<std::pair<s32, s32>> CoopHpScale(s32 unscaled_max, s32 max, s32 hp,
                                               std::span<const s32> effects) {
    // SpEffectParam maxHpRate of the co-op boss effects 7500, 7501, 7502.
    static constexpr std::array<float, 3> Rates{1.5f, 2.0f, 2.5f};
    if (unscaled_max <= 0 || hp <= 0 || max != unscaled_max) {
        return std::nullopt;
    }
    for (const s32 effect : effects) {
        if (effect >= 7500 && effect < 7500 + static_cast<s32>(Rates.size())) {
            const float rate = Rates[effect - 7500];
            return std::pair{static_cast<s32>(std::lround(static_cast<float>(max) * rate)),
                             static_cast<s32>(std::lround(static_cast<float>(hp) * rate))};
        }
    }
    return std::nullopt;
}

bool SetsGuestWorldChangesOnLoad(bool in_session, bool on_trip) {
    return !in_session || on_trip;
}

std::string FormatCrashLine(u32 code, u64 address, u64 image_base, u64 image_size,
                            std::string_view thread) {
    const bool in_game = address >= image_base && address - image_base < image_size;
    return fmt::format("crash: exception {:#x} at {:#x} ({}) thread {}", code, address,
                       in_game ? fmt::format("eboot+{:#x}", address - image_base) : "emulator",
                       thread.empty() ? "?" : thread);
}

std::string FormatGameAddresses(std::span<const u64> words, u64 image_base, u64 image_size,
                                size_t max) {
    std::string text;
    size_t found = 0;
    for (const u64 word : words) {
        if (found == max) {
            break;
        }
        if (word >= image_base && word - image_base < image_size) {
            text += fmt::format(" eboot+{:#x}", word - image_base);
            ++found;
        }
    }
    return text;
}

bool IsBossFogEntryFlag(s32 flag) {
    // The flag each boss fog's host event sets and its phantom event waited for (found by
    // party_boss_events.py's open_boss_fog_for_guests in the 01.09 map event files).
    static constexpr std::array<s32, 21> Flags{
        12104800, 12104850, 12204800, 12304700, 12304800, 12404800, 12414700,
        12414800, 12424700, 12424800, 12504800, 12604800, 12604850, 12704800,
        12804800, 13304800, 13404808, 13404858, 13504808, 13504858, 13604808,
    };
    return std::ranges::find(Flags, flag) != Flags.end();
}

bool KeepsLocalFlag(bool party_configured, s32 flag) {
    constexpr s32 FirstMessengerGift = 12101020;
    constexpr s32 LastMessengerGift = 12101029;
    constexpr s32 FirstMessengerPick = 72101000;
    constexpr s32 LastMessengerPick = 72101099;
    const bool gift = flag >= FirstMessengerGift && flag <= LastMessengerGift;
    const bool pick = flag >= FirstMessengerPick && flag <= LastMessengerPick;
    return party_configured && (gift || pick);
}

std::optional<s32> TracedFlagSetting(const char* setting) {
    if (setting == nullptr) {
        return std::nullopt;
    }
    const std::string_view text{setting};
    s32 flag = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), flag);
    if (error != std::errc{} || end != text.data() + text.size() || flag <= 0) {
        return std::nullopt;
    }
    return flag;
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

DeathRespawn ChooseDeathRespawn(s32 point, bool in_loaded_block, bool arriving_from_trip,
                                bool reset) {
    if (point <= 0) {
        return DeathRespawn::StayPut;
    }
    if (reset && !arriving_from_trip) {
        return DeathRespawn::Travel;
    }
    if (in_loaded_block) {
        return DeathRespawn::MoveToPoint;
    }
    return arriving_from_trip ? DeathRespawn::StayPut : DeathRespawn::Travel;
}

std::vector<std::pair<s32, bool>> FlagsToShare(const FlagChanges& changes) {
    std::vector<std::pair<s32, bool>> shared;
    for (const s32 flag : changes.set) {
        if (SharedWithParty(flag)) {
            shared.emplace_back(flag, true);
        }
    }
    return shared;
}

bool SharedWithParty(s32 flag) {
    return LampOfActiveFlag(flag).has_value() || LampOfActiveFlag(flag - 1).has_value() ||
           IsBossFogEntryFlag(flag);
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
            (state == '0' || state == '1') && CarriesToGuestWorld(flag)) {
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

std::string EncodeResumePoint(const ResumePoint& point) {
    return fmt::format("{} {} {} {} {}", point.map, point.position[0], point.position[1],
                       point.position[2], point.progress);
}

std::optional<ResumePoint> ParseResumePoint(std::string_view text) {
    ResumePoint point;
    const char* cursor = text.data();
    const char* end = text.data() + text.size();
    const auto [map_end, map_error] = std::from_chars(cursor, end, point.map);
    if (map_error != std::errc{}) {
        return std::nullopt;
    }
    cursor = map_end;
    for (float& coordinate : point.position) {
        if (cursor == end || *cursor != ' ') {
            return std::nullopt;
        }
        ++cursor;
        const auto [next, error] = std::from_chars(cursor, end, coordinate);
        if (error != std::errc{}) {
            return std::nullopt;
        }
        cursor = next;
    }
    if (cursor == end || *cursor != ' ') {
        return std::nullopt;
    }
    ++cursor;
    const auto [progress_end, progress_error] = std::from_chars(cursor, end, point.progress);
    if (progress_error != std::errc{}) {
        return std::nullopt;
    }
    cursor = progress_end;
    while (cursor != end && (*cursor == '\n' || *cursor == '\r' || *cursor == ' ')) {
        ++cursor;
    }
    if (cursor != end) {
        return std::nullopt;
    }
    return point;
}

s32 MapWarpPoint(u32 packed_map) {
    constexpr u32 HuntersDream = 0x15000000;
    constexpr s32 HuntersDreamHeadstones = 2102962;
    if (packed_map == HuntersDream) {
        return HuntersDreamHeadstones;
    }
    const s32 area = static_cast<s32>((packed_map >> 24) & 0xFF);
    const s32 block = static_cast<s32>((packed_map >> 16) & 0xFF);
    return (area * 10 + block) * 10000 + 2950;
}

ResumeStep ChooseResumeStep(const ResumePoint& point, u32 map, const LampPosition& at, s32 progress,
                            u32 loaded_map) {
    if (point.map == 0 || (loaded_map != 0 && loaded_map != map)) {
        return {};
    }
    // Event flags only add up as a character plays; far fewer set means another character.
    constexpr s32 ProgressSlack = 20;
    if (progress + ProgressSlack < point.progress) {
        return {ResumeStep::Kind::Forget, 0};
    }
    if (point.map != map) {
        return {ResumeStep::Kind::Travel, MapWarpPoint(point.map)};
    }
    const float dx = point.position[0] - at[0];
    const float dy = point.position[1] - at[1];
    const float dz = point.position[2] - at[2];
    if (dx * dx + dy * dy + dz * dz <= 5.0f * 5.0f) {
        return {};
    }
    return {ResumeStep::Kind::Move, 0};
}

s32 MapFlagBase(u32 packed_map) {
    const s32 area = static_cast<s32>((packed_map >> 24) & 0xFF);
    const s32 block = static_cast<s32>((packed_map >> 16) & 0xFF);
    return (1000 + area * 10 + block) * MapFlagCount;
}

bool TakesHostMapFlag(s32 flag) {
    const s32 prefix = flag / MapFlagCount;
    const bool map_flag = prefix >= 1200 && prefix < 1400;
    const bool boss_fog = IsBossFogEntryFlag(flag) || IsBossFogEntryFlag(flag - 1);
    return map_flag && !boss_fog && !IsMapPickupFlag(flag) && !KeepsLocalFlag(true, flag);
}

std::string EncodeMapStateUpdate(std::string_view member, u32 map, std::span<const u8> bits) {
    std::string body = std::string{member} + ' ' + std::to_string(map);
    if (!bits.empty()) {
        body += ' ';
        for (const u8 byte : bits) {
            body += fmt::format("{:02x}", byte);
        }
    }
    return body;
}

std::optional<std::vector<MemberMapState>> ParseMapStateBoard(std::string_view body) {
    std::vector<MemberMapState> states;
    const auto nibble = [](char c) -> s32 {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        return -1;
    };
    while (!body.empty()) {
        const auto end = body.find('\n');
        const std::string_view line = body.substr(0, end);
        body = end == std::string_view::npos ? std::string_view{} : body.substr(end + 1);
        if (line.empty()) {
            continue;
        }
        const auto space = line.find(' ');
        if (space == std::string_view::npos) {
            return std::nullopt;
        }
        MemberMapState state;
        const auto [map_end, error] = std::from_chars(line.data(), line.data() + space, state.map);
        const std::string_view hex = line.substr(space + 1);
        if (error != std::errc{} || map_end != line.data() + space || hex.size() % 2 != 0) {
            return std::nullopt;
        }
        for (size_t index = 0; index < hex.size(); index += 2) {
            const s32 high = nibble(hex[index]);
            const s32 low = nibble(hex[index + 1]);
            if (high < 0 || low < 0) {
                return std::nullopt;
            }
            state.bits.push_back(static_cast<u8>(high * 16 + low));
        }
        states.push_back(std::move(state));
    }
    return states;
}

u64 VisitTracker::Update(u32 new_map, u64 new_player, bool in_session, u64 new_trips) {
    const bool reloaded_alone = new_player != player && !in_session;
    if (new_map != map || new_trips != trips || reloaded_alone || generation == 0) {
        ++generation;
    }
    map = new_map;
    player = new_player;
    trips = new_trips;
    return (u64{map} << 32) | generation;
}

namespace {
template <typename T>
void AppendLittleEndian(std::vector<u8>& bytes, T value) {
    for (size_t index = 0; index < sizeof(T); ++index) {
        bytes.push_back(static_cast<u8>(static_cast<u64>(value) >> (8 * index)));
    }
}

template <typename T>
T ReadLittleEndian(std::span<const u8> bytes, size_t offset) {
    u64 value = 0;
    for (size_t index = 0; index < sizeof(T); ++index) {
        value |= u64{bytes[offset + index]} << (8 * index);
    }
    return static_cast<T>(value);
}
} // namespace

std::vector<u8> EncodeEnemyHp(u64 visit, std::span<const EnemyHp> enemies) {
    std::vector<u8> bytes;
    bytes.reserve(8 + enemies.size() * 12);
    AppendLittleEndian(bytes, visit);
    for (const EnemyHp& enemy : enemies) {
        AppendLittleEndian(bytes, enemy.handle);
        AppendLittleEndian(bytes, static_cast<u32>(enemy.hp));
        AppendLittleEndian(bytes, static_cast<u32>(enemy.max));
    }
    return bytes;
}

std::optional<EnemyHpReport> DecodeEnemyHp(std::span<const u8> bytes) {
    if (bytes.size() < 8 || (bytes.size() - 8) % 12 != 0) {
        return std::nullopt;
    }
    EnemyHpReport report{ReadLittleEndian<u64>(bytes, 0), {}};
    for (size_t offset = 8; offset < bytes.size(); offset += 12) {
        report.enemies.push_back({ReadLittleEndian<u32>(bytes, offset),
                                  static_cast<s32>(ReadLittleEndian<u32>(bytes, offset + 4)),
                                  static_cast<s32>(ReadLittleEndian<u32>(bytes, offset + 8))});
    }
    return report;
}

std::optional<s32> LowestSharedHp(u32 handle, s32 hp, s32 max, u32 map,
                                  std::span<const MemberMapState> others) {
    std::optional<s32> lowest;
    for (const MemberMapState& state : others) {
        // Any block of the same area (packed map 0xAABBCCDD, area AA): a member who joined in
        // place stands on another block of it (Cathedral Ward next to Central Yharnam), and the
        // blocks give their enemies distinct handles; another area may reuse a handle.
        const auto report =
            (state.map >> 24) == (map >> 24) ? DecodeEnemyHp(state.bits) : std::nullopt;
        if (!report) {
            continue;
        }
        for (const EnemyHp& enemy : report->enemies) {
            if (enemy.handle != handle) {
                continue;
            }
            if (const auto target = SharedEnemyHp(lowest.value_or(hp), max, enemy.hp, enemy.max)) {
                lowest = target;
            }
        }
    }
    return lowest;
}

std::optional<s32> SharedEnemyHp(s32 hp, s32 max, s32 other_hp, s32 other_max) {
    if (hp <= 0 || max <= 0 || other_max <= 0 || other_hp < 0) {
        return std::nullopt;
    }
    // Rounded up: a sliver left on either side is still alive here.
    const s64 scaled = (s64{other_hp} * max + other_max - 1) / other_max;
    const s32 target = static_cast<s32>(std::max<s64>(scaled, 1));
    return target < hp ? std::optional<s32>{target} : std::nullopt;
}

bool WatchesKills(bool sharing, bool trip_active, bool loading) {
    return sharing && !trip_active && !loading;
}

bool CharacterSettles(s64 now_ms, s64 world_changed_ms, std::optional<s64> owner_changed_ms) {
    const s64 settle =
        std::chrono::duration_cast<std::chrono::milliseconds>(EnemyOwnerChangeSettles).count();
    return now_ms - world_changed_ms < settle ||
           (owner_changed_ms && now_ms - *owner_changed_ms < settle);
}

bool IsRealDeath(u8 death_flags) {
    constexpr u8 PutDownByTheWorld = 0x20;
    return (death_flags & 0x05) != 0 && (death_flags & PutDownByTheWorld) == 0;
}

std::vector<std::pair<s32, bool>> VisitKills::Observe(u64 visit,
                                                      std::span<const SeenCharacter> characters) {
    std::vector<std::pair<s32, bool>> changes;
    if (visit != current) {
        for (const u32 handle : killed) {
            changes.emplace_back(static_cast<s32>(handle), false);
        }
        killed.clear();
        seen.clear();
        current = visit;
    }
    for (const auto& [handle, now, instance, settling] : characters) {
        const auto [it, added] = seen.try_emplace(handle, Seen{now, instance});
        // The same object seen alive, now dead: a world that loaded brings new objects, some
        // dead already (in a save that killed them for good), and one that settles after a
        // takeover puts such ones down where they stand.
        if (!added && !settling && it->second.instance == instance && it->second.hp > 0 &&
            now <= 0 && std::ranges::find(killed, handle) == killed.end()) {
            killed.push_back(handle);
            changes.emplace_back(static_cast<s32>(handle), true);
        }
        it->second = {now, instance};
    }
    return changes;
}

bool SharesWithParty(bool party_configured, bool player_loaded) {
    return party_configured && player_loaded;
}

bool SharesKillEchoes(u64 caller_offset, bool someone_elsewhere) {
    constexpr u64 EnemyKillReward = 0x018FC99B;
    constexpr u64 BossKillReward = 0x01385263;
    return someone_elsewhere &&
           (caller_offset == EnemyKillReward || caller_offset == BossKillReward);
}

bool GrantsKillEchoes(u32 kill_map, u32 my_map) {
    return kill_map == 0 || kill_map != my_map;
}

std::string EncodeEchoUpdate(std::string_view member, s64 since,
                             std::span<const KillEchoes> echoes) {
    std::string body = std::string{member} + ' ' + std::to_string(since);
    for (const KillEchoes& kill : echoes) {
        body += ' ' + std::to_string(kill.amount);
        if (kill.map != 0) {
            body += ':' + std::to_string(kill.map);
        }
    }
    return body;
}

std::optional<KillBoardAnswer> ParseKillBoard(std::string_view body) {
    KillBoardAnswer answer;
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
        s32 handle = 0;
        const auto [handle_end, handle_error] =
            std::from_chars(line.data(), line.data() + std::min(space, line.size()), handle);
        if (space == std::string_view::npos || handle_error != std::errc{} ||
            handle_end != line.data() + space || line.size() < space + 2 ||
            (line[space + 1] != '0' && line[space + 1] != '1') ||
            (line.size() > space + 2 && line[space + 2] != ' ')) {
            return std::nullopt;
        }
        const std::string_view member =
            line.size() > space + 3 ? line.substr(space + 3) : std::string_view{};
        answer.changes.push_back(KillChange{handle, line[space + 1] == '1', std::string{member}});
    }
    return answer;
}

void PartyKills::Apply(std::span<const KillChange> changes) {
    for (const KillChange& change : changes) {
        const u32 handle = static_cast<u32>(change.handle);
        if (change.killed) {
            by_member[change.member].insert(handle);
        } else if (const auto it = by_member.find(change.member); it != by_member.end()) {
            it->second.erase(handle);
        }
    }
}

bool PartyKills::Contains(u32 handle) const {
    return std::ranges::any_of(by_member,
                               [&](const auto& entry) { return entry.second.contains(handle); });
}

std::unordered_set<u32> PartyKills::All() const {
    std::unordered_set<u32> all;
    for (const auto& [member, handles] : by_member) {
        all.insert(handles.begin(), handles.end());
    }
    return all;
}

std::optional<EchoBoardAnswer> ParseEchoBoard(std::string_view body) {
    EchoBoardAnswer answer;
    bool first = true;
    while (!body.empty()) {
        const auto end = body.find('\n');
        const std::string_view line = body.substr(0, end);
        body = end == std::string_view::npos ? std::string_view{} : body.substr(end + 1);
        if (line.empty() && !first) {
            continue;
        }
        if (first) {
            const auto [latest_end, error] =
                std::from_chars(line.data(), line.data() + line.size(), answer.latest);
            if (error != std::errc{} || latest_end != line.data() + line.size()) {
                return std::nullopt;
            }
            first = false;
            continue;
        }
        const auto space = line.find(' ');
        const std::string_view amount_text = line.substr(0, space);
        s32 amount = 0;
        const auto [amount_end, error] =
            std::from_chars(amount_text.data(), amount_text.data() + amount_text.size(), amount);
        if (error != std::errc{} || amount_end != amount_text.data() + amount_text.size() ||
            amount <= 0) {
            return std::nullopt;
        }
        u32 map = 0;
        if (space != std::string_view::npos) {
            const std::string_view map_text = line.substr(space + 1);
            const auto [map_end, map_error] =
                std::from_chars(map_text.data(), map_text.data() + map_text.size(), map);
            if (map_error != std::errc{} || map_end != map_text.data() + map_text.size()) {
                return std::nullopt;
            }
        }
        answer.echoes.push_back(KillEchoes{amount, map});
    }
    if (first) {
        return std::nullopt;
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

bool ReopensClosedSession(bool party, s32 matching_state, u8 closing) {
    return party && matching_state == MatchingStateHosting && closing != 0;
}

s32 RemoteTaskKind(bool hosting) {
    return hosting ? RemoteGuestTask : RemoteHostTask;
}

s32 RebuiltTaskKind(bool hosting, std::span<const SavedJoinTask> saved, u64 identity) {
    if (hosting) {
        return RemoteGuestTask;
    }
    bool host_elsewhere = false;
    for (const SavedJoinTask& task : saved) {
        if (task.kind != RemoteHostTask && task.kind != RemoteGuestTask) {
            continue; // this player's own task
        }
        if (task.identity == identity) {
            return task.kind;
        }
        host_elsewhere |= task.kind == RemoteHostTask;
    }
    return host_elsewhere ? RemoteGuestTask : RemoteHostTask;
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

bool SkipsLeaveLimit(s32 character_type, size_t other_members) {
    return character_type != WhiteGhostCharacterType || other_members > 0;
}

PartyAction PartyController::Tick(const PartyObservation& observation, Clock::time_point now) {
    if (!observation.player_valid || !observation.map_usable || observation.stage_transition ||
        observation.boss_sequence) {
        stable_ticks = 0;
        return PartyAction::Wait;
    }
    const bool may_search =
        observation.matching_idle || (role == PartyRole::Host && observation.room_for_more);
    // A host with room whose bell stays on: on the port the effect outlasted the game's search
    // after the first guest arrived (rig join3 2026-10-09), so the bell rings again every
    // RetryDelay until the session is full.
    if (may_search && role == PartyRole::Host && observation.room_for_more &&
        observation.search_effect_active && !observation.white_phantom) {
        if (!ringing_since) {
            ringing_since = now;
            return PartyAction::Wait;
        }
        if (now - *ringing_since < RetryDelay) {
            return PartyAction::Wait;
        }
        ringing_since = now;
        last_search = now;
        return PartyAction::StartSearch;
    }
    ringing_since.reset();
    if (!may_search || observation.search_effect_active || observation.white_phantom) {
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

std::vector<AutoplayCommand> ParseAutoplayScript(std::string_view text) {
    std::vector<AutoplayCommand> script;
    while (!text.empty()) {
        const auto end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (const auto comment = line.find('#'); comment != std::string_view::npos) {
            line = line.substr(0, comment);
        }
        std::vector<std::string> words;
        size_t at = 0;
        while (at < line.size()) {
            const auto start = line.find_first_not_of(" \t\r", at);
            if (start == std::string_view::npos) {
                break;
            }
            const auto stop = line.find_first_of(" \t\r", start);
            words.emplace_back(line.substr(start, stop - start));
            at = stop == std::string_view::npos ? line.size() : stop;
        }
        if (!words.empty()) {
            script.push_back({words.front(), {words.begin() + 1, words.end()}});
        }
    }
    return script;
}

u32 AutoplayButton(std::string_view name) {
    static constexpr std::array<std::pair<std::string_view, u32>, 15> Buttons{{
        {"cross", 0x4000},
        {"circle", 0x2000},
        {"square", 0x8000},
        {"triangle", 0x1000},
        {"options", 0x8},
        {"up", 0x10},
        {"right", 0x20},
        {"down", 0x40},
        {"left", 0x80},
        {"l1", 0x400},
        {"r1", 0x800},
        {"l2", 0x100},
        {"r2", 0x200},
        {"l3", 0x2},
        {"r3", 0x4},
    }};
    for (const auto& [button, bit] : Buttons) {
        if (button == name) {
            return bit;
        }
    }
    return 0;
}

bool TitlePressesCross(u64 ms) {
    constexpr u64 Period = 2000;
    constexpr u64 Held = 150;
    return ms >= Period && ms % Period < Held;
}

std::optional<u32> ParseAutoplayTarget(std::string_view text) {
    if (text == "nearest") {
        return 0;
    }
    if (text == "self") {
        return AutoplaySelf;
    }
    if (text == "marked") {
        return AutoplayMarked;
    }
    if (text.starts_with("0x")) {
        text.remove_prefix(2);
    }
    u32 handle = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), handle, 16);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return handle;
}

std::optional<bool> CompareAutoplay(s64 value, std::string_view op, s64 limit) {
    if (op == "<") {
        return value < limit;
    }
    if (op == "<=") {
        return value <= limit;
    }
    if (op == "==") {
        return value == limit;
    }
    if (op == "!=") {
        return value != limit;
    }
    if (op == ">=") {
        return value >= limit;
    }
    if (op == ">") {
        return value > limit;
    }
    return std::nullopt;
}

AttackStance AttackStanceFor(float target_x, float target_z, u32 cycle) {
    static constexpr std::array<std::pair<float, float>, 4> Sides{
        {{0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, -1.0f}, {-1.0f, 0.0f}}};
    const auto [x, z] = Sides[cycle % Sides.size()];
    return {target_x + AttackDistance * x, target_z + AttackDistance * z};
}

float HeadingTowards(float x, float z, float to_x, float to_z) {
    return std::atan2(-(to_x - x), to_z - z);
}

u32 AttackCycleAt(double elapsed) {
    return elapsed <= 0.0 ? 0u : static_cast<u32>(elapsed / AttackCycleSeconds);
}

bool AttackHoldsR1(double elapsed) {
    const double into = std::max(elapsed, 0.0) - AttackCycleAt(elapsed) * AttackCycleSeconds;
    return into >= 0.3 && into < 0.5;
}

bool PartyDeathEnabled(const char* setting) {
    return SettingIsOn(setting);
}

namespace {
// Read from the 01.09 eboot: each is the address after a `call IsNoDead` (see the eboot tests).
constexpr std::array<NoDeadCaller, 12> NoDeadCallerTable{{
    // Hit damage (0x01A2A770: HP minus the hit, asked only when that is 0 or less), and its check
    // once the hit is applied, asked after every hit (0x01A2C03B skips only the kill).
    {0x01A2BF7D, NoDeadCheck::Lethal},
    {0x01A2C05F, NoDeadCheck::AfterHit},
    // Special effect HP changes (0x01A2FA40: poison, rapid poison, frenzy and the like).
    {0x01A2FB4A, NoDeadCheck::Lethal},
    {0x01A2FBE6, NoDeadCheck::Lethal},
    {0x01A2FDD4, NoDeadCheck::Lethal},
    // Fall damage (0x01A35A00, from the landing at 0x01516138), and its check afterwards.
    {0x01A35E10, NoDeadCheck::Lethal},
    {0x01A35E7E, NoDeadCheck::Lethal},
    // The game's kills: an event's ForceCharacterDeath (KillCharacterOffset), the character's
    // own kill (0x018BF760, from its vtable) and another (0x018CDE10).
    {0x018BF9EE, NoDeadCheck::Instant},
    {0x018BF86C, NoDeadCheck::Instant},
    {0x018CE5EE, NoDeadCheck::Instant},
    // HP set outright (0x018BCD40, which the revive and the event bindings use, and 0x018BC470).
    {0x018BCD9C, NoDeadCheck::Instant},
    {0x018BC5C5, NoDeadCheck::Instant},
}};

template <typename T>
std::optional<T> ParseNumber(std::string_view text, int base = 10) {
    T value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

std::vector<std::string_view> SplitFields(std::string_view line) {
    std::vector<std::string_view> fields;
    while (!line.empty()) {
        const std::size_t space = line.find(' ');
        fields.push_back(line.substr(0, space));
        line.remove_prefix(space == std::string_view::npos ? line.size() : space + 1);
    }
    return fields;
}
} // namespace

std::span<const NoDeadCaller> NoDeadCallers() {
    return NoDeadCallerTable;
}

NoDeadCheck ClassifyNoDeadCheck(u64 return_offset) {
    const auto caller =
        std::ranges::find(NoDeadCallerTable, return_offset,
                          [](const NoDeadCaller& entry) { return entry.return_offset; });
    return caller == NoDeadCallerTable.end() ? NoDeadCheck::Other : caller->check;
}

NoDeadCheck NoDeadCheckAt(NoDeadCheck check, s32 hp) {
    if (check != NoDeadCheck::AfterHit) {
        return check;
    }
    return hp <= 0 ? NoDeadCheck::Lethal : NoDeadCheck::Other;
}

bool KeepsPlayerAlive(NoDeadCheck check, bool armed, bool down, bool answer) {
    return (check == NoDeadCheck::Lethal && (armed || down)) || answer;
}

std::string_view VitalName(Vital vital) {
    switch (vital) {
    case Vital::Up:
        return "up";
    case Vital::Down:
        return "down";
    case Vital::Dead:
        return "dead";
    case Vital::Off:
        break;
    }
    return "off";
}

std::optional<Vital> ParseVital(std::string_view name) {
    for (const Vital vital : {Vital::Off, Vital::Up, Vital::Down, Vital::Dead}) {
        if (VitalName(vital) == name) {
            return vital;
        }
    }
    return std::nullopt;
}

u32 ReviveHitsNeeded(u32 downs) {
    return downs <= 1 ? 3 : downs == 2 ? 6 : 12;
}

s32 RevivedHp(s32 max) {
    return std::max(max / 2, 1);
}

u32 DownedState::GoDown(s64 now_ms) {
    if (up_since && now_ms - *up_since >= PenaltyResetMs) {
        downs = 0;
    }
    ++downs;
    vital = Vital::Down;
    down_id = next_id++;
    needed = ReviveHitsNeeded(downs);
    landed = 0;
    seen_hits = 0;
    return down_id;
}

bool DownedState::Hits(u32 total, s64 now_ms) {
    if (vital != Vital::Down || total <= seen_hits) {
        return false;
    }
    landed = std::min(needed, landed + (total - seen_hits));
    seen_hits = total;
    drain_at = now_ms + RefillDelayMs;
    return landed >= needed;
}

void DownedState::Tick(s64 now_ms) {
    if (vital != Vital::Down || landed == 0 || now_ms < drain_at) {
        return;
    }
    // One hit per step due since drain_at, worked out at once: as a loop, clang made it a 128-bit
    // division (__udivti3), which clang-cl's links on Windows lack.
    const s64 due = (now_ms - drain_at) / RefillStepMs + 1;
    const u32 drained = due < static_cast<s64>(landed) ? static_cast<u32>(due) : landed;
    landed -= drained;
    drain_at += static_cast<s64>(drained) * RefillStepMs;
}

void DownedState::GetUp(s64 now_ms) {
    vital = Vital::Up;
    landed = 0;
    up_since = now_ms;
}

void DownedState::Died() {
    vital = Vital::Dead;
    landed = 0;
}

void DownedState::Alive(s64 now_ms) {
    vital = Vital::Up;
    downs = 0;
    up_since = now_ms;
}

void DownedState::ClearPenalty() {
    downs = 0;
}

bool HoldToConfirm::Tick(bool held, s64 now_ms) {
    if (!held) {
        held_since.reset();
        fired = false;
        return false;
    }
    if (!held_since) {
        held_since = now_ms;
    }
    if (fired || now_ms - *held_since < hold_ms) {
        return false;
    }
    fired = true;
    return true;
}

void HoldToConfirm::Restart(bool held) {
    held_since.reset();
    fired = held;
}

float HoldToConfirm::Progress(s64 now_ms) const {
    if (!held_since || fired || hold_ms <= 0) {
        return 0.0f;
    }
    return std::clamp(static_cast<float>(now_ms - *held_since) / static_cast<float>(hold_ms), 0.0f,
                      1.0f);
}

std::string EncodeVitalsUpdate(const MemberVitals& self, std::span<const ReviveHitTotal> hits) {
    std::string update =
        fmt::format("{} {:x} {} {} {} {} {} {}", self.member, self.map, VitalName(self.vital),
                    self.hp, self.max, self.down, self.landed, self.needed);
    for (const float coordinate : self.at) {
        update += ' ' + FormatCoordinate(coordinate);
    }
    for (const ReviveHitTotal& total : hits) {
        update += fmt::format(" {}:{}:{}", total.target, total.down, total.hits);
    }
    return update;
}

std::optional<VitalsBoard> ParseVitalsBoard(std::string_view body) {
    VitalsBoard board;
    while (!body.empty()) {
        const std::size_t end = body.find('\n');
        const std::string_view line = body.substr(0, end);
        body.remove_prefix(end == std::string_view::npos ? body.size() : end + 1);
        const std::vector<std::string_view> fields = SplitFields(line);
        if (fields.empty()) {
            continue;
        }
        if (fields[0] == "wipe") {
            const auto id = fields.size() >= 4 ? ParseNumber<u32>(fields[1]) : std::nullopt;
            const auto map = fields.size() >= 4 ? ParseNumber<u32>(fields[2], 16) : std::nullopt;
            if (!id || !map) {
                return std::nullopt;
            }
            board.wipes.push_back(PartyWipe{*id, *map, {fields.begin() + 3, fields.end()}});
            continue;
        }
        if (fields.size() != 12 || fields[0].empty()) {
            return std::nullopt;
        }
        const auto map = ParseNumber<u32>(fields[1], 16);
        const auto vital = ParseVital(fields[2]);
        const auto hp = ParseNumber<s32>(fields[3]);
        const auto max = ParseNumber<s32>(fields[4]);
        const auto down = ParseNumber<u32>(fields[5]);
        const auto landed = ParseNumber<u32>(fields[6]);
        const auto needed = ParseNumber<u32>(fields[7]);
        const auto x = ParseCoordinate(fields[8]);
        const auto y = ParseCoordinate(fields[9]);
        const auto z = ParseCoordinate(fields[10]);
        const auto revive_hits = ParseNumber<u32>(fields[11]);
        if (!map || !vital || !hp || !max || !down || !landed || !needed || !x || !y || !z ||
            !revive_hits) {
            return std::nullopt;
        }
        board.members.push_back(MemberVitals{.member = std::string{fields[0]},
                                             .map = *map,
                                             .vital = *vital,
                                             .hp = *hp,
                                             .max = *max,
                                             .down = *down,
                                             .landed = *landed,
                                             .needed = *needed,
                                             .at = {*x, *y, *z},
                                             .revive_hits = *revive_hits});
    }
    return board;
}

std::optional<u32> WipeFor(const VitalsBoard& board, std::string_view self) {
    for (const PartyWipe& wipe : board.wipes) {
        if (std::ranges::find(wipe.members, self) != wipe.members.end()) {
            return wipe.id;
        }
    }
    return std::nullopt;
}

bool SomeoneElseStands(std::span<const MemberVitals> board, std::string_view self, u32 map) {
    return map != 0 && std::ranges::any_of(board, [&](const MemberVitals& member) {
               return member.member != self && member.map == map && member.vital == Vital::Up;
           });
}

bool ReviveHits::Press(std::string_view target, u32 down, s64 now_ms) {
    if (last_press && now_ms - *last_press < GapMs) {
        return false;
    }
    last_press = now_ms;
    const auto total = std::ranges::find(totals, target, &ReviveHitTotal::target);
    if (total == totals.end()) {
        totals.push_back(ReviveHitTotal{std::string{target}, down, 1});
    } else if (total->down != down) {
        *total = ReviveHitTotal{std::string{target}, down, 1};
    } else {
        ++total->hits;
    }
    return true;
}

std::vector<ReviveHitTotal> ReviveHits::Totals(std::span<const MemberVitals> board) {
    std::erase_if(totals, [&](const ReviveHitTotal& total) {
        return std::ranges::none_of(board, [&](const MemberVitals& member) {
            return member.member == total.target && member.vital == Vital::Down &&
                   member.down == total.down;
        });
    });
    return totals;
}

std::optional<DownedMember> ReviveTarget(std::span<const MemberVitals> board, std::string_view self,
                                         u32 map, const LampPosition& me) {
    std::optional<DownedMember> nearest;
    float nearest_distance = ReviveHits::Reach * ReviveHits::Reach;
    for (const MemberVitals& member : board) {
        if (member.member == self || member.map != map || member.vital != Vital::Down) {
            continue;
        }
        const float distance = DistanceSquared(member.at, me);
        if (distance <= nearest_distance) {
            nearest_distance = distance;
            nearest = DownedMember{member.member, member.down};
        }
    }
    return nearest;
}

PadInput DownedPad(const PadInput& pad) {
    return PadInput{.buttons = pad.buttons & PadOptions,
                    .sticks = {128, 128, pad.sticks[2], pad.sticks[3]}};
}

bool AttackPressed(u32 before, u32 now) {
    return (now & ~before & (PadR1 | PadR2)) != 0;
}

DownEnd EndOfDown(bool wiped, bool gave_up, bool revived, s64 board_age_ms) {
    if (wiped) {
        return DownEnd::Wipe;
    }
    if (gave_up) {
        return DownEnd::GiveUp;
    }
    if (revived) {
        return DownEnd::Revive;
    }
    return board_age_ms >= BoardLostMs ? DownEnd::BoardLost : DownEnd::None;
}

bool DeathResetsMap(bool wiped, bool someone_else_stands) {
    return wiped || !someone_else_stands;
}

PartyPanel BuildPartyPanel(std::span<const MemberVitals> board, std::string_view self, u32 map,
                           const std::optional<LampPosition>& me) {
    PartyPanel panel;
    const auto target = me ? ReviveTarget(board, self, map, *me) : std::nullopt;
    for (const MemberVitals& entry : board) {
        if (entry.member == self || entry.vital == Vital::Off) {
            continue;
        }
        if (target && entry.member == target->member) {
            panel.revive_target = static_cast<s32>(panel.members.size());
        }
        panel.members.push_back(PanelMember{
            .member = entry.member,
            .name = OnlineIdOfBoardMember(entry.member),
            .vital = entry.vital,
            .here = map != 0 && entry.map == map,
            .hp = entry.hp,
            .max = entry.max,
            .landed = entry.landed,
            .needed = entry.needed,
        });
    }
    return panel;
}

} // namespace Core::Bloodborne
