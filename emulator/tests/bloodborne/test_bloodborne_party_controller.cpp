// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/bloodborne_party.h"

using namespace Core::Bloodborne;
using namespace std::chrono_literals;

namespace {

// A player standing still in their own world: loaded, no transition, matching idle, no search.
PartyObservation Idle() {
    return PartyObservation{
        .player_valid = true,
        .map_usable = true,
        .stage_transition = false,
        .matching_idle = true,
        .search_effect_active = false,
    };
}

PartyAction TickUntilDecision(PartyController& controller, const PartyObservation& observation,
                              PartyController::Clock::time_point now, int max_ticks = 100) {
    for (int i = 0; i < max_ticks; ++i) {
        const PartyAction action = controller.Tick(observation, now);
        if (action == PartyAction::StartSearch) {
            return action;
        }
    }
    return PartyAction::Wait;
}

const PartyController::Clock::time_point T0{};

} // namespace

TEST(BloodbornePartyController, StartsTheRoleSearchOnceTheWorldHasBeenStableForAWhile) {
    PartyController controller(PartyRole::Guest);
    EXPECT_EQ(controller.SearchEffectId(), SmallResonantBellEffect);

    for (u32 i = 1; i < PartyController::StableTicksBeforeSearch; ++i) {
        EXPECT_EQ(controller.Tick(Idle(), T0), PartyAction::Wait) << "tick " << i;
    }
    EXPECT_EQ(controller.Tick(Idle(), T0), PartyAction::StartSearch);
}

TEST(BloodbornePartyController, HostSearchesWithTheBeckoningBellEffect) {
    PartyController controller(PartyRole::Host);
    // A real Beckoning Bell leaves 9000 (special state 189) on the host, which the game then
    // pairs with 9003. 9001 is not a search effect and does nothing visible.
    EXPECT_EQ(controller.SearchEffectId(), 9000);
    EXPECT_EQ(TickUntilDecision(controller, Idle(), T0), PartyAction::StartSearch);
}

TEST(BloodbornePartyController, NeverSearchesWhileInASessionLoadingOrAlreadySearching) {
    for (auto broken : {&PartyObservation::player_valid, &PartyObservation::map_usable,
                        &PartyObservation::matching_idle}) {
        PartyController controller(PartyRole::Guest);
        PartyObservation observation = Idle();
        observation.*broken = false;
        EXPECT_EQ(TickUntilDecision(controller, observation, T0), PartyAction::Wait);
    }
    for (auto blocking :
         {&PartyObservation::stage_transition, &PartyObservation::search_effect_active}) {
        PartyController controller(PartyRole::Guest);
        PartyObservation observation = Idle();
        observation.*blocking = true;
        EXPECT_EQ(TickUntilDecision(controller, observation, T0), PartyAction::Wait);
    }
}

TEST(BloodbornePartyController, AnyDisturbanceRestartsTheStabilityCount) {
    PartyController controller(PartyRole::Guest);
    for (u32 i = 1; i < PartyController::StableTicksBeforeSearch; ++i) {
        ASSERT_EQ(controller.Tick(Idle(), T0), PartyAction::Wait);
    }
    PartyObservation loading = Idle();
    loading.stage_transition = true;
    EXPECT_EQ(controller.Tick(loading, T0), PartyAction::Wait);
    // Needs a full stable window again, not just one more tick.
    EXPECT_EQ(controller.Tick(Idle(), T0), PartyAction::Wait);
    EXPECT_EQ(TickUntilDecision(controller, Idle(), T0), PartyAction::StartSearch);
}

TEST(BloodbornePartyController, WaitsForTheCooldownBeforeSearchingAgain) {
    PartyController controller(PartyRole::Guest);
    ASSERT_EQ(TickUntilDecision(controller, Idle(), T0), PartyAction::StartSearch);

    // The search did not take (no effect appeared): retry only after the cooldown.
    EXPECT_EQ(TickUntilDecision(controller, Idle(), T0 + PartyController::RetryDelay - 1s),
              PartyAction::Wait);
    EXPECT_EQ(TickUntilDecision(controller, Idle(), T0 + PartyController::RetryDelay),
              PartyAction::StartSearch);
}

TEST(BloodbornePartyController, SearchesAgainAfterTheSessionEnds) {
    PartyController controller(PartyRole::Guest);
    ASSERT_EQ(TickUntilDecision(controller, Idle(), T0), PartyAction::StartSearch);

    // The search took, we got summoned and played for a while...
    PartyObservation summoned = Idle();
    summoned.search_effect_active = true;
    EXPECT_EQ(controller.Tick(summoned, T0 + 1s), PartyAction::Wait);
    PartyObservation in_session = Idle();
    in_session.matching_idle = false;
    EXPECT_EQ(TickUntilDecision(controller, in_session, T0 + 2s), PartyAction::Wait);

    // ...then died and were sent home. The next search must not wait for the old cooldown.
    EXPECT_EQ(TickUntilDecision(controller, Idle(), T0 + 3s), PartyAction::StartSearch);
}

