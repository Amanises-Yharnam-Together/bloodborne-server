// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/bloodborne_party.h"

using namespace Core::Bloodborne;

namespace {

// Central Yharnam's first lamp (m24_01, lamp 0): 70000100 + 241 * 10000 + 0.
constexpr s32 CentralYharnamLamp = 72410100;
constexpr s32 GreatBridgeLamp = 72410101;
constexpr LampPosition AtLamp{10.0f, 0.0f, 5.0f};

} // namespace

TEST(BloodborneLampRest, RecognisesTheFlagALampsTravelDialogSets) {
    EXPECT_TRUE(IsLampTravelFlag(CentralYharnamLamp));
    EXPECT_TRUE(IsLampTravelFlag(GreatBridgeLamp));
    // A chalice dungeon lamp (t290110 sets the same pattern with its own map code).
    EXPECT_TRUE(IsLampTravelFlag(72900105));
    // Lighting a lamp for the first time (7MMM0200 + lamp) is left to the game.
    EXPECT_FALSE(IsLampTravelFlag(72410200));
    // The Doll's dialog in the Hunter's Dream sets 721001xx; the Dream has no lamp to travel
    // from (a guest levelling up was warped and kicked).
    EXPECT_FALSE(IsLampTravelFlag(72100100));
    EXPECT_FALSE(IsLampTravelFlag(72100107));
    // Gilbert's talk at his window by the Central Yharnam lamp (t241319) sets 70000140, a flag of
    // no map: a guest talking to him was sent to the Dream (Kaniel 2026-10-09, rig
    // guest-talk-gilbert 2026-10-10).
    EXPECT_FALSE(IsLampTravelFlag(70000140));
    EXPECT_FALSE(IsLampTravelFlag(70000100));
    // Nor 799001xx (no map 990), in the talks of many NPCs (t241190 and t241335 in Central
    // Yharnam, t210140 in the Dream).
    EXPECT_FALSE(IsLampTravelFlag(79900104));
    EXPECT_FALSE(IsLampTravelFlag(79900111));
    // A later area's lamp still is (m34: t340502 asks for 73400120).
    EXPECT_TRUE(IsLampTravelFlag(73400120));
    // The Hunter's Dream headstones and anything else are not lamps.
    EXPECT_FALSE(IsLampTravelFlag(72100000));
    EXPECT_FALSE(IsLampTravelFlag(6707));
    EXPECT_FALSE(IsLampTravelFlag(4000));
    EXPECT_FALSE(IsLampTravelFlag(-1));
}

// Party rests are work in progress (Kaniel, 2026-10-06): lamps travel as usual unless
// SHADPS4_BLOODBORNE_LAMP_REST=1 turns the rests back on.
TEST(BloodborneLampRest, PartyRestsAreOffUnlessSwitchedOn) {
    EXPECT_FALSE(LampRestsEnabled(nullptr));
    EXPECT_FALSE(LampRestsEnabled(""));
    EXPECT_FALSE(LampRestsEnabled("0"));
    EXPECT_TRUE(LampRestsEnabled("1"));
}

// Talk scripts gate their prompt on ESD function 14, the talking player's character type: a
// white phantom (1) gets no lamp prompt. A party guest talks like the host (0) instead.
TEST(BloodborneLampRest, APartyGuestTalksToLampsLikeTheHost) {
    EXPECT_TRUE(TalksAsHost(true, WhiteGhostCharacterType));
    EXPECT_FALSE(TalksAsHost(false, WhiteGhostCharacterType)) << "co-op with strangers";
    EXPECT_FALSE(TalksAsHost(true, 0)) << "already the host";
    EXPECT_FALSE(TalksAsHost(true, 2)) << "an invader";
}

