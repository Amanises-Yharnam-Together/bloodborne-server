// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The teammate compass (Kaniel 2026-10-17): bearings seen from the camera, the strip's places and
// the members' live positions.

#include <cmath>
#include <numbers>

#include <gtest/gtest.h>

#include "core/bloodborne_compass.h"

using namespace Core::Bloodborne;

namespace {

constexpr float Pi = std::numbers::pi_v<float>;
constexpr float Degree = Pi / 180.0f;

// A camera 4 m behind and 1.5 m above `pivot`, looking along `heading` (the camera object's yaw:
// runtime_camhook.c's direction (sin y, cos y) on the ground).
CompassPoint CameraBehind(const CompassPoint& pivot, float heading) {
    return {pivot[0] - std::sin(heading) * 4.0f, pivot[1] + 1.5f,
            pivot[2] - std::cos(heading) * 4.0f};
}

TEST(BloodborneCompass, CameraHeadingIsTheWayFromTheCameraToItsPivot) {
    const CompassPoint pivot{-215.0f, -54.5f, 178.0f};
    for (const float heading : {0.0f, 0.5f, 1.5707964f, -2.0f, 3.0f}) {
        const auto seen = CameraHeading(pivot, CameraBehind(pivot, heading));
        ASSERT_TRUE(seen.has_value());
        EXPECT_NEAR(*seen, heading, 1e-4f);
    }
    // Looking along +x is a quarter turn right of +z.
    EXPECT_NEAR(*CameraHeading({1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}), Pi / 2, 1e-5f);
    // Straight down: no heading.
    EXPECT_FALSE(CameraHeading({5.0f, 0.0f, 5.0f}, {5.0f, 4.0f, 5.0f}).has_value());
}

TEST(BloodborneCompass, WrapAngleStaysInOneTurn) {
    EXPECT_NEAR(WrapAngle(0.25f), 0.25f, 1e-6f);
    EXPECT_NEAR(WrapAngle(2 * Pi + 0.25f), 0.25f, 1e-5f);
    EXPECT_NEAR(WrapAngle(-Pi - 0.25f), Pi - 0.25f, 1e-5f);
    EXPECT_NEAR(WrapAngle(-Pi), Pi, 1e-5f);
    EXPECT_NEAR(std::abs(WrapAngle(5 * Pi)), Pi, 1e-4f); // half a turn, either way round
    EXPECT_NEAR(WrapAngle(-7 * Pi / 2), Pi / 2, 1e-4f);
}

TEST(BloodborneCompass, AMemberAheadIsAtBearingZero) {
    const CompassPoint me{10.0f, 0.0f, 10.0f};
    const auto ahead = BearingOf(me, 0.0f, {10.0f, 0.0f, 22.0f});
    ASSERT_TRUE(ahead.has_value());
    EXPECT_NEAR(ahead->bearing, 0.0f, 1e-5f);
    EXPECT_NEAR(ahead->distance, 12.0f, 1e-4f);
}

TEST(BloodborneCompass, RightOfTheCameraIsPositive) {
    // Looking along +z, turning right turns toward +x: a member 5 m to +x is a quarter turn right.
    const CompassPoint me{0.0f, 0.0f, 0.0f};
    EXPECT_NEAR(BearingOf(me, 0.0f, {5.0f, 0.0f, 0.0f})->bearing, Pi / 2, 1e-5f);
    EXPECT_NEAR(BearingOf(me, 0.0f, {-5.0f, 0.0f, 0.0f})->bearing, -Pi / 2, 1e-5f);
    // The camera turned a quarter right (looking along +x): +x is ahead, +z on the left.
    EXPECT_NEAR(BearingOf(me, Pi / 2, {5.0f, 0.0f, 0.0f})->bearing, 0.0f, 1e-5f);
    EXPECT_NEAR(BearingOf(me, Pi / 2, {0.0f, 0.0f, 5.0f})->bearing, -Pi / 2, 1e-5f);
    // 45 degrees right, with height counted in the distance only.
    const auto up = BearingOf(me, 0.0f, {3.0f, 4.0f, 3.0f});
    EXPECT_NEAR(up->bearing, 45 * Degree, 1e-5f);
    EXPECT_NEAR(up->distance, std::sqrt(34.0f), 1e-4f);
}

TEST(BloodborneCompass, BearingWrapsBehindTheCamera) {
    const CompassPoint me{0.0f, 0.0f, 0.0f};
    // Looking along +z at 170 degrees right: a member straight behind and a little left of it.
    const auto behind =
        BearingOf(me, 170 * Degree, {std::sin(-30 * Degree), 0.0f, std::cos(-30 * Degree)});
    ASSERT_TRUE(behind.has_value());
    EXPECT_NEAR(behind->bearing, 160 * Degree, 1e-4f);
}

TEST(BloodborneCompass, AMemberOnTheSameSpotHasNoBearing) {
    EXPECT_FALSE(BearingOf({1.0f, 2.0f, 3.0f}, 0.0f, {1.0f, 5.0f, 3.0f}).has_value());
}

TEST(BloodborneCompass, StripPlacesAcrossItsSpanAndPinsTheRest) {
    EXPECT_EQ(PlaceOnStrip(0.0f), (CompassPlace{0.0f, false}));
    EXPECT_NEAR(PlaceOnStrip(45 * Degree).x, 0.5f, 1e-5f);
    EXPECT_NEAR(PlaceOnStrip(-45 * Degree).x, -0.5f, 1e-5f);
    EXPECT_FALSE(PlaceOnStrip(89 * Degree).pinned);
    EXPECT_EQ(PlaceOnStrip(120 * Degree), (CompassPlace{1.0f, true}));
    EXPECT_EQ(PlaceOnStrip(-179 * Degree), (CompassPlace{-1.0f, true}));
    EXPECT_EQ(PlaceOnStrip(Pi), (CompassPlace{1.0f, true}));
}

// Kaniel 2026-10-17: a downed member's marker comes over the others (drawn last), wherever it is.
TEST(BloodborneCompass, DownedMarkersComeLast) {
    const CompassPoint me{0.0f, 0.0f, 0.0f};
    const std::vector<std::optional<CompassPoint>> members{
        CompassPoint{0.0f, 0.0f, 5.0f}, CompassPoint{0.0f, 0.0f, -40.0f},
        CompassPoint{1.0f, 0.0f, 5.0f}, std::nullopt};
    const std::vector<bool> down{false, true, false, true};
    const auto markers = CompassMarkers(members, me, 0.0f, down);
    ASSERT_EQ(markers.size(), 3u);
    EXPECT_EQ(markers[0].member, 0);
    EXPECT_EQ(markers[1].member, 2);
    EXPECT_EQ(markers[2].member, 1);
    EXPECT_TRUE(markers[2].down);
    EXPECT_TRUE(markers[2].place.pinned); // behind, at the strip's end, still shown
    EXPECT_FALSE(markers[0].down);
}

TEST(BloodborneCompass, LivePositionsTakeTheNearestCharacterOnce) {
    const std::vector<std::optional<CompassPoint>> members{
        CompassPoint{0.0f, 0.0f, 0.0f}, CompassPoint{10.0f, 0.0f, 0.0f}, std::nullopt,
        CompassPoint{100.0f, 0.0f, 0.0f}};
    const std::vector<std::optional<CompassPoint>> characters{
        CompassPoint{9.0f, 0.0f, 0.0f}, CompassPoint{0.5f, 0.0f, 0.0f}, std::nullopt,
        CompassPoint{1.0f, 0.0f, 0.0f}};
    const auto live = LivePositions(members, characters, 25.0f);
    ASSERT_EQ(live.size(), members.size());
    EXPECT_EQ(live[0], (CompassPoint{0.5f, 0.0f, 0.0f}));
    EXPECT_EQ(live[1], (CompassPoint{9.0f, 0.0f, 0.0f}));
    EXPECT_FALSE(live[2].has_value());
    EXPECT_FALSE(live[3].has_value()); // the one left over is 99 m away
}

TEST(BloodborneCompass, MarkersForTheMembersStandingHere) {
    // The panel's members by index: one ahead-right, one on another map, one behind, one on this
    // player's spot.
    const CompassPoint me{-215.0f, -54.5f, 178.0f};
    const std::vector<std::optional<CompassPoint>> members{
        CompassPoint{-211.0f, -54.5f, 182.0f}, std::nullopt, CompassPoint{-215.0f, -54.5f, 168.0f},
        me};
    const auto markers = CompassMarkers(members, me, 0.0f);
    ASSERT_EQ(markers.size(), 2u);
    EXPECT_EQ(markers[0].member, 0);
    EXPECT_NEAR(markers[0].bearing, 45 * Degree, 1e-5f);
    EXPECT_NEAR(markers[0].distance, std::sqrt(32.0f), 1e-4f);
    EXPECT_FALSE(markers[0].place.pinned);
    EXPECT_NEAR(markers[0].place.x, 0.5f, 1e-5f);
    EXPECT_EQ(markers[1].member, 2);
    EXPECT_NEAR(std::abs(markers[1].bearing), Pi, 1e-5f);
    EXPECT_TRUE(markers[1].place.pinned);
    EXPECT_NEAR(markers[1].distance, 10.0f, 1e-4f);
}

// Kaniel 2026-10-20 (after v1.0.2: "you only see your bloodstain on compass and map"): this
// player's own bloodstain on the compass at its bearing; the caller passes it only on this map.
TEST(BloodborneCompass, OnlyThisPlayersOwnStain) {
    const CompassPoint me{-215.0f, -54.5f, 178.0f};
    const auto ahead = CompassStainOf(CompassPoint{-215.0f, -54.5f, 198.0f}, me, 0.0f);
    ASSERT_TRUE(ahead.has_value());
    EXPECT_NEAR(ahead->bearing, 0.0f, 1e-5f);
    EXPECT_NEAR(ahead->distance, 20.0f, 1e-4f);
    const auto right = CompassStainOf(CompassPoint{-205.0f, -54.5f, 178.0f}, me, 0.0f);
    ASSERT_TRUE(right.has_value());
    EXPECT_NEAR(right->bearing, 90 * Degree, 1e-5f);
    EXPECT_NEAR(right->place.x, 1.0f, 1e-5f);
    EXPECT_FALSE(CompassStainOf(std::nullopt, me, 0.0f).has_value());
}

} // namespace
