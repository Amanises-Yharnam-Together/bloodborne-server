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

// Independent travel: a guest whose host left stays where it is, still a white phantom in the
// host's old world. Searching from there would pull it to wherever the host went.
TEST(BloodbornePartyController, AGuestLeftBehindByItsHostDoesNotSearch) {
    PartyController controller(PartyRole::Guest);
    PartyObservation left_behind = Idle();
    left_behind.white_phantom = true;
    EXPECT_EQ(TickUntilDecision(controller, left_behind, T0), PartyAction::Wait);
}

// A guest that travelled on its own is in its own world; it rejoins only once it is back on the
// host's map, so the cross-map join does not pull it to wherever the host is.
TEST(BloodbornePartyController, AGuestApartWaitsUntilItIsOnTheHostsMap) {
    PartyController controller(PartyRole::Guest);
    PartyObservation apart = Idle();
    apart.waiting_to_meet = true;
    EXPECT_EQ(TickUntilDecision(controller, apart, T0), PartyAction::Wait);
    EXPECT_EQ(TickUntilDecision(controller, Idle(), T0), PartyAction::StartSearch);
}

// The guest is still in the old session for a moment after its trip starts; only joining a
// session again ends "apart".
TEST(BloodborneGuestApart, EndsOnlyWithTheNextJoin) {
    GuestApart apart;
    apart.Tick(true);
    EXPECT_FALSE(apart.Apart());
    apart.Travelled();
    apart.Tick(true); // the session it travelled out of
    EXPECT_TRUE(apart.Apart());
    apart.Tick(false);
    EXPECT_TRUE(apart.Apart());
    apart.Tick(true); // joined again
    EXPECT_FALSE(apart.Apart());
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
    trip.Start(T0, 0x18010000);
    EXPECT_TRUE(trip.Active(T0 + 1s));
    trip.Finish();
    EXPECT_FALSE(trip.Active(T0 + 2s));
}

// The trip ends at the first tick on the new stage (the party tick does not run while loading).
TEST(BloodbornePartyTrip, EndsOnceThePlayerIsOnTheNewStage) {
    constexpr u32 CentralYharnam = 0x18010000;
    constexpr u32 HuntersDream = 0x15000000;
    PartyTrip trip;
    trip.Start(T0, CentralYharnam);
    trip.ObserveStage(CentralYharnam, false);
    EXPECT_TRUE(trip.Active(T0 + 1s)) << "still on the old stage";
    trip.ObserveStage(HuntersDream, true);
    EXPECT_TRUE(trip.Active(T0 + 2s)) << "still loading";
    trip.ObserveStage(HuntersDream, false);
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

TEST(BloodbornePartyTrip, ATripThatNeverLoadsStopsCountingAfterAWhile) {
    PartyTrip trip;
    trip.Start(T0, 0x18010000);
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
TEST(BloodborneHostDeath, OnlyAHostWithASessionSkipsTheReload) {
    EXPECT_TRUE(ShouldReviveHostInPlace(MatchingStateHosting));
    for (const s32 state : {0, 1, 2, 4, 5, MatchingStateSummonedGuest, 7}) {
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