// Common event 7200 (the lamp's trip) ends at once for a client, so a party guest's trip is done
// by the emulator: WarpNextStage_Bonfire to the Hunter's Dream lamp, 2102962. The binding packs
// the map from the warp point the way this does.
TEST(BloodborneLampRest, AGuestsLampTripGoesToTheHuntersDreamLamp) {
    EXPECT_EQ(HuntersDreamWarpPoint, 2102962);
    EXPECT_EQ(PackedMapOfWarpPoint(HuntersDreamWarpPoint), 0x15000000u);
    EXPECT_EQ(PackedMapOfWarpPoint(2412951), 0x18010000u); // Central Yharnam's first lamp
    // By role, not character type: after a trip the guest is no longer a white phantom.
    EXPECT_TRUE(GuestMakesItsOwnLampTrip(true, PartyRole::Guest));
    EXPECT_FALSE(GuestMakesItsOwnLampTrip(true, PartyRole::Host))
        << "the host's trip is the game's";
    EXPECT_FALSE(GuestMakesItsOwnLampTrip(false, PartyRole::Guest));
}

// The Hunter's Dream headstones set 7210MMML (map MMM, lamp L) for event 12107000, which ends for
// a client too; the lamp it leads to is respawn point MMM2950 + L.
TEST(BloodborneLampRest, AGuestsHeadstoneTripGoesToTheChosenLamp) {
    EXPECT_TRUE(IsHeadstoneTravelFlag(72102411));
    EXPECT_EQ(HeadstoneWarpPoint(72102411), 2412951); // Central Yharnam, Great Bridge
    EXPECT_EQ(HeadstoneWarpPoint(72102200), 2202950);
    EXPECT_EQ(HeadstoneWarpPoint(72103401), 3402951);
    EXPECT_EQ(HeadstoneWarpPoint(72102110), 2112950);
    // Chalice and dungeon warps of the Dream are something else.
    EXPECT_FALSE(IsHeadstoneTravelFlag(72100300));
    EXPECT_FALSE(IsHeadstoneTravelFlag(72100420));
    EXPECT_FALSE(IsHeadstoneTravelFlag(72410101)) << "a lamp";
}

TEST(BloodborneLampRest, OnlyAPartySessionTreatsLampsAsCampfires) {
    EXPECT_TRUE(IsPartySession(true, MatchingStateHosting));
    EXPECT_TRUE(IsPartySession(true, MatchingStateSummonedGuest));
    EXPECT_FALSE(IsPartySession(true, 0));
    // Co-op with strangers stays vanilla.
    EXPECT_FALSE(IsPartySession(false, MatchingStateHosting));
}

TEST(BloodborneLampRest, RestingEndsWhenThePlayerWalksAwayFromTheLamp) {
    LampRestState rest;
    EXPECT_EQ(rest.Tick(AtLamp, true), std::nullopt);

    rest.Rest(CentralYharnamLamp, AtLamp);
    EXPECT_EQ(rest.Tick(AtLamp, true), CentralYharnamLamp);
    EXPECT_EQ(rest.Tick(LampPosition{12.0f, 0.5f, 6.0f}, true), CentralYharnamLamp);
    // No position this tick (loading, dead): still resting.
    EXPECT_EQ(rest.Tick(std::nullopt, true), CentralYharnamLamp);

    EXPECT_EQ(
        rest.Tick(LampPosition{10.0f, 0.0f, 5.0f + LampRestState::LeaveDistance + 0.1f}, true),
        std::nullopt);
    EXPECT_EQ(rest.Tick(AtLamp, true), std::nullopt) << "walking back is not resting again";
}

TEST(BloodborneLampRest, RestingEndsWithTheSession) {
    LampRestState rest;
    rest.Rest(CentralYharnamLamp, AtLamp);
    EXPECT_EQ(rest.Tick(AtLamp, false), std::nullopt);
    EXPECT_EQ(rest.Tick(AtLamp, true), std::nullopt);
}

TEST(BloodborneLampRest, UsingAnotherLampMovesTheRest) {
    LampRestState rest;
    rest.Rest(CentralYharnamLamp, AtLamp);
    rest.Rest(GreatBridgeLamp, LampPosition{100.0f, 0.0f, 0.0f});
    EXPECT_EQ(rest.Tick(LampPosition{100.0f, 0.0f, 1.0f}, true), GreatBridgeLamp);
}

