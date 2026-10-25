// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Spectating teammates and the free camera (Kaniel 2026-10-25): who is watched, what ends it, the
// pads, and the cameras.

#include <cmath>
#include <numbers>

#include <gtest/gtest.h>

#include "core/bloodborne_spectate.h"

using namespace Core::Bloodborne;

namespace {

constexpr float Pi = std::numbers::pi_v<float>;
constexpr u32 Map = 0x18010000;
constexpr u32 OtherMap = 0x18000000;

MemberVitals Member(std::string name, Vital vital, u32 map = Map, u32 lamp_uses = 0) {
    MemberVitals member;
    member.member = std::move(name);
    member.vital = vital;
    member.map = map;
    member.lamp_uses = lamp_uses;
    return member;
}

// A camera as the game's (looking north: its x axis points west, x = -(y cross z)).
PingCamera GameCameraAt(const PingPoint& at, float heading, float pitch = 0.0f) {
    const float sh = std::sin(heading), ch = std::cos(heading);
    const float sp = std::sin(pitch), cp = std::cos(pitch);
    const std::array<float, 3> looks{sh * cp, -sp, ch * cp};
    const std::array<float, 3> up{sh * sp, cp, ch * sp};
    const std::array<float, 3> cross{up[1] * looks[2] - up[2] * looks[1],
                                     up[2] * looks[0] - up[0] * looks[2],
                                     up[0] * looks[1] - up[1] * looks[0]};
    PingCamera camera;
    camera.rows[0] = {-cross[0], -cross[1], -cross[2], 0.0f};
    camera.rows[1] = {up[0], up[1], up[2], 0.0f};
    camera.rows[2] = {looks[0], looks[1], looks[2], 0.0f};
    camera.rows[3] = {at[0], at[1], at[2], 1.0f};
    camera.fov = 0.75f;
    camera.aspect = 16.0f / 9.0f;
    camera.near_plane = 0.05f;
    camera.far_plane = 3000.0f;
    return camera;
}

PadInput Pad(u32 buttons, u8 lx = 128, u8 ly = 128, u8 rx = 128, u8 ry = 128) {
    PadInput pad;
    pad.buttons = buttons;
    pad.sticks = {lx, ly, rx, ry};
    return pad;
}

} // namespace

// The others standing on the spectator's map, in the board's order: not those down, dead, off,
// spectating or elsewhere, and never itself.
TEST(BloodborneSpectate, WatchesTheTeammatesStandingOnItsMap) {
    const std::vector<MemberVitals> board{
        Member("me", Vital::Spectate),     Member("ada", Vital::Up),  Member("bo", Vital::Down),
        Member("cy", Vital::Up, OtherMap), Member("di", Vital::Dead), Member("ed", Vital::Spectate),
        Member("fa", Vital::Up),           Member("gi", Vital::Off),
    };
    EXPECT_EQ(SpectateTargets(board, "me", Map), (std::vector<std::string>{"ada", "fa"}));
    EXPECT_TRUE(SpectateTargets(board, "me", 0).empty());
}

// L1 the one before, R1 the next, round the list; a teammate no longer standing gives way to the
// first.
TEST(BloodborneSpectate, L1AndR1StepRoundTheTeammates) {
    const std::vector<std::string> targets{"ada", "bo", "cy"};
    EXPECT_EQ(NextSpectateTarget(targets, std::nullopt, 0), "ada");
    EXPECT_EQ(NextSpectateTarget(targets, "bo", 0), "bo");
    EXPECT_EQ(NextSpectateTarget(targets, "bo", 1), "cy");
    EXPECT_EQ(NextSpectateTarget(targets, "cy", 1), "ada");
    EXPECT_EQ(NextSpectateTarget(targets, "ada", -1), "cy");
    EXPECT_EQ(NextSpectateTarget(targets, "gone", 1), "ada");
    EXPECT_EQ(NextSpectateTarget({}, "ada", 1), std::nullopt);
}

