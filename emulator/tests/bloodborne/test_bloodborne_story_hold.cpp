// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Story removals held over a load (StoryRemovals) and the loads that start a new world
// (StartsNewWorld): Gilbert and Iosefka drawn for seconds after a load (playtest 2026-10-15).

#include <vector>

#include <gtest/gtest.h>

#include "core/bloodborne_party.h"

using namespace Core::Bloodborne;

namespace {

constexpr u32 Yharnam = 0x18010000;
constexpr u32 Gilbert = 0x1001400c;
constexpr u32 Enemy = 0x1001400b;

StoryCharacter Shown(u32 handle) {
    return {handle, false, false, false, false};
}
StoryCharacter Removed(u32 handle) {
    return {handle, true, false, false, false};
}
StoryCharacter Off(u32 handle, bool removed = false) {
    return {handle, removed, true, false, false};
}

// The first visit: the story removes Gilbert 5 s after the load; settled after StoryHoldMs.
StoryRemovals AfterAFirstVisit() {
    StoryRemovals removals;
    const std::vector<StoryCharacter> before{Shown(Gilbert), Shown(Enemy)};
    EXPECT_EQ(removals.Step(Yharnam, 0, 1'000, before), StoryHoldStep{});
    const std::vector<StoryCharacter> after{Removed(Gilbert), Shown(Enemy)};
    EXPECT_EQ(removals.Step(Yharnam, 0, StoryHoldMs, after), StoryHoldStep{});
    EXPECT_TRUE(removals.Remembers(Yharnam, Gilbert));
    EXPECT_FALSE(removals.Remembers(Yharnam, Enemy));
    return removals;
}

// Kaniel's playtest 2026-10-16: a new character (another save) loaded with the last save's
// memory of the same map. Iosefka's door copy and the first dog were held off, then switched on
// at the time-out: Iosefka stood by her door, and the dog missed its scripted HP drop.
TEST(StoryHold, AnotherSaveForgetsWhatTheLastOnesStoryRemoved) {
    StoryRemovals removals = AfterAFirstVisit();
    removals.Forget();
    EXPECT_FALSE(removals.Remembers(Yharnam, Gilbert));
    const std::vector<StoryCharacter> fresh{Shown(Gilbert), Shown(Enemy)};
    EXPECT_TRUE(removals.Step(Yharnam, 60'000, 60'100, fresh).hold.empty());
}

// An enemy dead in its world at the settle (killed, or put down at its load) is no story removal:
// held off over the next load it would miss what the map's events do to it then.
TEST(StoryHold, ADeadEnemyIsNoStoryRemoval) {
    StoryRemovals removals = AfterAFirstVisit();
    StoryCharacter dead = Removed(Enemy);
    dead.dead = true;
    const std::vector<StoryCharacter> settled{Removed(Gilbert), dead};
    EXPECT_EQ(removals.Step(Yharnam, 0, StoryHoldMs + 1'000, settled), StoryHoldStep{});
    EXPECT_FALSE(removals.Remembers(Yharnam, Enemy));
    // One remembered from before it died is not held either, and one held is switched on.
    StoryRemovals other = AfterAFirstVisit();
    const s64 load = 60'000;
    const std::vector<StoryCharacter> fresh{Shown(Gilbert)};
    ASSERT_EQ(other.Step(Yharnam, load, load + 100, fresh).hold, std::vector<u32>{Gilbert});
    other.Held(Gilbert, load + 100);
    StoryCharacter gone = Off(Gilbert);
    gone.dead = true;
    const std::vector<StoryCharacter> died{gone};
    EXPECT_EQ(other.Step(Yharnam, load, load + 200, died).release, std::vector<u32>{Gilbert});
    EXPECT_FALSE(other.Remembers(Yharnam, Gilbert));
}

TEST(StoryHold, NothingIsHeldOnAMapNeverSettled) {
    StoryRemovals removals;
    const std::vector<StoryCharacter> seen{Shown(Gilbert)};
    EXPECT_TRUE(removals.Step(Yharnam, 0, 100, seen).hold.empty());
    EXPECT_FALSE(removals.Remembers(Yharnam, Gilbert));
}

TEST(StoryHold, WhatTheStoryRemovedIsSwitchedOffOnTheNextVisitUntilItIsRemovedAgain) {
    StoryRemovals removals = AfterAFirstVisit();
    const s64 load = 60'000;
    const std::vector<StoryCharacter> fresh{Shown(Gilbert), Shown(Enemy)};
    const StoryHoldStep first = removals.Step(Yharnam, load, load + 100, fresh);
    EXPECT_EQ(first.hold, std::vector<u32>{Gilbert});
    removals.Held(Gilbert, load + 100);
    const std::vector<StoryCharacter> held{Off(Gilbert), Shown(Enemy)};
    EXPECT_EQ(removals.Step(Yharnam, load, load + 3'000, held), StoryHoldStep{});
    // The story runs: removed (and still switched off by us).
    const std::vector<StoryCharacter> story{Off(Gilbert, true), Shown(Enemy)};
    EXPECT_EQ(removals.Step(Yharnam, load, load + 6'000, story).release, std::vector<u32>{Gilbert});
    const std::vector<StoryCharacter> after{Removed(Gilbert), Shown(Enemy)};
    EXPECT_EQ(removals.Step(Yharnam, load, load + 7'000, after), StoryHoldStep{});
    EXPECT_TRUE(removals.Remembers(Yharnam, Gilbert));
}

TEST(StoryHold, OneTheStoryNoLongerRemovesComesBackAndIsForgotten) {
    StoryRemovals removals = AfterAFirstVisit();
    const s64 load = 60'000;
    const std::vector<StoryCharacter> fresh{Shown(Gilbert)};
    ASSERT_EQ(removals.Step(Yharnam, load, load + 100, fresh).hold, std::vector<u32>{Gilbert});
    removals.Held(Gilbert, load + 100);
    const std::vector<StoryCharacter> held{Off(Gilbert)};
    EXPECT_TRUE(removals.Step(Yharnam, load, load + 100 + StoryHoldMs - 1, held).release.empty());
    EXPECT_EQ(removals.Step(Yharnam, load, load + 100 + StoryHoldMs, held).release,
              std::vector<u32>{Gilbert});
    EXPECT_FALSE(removals.Remembers(Yharnam, Gilbert));
}

TEST(StoryHold, AKillIsNoRemovalButAHideFollowingTheHostIs) {
    StoryRemovals removals;
    const std::vector<StoryCharacter> killed{{Enemy, true, false, false, true}};
    removals.Step(Yharnam, 0, StoryHoldMs, killed);
    EXPECT_FALSE(removals.Remembers(Yharnam, Enemy));
    // Switched off only, as the host's board has it: not a removal.
    const std::vector<StoryCharacter> switched{{Enemy, false, true, true, false}};
    removals.Step(Yharnam, 0, StoryHoldMs, switched);
    EXPECT_FALSE(removals.Remembers(Yharnam, Enemy));
    // A guest hid Gilbert as the host's board has him: held on its next visit.
    const std::vector<StoryCharacter> followed{{Gilbert, true, true, true, false}};
    removals.Step(Yharnam, 0, StoryHoldMs, followed);
    EXPECT_TRUE(removals.Remembers(Yharnam, Gilbert));
    const std::vector<StoryCharacter> fresh{Shown(Gilbert)};
    EXPECT_EQ(removals.Step(Yharnam, 60'000, 60'100, fresh).hold, std::vector<u32>{Gilbert});
}

TEST(StoryHold, AHeldOneTheHostsBoardTakesOverIsHandedOver) {
    StoryRemovals removals;
    // Held, then the host's board takes it over: handed over, never switched on here.
    removals = AfterAFirstVisit();
    const s64 load = 60'000;
    const std::vector<StoryCharacter> fresh{Shown(Gilbert)};
    ASSERT_EQ(removals.Step(Yharnam, load, load + 100, fresh).hold, std::vector<u32>{Gilbert});
    removals.Held(Gilbert, load + 100);
    const std::vector<StoryCharacter> following{{Gilbert, true, true, true, false}};
    const StoryHoldStep step = removals.Step(Yharnam, load, load + 200, following);
    EXPECT_EQ(step.hand_over, std::vector<u32>{Gilbert});
    EXPECT_TRUE(step.release.empty());
}

TEST(StoryHold, NothingIsHeldOnceTheVisitSettledOrWhenAlreadyOff) {
    StoryRemovals removals = AfterAFirstVisit();
    const s64 load = 60'000;
    const std::vector<StoryCharacter> late{Shown(Gilbert)};
    EXPECT_TRUE(removals.Step(Yharnam, load, load + StoryHoldMs, late).hold.empty());
    removals = AfterAFirstVisit();
    const std::vector<StoryCharacter> off{Off(Gilbert)};
    EXPECT_TRUE(removals.Step(Yharnam, load, load + 100, off).hold.empty());
}

TEST(StoryHold, AHoldWhoseSwitchFailedIsTriedAgain) {
    StoryRemovals removals = AfterAFirstVisit();
    const s64 load = 60'000;
    const std::vector<StoryCharacter> fresh{Shown(Gilbert)};
    ASSERT_EQ(removals.Step(Yharnam, load, load + 100, fresh).hold, std::vector<u32>{Gilbert});
    // Not Held: the next step asks again.
    EXPECT_EQ(removals.Step(Yharnam, load, load + 200, fresh).hold, std::vector<u32>{Gilbert});
}

PlaceChange Change(PlaceState from, u64 from_visit, PlaceState to, u64 to_visit, PlaceCause cause) {
    PlaceChange change;
    change.from.state = from;
    change.from.visit = from_visit;
    change.to.state = to;
    change.to.visit = to_visit;
    change.cause = cause;
    return change;
}

TEST(StoryHold, EveryLoadThatPlaysStartsANewWorld) {
    EXPECT_TRUE(
        StartsNewWorld(Change(PlaceState::Unknown, 0, PlaceState::Playing, 1, PlaceCause::Load)));
    EXPECT_TRUE(
        StartsNewWorld(Change(PlaceState::Loading, 1, PlaceState::Playing, 2, PlaceCause::Lamp)));
    EXPECT_TRUE(
        StartsNewWorld(Change(PlaceState::Playing, 4, PlaceState::Playing, 5, PlaceCause::Lamp)));
    EXPECT_FALSE(
        StartsNewWorld(Change(PlaceState::Playing, 5, PlaceState::Playing, 6, PlaceCause::Border)));
    EXPECT_FALSE(StartsNewWorld(
        Change(PlaceState::Playing, 5, PlaceState::Playing, 6, PlaceCause::Teleport)));
    EXPECT_FALSE(
        StartsNewWorld(Change(PlaceState::Playing, 2, PlaceState::Loading, 2, PlaceCause::Lamp)));
}

} // namespace
