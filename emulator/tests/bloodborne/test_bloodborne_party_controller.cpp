// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>

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
    EXPECT_TRUE(ShouldEndSessionOnWorldTeardown(MatchingStateSummonedGuest, 0, false));
}

TEST(BloodborneWorldTeardown, KeepsTheSessionDuringASummonReload) {
    EXPECT_FALSE(
        ShouldEndSessionOnWorldTeardown(MatchingStateSummonedGuest, SummonReloadInProgress, false));
    EXPECT_FALSE(ShouldEndSessionOnWorldTeardown(MatchingStateSummonedGuest, 0, true));
}

TEST(BloodborneWorldTeardown, LeavesHostsSearchesAndIdlePlayersAlone) {
    for (const s32 state : {0, 1, 2, MatchingStateHosting, 4, 5, 7}) {
        EXPECT_FALSE(ShouldEndSessionOnWorldTeardown(state, 0, false)) << "state " << state;
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