// Travel together: the first use of a lamp rests, using it again while resting there takes the
// player (and so the party) on the trip the dialog offers.
TEST(BloodborneLampRest, UsingTheLampAgainWhileRestingThereTravels) {
    LampRestState rest;
    EXPECT_EQ(rest.Use(CentralYharnamLamp, AtLamp), LampUse::Rest);
    EXPECT_EQ(rest.Tick(AtLamp, true), CentralYharnamLamp);

    EXPECT_EQ(rest.Use(CentralYharnamLamp, AtLamp), LampUse::Travel);
    EXPECT_EQ(rest.Tick(AtLamp, true), std::nullopt) << "a trip ends the rest";
    EXPECT_EQ(rest.Use(CentralYharnamLamp, AtLamp), LampUse::Rest);
}

TEST(BloodborneLampRest, AnotherLampOrAFinishedRestStartsANewRest) {
    LampRestState rest;
    EXPECT_EQ(rest.Use(CentralYharnamLamp, AtLamp), LampUse::Rest);
    EXPECT_EQ(rest.Use(GreatBridgeLamp, AtLamp), LampUse::Rest);
    EXPECT_EQ(rest.Tick(AtLamp, true), GreatBridgeLamp);

    // Walked away: the rest is over, so the lamp rests again.
    rest.Tick(LampPosition{10.0f, 0.0f, 5.0f + LampRestState::LeaveDistance + 0.1f}, true);
    EXPECT_EQ(rest.Use(GreatBridgeLamp, AtLamp), LampUse::Rest);
}

TEST(BloodborneLampRest, BoardUpdatesAreOneLine) {
    EXPECT_EQ(EncodeLampRestUpdate("a1b2", CentralYharnamLamp), "a1b2 72410100");
    EXPECT_EQ(EncodeLampRestUpdate("a1b2", NotResting), "a1b2 0");
}

TEST(BloodborneLampRest, ParsesTheServersRestBoard) {
    const auto board = ParseLampRestBoard("host 72410100\nguest 72410101\n");
    ASSERT_TRUE(board.has_value());
    ASSERT_EQ(board->size(), 2u);
    EXPECT_EQ((*board)[0].member, "host");
    EXPECT_EQ((*board)[0].lamp, CentralYharnamLamp);
    EXPECT_EQ((*board)[1].member, "guest");
    EXPECT_EQ((*board)[1].lamp, GreatBridgeLamp);

    const auto empty = ParseLampRestBoard("");
    ASSERT_TRUE(empty.has_value());
    EXPECT_TRUE(empty->empty());
    EXPECT_FALSE(ParseLampRestBoard("host").has_value());
    EXPECT_FALSE(ParseLampRestBoard("host notanumber\n").has_value());
}

TEST(BloodborneLampRest, TheWorldRefreshesOnceWhenTheWholePartyRestsAtTheSameLamp) {
    LampRestCoordinator coordinator;
    const std::vector<LampRest> only_me{{"host", CentralYharnamLamp}};
    const std::vector<LampRest> both{{"host", CentralYharnamLamp}, {"guest", CentralYharnamLamp}};
    const std::vector<LampRest> apart{{"host", CentralYharnamLamp}, {"guest", GreatBridgeLamp}};

    EXPECT_FALSE(coordinator.Tick(CentralYharnamLamp, 2, only_me));
    EXPECT_FALSE(coordinator.Tick(CentralYharnamLamp, 2, apart));
    EXPECT_TRUE(coordinator.Tick(CentralYharnamLamp, 2, both));
    EXPECT_FALSE(coordinator.Tick(CentralYharnamLamp, 2, both)) << "only once per rest";

    // The guest walks off and comes back to rest again: that is a new rest for everyone.
    EXPECT_FALSE(coordinator.Tick(CentralYharnamLamp, 2, only_me));
    EXPECT_TRUE(coordinator.Tick(CentralYharnamLamp, 2, both));
}