TEST(BloodborneSpectate, WhatEndsSpectating) {
    SpectateCheck watching{.someone_up = true};
    EXPECT_EQ(EndOfSpectate(watching), SpectateEnd::None);
    // A teammate's lamp, the boss, giving up: woken at its own lamp.
    EXPECT_EQ(EndOfSpectate({.teammate_used_a_lamp = true, .someone_up = true}), SpectateEnd::Lamp);
    EXPECT_EQ(EndOfSpectate({.boss_died = true, .someone_up = true}), SpectateEnd::Boss);
    EXPECT_EQ(EndOfSpectate({.gave_up = true, .someone_up = true}), SpectateEnd::GiveUp);
    // Everyone else here dead (nobody up or down): the party's wipe, before anything else.
    EXPECT_EQ(EndOfSpectate({.gave_up = true, .teammate_used_a_lamp = true, .someone_dead = true}),
              SpectateEnd::Wipe);
    EXPECT_EQ(EndOfSpectate({.wiped = true, .someone_up = true}), SpectateEnd::Wipe);
    // Some down, nobody up: the server wipes them, then they are dead (wait for it).
    EXPECT_EQ(EndOfSpectate({.someone_down = true, .someone_dead = true}), SpectateEnd::None);
    EXPECT_EQ(EndOfSpectate({.someone_up = true, .someone_dead = true}), SpectateEnd::None);
    // Nobody else on the map any more (they travelled): woken after a moment.
    EXPECT_EQ(EndOfSpectate({.alone_ms = SpectateAloneMs - 1}), SpectateEnd::None);
    EXPECT_EQ(EndOfSpectate({.alone_ms = SpectateAloneMs}), SpectateEnd::PartyLeft);
    EXPECT_EQ(EndOfSpectate({.someone_up = true, .party_lost = true}), SpectateEnd::PartyLost);
}

TEST(BloodborneSpectate, TheBoardsOthersOnTheMap) {
    const std::vector<MemberVitals> board{Member("me", Vital::Spectate), Member("ada", Vital::Dead),
                                          Member("bo", Vital::Up, OtherMap)};
    SpectateCheck check;
    SpectateCheckOthers(check, board, "me", Map);
    EXPECT_FALSE(check.someone_up);
    EXPECT_FALSE(check.someone_down);
    EXPECT_TRUE(check.someone_dead);
    EXPECT_EQ(EndOfSpectate(check), SpectateEnd::Wipe);
}

// A teammate's lamp: its count going up. One seen the first time only says where it starts; the
// spectator's own count is not a teammate's.
TEST(BloodborneSpectate, ATeammatesLampIsItsCountGoingUp) {
    LampUseWatch watch;
    EXPECT_FALSE(watch.Tick(
        std::vector{Member("me", Vital::Spectate, Map, 0), Member("ada", Vital::Up, Map, 4)},
        "me"));
    EXPECT_FALSE(watch.Tick(
        std::vector{Member("me", Vital::Spectate, Map, 9), Member("ada", Vital::Up, Map, 4)},
        "me"));
    EXPECT_TRUE(watch.Tick(std::vector{Member("ada", Vital::Up, Map, 5)}, "me"));
    EXPECT_FALSE(watch.Tick(
        std::vector{Member("ada", Vital::Up, Map, 5), Member("bo", Vital::Up, Map, 2)}, "me"));
    EXPECT_TRUE(watch.Tick(std::vector{Member("bo", Vital::Up, Map, 3)}, "me"));
}

TEST(BloodborneSpectate, ABossDownIsItsDefeatFlagGoingOn) {
    BossDownWatch watch;
    EXPECT_FALSE(watch.Tick(std::vector<bool>{false, true}));
    EXPECT_FALSE(watch.Tick(std::vector<bool>{false, true}));
    EXPECT_TRUE(watch.Tick(std::vector<bool>{true, true}));
    watch.Restart();
    EXPECT_FALSE(watch.Tick(std::vector<bool>{true, true})); // already down when it began
}

