// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <bit>
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
