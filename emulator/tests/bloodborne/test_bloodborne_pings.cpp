// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Team pings (Kaniel 2026-10-24): the pings kept (one per member, 20 s, a second apart), the
// message both ways, where an aimed ping goes, where a ping shows on the screen and the map.

#include <cmath>
#include <numbers>

#include <gtest/gtest.h>

#include "coop/coop_messages.h"
#include "core/bloodborne_pings.h"

using namespace Core::Bloodborne;

namespace {

constexpr float Pi = std::numbers::pi_v<float>;

// A camera at `at` looking along `heading` (0 north, +z; growing to the right, toward +x) tilted
// `pitch` down, as the game's: its x axis to the picture's right (looking north: east).
PingCamera CameraAt(const PingPoint& at, float heading, float pitch = 0.0f) {
    const float sh = std::sin(heading), ch = std::cos(heading);
    const float sp = std::sin(pitch), cp = std::cos(pitch);
    const std::array<float, 3> looks{sh * cp, -sp, ch * cp};
    const std::array<float, 3> up{sh * sp, cp, ch * sp};
    const std::array<float, 3> right{up[1] * looks[2] - up[2] * looks[1],
                                     up[2] * looks[0] - up[0] * looks[2],
                                     up[0] * looks[1] - up[1] * looks[0]};
    PingCamera camera;
    camera.rows[0] = {right[0], right[1], right[2], 0.0f};
    camera.rows[1] = {up[0], up[1], up[2], 0.0f};
    camera.rows[2] = {looks[0], looks[1], looks[2], 0.0f};
    camera.rows[3] = {at[0], at[1], at[2], 1.0f};
    camera.fov = 0.75f;
    camera.aspect = 16.0f / 9.0f;
    camera.near_plane = 0.05f;
    camera.far_plane = 3000.0f;
    return camera;
}

TeamPingMark Mark(std::string member, u32 seq, s64 placed_ms, float x = 1.0f) {
    TeamPingMark ping;
    ping.member = std::move(member);
    ping.map = 0x18010000;
    ping.at = {x, 2.0f, 3.0f};
    ping.seq = seq;
    ping.placed_ms = placed_ms;
    return ping;
}

TEST(BloodbornePings, APingLastsTwentySeconds) {
    PingBoard board;
    ASSERT_TRUE(board.Take(Mark("guest", 1, 1'000)));
    EXPECT_EQ(board.Live(1'000).size(), 1u);
    EXPECT_EQ(board.Live(1'000 + PingLifetimeMs - 1).size(), 1u);
    EXPECT_TRUE(board.Live(1'000 + PingLifetimeMs).empty());
    // Gone for good, not back when the clock is read again.
    EXPECT_TRUE(board.Live(1'000).empty());
    EXPECT_EQ(PingLifetimeMs, 20'000);
}

TEST(BloodbornePings, ANewPingReplacesTheSendersOld) {
    PingBoard board;
    ASSERT_TRUE(board.Take(Mark("guest", 1, 1'000, 1.0f)));
    ASSERT_TRUE(board.Take(Mark("host", 1, 2'000, 5.0f)));
    ASSERT_TRUE(board.Take(Mark("guest", 2, 3'000, 9.0f)));
    const auto live = board.Live(3'000);
    ASSERT_EQ(live.size(), 2u);
    EXPECT_EQ(live[0].member, "host");
    EXPECT_EQ(live[1].member, "guest");
    EXPECT_EQ(live[1].at[0], 9.0f);
    EXPECT_EQ(live[1].seq, 2u);
    // The replacement lives its own 20 s.
    EXPECT_EQ(board.Live(3'000 + PingLifetimeMs - 1).size(), 1u);
}

TEST(BloodbornePings, ThePlayersOwnPingIsKeptLikeTheOthers) {
    PingBoard board;
    ASSERT_TRUE(board.Take(Mark("", 1, 0)));
    ASSERT_TRUE(board.Take(Mark("", 2, 500, 4.0f)));
    const auto live = board.Live(500);
    ASSERT_EQ(live.size(), 1u);
    EXPECT_EQ(live[0].member, "");
    EXPECT_EQ(live[0].at[0], 4.0f);
}

TEST(BloodbornePings, ThePingHeardAgainIsNotTakenTwice) {
    PingBoard board;
    ASSERT_TRUE(board.Take(Mark("guest", 7, 1'000)));
    EXPECT_FALSE(board.Take(Mark("guest", 7, 9'000)));
    // Still the first one's time: it does not live longer for being heard again.
    EXPECT_TRUE(board.Live(1'000 + PingLifetimeMs).empty());
    // A restarted game counts from 1 again: a new ping.
    EXPECT_TRUE(board.Take(Mark("guest", 1, 30'000)));
}

TEST(BloodbornePings, AnEnemyPingMovesWithItsEnemy) {
    PingBoard board;
    TeamPingMark ping = Mark("host", 1, 0);
    ping.enemy = 0x1001400b;
    ASSERT_TRUE(board.Take(ping));
    board.Follow("host", {40.0f, 1.0f, -3.0f});
    board.Follow("nobody", {0.0f, 0.0f, 0.0f});
    const auto live = board.Live(100);
    ASSERT_EQ(live.size(), 1u);
    EXPECT_EQ(live[0].at, (PingPoint{40.0f, 1.0f, -3.0f}));
    EXPECT_EQ(live[0].enemy, 0x1001400bu);
}

TEST(BloodbornePings, OnePingASecond) {
    PingCooldown cooldown;
    EXPECT_TRUE(cooldown.Allow(5'000));
    EXPECT_FALSE(cooldown.Allow(5'000 + PingCooldownMs - 1));
    EXPECT_TRUE(cooldown.Allow(5'000 + PingCooldownMs));
    // A refused press does not start the second again.
    EXPECT_FALSE(cooldown.Allow(6'500));
    EXPECT_TRUE(cooldown.Allow(7'000));
}

TEST(BloodbornePings, TheMessageReadsBackAsItWasWritten) {
    BloodborneCoop::TeamPing ping;
    ping.map = 0x18010000;
    ping.at = {-215.5f, -54.25f, 178.0f};
    ping.enemy = 0x1001400b;
    ping.seq = 42;
    ping.from_map = true;
    const bbcoop::Ping message = BloodborneCoop::ToMessage(ping);
    bbcoop::Envelope envelope;
    *envelope.mutable_ping() = message;
    const auto decoded = BloodborneCoop::DecodeEnvelope(BloodborneCoop::EncodeEnvelope(envelope));
    ASSERT_TRUE(decoded.has_value() && decoded->has_ping());
    bbcoop::Ping relayed = decoded->ping();
    relayed.set_member("host"); // the server says whose
    const BloodborneCoop::TeamPing back = BloodborneCoop::PingOf(relayed);
    EXPECT_EQ(back.member, "host");
    EXPECT_EQ(back.map, ping.map);
    EXPECT_EQ(back.at, ping.at);
    EXPECT_EQ(back.enemy, ping.enemy);
    EXPECT_EQ(back.seq, 42u);
    EXPECT_TRUE(back.from_map);
    // A game's own message names nobody: the server fills it in.
    EXPECT_TRUE(message.member().empty());
    // No enemy, from the world: nothing more on the wire than the spot.
    BloodborneCoop::TeamPing plain;
    plain.map = 0x18010000;
    plain.seq = 1;
    EXPECT_EQ(BloodborneCoop::PingOf(BloodborneCoop::ToMessage(plain)).enemy, 0u);
    EXPECT_FALSE(BloodborneCoop::PingOf(BloodborneCoop::ToMessage(plain)).from_map);
}

TEST(BloodbornePings, TheGamesCameraLooksRight) {
    EXPECT_TRUE(LooksLikePingCamera(CameraAt({0, 0, 0}, 0.3f, 0.2f)));
    PingCamera bad = CameraAt({0, 0, 0}, 0.0f);
    bad.rows[0][0] = 2.0f;
    EXPECT_FALSE(LooksLikePingCamera(bad));
    bad = CameraAt({0, 0, 0}, 0.0f);
    bad.fov = 0.0f;
    EXPECT_FALSE(LooksLikePingCamera(bad));
    bad = CameraAt({0, 0, 0}, 0.0f);
    bad.rows[3][1] = NAN;
    EXPECT_FALSE(LooksLikePingCamera(bad));
    EXPECT_FALSE(LooksLikePingCamera(PingCamera{}));
    // The game's own: looking south (-z), its x axis is west (-x).
    const PingCamera south = CameraAt({0, 0, 0}, Pi);
    EXPECT_NEAR(south.rows[0][0], -1.0f, 1e-5f);
    EXPECT_NEAR(CameraLooks(south)[2], -1.0f, 1e-5f);
    EXPECT_EQ(CameraPosition(CameraAt({1, 2, 3}, 0.0f)), (PingPoint{1, 2, 3}));
}

TEST(BloodbornePings, AheadIsTheMiddleOfThePicture) {
    const PingCamera camera = CameraAt({10, 0, 10}, 0.0f);
    const auto mid = ProjectPing(camera, {10, 0, 30});
    ASSERT_TRUE(mid.has_value());
    EXPECT_NEAR((*mid)[0], 0.0f, 1e-5f);
    EXPECT_NEAR((*mid)[1], 0.0f, 1e-5f);
    // Looking north, east (+x) is to the right; up is up.
    const auto right = ProjectPing(camera, {12, 0, 30});
    ASSERT_TRUE(right.has_value());
    EXPECT_GT((*right)[0], 0.0f);
    const auto up = ProjectPing(camera, {10, 2, 30});
    ASSERT_TRUE(up.has_value());
    EXPECT_GT((*up)[1], 0.0f);
    // The top edge: half the field of view up.
    const auto top = ProjectPing(camera, {10, 20.0f * std::tan(0.375f), 30});
    ASSERT_TRUE(top.has_value());
    EXPECT_NEAR((*top)[1], 1.0f, 1e-4f);
    // Behind the camera: nothing.
    EXPECT_FALSE(ProjectPing(camera, {10, 0, 5}).has_value());
}

TEST(BloodbornePings, AFarPingIsPinnedToTheEdgeItsWay) {
    const PingCamera camera = CameraAt({0, 0, 0}, 0.0f);
    const auto mid = PingScreenSpot(camera, {0, 0, 20}, 0.05f);
    ASSERT_TRUE(mid.has_value());
    EXPECT_FALSE(mid->pinned);
    EXPECT_NEAR(mid->x, 0.5f, 1e-5f);
    EXPECT_NEAR(mid->y, 0.5f, 1e-5f);
    // Far to the right (east, ahead): on the right edge, the arrow pointing right.
    const auto east = PingScreenSpot(camera, {100, 0, 5}, 0.05f);
    ASSERT_TRUE(east.has_value());
    EXPECT_TRUE(east->pinned);
    EXPECT_NEAR(east->x, 0.95f, 1e-4f);
    EXPECT_NEAR(east->arrow, 0.0f, 0.2f);
    // Behind and to the left (west): the left edge, never the right.
    const auto behind_west = PingScreenSpot(camera, {-10, 0, -30}, 0.05f);
    ASSERT_TRUE(behind_west.has_value());
    EXPECT_TRUE(behind_west->pinned);
    EXPECT_LT(behind_west->x, 0.5f);
    // Straight behind: pinned at the bottom.
    const auto behind = PingScreenSpot(camera, {0, 0, -30}, 0.05f);
    ASSERT_TRUE(behind.has_value());
    EXPECT_TRUE(behind->pinned);
    EXPECT_NEAR(behind->y, 0.95f, 1e-4f);
    EXPECT_NEAR(behind->arrow, Pi / 2, 0.2f);
    // High above: the top edge.
    const auto above = PingScreenSpot(camera, {0, 80, 10}, 0.05f);
    ASSERT_TRUE(above.has_value());
    EXPECT_TRUE(above->pinned);
    EXPECT_NEAR(above->y, 0.05f, 1e-4f);
    // Everything stays inside the margin.
    for (const PingPoint& point :
         {PingPoint{300, -50, 1}, PingPoint{-5, 3, -2}, PingPoint{7, -90, 3}}) {
        const auto spot = PingScreenSpot(camera, point, 0.05f);
        ASSERT_TRUE(spot.has_value());
        EXPECT_GE(spot->x, 0.05f - 1e-5f);
        EXPECT_LE(spot->x, 0.95f + 1e-5f);
        EXPECT_GE(spot->y, 0.05f - 1e-5f);
        EXPECT_LE(spot->y, 0.95f + 1e-5f);
    }
    EXPECT_FALSE(PingScreenSpot(PingCamera{}, {0, 0, 20}, 0.05f).has_value());
}

TEST(BloodbornePings, AnAimedPingGoesWhereTheLineMeetsTheGround) {
    // The camera 2 m over the feet, looking north and down 10 degrees: the ground ~11.3 m ahead.
    const PingPoint feet{0, -50, 0};
    const PingCamera camera = CameraAt({0, -48, -3}, 0.0f, 10.0f * Pi / 180.0f);
    const PingAim aim = AimPing(camera, feet, {});
    EXPECT_EQ(aim.enemy, 0u);
    EXPECT_NEAR(aim.at[1], -50.0f, 1e-3f);
    EXPECT_NEAR(aim.at[0], 0.0f, 1e-3f);
    EXPECT_NEAR(aim.at[2], -3.0f + 2.0f / std::tan(10.0f * Pi / 180.0f), 1e-2f);
}

TEST(BloodbornePings, AnAimedPingAtTheSkyGoesAFixedWayAlong) {
    const PingPoint feet{0, 0, 0};
    const PingCamera camera = CameraAt({0, 2, -3}, 0.0f, -0.3f); // looking up
    const PingAim aim = AimPing(camera, feet, {});
    const PingPoint from = CameraPosition(camera), looks = CameraLooks(camera);
    for (int i = 0; i < 3; ++i) {
        EXPECT_NEAR(aim.at[i], from[i] + looks[i] * PingAimFallback, 1e-3f);
    }
    // Ground too far away (a shallow look down): the fallback too, never past the reach.
    const PingCamera shallow = CameraAt({0, 2, -3}, 0.0f, 0.005f);
    const PingAim far = AimPing(shallow, feet, {});
    const float dx = far.at[0] - feet[0], dy = far.at[1] - feet[1], dz = far.at[2] - feet[2];
    EXPECT_LE(std::sqrt(dx * dx + dy * dy + dz * dz), PingReach + 1e-3f);
}

TEST(BloodbornePings, AnAimedPingGoesOnTheCharacterInTheWay) {
    const PingPoint feet{0, 0, 0};
    const PingCamera camera = CameraAt({0, 2, -3}, 0.0f, 2.0f * Pi / 180.0f);
    // One on the line 15 m ahead, one beside it, one behind the first along the line.
    const std::vector<PingCharacter> characters{
        {0x10010001, {6.0f, 0.0f, 12.0f}},
        {0x10010002, {0.3f, 0.0f, 25.0f}},
        {0x10010003, {0.2f, 0.0f, 12.0f}},
    };
    const PingAim aim = AimPing(camera, feet, characters);
    EXPECT_EQ(aim.enemy, 0x10010003u);
    EXPECT_EQ(aim.at, characters[2].at);
    // Out of reach: the ground instead.
    const std::vector<PingCharacter> far{{0x10010004, {0.0f, 0.0f, 90.0f}}};
    EXPECT_EQ(AimPing(CameraAt({0, 2, -3}, 0.0f, 0.0f), feet, far).enemy, 0u);
}

TEST(BloodbornePings, AnAimedPingStaysWithinReachOfThePlayer) {
    // Looking down a long slope: the ground far below is further than the reach.
    const PingPoint feet{0, 0, 0};
    const PingCamera camera = CameraAt({0, 2, -3}, 0.0f, 0.01f);
    const PingAim aim = AimPing(camera, PingPoint{0, -1, 0}, {});
    const float dx = aim.at[0] - feet[0], dy = aim.at[1] - feet[1], dz = aim.at[2] - feet[2];
    EXPECT_LE(std::sqrt(dx * dx + dy * dy + dz * dz), PingReach + 0.01f);
}

TEST(BloodbornePings, TheLockedOnTargetIsTheOneUnderTheLockCursor) {
    const PingCamera camera = CameraAt({0, 2, -3}, 0.0f, 0.1f);
    const std::vector<PingCharacter> characters{
        {0x10010001, {4.0f, 0.0f, 10.0f}},
        {0x10010002, {-3.0f, 0.0f, 9.0f}},
    };
    // Where the first's middle shows, on the HUD's 1920 x 1080 screen.
    const auto seen = ProjectPing(camera, {4.0f, 0.9f, 10.0f});
    ASSERT_TRUE(seen.has_value());
    const float cx = ((*seen)[0] + 1.0f) * 0.5f * 1920.0f,
                cy = (1.0f - (*seen)[1]) * 0.5f * 1080.0f;
    const auto locked = LockedCharacter(camera, cx + 15.0f, cy - 10.0f, characters);
    ASSERT_TRUE(locked.has_value());
    EXPECT_EQ(locked->handle, 0x10010001u);
    // Nothing near the cursor: none.
    EXPECT_FALSE(LockedCharacter(camera, 1900.0f, 20.0f, characters).has_value());
    EXPECT_FALSE(LockedCharacter(camera, cx, cy, {}).has_value());
}

TEST(BloodbornePings, AMapPingIsOnTheAreaUnderTheCursor) {
    // Two areas of one world side by side, and one of another world placed further on the atlas.
    const std::vector<PingArea> areas{
        {0x18010000, -300, 0, 0, 300, 0, 0},
        {0x17000000, 0, 300, 0, 300, 0, 0},
        {0x21000000, 1414, 1800, 620, 1000, 1414, 620},
    };
    EXPECT_EQ(PingAreaAt(areas, 0x18010000, -10.0f, 50.0f),
              (PingAreaSpot{0x18010000, -10.0f, 50.0f}));
    EXPECT_EQ(PingAreaAt(areas, 0x18010000, 10.0f, 50.0f),
              (PingAreaSpot{0x17000000, 10.0f, 50.0f}));
    // On another world's area: its own metres (the atlas place taken off).
    EXPECT_EQ(PingAreaAt(areas, 0x18010000, 1500.0f, 700.0f),
              (PingAreaSpot{0x21000000, 86.0f, 80.0f}));
    // Where two pictures overlap, the shown area's.
    const std::vector<PingArea> overlapping{
        {0x18010000, -300, 300, 0, 300, 0, 0},
        {0x18000000, -100, 100, 0, 100, 0, 0},
    };
    EXPECT_EQ(PingAreaAt(overlapping, 0x18000000, 0.0f, 50.0f)->map, 0x18000000u);
    EXPECT_EQ(PingAreaAt(overlapping, 0x18010000, 0.0f, 50.0f)->map, 0x18010000u);
    // Off every picture: no ping.
    EXPECT_FALSE(PingAreaAt(areas, 0x18010000, 5000.0f, 5000.0f).has_value());
}

TEST(BloodbornePings, ThePingSaysHowFarOrWhere) {
    EXPECT_EQ(PingLabel("Ada", 7.26f, "Central Yharnam"), "Ada  7.3 m");
    EXPECT_EQ(PingLabel("Ada", 42.6f, "Central Yharnam"), "Ada  43 m");
    EXPECT_EQ(PingLabel("Ada", std::nullopt, "Cathedral Ward"), "Ada  Cathedral Ward");
    EXPECT_EQ(PingLabel("Ada", std::nullopt, ""), "Ada");
}

} // namespace

namespace {

// Kaniel 2026-10-24 (after the first pictures): a map ping's words never sit on a place name.
// The mark at (100, 100), radius 10; the words 60 x 14; 4 px from the mark.
constexpr float Mx = 100, My = 100, Mr = 10, W = 60, H = 14, Gap = 4;

bool Crosses(const PingBox& a, const PingBox& b) {
    return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}

TEST(BloodbornePings, AMapPingsWordsGoUnderItWhenNothingIsThere) {
    const PingBox box = PlacePingLabel(Mx, My, Mr, W, H, {}, Gap);
    EXPECT_FLOAT_EQ(box.x0, Mx - W / 2);
    EXPECT_FLOAT_EQ(box.x1, Mx + W / 2);
    EXPECT_FLOAT_EQ(box.y0, My + Mr + Gap);
    EXPECT_FLOAT_EQ(box.y1, My + Mr + Gap + H);
}

TEST(BloodbornePings, AMapPingsWordsMoveOffAPlaceName) {
    // A place name just under the mark: the words go over it.
    const std::vector<PingBox> under{{70, 112, 160, 128}};
    const PingBox over = PlacePingLabel(Mx, My, Mr, W, H, under, Gap);
    EXPECT_FALSE(Crosses(over, under[0]));
    EXPECT_FLOAT_EQ(over.y1, My - Mr - Gap);
    EXPECT_FLOAT_EQ(over.x0, Mx - W / 2);
    // Names under and over it: to its right, level with it.
    const std::vector<PingBox> both{{70, 112, 160, 128}, {60, 70, 140, 88}};
    const PingBox right = PlacePingLabel(Mx, My, Mr, W, H, both, Gap);
    for (const PingBox& name : both) {
        EXPECT_FALSE(Crosses(right, name));
    }
    EXPECT_FLOAT_EQ(right.x0, Mx + Mr + Gap);
    EXPECT_FLOAT_EQ((right.y0 + right.y1) / 2, My);
    // Right blocked too: to its left.
    const std::vector<PingBox> three{{70, 112, 160, 128}, {60, 70, 140, 88}, {112, 90, 200, 110}};
    const PingBox left = PlacePingLabel(Mx, My, Mr, W, H, three, Gap);
    for (const PingBox& name : three) {
        EXPECT_FALSE(Crosses(left, name));
    }
    EXPECT_FLOAT_EQ(left.x1, Mx - Mr - Gap);
}

TEST(BloodbornePings, AMapPingsWordsTakeTheLeastCrowdedPlaceWhenAllAreTaken) {
    // Every side has a name; the one over the mark only touches a corner of the words' place.
    const std::vector<PingBox> names{{40, 112, 160, 140}, // under, all of it
                                     {125, 60, 140, 80},  // over, a corner
                                     {112, 80, 220, 120}, // right, all of it
                                     {0, 80, 88, 120}};   // left, all of it
    const PingBox box = PlacePingLabel(Mx, My, Mr, W, H, names, Gap);
    EXPECT_FLOAT_EQ(box.y1, My - Mr - Gap);
    EXPECT_FLOAT_EQ(box.x0, Mx - W / 2);
}

} // namespace
