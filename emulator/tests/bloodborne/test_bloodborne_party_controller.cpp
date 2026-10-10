// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/bloodborne_party.h"
#include "core/libraries/np/np_signaling/np_signaling_reuse.h"

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

// Independent travel: a guest whose host left stays where it is, still a white phantom in the
// host's old world. Searching from there would pull it to wherever the host went.
TEST(BloodbornePartyController, AGuestLeftBehindByItsHostDoesNotSearch) {
    PartyController controller(PartyRole::Guest);
    PartyObservation left_behind = Idle();
    left_behind.white_phantom = true;
    EXPECT_EQ(TickUntilDecision(controller, left_behind, T0), PartyAction::Wait);
}

// A white phantom whose host is gone (OnLeave_Limit, event 4046) is reset into its own world.
// A party guest stayed where it was instead, which a host's trip once needed; now a trip keeps
// the session and never gets there. The rig's host crashed: its guest stood on as a phantom
// with no host, and a phantom never rings, so they never joined again. It stays only while the
// session still has someone; otherwise it goes home (and resumes where it was, see
// ResumeWhereTheGuestLeft) and rings from there.
TEST(BloodbornePartyController, AGuestWhoseSessionIsGoneGoesHome) {
    EXPECT_TRUE(SkipsLeaveLimit(WhiteGhostCharacterType, 1))
        << "the session goes on (another guest, a trip)";
    EXPECT_TRUE(SkipsLeaveLimit(WhiteGhostCharacterType, 2));
    EXPECT_FALSE(SkipsLeaveLimit(WhiteGhostCharacterType, 0))
        << "the host crashed or quit: nobody left";
}

// The rig's crash-apart test: a guest's game died while it was in the Ward, apart from its host,
// and the host's game ran the leave limit too (from eboot+0x1393323): its warp "home" found no
// map and took the host to the title screen. A live player (the host, or a guest after its own
// trip) is in its own world already, so a party member never takes that warp.
TEST(BloodbornePartyController, ALivePlayerNeverTakesTheLeaveWarp) {
    EXPECT_TRUE(SkipsLeaveLimit(0, 0)) << "the host, its guest gone";
    EXPECT_TRUE(SkipsLeaveLimit(0, 1));
}