// Nothing of a spectator's pad reaches the game; L1 / R1 going down step, the right stick turns.
TEST(BloodborneSpectate, ASpectatorsPadDrivesOnlyItsCamera) {
    const SpectatorPad held =
        SpectatorPadOf(Pad(PadR1 | PadCircle | PadOptions, 0, 0, 255, 128), PadR1);
    EXPECT_EQ(held.game.buttons, 0u);
    EXPECT_EQ(held.game.sticks, (std::array<u8, 4>{128, 128, 128, 128}));
    EXPECT_EQ(held.step, 0); // R1 still held from before
    EXPECT_NEAR(held.turn_x, 1.0f, 0.01f);
    EXPECT_NEAR(held.turn_y, 0.0f, 0.01f);
    EXPECT_EQ(SpectatorPadOf(Pad(PadR1), 0).step, 1);
    EXPECT_EQ(SpectatorPadOf(Pad(PadL1), 0).step, -1);
    EXPECT_EQ(SpectatorPadOf(Pad(PadL1 | PadR1), 0).step, 0);
    // A stick near its middle turns nothing.
    EXPECT_EQ(SpectatorPadOf(Pad(0, 128, 128, 140, 118), 0).turn_x, 0.0f);
    EXPECT_EQ(StickAxis(0), -1.0f);
    EXPECT_EQ(StickAxis(255), 1.0f);
}

TEST(BloodborneSpectate, ACamerasHeadingAndPitch) {
    const PingCamera north = GameCameraAt({0, 0, 0}, 0.0f);
    EXPECT_NEAR(CameraHeadingOf(north), 0.0f, 1e-4f);
    EXPECT_NEAR(CameraPitchOf(north), 0.0f, 1e-4f);
    const PingCamera east_down = GameCameraAt({0, 0, 0}, Pi / 2, 0.4f);
    EXPECT_NEAR(CameraHeadingOf(east_down), Pi / 2, 1e-4f);
    EXPECT_NEAR(CameraPitchOf(east_down), 0.4f, 1e-4f);
}

// The spectator's camera is a camera as the game's, behind the teammate along its heading and
// looking at a spot over its feet; the game's x axis stays on its side.
TEST(BloodborneSpectate, TheSpectatorsCameraLooksOverTheTeammate) {
    const PingCamera game = GameCameraAt({50, 3, -20}, 1.0f, 0.2f);
    const PingPoint feet{10.0f, 2.0f, 30.0f};
    const PingCamera camera = SpectateCamera(game, feet, 0.0f, 0.3f);
    EXPECT_TRUE(LooksLikePingCamera(camera));
    EXPECT_NEAR(CameraHeadingOf(camera), 0.0f, 1e-4f);
    EXPECT_NEAR(CameraPitchOf(camera), 0.3f, 1e-4f);
    // Behind it (to the south, looking north) and above.
    EXPECT_LT(camera.rows[3][2], feet[2]);
    EXPECT_GT(camera.rows[3][1], feet[1] + SpectateHeight);
    const auto spot = ProjectPing(camera, {feet[0], feet[1] + SpectateHeight, feet[2]});
    ASSERT_TRUE(spot.has_value());
    EXPECT_NEAR((*spot)[0], 0.0f, 1e-3f);
    EXPECT_NEAR((*spot)[1], 0.0f, 1e-3f);
    // Looking north the game's x axis points west, as it did.
    EXPECT_NEAR(camera.rows[0][0], -1.0f, 1e-4f);
    EXPECT_EQ(camera.fov, game.fov);
    EXPECT_EQ(camera.far_plane, game.far_plane);
}

