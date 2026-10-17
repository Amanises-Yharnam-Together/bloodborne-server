// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The map screen (Kaniel 2026-10-17): its lamps, their flags and names, when a trip from the map
// may start and when it has arrived.

#include <algorithm>

#include <gtest/gtest.h>

#include "core/bloodborne_map.h"
#include "core/bloodborne_party.h"

using namespace Core::Bloodborne;

TEST(BloodborneMap, ListsEachLampOnceAndTheDreamOnce) {
    const auto& lamps = MapLamps();
    // Central Yharnam's map (m24_01) has its four lamps; 2412959 is a boss arena's point.
    std::vector<s32> m24_01;
    for (const MapLamp& lamp : lamps) {
        if (lamp.packed_map == 0x18010000) {
            m24_01.push_back(lamp.warp_point);
        }
    }
    EXPECT_EQ(m24_01, (std::vector<s32>{2412950, 2412951, 2412952, 2412953}));
    // The Dream's thirteen respawn points are one lamp.
    EXPECT_EQ(
        std::ranges::count_if(lamps, [](const MapLamp& l) { return l.packed_map == 0x15000000; }),
        1);
    ASSERT_NE(FindMapLamp(HuntersDreamWarpPoint), nullptr);
    EXPECT_EQ(FindMapLamp(2203998), nullptr); // not a lamp (an event's point)
    const MapLamp* bridge = FindMapLamp(2412952);
    ASSERT_NE(bridge, nullptr);
    EXPECT_FLOAT_EQ(bridge->x, -124.8f);
    EXPECT_FLOAT_EQ(bridge->z, 64.3f);
    EXPECT_TRUE(std::ranges::is_sorted(lamps, {}, &MapLamp::warp_point));
}

TEST(BloodborneMap, ALampsActiveFlagIsTheOneThatSaysItIsLit) {
    EXPECT_EQ(LampActiveFlag(2412950), 12417810);
    EXPECT_EQ(LampActiveFlag(2412953), 12417870);
    EXPECT_EQ(LampActiveFlag(HuntersDreamWarpPoint), std::nullopt);
    for (const MapLamp& lamp : MapLamps()) {
        if (const auto flag = LampActiveFlag(lamp.warp_point)) {
            EXPECT_EQ(LampOfActiveFlag(*flag), lamp.warp_point - 1000); // the lamp object's entity
        }
    }
}

TEST(BloodborneMap, NamesComeFromTheGamesText) {
    EXPECT_EQ(LampNameId(2412951), 70002411); // "Central Yharnam"
    EXPECT_EQ(LampNameId(2402951), 70002401); // "Grand Cathedral"
    EXPECT_EQ(LampNameId(HuntersDreamWarpPoint), 70002100);
    EXPECT_EQ(AreaNameId(0x18010000), 2410);
    EXPECT_EQ(AreaNameId(0x18000000), 2400);
    EXPECT_EQ(AreaNameId(0x20000000), 3200); // Byrgenwerth
}

TEST(BloodborneMap, ATripStartsOnlyToALitLampFromTheWorld) {
    const MapTravelState ready{.lit = true, .in_world = true};
    EXPECT_EQ(MapTravelCheck(ready), MapTravelBlock::None);
    auto state = ready;
    state.lit = false;
    EXPECT_EQ(MapTravelCheck(state), MapTravelBlock::NotLit);
    state = ready;
    state.in_world = false;
    EXPECT_EQ(MapTravelCheck(state), MapTravelBlock::NotInWorld);
    state = ready;
    state.down = true;
    EXPECT_EQ(MapTravelCheck(state), MapTravelBlock::Down);
    state = ready;
    state.boss_fight = true;
    EXPECT_EQ(MapTravelCheck(state), MapTravelBlock::BossFight);
    state = ready;
    state.travelling = true;
    EXPECT_EQ(MapTravelCheck(state), MapTravelBlock::Travelling);
}

TEST(BloodborneMap, ATripArrivesOnceItsMapIsInAfterALoad) {
    MapTripProgress trip{.target_map = 0x18010000, .seconds = 1.0};
    // Still on the target map before the load (a lamp on the same map): not yet.
    EXPECT_FALSE(MapTripArrived(trip, 0x18010000, false));
    trip.load_seen = true;
    EXPECT_FALSE(MapTripArrived(trip, 0x18010000, true));  // still loading
    EXPECT_FALSE(MapTripArrived(trip, 0x18000000, false)); // another map
    EXPECT_TRUE(MapTripArrived(trip, 0x18010000, false));
    // No load seen at all: arrived once on the map a while after the start.
    trip = {.target_map = 0x18010000, .seconds = 9.0};
    EXPECT_TRUE(MapTripArrived(trip, 0x18010000, false));
}