namespace {

struct AddSpEffectCall {
    int calls = 0;
    u64 character = 0;
    s32 effect_id = 0;
    u64 source = 0;
    s32 arg3 = -1;
    s32 arg4 = -1;
    s32 arg5 = -1;
    std::array<float, 5> scales{};
    s32 stack_arg = -1;
};
AddSpEffectCall last_add;

void PS4_SYSV_ABI FakeAddSpEffect(u64 character, s32 effect_id, u64 source, s32 arg3, s32 arg4,
                                  s32 arg5, float scale0, float scale1, float scale2, float scale3,
                                  float scale4, s32 stack_arg) {
    last_add = {
        .calls = last_add.calls + 1,
        .character = character,
        .effect_id = effect_id,
        .source = source,
        .arg3 = arg3,
        .arg4 = arg4,
        .arg5 = arg5,
        .scales = {scale0, scale1, scale2, scale3, scale4},
        .stack_arg = stack_arg,
    };
}

} // namespace

// 01.09 gives the local player 9005 itself (0x01C0D3B0) through the character's add-effect
// virtual with exactly these arguments. Doing the same starts the search with no bell item, no
// animation and no Insight.
TEST(BloodborneSummonSearch, AddsTheSearchEffectToThePlayerTheWayTheGameDoes) {
    last_add = {};
    constexpr u64 Player = 0x213cdffe0;
    EXPECT_TRUE(StartSummonSearch(Player, SmallResonantBellEffect, FakeAddSpEffect));

    EXPECT_EQ(last_add.calls, 1);
    EXPECT_EQ(last_add.character, Player);
    EXPECT_EQ(last_add.effect_id, SmallResonantBellEffect);
    EXPECT_EQ(last_add.source, Player);
    EXPECT_EQ(last_add.arg3, 0);
    EXPECT_EQ(last_add.arg4, 1);
    EXPECT_EQ(last_add.arg5, 0);
    for (const float scale : last_add.scales) {
        EXPECT_EQ(scale, 1.0f);
    }
    EXPECT_EQ(last_add.stack_arg, 0);
}

TEST(BloodborneSummonSearch, DoesNothingWithoutAPlayerOrTheGameFunction) {
    last_add = {};
    EXPECT_FALSE(StartSummonSearch(0, BeckoningBellEffect, FakeAddSpEffect));
    EXPECT_FALSE(StartSummonSearch(0x213cdffe0, BeckoningBellEffect, nullptr));
    EXPECT_EQ(last_add.calls, 0);
}

// The seamless patch keeps the session over every world teardown so the cross-map handoff
// survives its warp. A summoned guest whose world is torn down outside a summon reload is
// leaving the host's world (quit to the title screen, for one), and must end the session the
// way the unpatched game does, or the host keeps a ghost member and the guest never rejoins.
TEST(BloodborneWorldTeardown, ASummonedGuestLeavingTheHostWorldEndsTheSession) {
    EXPECT_TRUE(ShouldEndSessionOnWorldTeardown(MatchingStateSummonedGuest, 0, false, false));
}

// The same goes for a host whose world goes away (quit to the title screen, or the respawn
// after a death): unpatched, the game stops matching there, and keeping the room leaves the
// guests in it with a host that is gone.
TEST(BloodborneWorldTeardown, AHostLeavingItsWorldEndsTheSession) {
    EXPECT_TRUE(ShouldEndSessionOnWorldTeardown(MatchingStateHosting, 0, false, false));
}

// ...except when that world goes away because the host died: it respawns at its lamp and keeps
// the party (the guests ignore HostDead in seamless mode, see BloodborneHostDeath).
TEST(BloodborneWorldTeardown, AHostRespawningAfterDeathKeepsTheSession) {
    EXPECT_FALSE(ShouldEndSessionOnWorldTeardown(MatchingStateHosting, 0, false, true));
    // Only the host respawns that way; a guest leaving still ends its session.
    EXPECT_TRUE(ShouldEndSessionOnWorldTeardown(MatchingStateSummonedGuest, 0, false, true));
}

TEST(BloodborneWorldTeardown, KeepsTheSessionDuringASummonReload) {
    for (const s32 state : {MatchingStateSummonedGuest, MatchingStateHosting}) {
        EXPECT_FALSE(ShouldEndSessionOnWorldTeardown(state, SummonReloadInProgress, false, false));
        EXPECT_FALSE(ShouldEndSessionOnWorldTeardown(state, 0, true, false));
    }
}