TEST(BloodborneLampRest, NobodyRefreshesAloneOrWithoutResting) {
    LampRestCoordinator coordinator;
    const std::vector<LampRest> both{{"host", CentralYharnamLamp}, {"guest", CentralYharnamLamp}};
    // Not resting ourselves, even though the board says so (a stale entry).
    EXPECT_FALSE(coordinator.Tick(std::nullopt, 2, both));
    // Alone in the world: the lamp is a normal lamp, nothing to coordinate.
    EXPECT_FALSE(coordinator.Tick(CentralYharnamLamp, 1, both));
    // A third member who has not sat down yet holds the party back.
    EXPECT_FALSE(coordinator.Tick(CentralYharnamLamp, 3, both));
}

TEST(BloodborneLampRest, ARestDoesWhatTheLampReloadWouldDoWithoutTheLoad) {
    std::vector<std::string> calls;
    static std::vector<std::string>* log;
    log = &calls;
    const LampRestCalls rest_calls{
        .request_full_recover = [] { log->push_back("heal"); },
        .clamp_restock_item = [] { log->push_back("clamp"); },
        .restock_from_storage = [] { log->push_back("restock"); },
        .request_world_respawn = [] { log->push_back("respawn"); },
    };
    EXPECT_TRUE(RefreshAtLamp(rest_calls));
    EXPECT_EQ(calls, (std::vector<std::string>{"heal", "clamp", "restock", "respawn"}));

    EXPECT_FALSE(RefreshAtLamp(LampRestCalls{})) << "nothing to call, nothing done";
}

TEST(BloodborneLampRest, ARestCarriesWhereThePlayerRests) {
    EXPECT_EQ(EncodeLampRestUpdate("a1b2", CentralYharnamLamp, AtLamp), "a1b2 72410100 10 0 5");
    EXPECT_EQ(EncodeLampRestUpdate("a1b2", NotResting, std::nullopt), "a1b2 0");

    const auto board = ParseLampRestBoard("host 72410100 10 0 5.5\nguest 72410100\n");
    ASSERT_TRUE(board.has_value());
    ASSERT_EQ(board->size(), 2u);
    ASSERT_TRUE((*board)[0].at.has_value());
    EXPECT_EQ(*(*board)[0].at, (LampPosition{10.0f, 0.0f, 5.5f}));
    EXPECT_FALSE((*board)[1].at.has_value());
    EXPECT_FALSE(ParseLampRestBoard("host 72410100 10 0\n").has_value());
    EXPECT_FALSE(ParseLampRestBoard("host 72410100 10 0 x\n").has_value());
}

TEST(BloodborneLampRest, PartyMembersJoinARestByStandingNearTheRestingPlayer) {
    LampRestState rest;
    const std::vector<LampRest> board{{"host", CentralYharnamLamp, AtLamp}};

    // Too far away: not part of the rest.
    EXPECT_FALSE(rest.JoinNearby(board, LampPosition{30.0f, 0.0f, 5.0f}));
    EXPECT_EQ(rest.Tick(LampPosition{30.0f, 0.0f, 5.0f}, true), std::nullopt);

    EXPECT_TRUE(rest.JoinNearby(board, LampPosition{12.0f, 0.0f, 6.0f}));
    EXPECT_EQ(rest.RestedAt(), AtLamp) << "the rest is where the member who used the lamp is";
    EXPECT_EQ(rest.Tick(LampPosition{12.0f, 0.0f, 6.0f}, true), CentralYharnamLamp);
    // Already resting: nothing to join.
    EXPECT_FALSE(rest.JoinNearby(board, LampPosition{12.0f, 0.0f, 6.0f}));
    // Walking away from the lamp ends it like any rest.
    EXPECT_EQ(rest.Tick(LampPosition{30.0f, 0.0f, 5.0f}, true), std::nullopt);

    // Standing at the edge of a rest must not join and leave it over and over (each join
    // would be a new rest for the party): joining takes being closer than leaving does.
    LampRestState edge;
    const LampPosition at_edge{10.0f, 0.0f, 5.0f + LampRestState::LeaveDistance - 0.1f};
    EXPECT_FALSE(edge.JoinNearby(board, at_edge));
    EXPECT_TRUE(edge.JoinNearby(
        board, LampPosition{10.0f, 0.0f, 5.0f + LampRestState::JoinDistance - 0.1f}));

    // Rests without a position (and no position of our own) can't be joined.
    LampRestState other;
    EXPECT_FALSE(other.JoinNearby(std::vector<LampRest>{{"host", CentralYharnamLamp}}, AtLamp));
    EXPECT_FALSE(other.JoinNearby(board, std::nullopt));
}

