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
    // The Hunter's Dream headstones and anything else are not lamps.
    EXPECT_FALSE(IsLampTravelFlag(72100000));
    EXPECT_FALSE(IsLampTravelFlag(6707));
    EXPECT_FALSE(IsLampTravelFlag(4000));
    EXPECT_FALSE(IsLampTravelFlag(-1));
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