TEST(BloodborneWorldTeardown, LeavesSearchesAndIdlePlayersAlone) {
    for (const s32 state : {0, 1, 2, 4, 5, 7}) {
        EXPECT_FALSE(ShouldEndSessionOnWorldTeardown(state, 0, false, false)) << "state " << state;
    }
}

// The game's sign poster waits 5 s before a new sign's first post and 30 s between re-posts,
// whatever ss.info says. The encoder must reproduce 01.09's own stores exactly...
TEST(BloodborneSignTimers, EncodesTheGamesOwnTimerStores) {
    EXPECT_EQ(EncodeSignTimerStore(0x47, 5.0f),
              (std::array<u8, 7>{0xC7, 0x47, 0x28, 0x00, 0x00, 0xA0, 0x40}));
    EXPECT_EQ(EncodeSignTimerStore(0x43, 30.0f),
              (std::array<u8, 7>{0xC7, 0x43, 0x28, 0x00, 0x00, 0xF0, 0x41}));
}

// ...so a party member's sign goes up at once and is re-posted as often as the host searches.
TEST(BloodborneSignTimers, PostsTheFirstSignAtOnceAndRepostsEveryFiveSeconds) {
    EXPECT_EQ(FirstSignPostDelaySeconds, 0.0f);
    EXPECT_EQ(SignRepostSeconds, 5.0f);
}

// 01.09 runs the event scripts' death flow natively (global_event.lua in eventcommon.luabnd is
// only the source it was built from). When a white phantom dies, PartyGhostDeath broadcasts
// event 4063, which makes the host drop the guest from the session (OnGameLeave), and
// PartyGhostDeath_1 opens the "returned to your world" menu that leads to the warp home.
TEST(BloodborneGhostDeath, KeepsTheSessionByDroppingTheLeaveBroadcastAndItsFlags) {
    const auto& patches = GhostDeathKeepSessionPatches();
    ASSERT_EQ(patches.size(), 5u);
    for (const auto& patch : patches) {
        EXPECT_EQ(patch.original.size(), patch.patched.size()) << std::hex << patch.offset;
    }
    // The three calls: SetEventFlag(4047) twice and CustomLuaCallStart(4063, player).
    const std::array<std::pair<u64, u64>, 3> calls{{
        {0x01381FE7, PartyGhostSetEventFlagOffset},
        {0x01381FF6, PartyGhostSetEventFlagOffset},
        {0x0138201B, PartyGhostCustomLuaCallStartOffset},
    }};
    for (std::size_t i = 0; i < calls.size(); ++i) {
        EXPECT_EQ(patches[i].offset, calls[i].first);
        EXPECT_EQ(DecodeNearCallTarget(patches[i].offset, patches[i].original), calls[i].second);
        EXPECT_EQ(patches[i].patched, (std::vector<u8>{0x0F, 0x1F, 0x44, 0x00, 0x00}));
    }
    // SetFlagInitState(2), inlined as `mov dword ptr [rax], 2`.
    EXPECT_EQ(patches[3].original, (std::vector<u8>{0xC7, 0x00, 0x02, 0x00, 0x00, 0x00}));
    EXPECT_EQ(patches[3].patched, (std::vector<u8>{0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00}));
    // PartyGhostDeath_1: `if not ClearBoss then <menu, then warp home>` always skips the menu.
    EXPECT_EQ(patches[4].offset, PartyGhostDeathMenuBranchOffset);
    EXPECT_EQ(patches[4].original, (std::vector<u8>{0x75, 0x2C}));
    EXPECT_EQ(patches[4].patched, (std::vector<u8>{0xEB, 0x2C}));
}

TEST(BloodborneGhostDeath, DecodesOnlyNearCalls) {
    EXPECT_EQ(DecodeNearCallTarget(0x1000, std::vector<u8>{0xE8, 0x00, 0x00, 0x00, 0x00}), 0x1005u);
    EXPECT_EQ(DecodeNearCallTarget(0x1000, std::vector<u8>{0xE8, 0xFB, 0xFF, 0xFF, 0xFF}), 0x1000u);
    EXPECT_EQ(DecodeNearCallTarget(0x1000, std::vector<u8>{0xE9, 0x00, 0x00, 0x00, 0x00}), 0u);
    EXPECT_EQ(DecodeNearCallTarget(0x1000, std::vector<u8>{0xE8, 0x00}), 0u);
}

