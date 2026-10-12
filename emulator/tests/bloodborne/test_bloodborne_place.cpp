// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/bloodborne_party.h"

using namespace Core::Bloodborne;

namespace {

constexpr u32 CentralYharnam = 0x18000000;
constexpr u32 CathedralWard = 0x18010000;
constexpr u32 HuntersDream = 0x15000000;
constexpr u64 Player = 0x1000;

// One tick of a player standing in the world at `at`.
PlaceReading Standing(s64 now_ms, u32 map, LampPosition at = {0.0f, 0.0f, 0.0f},
                      u64 player = Player) {
    return PlaceReading{.now_ms = now_ms, .map = map, .player = player, .at = at};
}

PlaceReading Loading(s64 now_ms, u32 map = 0, bool trip = false) {
    return PlaceReading{.now_ms = now_ms, .map = map, .player = 0, .loading = true, .trip = trip};
}

// A tracker that has arrived on `map` at t=0 (visit 1).
PlaceTracker ArrivedOn(u32 map) {
    PlaceTracker tracker;
    tracker.Update(Standing(0, map));
    return tracker;
}

} // namespace

TEST(BloodbornePlace, StartsUnknownAndArrivesOnTheFirstGoodRead) {
    PlaceTracker tracker;
    EXPECT_EQ(tracker.Current().state, PlaceState::Unknown);
    EXPECT_EQ(tracker.Current().map, 0u);
    EXPECT_EQ(tracker.Current().visit, 0u);
    EXPECT_FALSE(tracker.Update(Standing(10, 0))) << "no map read yet";
    const auto change = tracker.Update(Standing(20, CentralYharnam));
    ASSERT_TRUE(change);
    EXPECT_EQ(change->from.state, PlaceState::Unknown);
    EXPECT_EQ(change->to, (Place{PlaceState::Playing, CentralYharnam, 1, 20}));
    EXPECT_EQ(change->cause, PlaceCause::Load);
    EXPECT_FALSE(tracker.Update(Standing(30, CentralYharnam))) << "nothing changed";
}

TEST(BloodbornePlace, FailedReadsMidPlayKeepThePlace) {
    PlaceTracker tracker = ArrivedOn(CentralYharnam);
    EXPECT_FALSE(tracker.Update(Standing(100, 0)));
    EXPECT_FALSE(tracker.Update(Standing(200, 0xFFFFFFFF)));
    EXPECT_EQ(tracker.Current(), (Place{PlaceState::Playing, CentralYharnam, 1, 0}));
    EXPECT_FALSE(tracker.Update(Standing(300, CentralYharnam)));
    EXPECT_EQ(tracker.Current().visit, 1u);
}