// The rig's host-crash-apart test: the guest joined in Central Yharnam (the placement its sign got
// with the claim is its own spot there), travelled to the Ward and lost its host; once out of the
// session, that old placement warped it back to where it had joined. A placement is for its one
// join: spent once the session it brought is over.
TEST(BloodbornePartyController, AJoinsPlacementIsSpentWhenItsSessionEnds) {
    JoinPlacementLife life;
    life.Received();
    EXPECT_FALSE(life.Spent(0, true)) << "not joined yet: the claim is on its way";
    EXPECT_FALSE(life.Spent(MatchingStateSummonedGuest, false)) << "in the session it brought";
    EXPECT_FALSE(life.Spent(MatchingStateSummonedGuest, false));
    EXPECT_FALSE(life.Spent(0, false)) << "still leaving (a reload or the room's close pending)";
    EXPECT_TRUE(life.Spent(0, true)) << "the session is over";
    EXPECT_FALSE(life.Spent(0, true)) << "spent once";
    life.Received();
    EXPECT_FALSE(life.Spent(0, true)) << "the next join's placement";
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

// The same goes for a host whose world goes away (quit to the title screen; a host with a party
// no longer reloads on death): unpatched, the game stops matching there, and keeping the room
// leaves the guests in it with a host that is gone.
TEST(BloodborneWorldTeardown, AHostLeavingItsWorldEndsTheSession) {
    EXPECT_TRUE(ShouldEndSessionOnWorldTeardown(MatchingStateHosting, 0, false));
}

TEST(BloodborneWorldTeardown, KeepsTheSessionDuringASummonReload) {
    for (const s32 state : {MatchingStateSummonedGuest, MatchingStateHosting}) {
        EXPECT_FALSE(ShouldEndSessionOnWorldTeardown(state, SummonReloadInProgress, false));
        EXPECT_FALSE(ShouldEndSessionOnWorldTeardown(state, 0, true));
    }
}

TEST(BloodborneWorldTeardown, LeavesSearchesAndIdlePlayersAlone) {
    for (const s32 state : {0, 1, 2, 4, 5, 7}) {
        EXPECT_FALSE(ShouldEndSessionOnWorldTeardown(state, 0, false)) << "state " << state;
    }
}

// The game's sign poster waits 5 s before a new sign's first post and 30 s between re-posts,
// whatever ss.info says. The encoder must reproduce 01.09's own stores exactly...
// Seamless travel: a party member's trip (the game's stage transition, 0x013CDE30) keeps the
// session through the world teardown and through the leave 01.09 does once the new stage loads
// (0x0193C689). Quitting to the title is no trip, so it still ends the session.
TEST(BloodbornePartyTrip, KeepsTheSessionFromTheTransitionUntilTheNewStageHasLoaded) {
    PartyTrip trip;
    EXPECT_FALSE(trip.Active(T0));
    trip.Start(T0, 0x18010000, 0x1000);
    EXPECT_TRUE(trip.Active(T0 + 1s));
    trip.Finish();
    EXPECT_FALSE(trip.Active(T0 + 2s));
}

// The trip ends at the first tick on the new stage (the party tick does not run while loading).
TEST(BloodbornePartyTrip, EndsOnceThePlayerIsOnTheNewStage) {
    constexpr u32 CentralYharnam = 0x18010000;
    constexpr u32 HuntersDream = 0x15000000;
    PartyTrip trip;
    trip.Start(T0, CentralYharnam, 0x1000);
    trip.ObserveStage(CentralYharnam, false, 0x1000);
    EXPECT_TRUE(trip.Active(T0 + 1s)) << "still on the old stage";
    trip.ObserveStage(HuntersDream, true, 0x1000);
    EXPECT_TRUE(trip.Active(T0 + 2s)) << "still loading";
    trip.ObserveStage(HuntersDream, false, 0x1000);
    EXPECT_FALSE(trip.Active(T0 + 3s));
}

// A trip reloads the traveller's world, which destroys the other players' characters, while
// their join tasks (CSMultiPlayerIns) stay in the in-session step. Sending a remote player's task
// back to the step that creates its character brings it back on the new stage.
TEST(BloodbornePartyTrip, RecreatesRemotePlayersThatWereInTheSession) {
    EXPECT_TRUE(ShouldRecreateAfterTrip(RemoteHostTask, InSessionStep));
    EXPECT_TRUE(ShouldRecreateAfterTrip(RemoteGuestTask, InSessionStep));
    EXPECT_EQ(CreateCharacterStep, InSessionStep - 2);
}

// The reload also empties the join-task list, so a member without a task gets a new one of the
// kind the game gives it on join: a host makes remote guests, a guest makes the remote host.
TEST(BloodbornePartyTrip, RebuildsMissingTasksWithTheKindTheGameUsesOnJoin) {
    EXPECT_EQ(RemoteTaskKind(true), RemoteGuestTask);
    EXPECT_EQ(RemoteTaskKind(false), RemoteHostTask);
}

// On a guest, its own join task (kind 2) is the one that creates the host's character; a host's
// own task is kind 3. A guest's trip loses it like the others.
TEST(BloodbornePartyTrip, RebuildsTheGuestsOwnTaskToo) {
    EXPECT_EQ(LocalTaskKind(false), 2);
    EXPECT_EQ(LocalTaskKind(true), 3);
}

TEST(BloodbornePartyTrip, LeavesLocalAndUnfinishedJoinsAlone) {
    for (const s32 local : {2, 3}) {
        EXPECT_FALSE(ShouldRecreateAfterTrip(local, InSessionStep)) << local;
    }
    for (s32 step = 0; step <= 10; ++step) {
        if (step != InSessionStep) {
            EXPECT_FALSE(ShouldRecreateAfterTrip(RemoteGuestTask, step)) << step;
        }
    }
}

// A reload of the same map (a guest's death far from its lamp) is a trip too: the player is a
// new character once it is over.
TEST(BloodbornePartyTrip, EndsAfterAReloadOfTheSameMap) {
    constexpr u32 CentralYharnam = 0x18010000;
    PartyTrip trip;
    trip.Start(T0, CentralYharnam, 0x1000);
    trip.ObserveStage(CentralYharnam, false, 0x1000);
    EXPECT_TRUE(trip.Active(T0 + 1s)) << "same map, same character";
    trip.ObserveStage(CentralYharnam, false, 0);
    EXPECT_TRUE(trip.Active(T0 + 2s)) << "no character yet";
    trip.ObserveStage(CentralYharnam, false, 0x2000);
    EXPECT_FALSE(trip.Active(T0 + 3s));
}

TEST(BloodbornePartyTrip, ATripThatNeverLoadsStopsCountingAfterAWhile) {
    PartyTrip trip;
    trip.Start(T0, 0x18010000, 0x1000);
    EXPECT_TRUE(trip.Active(T0 + PartyTrip::Timeout - 1s));
    EXPECT_FALSE(trip.Active(T0 + PartyTrip::Timeout));
}

// The party's map board on shadNet (POST /bloodborne/party/map): "<member> <map>" in hex.
TEST(BloodbornePartyMaps, PostsAndReadsTheMapEachMemberIsOn) {
    EXPECT_EQ(EncodeMapUpdate("a1b2", 0x18010000), "a1b2 18010000");
    const auto board = ParseMapBoard("a1b2 18010000\nc3d4 15000000\n");
    ASSERT_TRUE(board.has_value());
    ASSERT_EQ(board->size(), 2u);
    EXPECT_EQ((*board)[1].member, "c3d4");
    EXPECT_EQ((*board)[1].map, 0x15000000u);
    EXPECT_FALSE(ParseMapBoard("a1b2\n").has_value());
    EXPECT_FALSE(ParseMapBoard("a1b2 zz\n").has_value());
}

// A trip's re-created players are only made once every other member is on this map: creating
// a player into a map it is not on makes its game leave the session.
TEST(BloodbornePartyMaps, RecreatesOnlyWhenEveryoneElseIsOnThisMap) {
    const std::vector<MemberMap> apart{{"me", 0x15000000}, {"them", 0x18010000}};
    const std::vector<MemberMap> together{{"me", 0x18010000}, {"them", 0x18010000}};
    EXPECT_FALSE(EveryoneElseOnMap(apart, "me", 0x15000000));
    EXPECT_TRUE(EveryoneElseOnMap(together, "me", 0x18010000));
    const std::vector<MemberMap> alone{{"me", 0x18010000}};
    EXPECT_FALSE(EveryoneElseOnMap(alone, "me", 0x18010000)) << "nobody else";
}

// A guest alone on a map (the others elsewhere): the host has none of its enemies.
TEST(BloodbornePartyMaps, AGuestCanBeAloneOnItsMap) {
    const std::vector<MemberMap> apart{{"me", 0x18000000}, {"them", 0x18010000}};
    const std::vector<MemberMap> together{{"me", 0x18010000}, {"them", 0x18010000}};
    EXPECT_TRUE(NoOneElseOnMap(apart, "me", 0x18000000));
    EXPECT_FALSE(NoOneElseOnMap(together, "me", 0x18010000));
    EXPECT_FALSE(NoOneElseOnMap(apart, "me", 0)) << "loading";
}

// The new stage's load drops the bell's search effect (9000/9003 for the host), which the game
// only adds when a bell is rung. After a trip in a party session it is put back.
// The multiplayer effects a player carries into a trip (the host's 9000/9003, the guest's
// 9005/9006) are lost with the old stage; the ones it had are put back on arrival.
TEST(BloodbornePartyTrip, PutsBackTheMultiplayerEffectsThePlayerHad) {
    const std::vector<s32> before{6300, 9000, 40, 9003, 9001};
    const std::vector<s32> after{6300, 40};
    EXPECT_EQ(EffectsToRestoreAfterTrip(before, after), (std::vector<s32>{9000, 9003}));
    const std::vector<s32> guest_before{6300, 40, 9006};
    EXPECT_EQ(EffectsToRestoreAfterTrip(guest_before, after), (std::vector<s32>{9006}));
    EXPECT_TRUE(EffectsToRestoreAfterTrip(before, before).empty());
}

TEST(BloodbornePartyTrip, PutsTheSearchEffectBackAfterTheTrip) {
    EXPECT_TRUE(ShouldRestoreSearchAfterTrip(true, false));
    EXPECT_FALSE(ShouldRestoreSearchAfterTrip(true, true)) << "still there";
    EXPECT_FALSE(ShouldRestoreSearchAfterTrip(false, false)) << "no party session";
}

// Experiment (Kaniel, 2026-10-06): SHADPS4_BLOODBORNE_GUEST_AS_HOST=1 makes a party guest a live
// player (character type 0) instead of a white phantom while it is in the session.
TEST(BloodborneGuestAsHost, OnlyWhenSwitchedOnAndAWhitePhantomInAPartySession) {
    EXPECT_TRUE(SettingIsOn("1"));
    EXPECT_FALSE(SettingIsOn(nullptr));
    EXPECT_FALSE(SettingIsOn("0"));
    EXPECT_TRUE(ShouldPlayGuestAsHost(true, true, WhiteGhostCharacterType));
    EXPECT_FALSE(ShouldPlayGuestAsHost(false, true, WhiteGhostCharacterType));
    EXPECT_FALSE(ShouldPlayGuestAsHost(true, false, WhiteGhostCharacterType));
    EXPECT_FALSE(ShouldPlayGuestAsHost(true, true, 0)) << "already live";
}

// The multiplayer walls (common event 7600: wall object and its fog SFX, shown while in
// multiplayer) are what keeps a summoned guest inside an area. A party session turns them off.
TEST(BloodborneMultiplayerWalls, ListsTheWallsOfTheMapThePlayerIsOn) {
    const auto central_yharnam = MultiplayerWallsOn(0x18010000);
    ASSERT_EQ(central_yharnam.size(), 3u);
    EXPECT_EQ(central_yharnam[0].object, 2411999);
    EXPECT_EQ(central_yharnam[0].sfx, 2413999);
    EXPECT_EQ(MultiplayerWallsOn(0x18000000).size(), 5u); // m24_00
    EXPECT_TRUE(MultiplayerWallsOn(0x15000000).empty()) << "the Hunter's Dream has none";
    for (const u32 map : {0x16000000u, 0x17000000u, 0x18000000u, 0x18010000u, 0x18020000u,
                          0x1B000000u, 0x1C000000u, 0x20000000u, 0x23000000u}) {
        for (const MultiplayerWall& wall : MultiplayerWallsOn(map)) {
            EXPECT_EQ(PackedMapOfWarpPoint(wall.object), map) << wall.object;
            EXPECT_EQ(wall.sfx, wall.object + 2000);
        }
    }
}

// A summoned guest carries special effect 9006, whose max HP rate (SpEffectParam +0x10) is 0.7.
// A party guest keeps its full HP: the row is found in the loaded param and set to 1.0.
TEST(BloodborneGuestHp, FindsARowInAParamFile) {
    // PARAM: row count (u16) at +0x0A, the type name at +0x0C, then 24-byte row entries at +0x40
    // (id, padding, data offset, name offset), sorted by id.
    std::vector<u8> param(0x40 + 3 * 24 + 3 * 16, 0);
    param[0x0A] = 3;
    const std::string name = "SP_EFFECT_PARAM_ST";
    std::copy(name.begin(), name.end(), param.begin() + 0x0C);
    const std::array<s32, 3> ids{9005, 9006, 9008};
    for (std::size_t row = 0; row < ids.size(); ++row) {
        const u64 data = 0x40 + 3 * 24 + row * 16;
        std::memcpy(param.data() + 0x40 + row * 24, &ids[row], sizeof(s32));
        std::memcpy(param.data() + 0x40 + row * 24 + 8, &data, sizeof(u64));
    }
    EXPECT_TRUE(IsParamOfType(param, "SP_EFFECT_PARAM_ST"));
    EXPECT_FALSE(IsParamOfType(param, "EQUIP_PARAM_GOODS_ST"));
    EXPECT_EQ(FindParamRow(param, 9006), std::optional<u64>{0x40 + 3 * 24 + 16});
    EXPECT_EQ(FindParamRow(param, 9007), std::nullopt);
    EXPECT_EQ(PhantomHpEffect, 9006);
    EXPECT_EQ(SpEffectMaxHpRateOffset, 0x10u);
}

// A guest above the host's level band gets special effect 9200, which scales its max HP,
// stamina and attack down to the host's level. A party guest is never synced down.
TEST(BloodborneGuestHp, PartyGuestsAreNeverLevelSynced) {
    EXPECT_EQ(LevelSyncEffect, 9200);
    EXPECT_TRUE(LevelSyncsGuest(false, true)) << "outside a party the game decides";
    EXPECT_FALSE(LevelSyncsGuest(false, false));
    EXPECT_FALSE(LevelSyncsGuest(true, true));
    EXPECT_FALSE(LevelSyncsGuest(true, false));
}

// On a summoned guest's side, NPCs whose NpcParam has isChangeWanderGhost (+0x145 bit 7) are
// created as wandering ghosts (character type 4, drawn see-through). A party guest sees them as
// the host does.
TEST(BloodborneGuestNpcs, PartyGuestsSeeNpcsSolid) {
    EXPECT_EQ(NpcWanderGhostFlagOffset, 0x145u);
    EXPECT_EQ(NpcWanderGhostFlag, 0x80u);
    EXPECT_TRUE(NpcTurnsIntoWanderGhost(false, true)) << "outside a party the game decides";
    EXPECT_FALSE(NpcTurnsIntoWanderGhost(false, false));
    EXPECT_FALSE(NpcTurnsIntoWanderGhost(true, true));
    EXPECT_FALSE(NpcTurnsIntoWanderGhost(true, false));
}

// The map item module skips a summoned guest: it creates no map items (matching state 6 or a
// joined session, [session+0x98] != -1) and only lets it pick one up while GlobalState +0x1592
// is set. A party guest reads its answers from a stand-in that says "solo".
TEST(BloodborneGuestLoot, PartyGuestsHandleMapItemsLikeASoloPlayer) {
    const auto& stand_in = SoloMapItemStandIn();
    s32 state = -1;
    std::memcpy(&state, stand_in.data() + MatchingStateOffset, sizeof(state));
    EXPECT_EQ(state, 0) << "not a summoned guest";
    s32 joined = 0;
    std::memcpy(&joined, stand_in.data() + JoinedSessionOffset, sizeof(joined));
    EXPECT_EQ(joined, -1) << "no joined session";
    EXPECT_NE(stand_in[GuestItemPickupAllowedOffset], 0) << "pickups allowed";
    EXPECT_TRUE(GuestHandlesMapItemsAlone(true));
    EXPECT_FALSE(GuestHandlesMapItemsAlone(false));
}

// Map item packets (channel 0x2C, header word 1, then the message kind): the host's item sync
// clears the guest's map items, which party members keep for themselves. A configured party
// reads every such packet as empty, so each player's map items stay its own.
TEST(BloodborneGuestLoot, PartyMembersIgnoreEachOthersMapItemSync) {
    EXPECT_EQ(MapItemPacketChannel, 0x2Cu);
    constexpr u32 clear_items = 0x00030001;
    EXPECT_EQ(MapItemPacketHeaderSeen(clear_items, true), 0u);
    EXPECT_EQ(MapItemPacketHeaderSeen(clear_items, false), clear_items);
}

// Shared trophies (POST /bloodborne/party/trophy): each member posts "<member> [<id>...]" with
// what it unlocked in the party and reads back one id per line; it unlocks the ones it has not
// handled yet.
TEST(BloodborneTrophies, MembersShareWhatTheyUnlockInTheParty) {
    const std::vector<s32> earned{12, 3};
    EXPECT_EQ(EncodeTrophyUpdate("host", earned), "host 12 3");
    EXPECT_EQ(EncodeTrophyUpdate("guest", {}), "guest");
    EXPECT_EQ(ParseTrophyBoard("3\n12\n"), (std::optional<std::vector<s32>>{{3, 12}}));
    EXPECT_EQ(ParseTrophyBoard(""), (std::optional<std::vector<s32>>{std::vector<s32>{}}));
    EXPECT_EQ(ParseTrophyBoard("3\nx\n"), std::nullopt);
    const std::vector<s32> board{3, 12, 40};
    const std::vector<s32> handled{12};
    EXPECT_EQ(TrophiesToUnlock(board, handled), (std::vector<s32>{3, 40}));
}

// A guest's progress in the host's world (picked-up items: ItemLotParam getItemFlagId,
// 5[0-3]MMxxxx) is recorded and written into its own world once it is back home.
TEST(BloodborneGuestProgress, RecordsMapPickupsAndKeepsThemInAFile) {
    EXPECT_TRUE(IsMapPickupFlag(52400100));
    EXPECT_TRUE(IsMapPickupFlag(50000040));
    EXPECT_TRUE(IsMapPickupFlag(53600000));
    EXPECT_FALSE(IsMapPickupFlag(52901000)) << "chalice dungeons";
    EXPECT_FALSE(IsMapPickupFlag(72410100)) << "lamp";
    EXPECT_FALSE(IsMapPickupFlag(4000));
    const std::vector<s32> flags{52400100, 53600000};
    EXPECT_EQ(FormatFlagList(flags), "52400100\n53600000\n");
    EXPECT_EQ(ParseFlagList("52400100\n53600000\n"), flags);
    EXPECT_EQ(ParseFlagList("52400100\nbad\n\n53600000"), flags) << "bad lines are skipped";
}

// The event flag store: blocks of flags (EventFlagMan +0x1C flags each), a flag's bit at byte
// index/8, most significant bit first. Comparing two copies of a block gives the flags that
// were set or cleared in between.
TEST(BloodborneGuestProgress, FindsTheFlagsABlockChanged) {
    std::vector<u8> before(4, 0);
    std::vector<u8> after(4, 0);
    after[0] = 0x80;  // index 0
    after[2] = 0x01;  // index 23
    before[3] = 0x40; // index 25, cleared
    const FlagChanges changes = ChangedFlags(1241, 1000, before, after);
    EXPECT_EQ(changes.set, (std::vector<s32>{1241000, 1241023}));
    EXPECT_EQ(changes.cleared, (std::vector<s32>{1241025}));
}

// Shared progression: what changes in the host's world during a session (lamps, shortcuts,
// bosses, quest steps) reaches the guest's own world. A join reload changes hundreds of flags at
// once and is not progress; nor are the death/multiplayer (4xxx) flags or the
// pickups, which the pickup list keeps.
TEST(BloodborneGuestProgress, CarriesTheSessionsWorldChangesHome) {
    EXPECT_TRUE(CarriesToGuestWorld(7116)) << "lamp";
    EXPECT_TRUE(CarriesToGuestWorld(12410102)) << "shortcut";
    EXPECT_TRUE(CarriesToGuestWorld(70000030));
    EXPECT_FALSE(CarriesToGuestWorld(4000)) << "death";
    EXPECT_TRUE(CarriesToGuestWorld(9456)) << "boss defeated (set with 2411 and 7017)";
    EXPECT_FALSE(CarriesToGuestWorld(52410360)) << "pickups have their own list";
    EXPECT_FALSE(CarriesToGuestWorld(72410101)) << "a lamp trip, not progress";
    EXPECT_FALSE(CarriesToGuestWorld(72102411)) << "a headstone trip";
    // The bell's bookkeeping (common events 9181-9198): 9185 "a bell is ringing" makes 9183
    // refund 1 Insight with a "resonance ended" message once the bell effect is gone. Kept in
    // the guest's world it came back on every load (friend run 2026-10-08: the bell messages
    // kept coming and the guest's Insight went up each time).
    EXPECT_FALSE(CarriesToGuestWorld(9185));
    EXPECT_FALSE(CarriesToGuestWorld(9199));
    EXPECT_FALSE(CarriesToGuestWorld(9180));
    EXPECT_EQ(ParseWorldChanges("9185 1\n7116 1\n9199 1\n"), (std::map<s32, bool>{{7116, true}}))
        << "a saved world drops them too";
    EXPECT_TRUE(IsBulkFlagChange(65));
    EXPECT_FALSE(IsBulkFlagChange(12));

    std::map<s32, bool> world;
    RecordWorldChanges(world, FlagChanges{{7116, 4000, 12410102}, {12700000}});
    RecordWorldChanges(world, FlagChanges{{12700000}, {7116}});
    EXPECT_EQ(world, (std::map<s32, bool>{{7116, false}, {12410102, true}, {12700000, true}}));
    const std::string text = FormatWorldChanges(world);
    EXPECT_EQ(text, "7116 0\n12410102 1\n12700000 1\n");
    EXPECT_EQ(ParseWorldChanges(text), world);
}

// Kill echoes (0x018FC860, and the calculation 0x018FC9D0) are multiplied by 0.5 for a summoned
// guest: `vmovss xmm3, [rip+disp]` loads 0.5 from 0x04927EC0 (or 0x04927ED0). A party guest's
// load is pointed at the 1.0 next to it (0x04927EBC).
TEST(BloodborneGuestLoot, PartyGuestsGetFullKillEchoes) {
    const std::array<u8, 8> original{0xC5, 0xFA, 0x10, 0x1D, 0x71, 0xB5, 0x02, 0x03};
    EXPECT_EQ(RetargetRipLoad(original, 0x018FC947, 0x04927EBC),
              (std::array<u8, 8>{0xC5, 0xFA, 0x10, 0x1D, 0x6D, 0xB5, 0x02, 0x03}));
    const std::array<u8, 8> second{0xC5, 0xFA, 0x10, 0x1D, 0x21, 0xB4, 0x02, 0x03};
    EXPECT_EQ(RetargetRipLoad(second, 0x018FCAA7, 0x04927EBC),
              (std::array<u8, 8>{0xC5, 0xFA, 0x10, 0x1D, 0x0D, 0xB4, 0x02, 0x03}));
    // Boss echoes (0x01335589, `vmulss xmm0, xmm0, [rip+disp]` by 0.5 at 0x04925B1C for a
    // phantom in multiplayer).
    const std::array<u8, 8> boss{0xC5, 0xFA, 0x59, 0x05, 0x8B, 0x05, 0x5F, 0x03};
    EXPECT_EQ(RetargetRipLoad(boss, 0x01335589, 0x04927EBC),
              (std::array<u8, 8>{0xC5, 0xFA, 0x59, 0x05, 0x2B, 0x29, 0x5F, 0x03}));
}

// A guest's lost echoes (GameDataMan +0x28 "has a bloodstain", the record's echoes at +0x38 and
// block at +0x3C): joining the host clears the mark but keeps the record, so the enemy holding
// the echoes gives nothing back. Picking the echoes up clears the record too (-1, 0xFFFFFFFF).
TEST(BloodborneGuestBloodstain, AJoinKeepsTheGuestsLostEchoes) {
    EXPECT_TRUE(RestoresBloodstainMark(true, true, false, 300, 0x18010000));
    EXPECT_FALSE(RestoresBloodstainMark(false, true, false, 300, 0x18010000)) << "not a guest";
    EXPECT_FALSE(RestoresBloodstainMark(true, false, false, 300, 0x18010000)) << "had none";
    EXPECT_FALSE(RestoresBloodstainMark(true, true, true, 300, 0x18010000)) << "still set";
    EXPECT_FALSE(RestoresBloodstainMark(true, true, false, -1, 0xFFFFFFFF)) << "picked up";
}

// The guest's kept progress belongs to one character: the save slot the game writes
// (/savedata0/userdata000N, N 0-9; userdata0010 holds the system data).
TEST(BloodborneGuestProgress, BelongsToTheCharacterBeingSaved) {
    EXPECT_EQ(CharacterSlotOfSavePath("/savedata0/userdata0005"), std::optional<int>{5});
    EXPECT_EQ(CharacterSlotOfSavePath("/savedata0/userdata0000"), std::optional<int>{0});
    EXPECT_EQ(CharacterSlotOfSavePath("/savedata0/userdata0010"), std::nullopt);
    EXPECT_EQ(CharacterSlotOfSavePath("/savedata0/backup0005"), std::nullopt);
    EXPECT_EQ(CharacterSlotOfSavePath("/app0/userdata0005"), std::nullopt);
}

// Shared world changes (POST /bloodborne/party/flags): "<member> <since> [<flag>:<0|1>...]";
// the answer is the latest sequence number and the other members' changes after <since>.
TEST(BloodborneGuestProgress, SendsTheGuestsWorldChangesToTheHost) {
    const std::vector<std::pair<s32, bool>> changes{{12417830, true}, {7116, false}};
    EXPECT_EQ(EncodeFlagUpdate("guest", 4, changes), "guest 4 12417830:1 7116:0");
    EXPECT_EQ(EncodeFlagUpdate("host", 0, {}), "host 0");
    const auto board = ParseFlagBoard("6\n12417830 1\n7116 0\n");
    ASSERT_TRUE(board.has_value());
    EXPECT_EQ(board->latest, 6);
    EXPECT_EQ(board->changes, changes);
    EXPECT_EQ(ParseFlagBoard("x\n"), std::nullopt);
    EXPECT_EQ(ParseFlagBoard("6\n7116 2\n"), std::nullopt);
}

// A lamp counts as lit for a character only if it is in the character's lamp list; its
// "active" flag (10007810 + map * 10000 + lamp * 20, the talk scripts' check) belongs to lamp
// entity map * 10000 + 1950 + lamp. A lamp lit by another party member gets added to the list.
TEST(BloodborneLamps, ALampLitByAnotherMemberIsTheirsToo) {
    EXPECT_EQ(LampOfActiveFlag(12417830), std::optional<s32>{2411951});
    EXPECT_EQ(LampOfActiveFlag(12417810), std::optional<s32>{2411950});
    EXPECT_EQ(LampOfActiveFlag(12117850), std::optional<s32>{2111952});
    EXPECT_EQ(LampOfActiveFlag(12417831), std::nullopt);
    EXPECT_EQ(LampOfActiveFlag(7116), std::nullopt);
    EXPECT_EQ(LampOfActiveFlag(12415390), std::nullopt);
}

// The lamp tracer watches the byte holding one flag: block, byte and mask, MSB first.
TEST(BloodborneLamps, TheTracerFindsTheByteOfAFlag) {
    const FlagBit bit = FlagBitLocation(12417830, 1000);
    EXPECT_EQ(bit.block, 12417u);
    EXPECT_EQ(bit.byte, 103u);
    EXPECT_EQ(bit.mask, 0x02);
    EXPECT_EQ(FlagBitLocation(12417831, 1000).mask, 0x01);
    EXPECT_EQ(FlagBitLocation(8000, 1000).mask, 0x80);
}

// Registering a lamp (0x0146DA20, first time for this character) writes its 12 flags from base
// off: the host's game did that to a lamp a guest had lit. In a party the ones already on stay.
TEST(BloodborneLamps, RegisteringALampKeepsItLitInAParty) {
    EXPECT_EQ(LampFlagsKeptAtRegistration(true, 12417820, 0b1100'0000'0000),
              (std::vector<s32>{12417830, 12417831}));
    EXPECT_EQ(LampFlagsKeptAtRegistration(true, 12417820, 0b0000'0000'0101),
              (std::vector<s32>{12417820, 12417822}));
    EXPECT_TRUE(LampFlagsKeptAtRegistration(false, 12417820, 0b1100'0000'0000).empty());
    EXPECT_TRUE(LampFlagsKeptAtRegistration(true, -1, 0b1100'0000'0000).empty());
}

// A party guest's boss item (2003:36's award, returning to 0x0132E4B5) waits until the
// boss-clear sequence is over; other awards and other players are not touched.
TEST(BloodborneBossLoot, AGuestsBossItemArrivesAfterTheBossClear) {
    EXPECT_TRUE(DefersItemLotAward(true, true, ClientItemLotAwardReturn));
    EXPECT_FALSE(DefersItemLotAward(false, true, ClientItemLotAwardReturn));
    EXPECT_FALSE(DefersItemLotAward(true, false, ClientItemLotAwardReturn));
    EXPECT_FALSE(DefersItemLotAward(true, true, 0x0132E498));
}

// A guest's changes reach the host only for lamps and boss fog entries (the game syncs the rest
// itself while both are there): its trips (the host's lamp travelled too) and gifts (the host
// lost its own) stay out. Sending every map flag (2026-10-08) flooded the host with a new
// guest character's own world (a friend run: 20 flags at a time, the dog dead for both).
TEST(BloodborneSharedProgression, OnlyLampsGoFromGuestToHost) {
    EXPECT_FALSE(SharedWithParty(12410102)) << "a shortcut";
    EXPECT_FALSE(SharedWithParty(12401800)) << "a boss killed";
    EXPECT_TRUE(SharedWithParty(12417830));
    EXPECT_TRUE(SharedWithParty(12417831));
    EXPECT_FALSE(SharedWithParty(72410101)) << "lamp trip";
    EXPECT_FALSE(SharedWithParty(72410201)) << "lighting talk; the event's flags carry it";
    EXPECT_FALSE(SharedWithParty(6620)) << "messenger gift received";
    EXPECT_FALSE(SharedWithParty(12101020)) << "messenger gift taken";
    EXPECT_FALSE(SharedWithParty(12417832));
}

// A guest walking into a boss fog sets the boss's "host entered" flag (party_boss_events.py),
// which starts the boss; the guest's fog event is not synced, so the flag goes to the host over
// the party's board. It only holds for this visit, so it never goes into the guest's own world.
TEST(BloodborneBosses, AGuestEnteringTheBossFogStartsTheBossForTheHost) {
    EXPECT_TRUE(SharedWithParty(12414700)) << "Father Gascoigne";
    EXPECT_TRUE(SharedWithParty(12104800)) << "Gehrman";
    EXPECT_TRUE(SharedWithParty(13604808)) << "Orphan of Kos";
    EXPECT_FALSE(SharedWithParty(12414701)) << "a phantom entered (local music)";
    EXPECT_FALSE(CarriesToGuestWorld(12414700));
    EXPECT_FALSE(CarriesToGuestWorld(12414701));
    EXPECT_TRUE(CarriesToGuestWorld(12411700)) << "boss defeated";
}

// A boss start gives the boss a co-op HP effect (7500-7502: max HP x1.5/x2/x2.5). A boss still
// hidden by its entrance on one side (the other side started the fight) keeps its unscaled max
// HP there, so the two games' bars disagreed (Gascoigne: 2031 on the host, 3047 on the guest,
// and his second body outlived the kill). Such a boss is scaled once it shows the effect.
TEST(BloodborneBosses, ABossMissedByTheCoopHpBonusIsScaled) {
    EXPECT_EQ(CoopHpScale(2031, 2031, 2031, std::vector<s32>{7500}),
              (std::optional<std::pair<s32, s32>>{{3047, 3047}}));
    EXPECT_EQ(CoopHpScale(2000, 2000, 1000, std::vector<s32>{7501}),
              (std::optional<std::pair<s32, s32>>{{4000, 2000}}));
    EXPECT_EQ(CoopHpScale(2000, 3000, 3000, std::vector<s32>{7500}), std::nullopt)
        << "already scaled";
    EXPECT_EQ(CoopHpScale(2000, 2000, 2000, std::vector<s32>{}), std::nullopt) << "solo";
    EXPECT_EQ(CoopHpScale(2000, 2000, 0, std::vector<s32>{7500}), std::nullopt) << "dead";
    EXPECT_EQ(CoopHpScale(0, 2000, 2000, std::vector<s32>{7500}), std::nullopt)
        << "never seen without the effect";
}

// A guest's trip (to the Hunter's Dream, say) loads its own world while the session stays up,
// so the session's world changes go in on that load too: the lamp it lit at Gascoigne was
// missing from its headstone, the host's had it.
TEST(BloodborneGuestProgress, ATripLoadsTheSessionsWorldChangesToo) {
    EXPECT_TRUE(SetsGuestWorldChangesOnLoad(false, false)) << "home, solo";
    EXPECT_TRUE(SetsGuestWorldChangesOnLoad(true, true)) << "a party trip";
    EXPECT_FALSE(SetsGuestWorldChangesOnLoad(true, false)) << "the join's reload into the host";
}

// The trip's load can create map items more than once, and the 5 s throttle of the pickup
// pass skipped the Hunter's Dream load entirely (the Central Yharnam lamp was missing from the
// guest's headstone). The world changes go in once per trip, whatever the throttle.
TEST(BloodborneGuestProgress, EachTripGetsTheWorldChangesOnce) {
    OncePerTrip once;
    EXPECT_FALSE(once.Take(false));
    EXPECT_TRUE(once.Take(true));
    EXPECT_FALSE(once.Take(true)) << "same trip";
    EXPECT_FALSE(once.Take(false));
    EXPECT_TRUE(once.Take(true)) << "the next trip";
}

// A party death wakes where the game says (GlobalState +0x1528): the events set it on a death
// (the first one: the Hunter's Dream 2102962; a kidnapping: the Gaol 2802959). In a loaded block
// the player is moved there, otherwise it travels; after that trip it never travels again.
TEST(BloodborneDeaths, APartyDeathWakesWhereTheGameSays) {
    EXPECT_EQ(ChooseDeathRespawn(2412951, true, false, false), DeathRespawn::MoveToPoint);
    EXPECT_EQ(ChooseDeathRespawn(2102962, false, false, false), DeathRespawn::Travel);
    EXPECT_EQ(ChooseDeathRespawn(2102962, false, true, false), DeathRespawn::StayPut);
    EXPECT_EQ(ChooseDeathRespawn(-1, false, false, false), DeathRespawn::StayPut);
    EXPECT_EQ(ChooseDeathRespawn(0, false, false, false), DeathRespawn::StayPut);
}

// SHADPS4_BLOODBORNE_TRACE_FLAG=<flag> points the flag tracer at one flag in a session.
TEST(BloodborneFlagTracer, TheTracedFlagComesFromTheSetting) {
    EXPECT_EQ(TracedFlagSetting("12101020"), std::optional<s32>{12101020});
    EXPECT_EQ(TracedFlagSetting(nullptr), std::nullopt);
    EXPECT_EQ(TracedFlagSetting(""), std::nullopt);
    EXPECT_EQ(TracedFlagSetting("lamp"), std::nullopt);
    EXPECT_EQ(TracedFlagSetting("-5"), std::nullopt);
}

// The game's "set this event flag" message from the other player (0x017C0570) copied the
// Hunter's Dream messengers' "gift given" flags, so one pick took the other's gift. In a party
// those stay as they are locally.
TEST(BloodborneSharedLoot, AMessengerGiftIsEachPlayersOwn) {
    EXPECT_TRUE(KeepsLocalFlag(true, 12101020));
    EXPECT_TRUE(KeepsLocalFlag(true, 12101029));
    EXPECT_FALSE(KeepsLocalFlag(false, 12101020));
    EXPECT_FALSE(KeepsLocalFlag(true, 12101030));
    EXPECT_FALSE(KeepsLocalFlag(true, 12417830)) << "a lamp is shared";
    // The pick in the messenger's talk (721010N0-N2 for messenger N) ended the other player's
    // gift event too when both stood there.
    EXPECT_TRUE(KeepsLocalFlag(true, 72101011));
    EXPECT_TRUE(KeepsLocalFlag(true, 72101092));
    EXPECT_FALSE(KeepsLocalFlag(true, 72101100));
}

// Every member posts the lamps it lit and sets the others' (the game copies only a lamp's
// counters, so a lamp the host lit was missing from the guest's headstone). Only lighting
// travels: a lamp never goes out because another player's game cleared it.
TEST(BloodborneSharedProgression, LampsLitAnywhereAreLitForEveryone) {
    const auto shared = FlagsToShare(FlagChanges{{12417830, 12417831, 7116, 12101020}, {12417811}});
    EXPECT_EQ(shared, (std::vector<std::pair<s32, bool>>{{12417830, true}, {12417831, true}}));
}

// A save slot's kept lists belong to the character that was there: once its pickups have been
// set in its own world, a load where none of them is set is a new character in that slot.
TEST(BloodborneGuestProgress, ANewCharacterInASlotStartsClean) {
    const std::vector<s32> confirmed{52410360, 52410120};
    EXPECT_TRUE(IsNewCharacterInSlot(confirmed, 0));
    EXPECT_FALSE(IsNewCharacterInSlot(confirmed, 1));
    EXPECT_FALSE(IsNewCharacterInSlot({}, 0)) << "nothing confirmed yet";
}

// The menu scripts' "am I a summoned client" answer (0x01A13FD1): a party guest's item dialog
// waited for a confirmation it never offered, so a party member answers "no".
TEST(BloodborneGuestMenus, PartyGuestsAreNotClientsToTheMenus) {
    EXPECT_TRUE(MenuSeesAClient(false, true));
    EXPECT_FALSE(MenuSeesAClient(true, true));
    EXPECT_FALSE(MenuSeesAClient(false, false));
}

TEST(BloodborneSignTimers, EncodesTheGamesOwnTimerStores) {
    EXPECT_EQ(EncodeSignTimerStore(0x47, 5.0f),
              (std::array<u8, 7>{0xC7, 0x47, 0x28, 0x00, 0x00, 0xA0, 0x40}));
    EXPECT_EQ(EncodeSignTimerStore(0x43, 30.0f),
              (std::array<u8, 7>{0xC7, 0x43, 0x28, 0x00, 0x00, 0xF0, 0x41}));
}

// ...so a party member's sign goes up at once and is re-posted every second. A guest only hears
// of a summon (the host's claim) when it re-posts its sign, while the host's connection reaches
// it at once, and its game drops a connection it cannot place after about 5 s: with a re-post
// every 5 s, a claim that came just after one was heard too late (the rig's dog test: the claim
// 73 ms after the guest's post, the guest's game closed the connection 5.2 s later, and the
// delivered claim found nothing to join).
TEST(BloodborneSignTimers, PostsTheFirstSignAtOnceAndRepostsEverySecond) {
    EXPECT_EQ(FirstSignPostDelaySeconds, 0.0f);
    EXPECT_EQ(SignRepostSeconds, 1.0f);
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
void FakeClearReturnState() {
    revive_calls.push_back("ClearReturnState");
}
void PS4_SYSV_ABI FakeReviveLocalCharacter() {
    revive_calls.push_back("ReviveLocalCharacter");
}
void FakeRestoreAliveState() {
    revive_calls.push_back("RestoreAliveState");
}
void PS4_SYSV_ABI FakeSetChrType(u64, s32 entity, s32 type) {
    revive_calls.push_back("SetChrType " + std::to_string(entity) + " " + std::to_string(type));
}
void PS4_SYSV_ABI FakeChrResetAnimation(u64, s32 entity) {
    revive_calls.push_back("ChrResetAnimation " + std::to_string(entity));
}
void FakeWarpToLastLamp() {
    revive_calls.push_back("WarpToLastLamp");
}

constexpr GhostDeathCalls FakeCalls{
    .is_white_ghost = FakeIsWhiteGhost,
    .set_event_flag = FakeSetEventFlag,
    .clear_return_state = FakeClearReturnState,
    .request_full_recover = FakeRequestFullRecover,
    .update_blood_mark = FakeUpDateBloodMark,
    .revive_player = FakeRevivePlayer,
    .revive_local_character = FakeReviveLocalCharacter,
    .restore_alive_state = FakeRestoreAliveState,
    .set_chr_type = FakeSetChrType,
    .chr_reset_animation = FakeChrResetAnimation,
    .warp_to_last_lamp = FakeWarpToLastLamp,
};

} // namespace

// Where the game would open that menu, a party guest gets up again instead: the death event
// (4000) and the "returning to its world" state OnEvent_4000 sets ([SprjEventMan+0x10]+8 = 4;
// any death is ignored while it is above 0, and only a load resets it) are cleared so the next
// death runs, the guest drops its echoes in a bloodstain the way a
// host death does (UpDateBloodMark) and is revived in place. RevivePlayer alone leaves the
// character in its death pose and makes it a live player (type 0), so the white phantom type is
// set back (SetChrType also refreshes the character) and its behaviour is reset to idle.
// RevivePlayer also leaves what the kill and the death processing set (the dead bit at +0x1E3 and
// the action state the death sets): play-testing showed a guest that could attack but not move,
// took no damage and that enemies ignored, so the state saved while it was alive goes back. The
// lock itself is the death's controller flags ([+0x58]: +0x114 bit 0, [+0x38]+0x48 bit 0), which
// 01.09's own local-player revive (0x013179B0) clears along with refilling HP.
// (A summon reload instead needs the host's side of the summon handshake and drops the session.)
// Last, like a death in its own world, it is moved to the last lamp it rested at, which works
// whenever that lamp is in the map the host's world has loaded; otherwise it stays where it fell.
TEST(BloodborneGhostDeath, ADeadPartyGuestRevivesWhereItFell) {
    revive_calls.clear();
    white_ghost = true;
    EXPECT_TRUE(ReviveDeadPartyGuest(FakeCalls));
    EXPECT_EQ(revive_calls, (std::vector<std::string>{
                                "IsWhiteGhost",
                                "SetEventFlag 4000 off",
                                "ClearReturnState",
                                "RequestFullRecover",
                                "UpDateBloodMark",
                                "RevivePlayer",
                                "ReviveLocalCharacter",
                                "RestoreAliveState",
                                "SetChrType 10000 1",
                                "ChrResetAnimation 10000",
                                "WarpToLastLamp",
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
    ASSERT_EQ(patches.size(), 2u);
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

// The alive state to put back is captured from the guest itself while it is alive, since the
// values the death overwrites are not constants.
TEST(BloodborneAliveState, KeepsTheLastStateSeenWhileAlive) {
    AliveStateSnapshot snapshot;
    EXPECT_FALSE(snapshot.Saved().has_value());

    snapshot.Observe(true, AliveState{.flags = 0x11, .action = 0, .action_sub = 1});
    snapshot.Observe(true, AliveState{.flags = 0x22, .action = 0, .action_sub = 1});
    snapshot.Observe(false, AliveState{.flags = 0x23, .action = 2, .action_sub = 2}); // dying
    ASSERT_TRUE(snapshot.Saved().has_value());
    EXPECT_EQ(snapshot.Saved()->flags, 0x22u);
    EXPECT_EQ(snapshot.Saved()->action, 0);
    EXPECT_EQ(snapshot.Saved()->action_sub, 1);
}

// A dead guest's bloodstain in the host's world: the native g_Initialize registers the
// self-bloodstain pickup (event 4032, SelfBloodMark) only for ATTR_LIVE | ATTR_GREY (0x30), so a
// white phantom could never touch it. ATTR_WHITE (0x40) is added.
TEST(BloodborneGuestBloodstain, LetsAWhitePhantomPickUpItsOwnBloodstain) {
    const auto& patches = GuestBloodstainPatches();
    ASSERT_EQ(patches.size(), 1u);
    EXPECT_EQ(patches[0].offset, 0x0137EBC7u);
    EXPECT_EQ(patches[0].original, (std::vector<u8>{0xB9, 0x30, 0x00, 0x00, 0x00}));
    EXPECT_EQ(patches[0].patched, (std::vector<u8>{0xB9, 0x70, 0x00, 0x00, 0x00}));
}

// No echo theft, everywhere (Kaniel): the steal's `je` (to the store of the stolen flag) is
// nopped, so it always returns without taking the bloodstain.
TEST(BloodborneGuestBloodstain, NoEnemyStealsTheBloodstain) {
    const auto& patches = NoEchoTheftPatches();
    ASSERT_EQ(patches.size(), 1u);
    EXPECT_EQ(patches[0].offset, 0x018E7613u);
    EXPECT_EQ(patches[0].original, (std::vector<u8>{0x74, 0x09}));
    EXPECT_EQ(patches[0].patched, (std::vector<u8>{0x90, 0x90}));
}

// Host death (Kaniel): like a guest's, with no world reload. Native SoloPlayDeath_2 (the death
// respawn that reloads the world at the lamp) is replaced for a host with a party: same echo
// drop, revive and lamp teleport as a guest, but it stays a live player and is not checked for
// being a white phantom.
TEST(BloodborneHostDeath, AHostWithAPartyRevivesLikeAGuestWithoutAReload) {
    revive_calls.clear();
    white_ghost = false;
    ReviveDeadPartyHost(FakeCalls);
    EXPECT_EQ(revive_calls, (std::vector<std::string>{
                                "SetEventFlag 4000 off",
                                "ClearReturnState",
                                "RequestFullRecover",
                                "UpDateBloodMark",
                                "RevivePlayer",
                                "ReviveLocalCharacter",
                                "RestoreAliveState",
                                "ChrResetAnimation 10000",
                                "WarpToLastLamp",
                            }));
}

// Only while hosting: a solo player (or a host whose room is gone) dies the game's way.
// A guest that travelled is a live player (type 0), so its death takes the live player's path
// (SoloPlayDeath_2, a reload at the lamp) too; it revives in place like the host.
TEST(BloodborneHostDeath, OnlyAPlayerInASessionSkipsTheReload) {
    EXPECT_TRUE(ShouldReviveHostInPlace(MatchingStateHosting));
    EXPECT_TRUE(ShouldReviveHostInPlace(MatchingStateSummonedGuest));
    for (const s32 state : {0, 1, 2, 4, 5, 7}) {
        EXPECT_FALSE(ShouldReviveHostInPlace(state)) << "state " << state;
    }
}

// Play-test: after a host death the guest still saw the host, but the host no longer had the
// guest's character. The character death step (0x018C49F0) walks the session's members when the
// local player is hosting (state 3) and removes them, since unpatched the host is about to
// reload. With no reload that branch is skipped: `jne` past it becomes a plain `jmp`.
TEST(BloodborneHostDeath, TheHostKeepsItsGuestsCharactersWhenItDies) {
    const auto& patch = HostDeathKeepGuestsPatches().at(1);
    EXPECT_EQ(patch.offset, 0x018C4D1Du);
    EXPECT_EQ(patch.original, (std::vector<u8>{0x0F, 0x85, 0x97, 0x00, 0x00, 0x00}));
    EXPECT_EQ(DecodeNearJumpTarget(patch.offset, std::span{patch.patched}.first(5)), 0x018C4DBAu);
    EXPECT_EQ(patch.patched.back(), 0x90);
}

// Random crashes left logs that just stop (the logger never flushed). The crash catcher writes
// one line itself: the exception, where it happened and, inside the game, the eboot offset that
// the RE notes use.
TEST(BloodborneCrashes, TheCrashLineNamesTheGameOffset) {
    EXPECT_EQ(FormatCrashLine(0xC0000005, 0x8018BF9A0, 0x800000000, 0x50DC000, "Game:Main"),
              "crash: exception 0xc0000005 at 0x8018bf9a0 (eboot+0x18bf9a0) thread Game:Main");
    EXPECT_EQ(FormatCrashLine(0xC0000005, 0x7FF612345678, 0x800000000, 0x50DC000, ""),
              "crash: exception 0xc0000005 at 0x7ff612345678 (emulator) thread ?");
}

// The host summoned the guest a second time 48 s after both closed their connection; the host's
// emulator reused the lingering connection and the guest's game never heard the invite. And in
// the rig's guest-crash test the guest's game died without closing anything: the host's game
// closed its connection, opened it again for the restarted guest, got the old one back, and the
// new guest never got a handshake. A connection this game closed is never reused.
TEST(BloodborneSignaling, AConnectionThisGameClosedIsNotReused) {
    using Libraries::Np::NpSignaling::LingeringConnectionIsFinished;
    EXPECT_TRUE(LingeringConnectionIsFinished(true, true, true, false));
    EXPECT_TRUE(LingeringConnectionIsFinished(true, true, false, false))
        << "the peer never closed it (its game died)";
    EXPECT_FALSE(LingeringConnectionIsFinished(true, false, true, false)) << "we are still on";
    EXPECT_FALSE(LingeringConnectionIsFinished(false, true, true, false)) << "never established";
    EXPECT_FALSE(LingeringConnectionIsFinished(true, true, true, true)) << "already dead";
}

// The guest's stack is not in the minidump (it is not a host thread stack), so the report
// lists the eboot addresses found on it: the return addresses of the crashing call chain.
TEST(BloodborneCrashes, TheReportListsTheGameAddressesOnTheStack) {
    const std::vector<u64> stack{0x30FD32890, 0x8018E6D48, 0x7EFB4E310, 0x801A2BFA8, 0x900000000};
    EXPECT_EQ(FormatGameAddresses(stack, 0x800000000, 0x50DC000, 8),
              " eboot+0x18e6d48 eboot+0x1a2bfa8");
    EXPECT_EQ(FormatGameAddresses(stack, 0x800000000, 0x50DC000, 1), " eboot+0x18e6d48");
}

// Side by side the game shares kill echoes itself; apart (a guest alone in the Cathedral Ward,
// the host in Central Yharnam) only the killer got them. A kill made with no other member on the
// map posts its echoes to the party's echo board, and the others add them.
TEST(BloodborneSharedEchoes, KillsMadeApartRewardTheWholeParty) {
    EXPECT_TRUE(SharesKillEchoes(0x018FC99B, true)) << "an enemy kill";
    EXPECT_TRUE(SharesKillEchoes(0x01385263, true)) << "a boss kill";
    EXPECT_FALSE(SharesKillEchoes(0x018FC99B, false)) << "side by side the game shares them";
    EXPECT_FALSE(SharesKillEchoes(0x0132E4B5, true)) << "not a kill (an item, a bloodstain)";
    EXPECT_EQ(EncodeEchoUpdate("guest", 3, std::vector<KillEchoes>{{120, 0}, {4000, 0}}),
              "guest 3 120 4000");
    EXPECT_EQ(EncodeEchoUpdate("host", 0, std::vector<KillEchoes>{}), "host 0");
    EXPECT_EQ(ParseEchoBoard("5\n120\n4000\n"),
              (std::optional<EchoBoardAnswer>{EchoBoardAnswer{5, {{120, 0}, {4000, 0}}}}));
    EXPECT_EQ(ParseEchoBoard("2\n"), (std::optional<EchoBoardAnswer>{EchoBoardAnswer{2, {}}}));
    EXPECT_EQ(ParseEchoBoard("x\n"), std::nullopt);
    EXPECT_EQ(ParseEchoBoard("2\n-5\n"), std::nullopt);
}

// Lamps, boss fog entries and trophies go between the members whenever they are a party, not
// only while in a session: a guest on its own trip (the Hunter's Dream) is out of one, and
// what the host did meanwhile only reached it after rejoining, if at all.
TEST(BloodborneSharedProgression, ThePartySharesInAndOutOfASession) {
    EXPECT_TRUE(SharesWithParty(true, true));
    EXPECT_FALSE(SharesWithParty(true, false)) << "loading: no player to apply them to";
    EXPECT_FALSE(SharesWithParty(false, true)) << "no party";
}

// A member arriving at a map loads it with every enemy alive; the session only carries what
// happens while both are there, so the wolf the host had killed stood alive on the guest's side
// (a leftover only the guest could fight). Every member posts the enemies it killed during its
// visit to a map (handles are the same in every game), and takes them back when the visit ends.
TEST(BloodborneSharedKills, AMembersKillsLastForItsVisit) {
    VisitKills kills;
    using Changes = std::vector<std::pair<s32, bool>>;
    const std::vector<SeenCharacter> alive{{0x10014085, 300}, {0x10014088, 250}};
    EXPECT_EQ(kills.Observe(1, alive), Changes{});
    const std::vector<SeenCharacter> one_dead{{0x10014085, 0}, {0x10014088, 250}};
    EXPECT_EQ(kills.Observe(1, one_dead), (Changes{{0x10014085, true}}));
    EXPECT_EQ(kills.Observe(1, one_dead), Changes{}) << "posted once";
    // A new visit (a trip, a reload): its kills there respawn.
    EXPECT_EQ(kills.Observe(2, alive), (Changes{{0x10014085, false}}));
    EXPECT_EQ(kills.Observe(2, alive), Changes{});
    // Seen dead from the start of a visit (already dead when it loaded): not this visit's kill.
    VisitKills fresh;
    EXPECT_EQ(fresh.Observe(3, one_dead), Changes{});
    EXPECT_EQ(fresh.Observe(3, one_dead), Changes{});
}

// What ends a visit is what respawns the enemies for that member: a trip (lamp travel, the
// Dream, a death sent home), another map, or a reload outside a session. A guest pulled into
// the host's world when the host arrives reloads the same map in a session: its kills there
// stand (friend run 2026-10-08: the guest killed the dog, the host came back, the guest's
// summon withdrew the kill and the host's dog stood alive).
TEST(BloodborneSharedKills, ASummonIntoTheHostsWorldKeepsTheGuestsVisit) {
    VisitTracker visits;
    const u64 first = visits.Update(0x18010000, 0x1000, false, 0);
    EXPECT_EQ(visits.Update(0x18010000, 0x1000, false, 0), first);
    EXPECT_EQ(visits.Update(0x18010000, 0x2000, true, 0), first) << "summoned on the same map";
    const u64 tripped = visits.Update(0x18010000, 0x3000, true, 1);
    EXPECT_NE(tripped, first) << "a trip to a lamp on the same map";
    const u64 moved = visits.Update(0x15000000, 0x3000, true, 1);
    EXPECT_NE(moved, tripped) << "another map";
    const u64 back = visits.Update(0x18010000, 0x4000, true, 1);
    EXPECT_NE(back, first);
    EXPECT_NE(back, moved);
    EXPECT_NE(visits.Update(0x18010000, 0x5000, false, 1), back) << "a reload out of a session";

    // The killer hides its own kills too while it stands in another world's copy of the map.
    VisitKills kills;
    kills.Observe(back, std::vector<SeenCharacter>{{0x10014085, 300}});
    kills.Observe(back, std::vector<SeenCharacter>{{0x10014085, 0}});
    EXPECT_EQ(kills.Killed(), (std::vector<u32>{0x10014085}));
    kills.Observe(back + 1, std::vector<SeenCharacter>{{0x10014085, 300}});
    EXPECT_TRUE(kills.Killed().empty());
}

// Rig test "dog" 2026-10-09: the host left, the guest's own world loaded on the same map (the
// visit goes on: a reload in a session keeps it), and two enemies dead in the guest's save but
// alive in the host's world a moment before were posted as kills; the host hid 2 living
// enemies. A kill is one character object going from alive to dead: a reload makes new ones.
// The rig's dog test, with the kills named: when the host left for the Dream its guest took over
// the map's enemies, and its own world (where it had killed Father Gascoigne, 0x1001409c and his
// beast form 0x1001409e, dead flags 0x26) put him down on the spot; posted as kills, they hid the
// host's living Gascoigne when the host came back. A death while a character settles after its
// takeover or hand-back is the member's world state, not a kill: seen, not posted. Only that
// character: a guest alone keeps taking over enemies as they load, and fights the others.
TEST(BloodborneSharedKills, ATakeoverIsNoKill) {
    VisitKills kills;
    using Changes = std::vector<std::pair<s32, bool>>;
    using Seen = std::vector<SeenCharacter>;
    EXPECT_EQ(kills.Observe(1, Seen{{0x1001409c, 2031, 0xA000}, {0x1001400b, 484, 0xB000}}),
              Changes{});
    EXPECT_EQ(kills.Observe(1, Seen{{0x1001409c, 0, 0xA000, true}, {0x1001400b, 0, 0xB000}}),
              (Changes{{0x1001400b, true}}))
        << "Gascoigne settling after his takeover; the other one killed meanwhile";
    EXPECT_EQ(kills.Observe(1, Seen{{0x1001409c, 0, 0xA000}, {0x1001400b, 0, 0xB000}}), Changes{})
        << "still dead once settled: not a kill either";
    EXPECT_GT(EnemyOwnerChangeSettles, std::chrono::seconds{0});
}

// With the kill board naming them: the two false kills came as the guest was summoned into the
// host's world (226 flags changed at once, a load that keeps the map's objects), where Father
// Gascoigne was dead and the guest's copy alive a moment before. A character settles for
// EnemyOwnerChangeSettles after such a load, and after its takeover or hand-back.
TEST(BloodborneSharedKills, ACharacterSettlesAfterALoadOrATakeover) {
    constexpr s64 Settle =
        std::chrono::duration_cast<std::chrono::milliseconds>(EnemyOwnerChangeSettles).count();
    EXPECT_TRUE(CharacterSettles(100'000, 100'000 - Settle + 1, std::nullopt)) << "after a load";
    EXPECT_FALSE(CharacterSettles(100'000, 100'000 - Settle, std::nullopt)) << "settled";
    EXPECT_TRUE(CharacterSettles(100'000, 0, 99'000)) << "just taken over";
    EXPECT_FALSE(CharacterSettles(100'000, 0, 100'000 - Settle)) << "taken over long ago";
    EXPECT_FALSE(CharacterSettles(100'000, 0, std::nullopt)) << "nothing happened";
}

TEST(BloodborneSharedKills, AWorldThatLoadsIsNoKill) {
    VisitKills kills;
    using Changes = std::vector<std::pair<s32, bool>>;
    using Seen = std::vector<SeenCharacter>;
    EXPECT_EQ(kills.Observe(1, Seen{{0x1001409c, 2031, 0xA000}}), Changes{});
    EXPECT_EQ(kills.Observe(1, Seen{{0x1001409c, 0, 0xB000}}), Changes{})
        << "dead already in the world that loaded";
    EXPECT_EQ(kills.Observe(1, Seen{{0x1001409c, 2031, 0xC000}}), Changes{});
    EXPECT_EQ(kills.Observe(1, Seen{{0x1001409c, 0, 0xC000}}), (Changes{{0x1001409c, true}}))
        << "killed in this world";
    EXPECT_EQ(kills.Killed(), (std::vector<u32>{0x1001409c}));
}

// Friend run 2026-10-09, Gascoigne: the host's copy went 2031 -> 131 -> 0, the guest's only
// moved with the guest's own hits (2031 -> 1753). Each member posts the enemies it hurt this
// visit and takes the lowest share of HP left.
TEST(BloodborneSharedEnemyHp, EveryMemberSeesTheLowestHpLeft) {
    const std::vector<EnemyHp> hurt{{0x1001409c, 1595, 2031}, {0x10014085, 120, 300}};
    const auto bytes = EncodeEnemyHp(0x1801000000000003, hurt);
    EXPECT_EQ(bytes.size(), 8u + 2 * 12u);
    EXPECT_EQ(DecodeEnemyHp(bytes), (EnemyHpReport{0x1801000000000003, hurt}));
    EXPECT_EQ(DecodeEnemyHp(EncodeEnemyHp(7, {})), (EnemyHpReport{7, {}})) << "nothing hurt";
    EXPECT_EQ(DecodeEnemyHp(std::vector<u8>{1, 2, 3}), std::nullopt);
    EXPECT_EQ(DecodeEnemyHp(std::vector<u8>(8 + 5)), std::nullopt) << "a cut entry";

    EXPECT_EQ(SharedEnemyHp(1753, 2031, 1595, 2031), 1595) << "the host hit harder";
    EXPECT_EQ(SharedEnemyHp(1595, 2031, 1753, 2031), std::nullopt) << "never back up";
    EXPECT_EQ(SharedEnemyHp(3047, 3047, 1016, 2031), 1525) << "a third left, co-op bonus here";
    EXPECT_EQ(SharedEnemyHp(500, 2031, 0, 2031), 1) << "the game kills it, not the board";
    EXPECT_EQ(SharedEnemyHp(0, 2031, 100, 2031), std::nullopt) << "already dead";
    EXPECT_EQ(SharedEnemyHp(500, 2031, 100, 0), std::nullopt);
}

// Kaniel 2026-10-10, Iosefka's Clinic: the guest saw Iosefka standing at her door while the
// host's story hid her (event disable byte +0x28). After its join the guest's world is rebuilt as
// the host's client and does not run the events that hide her, so every character the host's
// world hides or shows is mirrored to the guests on its map.
TEST(BloodborneHiddenCharacters, TheHostsHiddenCharactersTravelAsHandlesAndAByte) {
    const std::vector<CharacterShown> host{{0x10014005, true}, {0x1001400b, false}};
    const auto bytes = EncodeShownCharacters(host);
    EXPECT_EQ(bytes.size(), 2 * 5u);
    EXPECT_EQ(DecodeShownCharacters(bytes), host);
    EXPECT_EQ(DecodeShownCharacters(EncodeShownCharacters({})), std::vector<CharacterShown>{});
    EXPECT_EQ(DecodeShownCharacters(std::vector<u8>{1, 2, 3}), std::nullopt) << "a cut entry";
}

TEST(BloodborneHiddenCharacters, AGuestHidesAndShowsWhatTheHostDoes) {
    const std::vector<CharacterShown> host{
        {0x10014005, true},  // Iosefka, hidden by the host's story
        {0x100140ae, true},  // hidden on both already
        {0x1001400b, false}, // an enemy both show
        {0x10014053, false}, // shown by the host, hidden on the guest
    };
    const std::vector<CharacterShown> guest{
        {0x10014005, false}, {0x100140ae, true}, {0x1001400b, false},
        {0x10014053, true},  {0x10014099, true}, // the host has no such character: left alone
    };
    EXPECT_EQ(ShownLikeTheHost(host, guest, {}),
              (std::vector<CharacterShown>{{0x10014005, true}, {0x10014053, false}}));
    EXPECT_EQ(ShownLikeTheHost(host, guest, {0x10014053}),
              (std::vector<CharacterShown>{{0x10014005, true}}))
        << "one the kill board hid here (another member killed it) stays hidden";
    EXPECT_EQ(ShownLikeTheHost({}, guest, {}), std::vector<CharacterShown>{})
        << "nothing from the host: nothing changes";
}

// Kaniel 2026-10-10, the dog by the first lamp: before the guest joined, its own world posted
// its hurt dog to the HP board and the host lowered its dog to it; as the session formed the
// host's dog died (+235 echoes, its loot on the ground). Only damage done in the session counts:
// out of one nothing is posted, and an enemy hurt before counts from where it stood then.
TEST(BloodborneSharedEnemyHp, OnlyDamageDoneInTheSessionIsShared) {
    VisitDamage damage;
    const std::vector<EnemyHp> dog_hurt{{0x10014009, 5, 364}};
    EXPECT_EQ(damage.Hurt(1, false, dog_hurt), std::vector<EnemyHp>{})
        << "the guest's own world, before its join";
    EXPECT_EQ(damage.Hurt(1, true, dog_hurt), std::vector<EnemyHp>{})
        << "the session formed: hurt before it, not by it";
    const std::vector<EnemyHp> dog_full{{0x10014009, 364, 364}};
    EXPECT_EQ(damage.Hurt(1, true, dog_full), std::vector<EnemyHp>{})
        << "its world rebuilt as the host's client: the host's dog";
    const std::vector<EnemyHp> dog_hit{{0x10014009, 200, 364}};
    EXPECT_EQ(damage.Hurt(1, true, dog_hit), dog_hit) << "hit in the session";
    EXPECT_EQ(damage.Hurt(1, false, dog_hit), std::vector<EnemyHp>{}) << "the session ended";
    EXPECT_EQ(damage.Hurt(1, true, dog_hit), std::vector<EnemyHp>{})
        << "a new session starts from the HP then";
    EXPECT_EQ(damage.Hurt(2, true, dog_hit), std::vector<EnemyHp>{}) << "a new visit too";
    const std::vector<EnemyHp> dog_lower{{0x10014009, 150, 364}};
    EXPECT_EQ(damage.Hurt(2, true, dog_lower), dog_lower);
}

// Rig test 2026-10-09 ("apart"): a guest's trip out of the host's map posted 2 kills at its
// start (the old map's characters going away), so the host hid 2 living enemies while the guest
// was in the Dream. Deaths are only watched while the world stands: not on a trip or a load.
TEST(BloodborneSharedKills, NoKillsAreWatchedWhileTheWorldGoesAway) {
    EXPECT_TRUE(WatchesKills(true, false, false));
    EXPECT_FALSE(WatchesKills(true, true, false)) << "a party trip under way";
    EXPECT_FALSE(WatchesKills(true, false, true)) << "a load";
    EXPECT_FALSE(WatchesKills(false, false, false)) << "not sharing";
}

// What counts as a kill is a real death, from a character's death flags (+0x1E3): bit 0 from the
// game's kill (an event's ForceCharacterDeath, 0x018BFAD3), bit 2 from a death by damage (set
// after the kill reward, 0x018C52D1). Only bit 0 counted at first, so a fight's kills never
// reached the kill board (the rig: the host killed an enemy, +995 echoes, no kill posted; a
// friend run: 67 kill rewards, 2 kills posted). 0 HP alone is not one: a map torn down reads 0.
TEST(BloodborneSharedKills, AKillIsADeathTheGameMarked) {
    EXPECT_TRUE(IsRealDeath(0x01)) << "the game's kill";
    EXPECT_TRUE(IsRealDeath(0x04)) << "killed in a fight";
    EXPECT_TRUE(IsRealDeath(0x05));
    EXPECT_FALSE(IsRealDeath(0x00)) << "alive, or a map going away";
    EXPECT_FALSE(IsRealDeath(0x08)) << "bit 3 is something else (set and cleared at 0x018C2605)";
    EXPECT_FALSE(IsRealDeath(0xF2));
}

// The dog test again (2026-10-09, after a reboot): the host left for the Dream and the guest's own
// world put down Father Gascoigne, beaten in its save, a second before the guest saw the host
// gone, so the settle after its takeover came too late. A world's put-down has bit 5 (0x26, 48
// times in the day's rig logs); a fight's kill reads 0x6 and stays so (21 later snapshots).
TEST(BloodborneSharedKills, AnEnemyTheWorldPutsDownIsNoKill) {
    EXPECT_FALSE(IsRealDeath(0x26)) << "Gascoigne, dead in the guest's save";
    EXPECT_TRUE(IsRealDeath(0x06)) << "killed in a fight";
}

// A guest's trip loads the host's map with the guest's own save: a dog killed for good on the
// host's save stood alive for it. The host posts its map's flags (10000 from the map's base:
// 1241xxxx for m24_01) and the guest loading that map takes them, except its own pickups, the
// messengers' gifts and a boss fog's entered flags.
TEST(BloodborneSharedWorld, AGuestOnTheHostsMapTakesTheHostsState) {
    EXPECT_EQ(MapFlagBase(0x18010000), 12410000) << "Central Yharnam";
    EXPECT_EQ(MapFlagBase(0x18000000), 12400000) << "Cathedral Ward";
    EXPECT_EQ(MapFlagBase(0x15000000), 12100000) << "Hunter's Dream";
    EXPECT_TRUE(TakesHostMapFlag(12410102)) << "a shortcut";
    EXPECT_TRUE(TakesHostMapFlag(12411700)) << "a boss killed";
    EXPECT_FALSE(TakesHostMapFlag(52410360)) << "outside the map's range";
    EXPECT_FALSE(TakesHostMapFlag(12101020)) << "a messenger gift";
    EXPECT_FALSE(TakesHostMapFlag(12414700)) << "a boss fog entered";
    EXPECT_FALSE(TakesHostMapFlag(12414701));
    const std::vector<u8> bits{0x00, 0xFF, 0x10};
    EXPECT_EQ(EncodeMapStateUpdate("host", 0x18010000, bits), "host 402718720 00ff10");
    EXPECT_EQ(EncodeMapStateUpdate("guest", 0, {}), "guest 0");
    const auto board = ParseMapStateBoard("402718720 00ff10\n");
    ASSERT_TRUE(board.has_value());
    ASSERT_EQ(board->size(), 1u);
    EXPECT_EQ((*board)[0].map, 0x18010000u);
    EXPECT_EQ((*board)[0].bits, bits);
    EXPECT_EQ(ParseMapStateBoard(""), (std::optional<std::vector<MemberMapState>>{{}}));
    EXPECT_EQ(ParseMapStateBoard("1 0g\n"), std::nullopt);
}

// A guest that played for hours in a session and quit restarted where it was first summoned
// from (its save's position) and had to walk back. Its last map and position are remembered
// (every few seconds, in and out of a session) and the next start goes back there: a trip to the
// map's first lamp when it loads elsewhere, then a move to the position. A new character in the
// same slot (far fewer event flags set than when the point was kept) starts where it starts.
TEST(BloodborneGuestResume, AGuestResumesWhereItLeft) {
    const ResumePoint point{0x18000000, {-120.5f, 10.25f, 33.0f}, 900};
    EXPECT_EQ(EncodeResumePoint(point), "402653184 -120.5 10.25 33 900");
    EXPECT_EQ(ParseResumePoint("402653184 -120.5 10.25 33 900"), std::optional<ResumePoint>{point});
    EXPECT_EQ(ParseResumePoint("402653184 1 2 3"), std::nullopt);
    EXPECT_EQ(ParseResumePoint(""), std::nullopt);

    EXPECT_EQ(MapWarpPoint(0x18000000), 2402950) << "the Cathedral Ward's first lamp";
    EXPECT_EQ(MapWarpPoint(0x18010000), 2412950);
    EXPECT_EQ(MapWarpPoint(0x15000000), 2102962) << "the Hunter's Dream: its headstones' point";

    using Kind = ResumeStep::Kind;
    const LampPosition near{-121.0f, 10.0f, 33.5f};
    const LampPosition far{50.0f, 0.0f, 0.0f};
    EXPECT_EQ(ChooseResumeStep(point, 0x18000000, near, 905).kind, Kind::Stay) << "already there";
    EXPECT_EQ(ChooseResumeStep(point, 0x18000000, far, 905).kind, Kind::Move);
    const ResumeStep travel = ChooseResumeStep(point, 0x18010000, far, 900);
    EXPECT_EQ(travel.kind, Kind::Travel);
    EXPECT_EQ(travel.warp_point, 2402950);
    EXPECT_EQ(ChooseResumeStep(ResumePoint{0, {}, 0}, 0x18010000, far, 900).kind, Kind::Stay)
        << "nothing remembered";
    EXPECT_EQ(ChooseResumeStep(point, 0x18010000, far, 120).kind, Kind::Forget)
        << "a new character in the slot";
}

// The point is about where the game loaded the guest: a guest that travels on (a lamp, a trip)
// before it is settled there has chosen where to be. The rig's guest went to the Cathedral Ward
// right after loading, and the resume took it back to Central Yharnam, away from its host.
TEST(BloodborneGuestResume, NotOnceTheGuestTravelledOn) {
    using Kind = ResumeStep::Kind;
    const ResumePoint point{0x18010000, {-199.7f, -50.8f, 179.4f}, 900};
    const LampPosition ward{17.6f, -9.5f, 102.7f};
    EXPECT_EQ(ChooseResumeStep(point, 0x18000000, ward, 900, 0x18010000).kind, Kind::Stay)
        << "loaded in Central Yharnam, then travelled to the Ward";
    EXPECT_EQ(ChooseResumeStep(point, 0x18000000, ward, 900, 0x18000000).kind, Kind::Travel)
        << "loaded in the Ward: back to the point";
    EXPECT_EQ(ChooseResumeStep(point, 0x18000000, ward, 900).kind, Kind::Travel)
        << "where it loaded not known: as before";
}