namespace {

std::vector<std::string> revive_calls;
bool white_ghost = true;

bool PS4_SYSV_ABI FakeIsWhiteGhost() {
    revive_calls.push_back("IsWhiteGhost");
    return white_ghost;
}
bool PS4_SYSV_ABI FakeSetEventFlag(u64, s32 flag, bool value) {
    revive_calls.push_back("SetEventFlag " + std::to_string(flag) + (value ? " on" : " off"));
    return true;
}
bool PS4_SYSV_ABI FakeRequestFullRecover() {
    revive_calls.push_back("RequestFullRecover");
    return true;
}
bool PS4_SYSV_ABI FakeUpDateBloodMark() {
    revive_calls.push_back("UpDateBloodMark");
    return true;
}
bool PS4_SYSV_ABI FakeRevivePlayer() {
    revive_calls.push_back("RevivePlayer");
    return true;
}
void PS4_SYSV_ABI FakeSetChrType(u64, s32 entity, s32 type) {
    revive_calls.push_back("SetChrType " + std::to_string(entity) + " " + std::to_string(type));
}
void PS4_SYSV_ABI FakeChrResetAnimation(u64, s32 entity) {
    revive_calls.push_back("ChrResetAnimation " + std::to_string(entity));
}

constexpr GhostDeathCalls FakeCalls{
    .is_white_ghost = FakeIsWhiteGhost,
    .set_event_flag = FakeSetEventFlag,
    .request_full_recover = FakeRequestFullRecover,
    .update_blood_mark = FakeUpDateBloodMark,
    .revive_player = FakeRevivePlayer,
    .set_chr_type = FakeSetChrType,
    .chr_reset_animation = FakeChrResetAnimation,
};

} // namespace

// Where the game would open that menu, a party guest gets up again instead: the death event
// (4000) is cleared so the next death runs, the guest drops its echoes in a bloodstain the way a
// host death does (UpDateBloodMark) and is revived in place. RevivePlayer alone leaves the
// character in its death pose and makes it a live player (type 0), so the white phantom type is
// set back (SetChrType also refreshes the character) and its behaviour is reset to idle.
// (A summon reload instead needs the host's side of the summon handshake and drops the session.)
TEST(BloodborneGhostDeath, ADeadPartyGuestRevivesWhereItFell) {
    revive_calls.clear();
    white_ghost = true;
    EXPECT_TRUE(ReviveDeadPartyGuest(FakeCalls));
    EXPECT_EQ(revive_calls, (std::vector<std::string>{
                                "IsWhiteGhost",
                                "SetEventFlag 4000 off",
                                "RequestFullRecover",
                                "UpDateBloodMark",
                                "RevivePlayer",
                                "SetChrType 10000 1",
                                "ChrResetAnimation 10000",
                            }));
}

TEST(BloodborneGhostDeath, LeavesEveryoneElseAlone) {
    revive_calls.clear();
    white_ghost = false;
    EXPECT_FALSE(ReviveDeadPartyGuest(FakeCalls));
    EXPECT_EQ(revive_calls, std::vector<std::string>{"IsWhiteGhost"});

    revive_calls.clear();
    GhostDeathCalls missing = FakeCalls;
    missing.chr_reset_animation = nullptr;
    EXPECT_FALSE(ReviveDeadPartyGuest(missing));
    EXPECT_TRUE(revive_calls.empty());
}

// When the host dies, every guest runs HostDead (event 4000 param 1): for a white phantom it
// shows MissionFailed and five seconds later warps home (HostDead_1). In seamless mode the white
// phantom branch returns straight away, so the guests stay in the session.
TEST(BloodborneHostDeath, GuestsIgnoreTheHostsDeath) {
    const auto& patches = HostDeathKeepGuestsPatches();
    ASSERT_EQ(patches.size(), 1u);
    const auto& patch = patches[0];
    EXPECT_EQ(patch.offset, 0x013819C6u);
    // The branch starts with SetEventFlag(4047, true): `mov esi, 0xFCF`.
    EXPECT_EQ(patch.original, (std::vector<u8>{0xBE, 0xCF, 0x0F, 0x00, 0x00}));
    EXPECT_EQ(DecodeNearJumpTarget(patch.offset, patch.patched), HostDeadEpilogueOffset);
}

TEST(BloodborneHostDeath, DecodesOnlyNearJumps) {
    EXPECT_EQ(DecodeNearJumpTarget(0x1000, std::vector<u8>{0xE9, 0x00, 0x00, 0x00, 0x00}), 0x1005u);
    EXPECT_EQ(DecodeNearJumpTarget(0x1000, std::vector<u8>{0xE8, 0x00, 0x00, 0x00, 0x00}), 0u);
}
