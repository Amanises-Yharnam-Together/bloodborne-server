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
    return matching_state == MatchingStateHosting;
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
    // 7MMM01LL: map code MMM (241 = m24_01), lamp LL.
    return flag >= 70000000 && flag < 80000000 && flag % 10000 / 100 == 1;
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
    if (!observation.matching_idle || observation.search_effect_active) {
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