TEST(BloodborneLampRest, OnlyARestStartedAtTheLampCanBeJoined) {
    LampRestState user;
    user.Rest(CentralYharnamLamp, AtLamp);
    EXPECT_EQ(user.JoinablePosition(), AtLamp);

    // Someone who joined by standing close shares no position: otherwise the lamp user, walking
    // back after leaving, would join the joiner and start a rest nobody asked for.
    LampRestState joiner;
    ASSERT_TRUE(
        joiner.JoinNearby(std::vector<LampRest>{{"host", CentralYharnamLamp, AtLamp}}, AtLamp));
    EXPECT_EQ(joiner.JoinablePosition(), std::nullopt);
    EXPECT_EQ(joiner.Tick(AtLamp, true), CentralYharnamLamp);

    LampRestState idle;
    EXPECT_EQ(idle.JoinablePosition(), std::nullopt);
}

TEST(BloodborneLampRest, TheRespawnKeepsCharactersTheScriptsHadSwitchedOffSwitchedOff) {
    // A disabled character that is alive (full HP) is hidden by the event scripts, e.g. a quest
    // NPC that only shows up later: the bonfire respawn re-enables it, so it is disabled again.
    EXPECT_TRUE(KeepDisabledAfterRespawn(true, 300));
    // A killed enemy is what the respawn is for.
    EXPECT_FALSE(KeepDisabledAfterRespawn(true, 0));
    EXPECT_FALSE(KeepDisabledAfterRespawn(false, 300));
}

TEST(BloodborneLampRest, TheRespawnKeepsTheFlagsTheScriptsSetOnLiveCharacters) {
    // From the rig: hidden characters went 0x124400 -> 0x100400 (0x4000 | 0x20000 cleared),
    // others lost 0x40000000; the window monster near the Central Yharnam lamp came back.
    EXPECT_EQ(RestoreScriptFlagsAfterRespawn(0x124400, 0x100400, 70), 0x124400u);
    EXPECT_EQ(RestoreScriptFlagsAfterRespawn(0xFFFF000040100400, 0xFFFF000000100400, 3645),
              0xFFFF000040100400u);
    // Runtime state (aggro, 0x8000 / 0x100000000 ...) is what the respawn resets: left alone.
    EXPECT_EQ(RestoreScriptFlagsAfterRespawn(0x100108400, 0x100400, 150), 0x100400u);
    // A killed enemy comes back as the respawn made it.
    EXPECT_EQ(RestoreScriptFlagsAfterRespawn(0x124400, 0x100400, 0), 0x100400u);
}

TEST(BloodborneLampRest, APartyRestOnlyBringsBackWhatThePartyKilled) {
    // Living characters keep whatever state the scripts gave them.
    EXPECT_FALSE(RespawnsAtPartyRest(70, true));
    EXPECT_FALSE(RespawnsAtPartyRest(9999, true));
    // Killed during this visit to the map: back it comes.
    EXPECT_TRUE(RespawnsAtPartyRest(0, true));
    // Dead since the map loaded: the scripts keep it that way (the monster behind the window near
    // the Central Yharnam lamp came out after every rest).
    EXPECT_FALSE(RespawnsAtPartyRest(0, false));
}