TEST(BloodborneSpectate, TheSticksAndTheMouseTurnACamera) {
    const CameraTurn turned = TurnCamera({0.0f, 0.0f}, 1.0f, 0.0f, 0.0f, 0.0f, 0.5f);
    EXPECT_NEAR(turned.heading, CameraTurnRate * 0.5f, 1e-4f);
    const CameraTurn mouse = TurnCamera({0.0f, 0.0f}, 0.0f, 0.0f, 100.0f, 40.0f, 0.0f);
    EXPECT_NEAR(mouse.heading, 100.0f * CameraMouseRadians, 1e-4f);
    EXPECT_NEAR(mouse.pitch, 40.0f * CameraMouseRadians, 1e-4f);
    EXPECT_NEAR(TurnCamera({0.0f, 1.4f}, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f).pitch, CameraPitchLimit,
                1e-4f);
    EXPECT_NEAR(TurnCamera({0.0f, -1.4f}, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f).pitch, -CameraPitchLimit,
                1e-4f);
}

// The free camera starts where the game's is, flies along its heading with the left stick (level),
// up with R1, down with L1, fast with R2 held and slow with L2.
TEST(BloodborneSpectate, TheFreeCameraFlies) {
    const FreeCamera start = FreeCameraFrom(GameCameraAt({1, 2, 3}, Pi / 2, 0.5f));
    EXPECT_EQ(start.at, (PingPoint{1, 2, 3}));
    EXPECT_NEAR(start.turn.heading, Pi / 2, 1e-4f);
    EXPECT_NEAR(start.turn.pitch, 0.5f, 1e-4f);
    // Stick up: ahead (east), level.
    const FreeCamera ahead = StepFreeCamera(start, Pad(0, 128, 0), 0, 0, 1.0f);
    EXPECT_NEAR(ahead.at[0], 1.0f + FreeCameraSpeed, 0.1f);
    EXPECT_NEAR(ahead.at[1], 2.0f, 1e-4f);
    EXPECT_NEAR(ahead.at[2], 3.0f, 0.1f);
    // Stick right: to the right of east is south (-z).
    const FreeCamera right = StepFreeCamera(start, Pad(0, 255, 128), 0, 0, 1.0f);
    EXPECT_NEAR(right.at[2], 3.0f - FreeCameraSpeed, 0.1f);
    EXPECT_NEAR(StepFreeCamera(start, Pad(PadR1), 0, 0, 1.0f).at[1], 2.0f + FreeCameraSpeed, 1e-3f);
    EXPECT_NEAR(StepFreeCamera(start, Pad(PadL1), 0, 0, 1.0f).at[1], 2.0f - FreeCameraSpeed, 1e-3f);
    EXPECT_NEAR(StepFreeCamera(start, Pad(PadR1 | PadR2), 0, 0, 1.0f).at[1],
                2.0f + FreeCameraSpeed * FreeCameraFast, 1e-3f);
    EXPECT_NEAR(StepFreeCamera(start, Pad(PadR1 | PadL2), 0, 0, 1.0f).at[1],
                2.0f + FreeCameraSpeed * FreeCameraSlow, 1e-3f);
    // The mouse turns it.
    EXPECT_NEAR(StepFreeCamera(start, Pad(0), 100, 0, 0.0f).turn.heading,
                Pi / 2 + 100 * CameraMouseRadians, 1e-4f);
    // Drawn with it, the camera is where the free camera is.
    const PingCamera drawn = LookingCamera(GameCameraAt({0, 0, 0}, 0.0f), ahead.at,
                                           ahead.turn.heading, ahead.turn.pitch);
    EXPECT_TRUE(LooksLikePingCamera(drawn));
    EXPECT_NEAR(drawn.rows[3][0], ahead.at[0], 1e-4f);
}

TEST(BloodborneSpectate, TheFreeCamerasPadHotkey) {
    EXPECT_TRUE(FreeCameraHotkey(PadL3, PadL3 | PadSquare));
    EXPECT_FALSE(FreeCameraHotkey(PadL3 | PadSquare, PadL3 | PadSquare)); // held, not pressed
    EXPECT_FALSE(FreeCameraHotkey(0, PadSquare));                         // no L3
}