TEST(BloodbornePlace, ALoadKeepsTheMapAndArrivesAsANewVisit) {
    PlaceTracker tracker = ArrivedOn(CentralYharnam);
    const auto left = tracker.Update(Loading(1'000));
    ASSERT_TRUE(left);
    EXPECT_EQ(left->to, (Place{PlaceState::Loading, CentralYharnam, 1, 1'000}))
        << "the committed map never turns 0";
    EXPECT_EQ(left->cause, PlaceCause::Load);
    EXPECT_FALSE(tracker.Update(Loading(2'000)));
    const auto arrived = tracker.Update(Standing(9'000, HuntersDream));
    ASSERT_TRUE(arrived);
    EXPECT_EQ(arrived->from.state, PlaceState::Loading);
    EXPECT_EQ(arrived->to, (Place{PlaceState::Playing, HuntersDream, 2, 9'000}));
    EXPECT_EQ(arrived->cause, PlaceCause::Load);
}

TEST(BloodbornePlace, ALoadEndingOnTheSameMapIsANewVisit) {
    // The game respawned the map's enemies.
    PlaceTracker tracker = ArrivedOn(CentralYharnam);
    tracker.Update(Loading(1'000));
    const auto arrived = tracker.Update(Standing(5'000, CentralYharnam, {}, 0x2000));
    ASSERT_TRUE(arrived);
    EXPECT_EQ(arrived->to.map, CentralYharnam);
    EXPECT_EQ(arrived->to.visit, 2u);
}

TEST(BloodbornePlace, ALoadWithoutAMapReadStaysLoading) {
    PlaceTracker tracker = ArrivedOn(CentralYharnam);
    tracker.Update(Loading(1'000));
    EXPECT_FALSE(tracker.Update(Standing(2'000, 0))) << "the player is back, the map is not";
    EXPECT_EQ(tracker.Current().state, PlaceState::Loading);
    const auto arrived = tracker.Update(Standing(2'100, CathedralWard));
    ASSERT_TRUE(arrived);
    EXPECT_EQ(arrived->to.state, PlaceState::Playing);
}

TEST(BloodbornePlace, NoPlayerCharacterIsALoad) {
    PlaceTracker tracker = ArrivedOn(CentralYharnam);
    const auto change = tracker.Update(Standing(1'000, CentralYharnam, {}, 0));
    ASSERT_TRUE(change);
    EXPECT_EQ(change->to.state, PlaceState::Loading);
}

TEST(BloodbornePlace, ANewPlayerCharacterIsALoadTheTicksMissed) {
    PlaceTracker tracker = ArrivedOn(CentralYharnam);
    const auto change = tracker.Update(Standing(1'000, CentralYharnam, {}, 0x2000));
    ASSERT_TRUE(change);
    EXPECT_EQ(change->to, (Place{PlaceState::Playing, CentralYharnam, 2, 1'000}));
    EXPECT_EQ(change->cause, PlaceCause::Load);
}

TEST(BloodbornePlace, ATeleportDuringALoadArrivesOnceWhereTheLoadEnds) {
    PlaceTracker tracker = ArrivedOn(CentralYharnam);
    tracker.Update(Loading(1'000, 0, true));
    EXPECT_FALSE(tracker.Update(Loading(2'000, CathedralWard, true)));
    EXPECT_FALSE(tracker.Update(Loading(3'000, HuntersDream, true)));
    const auto arrived = tracker.Update(Standing(4'000, HuntersDream));
    ASSERT_TRUE(arrived);
    EXPECT_EQ(arrived->to, (Place{PlaceState::Playing, HuntersDream, 2, 4'000}));
    EXPECT_EQ(arrived->cause, PlaceCause::Lamp);
    EXPECT_FALSE(tracker.Update(Standing(5'000, HuntersDream)));
}

TEST(BloodbornePlace, ATripNamesTheLoadALampTrip) {
    PlaceTracker tracker = ArrivedOn(HuntersDream);
    // The trip starts in the old world, before the load screen.
    EXPECT_FALSE(tracker.Update(PlaceReading{
        .now_ms = 100, .map = HuntersDream, .player = Player, .at = LampPosition{}, .trip = true}));
    const auto left = tracker.Update(Loading(200, 0, true));
    ASSERT_TRUE(left);
    EXPECT_EQ(left->cause, PlaceCause::Lamp);
    const auto arrived = tracker.Update(Standing(5'000, CentralYharnam, {}, 0x2000));
    ASSERT_TRUE(arrived);
    EXPECT_EQ(arrived->cause, PlaceCause::Lamp);
    // The next load is a plain one again.
    tracker.Update(Loading(6'000));
    const auto next = tracker.Update(Standing(9'000, CentralYharnam, {}, 0x3000));
    ASSERT_TRUE(next);
    EXPECT_EQ(next->cause, PlaceCause::Load);
}

TEST(BloodbornePlace, ADeathReloadIsNamedSo) {
    PlaceTracker tracker = ArrivedOn(CentralYharnam);
    EXPECT_FALSE(tracker.Update(PlaceReading{.now_ms = 100,
                                             .map = CentralYharnam,
                                             .player = Player,
                                             .at = LampPosition{},
                                             .dead = true}))
        << "dying is no move";
    const auto left = tracker.Update(Loading(3'000));
    ASSERT_TRUE(left);
    EXPECT_EQ(left->cause, PlaceCause::DeathReload);
    const auto arrived = tracker.Update(Standing(8'000, CentralYharnam, {}, 0x2000));
    ASSERT_TRUE(arrived);
    EXPECT_EQ(arrived->to.visit, 2u);
    EXPECT_EQ(arrived->cause, PlaceCause::DeathReload);
}

TEST(BloodbornePlace, ARevivalInPlaceForgetsTheDeath) {
    // A party death revive: back up without a load, so the next load is no death reload.
    PlaceTracker tracker = ArrivedOn(CentralYharnam);
    tracker.Update(PlaceReading{.now_ms = 100,
                                .map = CentralYharnam,
                                .player = Player,
                                .at = LampPosition{},
                                .dead = true});
    EXPECT_FALSE(tracker.Update(Standing(2'000, CentralYharnam)));
    const auto left = tracker.Update(Loading(3'000));
    ASSERT_TRUE(left);
    EXPECT_EQ(left->cause, PlaceCause::Load);
}

TEST(BloodbornePlace, ABorderCrossingCommitsAfterSteadyReads) {
    PlaceTracker tracker = ArrivedOn(CentralYharnam);
    EXPECT_FALSE(tracker.Update(Standing(1'000, CathedralWard, {1.0f, 0.0f, 0.0f})));
    EXPECT_FALSE(tracker.Update(Standing(2'000, CathedralWard, {2.0f, 0.0f, 0.0f})));
    EXPECT_EQ(tracker.Current().map, CentralYharnam) << "still pending";
    const auto crossed = tracker.Update(Standing(2'500, CathedralWard, {3.0f, 0.0f, 0.0f}));
    ASSERT_TRUE(crossed);
    EXPECT_EQ(crossed->to, (Place{PlaceState::Playing, CathedralWard, 2, 2'500}));
    EXPECT_EQ(crossed->cause, PlaceCause::Border);
}

TEST(BloodbornePlace, PingPongAtABorderDoesNotCommit) {
    PlaceTracker tracker = ArrivedOn(CentralYharnam);
    s64 now = 0;
    for (int step = 0; step < 20; ++step) {
        now += 700;
        const u32 map = step % 2 == 0 ? CathedralWard : CentralYharnam;
        EXPECT_FALSE(tracker.Update(Standing(now, map, {static_cast<float>(step % 2), 0, 0})))
            << "step " << step;
    }
    EXPECT_EQ(tracker.Current(), (Place{PlaceState::Playing, CentralYharnam, 1, 0}));
}

TEST(BloodbornePlace, GoingBackCancelsAPendingCrossing) {
    PlaceTracker tracker = ArrivedOn(CentralYharnam);
    tracker.Update(Standing(1'000, CathedralWard));
    tracker.Update(Standing(2'000, CentralYharnam));
    EXPECT_FALSE(tracker.Update(Standing(2'400, CathedralWard))) << "the steady time starts over";
    EXPECT_FALSE(tracker.Update(Standing(3'800, CathedralWard)));
    EXPECT_TRUE(tracker.Update(Standing(3'900, CathedralWard)));
}

TEST(BloodbornePlace, FailedReadsDoNotCancelAPendingCrossing) {
    PlaceTracker tracker = ArrivedOn(CentralYharnam);
    tracker.Update(Standing(1'000, CathedralWard));
    EXPECT_FALSE(tracker.Update(Standing(1'800, 0)));
    const auto crossed = tracker.Update(Standing(2'500, CathedralWard));
    ASSERT_TRUE(crossed);
    EXPECT_EQ(crossed->cause, PlaceCause::Border);
}

TEST(BloodbornePlace, AMapChangeWithAJumpIsATeleportAtOnce) {
    PlaceTracker tracker = ArrivedOn(CentralYharnam);
    const auto moved = tracker.Update(Standing(100, CathedralWard, {400.0f, 0.0f, 0.0f}));
    ASSERT_TRUE(moved);
    EXPECT_EQ(moved->to, (Place{PlaceState::Playing, CathedralWard, 2, 100}));
    EXPECT_EQ(moved->cause, PlaceCause::Teleport);
}

TEST(BloodbornePlace, AJumpJustBeforeTheMapReadChangesIsStillATeleport) {
    PlaceTracker tracker = ArrivedOn(CentralYharnam);
    EXPECT_FALSE(tracker.Update(Standing(100, CentralYharnam, {400.0f, 0.0f, 0.0f})))
        << "a move on the same map is no change of place";
    const auto moved = tracker.Update(Standing(200, CathedralWard, {400.0f, 0.0f, 0.0f}));
    ASSERT_TRUE(moved);
    EXPECT_EQ(moved->cause, PlaceCause::Teleport);
}

TEST(BloodbornePlace, AnOldJumpDoesNotMakeABorderATeleport) {
    PlaceTracker tracker = ArrivedOn(CentralYharnam);
    tracker.Update(Standing(100, CentralYharnam, {400.0f, 0.0f, 0.0f}));
    EXPECT_FALSE(tracker.Update(Standing(5'000, CathedralWard, {401.0f, 0.0f, 0.0f})));
}

TEST(BloodbornePlace, ACutsceneComesAndGoesWithoutANewVisit) {
    PlaceTracker tracker = ArrivedOn(CentralYharnam);
    const auto started = tracker.Update(PlaceReading{.now_ms = 100,
                                                     .map = CentralYharnam,
                                                     .player = Player,
                                                     .at = LampPosition{},
                                                     .cutscene = true});
    ASSERT_TRUE(started);
    EXPECT_EQ(started->to, (Place{PlaceState::Cutscene, CentralYharnam, 1, 100}));
    EXPECT_EQ(started->cause, PlaceCause::Cutscene);
    EXPECT_FALSE(tracker.Update(PlaceReading{.now_ms = 3'000,
                                             .map = CathedralWard,
                                             .player = Player,
                                             .at = LampPosition{},
                                             .cutscene = true}))
        << "map reads in a cutscene are not followed";
    const auto ended = tracker.Update(Standing(4'000, CentralYharnam));
    ASSERT_TRUE(ended);
    EXPECT_EQ(ended->to, (Place{PlaceState::Playing, CentralYharnam, 1, 4'000}));
    EXPECT_EQ(ended->cause, PlaceCause::CutsceneEnd);
}

TEST(BloodbornePlace, ACutsceneThatReloadsTheWorldIsALoad) {
    // Gascoigne's door: the time-of-day cutscene reloads the map.
    PlaceTracker tracker = ArrivedOn(CentralYharnam);
    tracker.Update(PlaceReading{.now_ms = 100,
                                .map = CentralYharnam,
                                .player = Player,
                                .at = LampPosition{},
                                .cutscene = true});
    const auto left = tracker.Update(Loading(2'000));
    ASSERT_TRUE(left);
    EXPECT_EQ(left->from.state, PlaceState::Cutscene);
    EXPECT_EQ(left->to.state, PlaceState::Loading);
    const auto arrived = tracker.Update(Standing(6'000, CentralYharnam, {}, 0x2000));
    ASSERT_TRUE(arrived);
    EXPECT_EQ(arrived->to.visit, 2u);
}

TEST(BloodbornePlace, VisitsOnlyGrow) {
    PlaceTracker tracker;
    u64 last = 0;
    s64 now = 0;
    const std::vector<u32> maps{CentralYharnam, CathedralWard, 0, HuntersDream, CentralYharnam};
    for (int round = 0; round < 50; ++round) {
        now += 400;
        const u32 map = maps[round % maps.size()];
        if (round % 7 == 3) {
            tracker.Update(Loading(now));
        } else {
            tracker.Update(Standing(now, map, {static_cast<float>(round * 100), 0, 0}));
        }
        EXPECT_GE(tracker.Current().visit, last);
        last = tracker.Current().visit;
        if (tracker.Current().state != PlaceState::Unknown) {
            EXPECT_NE(tracker.Current().map, 0u);
        }
    }
}

TEST(BloodbornePlace, FormatsOneLinePerChange) {
    const PlaceChange change{Place{PlaceState::Playing, CentralYharnam, 3, 0},
                             Place{PlaceState::Loading, CentralYharnam, 3, 10}, PlaceCause::Lamp};
    EXPECT_EQ(FormatPlaceChange(change),
              "Co-op place: Playing -> Loading map 0x18000000 visit 3 (Lamp)");
    const PlaceChange crossed{Place{PlaceState::Playing, CentralYharnam, 3, 0},
                              Place{PlaceState::Playing, CathedralWard, 4, 10}, PlaceCause::Border};
    EXPECT_EQ(FormatPlaceChange(crossed),
              "Co-op place: Playing -> Playing map 0x18010000 visit 4 (Border)");
}

TEST(BloodborneLifecycle, RunsTheResetsRegisteredForTheEventInOrder) {
    LifecycleResets resets;
    std::vector<std::string> ran;
    resets.Register("kills", {Lifecycle::Wipe}, [&] { ran.push_back("kills"); });
    resets.Register("visit", {Lifecycle::Trip}, [&] { ran.push_back("visit"); });
    resets.Register("hp", {Lifecycle::Wipe, Lifecycle::Leave}, [&] { ran.push_back("hp"); });
    EXPECT_EQ(resets.Dispatch(Lifecycle::Wipe), (std::vector<std::string>{"kills", "hp"}));
    EXPECT_EQ(ran, (std::vector<std::string>{"kills", "hp"}));
    ran.clear();
    EXPECT_EQ(resets.Dispatch(Lifecycle::Trip), (std::vector<std::string>{"visit"}));
    EXPECT_EQ(resets.Dispatch(Lifecycle::Leave), (std::vector<std::string>{"hp"}));
    EXPECT_TRUE(resets.Dispatch(Lifecycle::NewCharacter).empty());
    EXPECT_EQ(ran, (std::vector<std::string>{"visit", "hp"}));
}

TEST(BloodborneLifecycle, TheSessionEpochGrowsOnWipesLeavesJoinsAndNewCharacters) {
    LifecycleResets resets;
    const u64 first = resets.Epoch();
    EXPECT_GT(first, 0u) << "0 stays free for unknown";
    resets.Dispatch(Lifecycle::Trip);
    EXPECT_EQ(resets.Epoch(), first) << "a trip keeps the session";
    resets.Dispatch(Lifecycle::Wipe);
    EXPECT_EQ(resets.Epoch(), first + 1);
    resets.Dispatch(Lifecycle::Leave);
    resets.Dispatch(Lifecycle::Join);
    resets.Dispatch(Lifecycle::NewCharacter);
    EXPECT_EQ(resets.Epoch(), first + 4);
}

TEST(BloodborneLifecycle, NamesTheEvents) {
    EXPECT_EQ(LifecycleName(Lifecycle::Wipe), "wipe");
    EXPECT_EQ(LifecycleName(Lifecycle::Trip), "trip");
    EXPECT_EQ(LifecycleName(Lifecycle::Leave), "leave");
    EXPECT_EQ(LifecycleName(Lifecycle::Join), "join");
    EXPECT_EQ(LifecycleName(Lifecycle::NewCharacter), "new character");
}
