// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <chrono>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common/types.h"

namespace Core::Bloodborne {

// Validates a "<name>:<secret>" party setting and returns the header value to send.
std::optional<std::string> ParsePartySetting(std::string_view setting);

enum class PartyRole { Host, Guest };
enum class PartyAction { Wait, StartSearch };

// The special effects a rung bell leaves on the player. While one is active the game's SOS
// status (0x0186FE40) reports the player as searching, so adding the effect is the whole bell.
// The Beckoning Bell's is 9000 (special state 189); the game adds 9003 next to it by itself.
constexpr s32 BeckoningBellEffect = 9000;
constexpr s32 SmallResonantBellEffect = 9005;

// 01.09's add-special-effect virtual (character vtable slot 0x3F0, 0x018C6DB0 for players).
// Only the effect id, the source and the 1.0 scales are understood; the rest are passed exactly
// as the game passes them.
using AddSpEffectFn = void PS4_SYSV_ABI (*)(u64 character, s32 effect_id, u64 source, s32 arg3,
                                            s32 arg4, s32 arg5, float scale0, float scale1,
                                            float scale2, float scale3, float scale4,
                                            s32 stack_arg);

// Starts the summon search without the bell item: adds the search effect to the player the same
// way 01.09 adds 9005 itself (0x01C0D3B0). No use-item path, so no animation, item or Insight.
bool StartSummonSearch(u64 player, s32 effect_id, AddSpEffectFn add_sp_effect);

// What the game looks like this tick, read from guest memory by bloodborne_re.cpp.
struct PartyObservation {
    bool player_valid = false;
    bool map_usable = false;
    bool stage_transition = false;
    bool matching_idle = false; // no multiplayer session, not even a pending one
    bool search_effect_active = false;
};

// CSMultiPlayMan session state (+0x124) as seen in the rig, and the summon-reload marker the
// game sets (+0x84 of the session rules) while it carries a guest into the host's world.
constexpr s32 MatchingStateHosting = 3;
constexpr s32 MatchingStateSummonedGuest = 6;
constexpr s32 SummonReloadInProgress = 3;

// Whether a world teardown must end the session even though the seamless patch keeps it: a
// summoned guest or a host leaving its world (quit to title, death respawn), but never during a
// summon reload (see the tests for why). `reload_pending` is our own deferred summon reload.
bool ShouldEndSessionOnWorldTeardown(s32 matching_state, s32 reload_state, bool reload_pending,
                                     bool host_death_respawn);

// Deaths that keep the party. 01.09 runs the event scripts' death flow natively (the Lua source,
// global_event.lua in script/eventcommon.luabnd.dcx, is never loaded), so the change is a few
// byte patches in PartyGhostDeath / PartyGhostDeath_1 plus a revive where the menu used to be.
constexpr u64 PartyGhostSetEventFlagOffset = 0x0132E5A0;       // SetEventFlag(_, flag, on)
constexpr u64 PartyGhostCustomLuaCallStartOffset = 0x0132ED40; // CustomLuaCallStart(proxy, id, arg)
constexpr u64 PartyGhostDeathMenuBranchOffset = 0x01382359;    // jne past the menu (ClearBoss)

struct CodePatch {
    u64 offset;
    std::vector<u8> original;
    std::vector<u8> patched;
};
const std::vector<CodePatch>& GhostDeathKeepSessionPatches();

// Target of a `call rel32` at `offset`, or 0 if the bytes are not one.
u64 DecodeNearCallTarget(u64 offset, std::span<const u8> bytes);

// The event script bindings the revive uses (PS4 SysV; the unused first argument is the script
// state the native code passes along).
struct GhostDeathCalls {
    bool PS4_SYSV_ABI (*is_white_ghost)() = nullptr;
    bool PS4_SYSV_ABI (*set_event_flag)(u64 unused, s32 flag, bool on) = nullptr;
    void (*clear_return_state)() = nullptr; // ours: [SprjEventMan+0x10]+8, set by OnEvent_4000
    bool PS4_SYSV_ABI (*request_full_recover)() = nullptr;
    bool PS4_SYSV_ABI (*update_blood_mark)() = nullptr;
    bool PS4_SYSV_ABI (*revive_player)() = nullptr;
    void PS4_SYSV_ABI (*revive_local_character)() = nullptr;
    void (*restore_alive_state)() = nullptr; // ours: see AliveStateSnapshot
    void PS4_SYSV_ABI (*set_chr_type)(u64 unused, s32 entity, s32 type) = nullptr;
    void PS4_SYSV_ABI (*chr_reset_animation)(u64 unused, s32 entity) = nullptr;
};
constexpr s32 DeathEventFlag = 4000;
constexpr s32 WhiteGhostCharacterType = 1;
constexpr s32 LocalPlayerEntity = 10000;

// What a character's death overwrites besides HP: the state flags at +0x1E0 (the kill sets the
// dead bit, +0x1E3 bit 0) and the action state the death processing writes ([+0x3B0]+8: +0x1B4
// and +0x1B8). Saved from the local player while it is alive, put back by the revive.
struct AliveState {
    u64 flags = 0;
    s32 action = 0;
    s32 action_sub = 0;
};
class AliveStateSnapshot {
public:
    void Observe(bool alive, const AliveState& state) {
        if (alive) {
            saved = state;
        }
    }
    const std::optional<AliveState>& Saved() const {
        return saved;
    }

private:
    std::optional<AliveState> saved;
};

// HostDead's white phantom branch and the epilogue the seamless patch jumps to.
constexpr u64 HostDeadEpilogueOffset = 0x01381A50;
const std::vector<CodePatch>& HostDeathKeepGuestsPatches();
// Target of a `jmp rel32` at `offset`, or 0 if the bytes are not one.
u64 DecodeNearJumpTarget(u64 offset, std::span<const u8> bytes);

// Revives the local player in place if it is a white phantom (a party guest). Returns whether it
// did; anyone else (and a missing binding) is left to the game.
bool ReviveDeadPartyGuest(const GhostDeathCalls& calls);

// The sign poster's timers (seconds, a float at +0x28 of the sign object): 01.09 waits 5 s
// before a new sign's first post and 30 s between re-posts, ignoring ss.info's interval.
constexpr float FirstSignPostDelaySeconds = 0.0f;
constexpr float SignRepostSeconds = 5.0f;

// `mov dword ptr [<reg> + 0x28], <seconds>` with the ModRM byte of the game's own store.
std::array<u8, 7> EncodeSignTimerStore(u8 modrm, float seconds);

// Auto-rejoin for a configured party: whenever this player is back in its own world with no
// session and no active search, start the role's search again (host: Beckoning, guest: Small
// Resonant) once the world has been stable for a while.
class PartyController {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr u32 StableTicksBeforeSearch = 20;
    static constexpr Clock::duration RetryDelay = std::chrono::seconds{15};

    explicit PartyController(PartyRole role) : role{role} {}

    s32 SearchEffectId() const {
        return role == PartyRole::Host ? BeckoningBellEffect : SmallResonantBellEffect;
    }
    PartyAction Tick(const PartyObservation& observation, Clock::time_point now);

private:
    PartyRole role;
    u32 stable_ticks = 0;
    std::optional<Clock::time_point> last_search;
};

} // namespace Core::Bloodborne
