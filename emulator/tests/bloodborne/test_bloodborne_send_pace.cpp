// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// The send pace of the opt-in unlocked frame rate online: the game's frame-counted packets go no
// more often than in the 60 FPS mode (Kaniel 2026-10-13: an uncapped host sent its state several
// times as often, and the guests saw everyone freeze and fly).
#include <chrono>

#include <gtest/gtest.h>

#include "core/bloodborne_send_pace.h"

using namespace Core::Bloodborne;
using namespace std::chrono_literals;

namespace {

SendPace::Clock::time_point At(std::chrono::microseconds since) {
    return SendPace::Clock::time_point{} + 1h + since;
}

} // namespace

TEST(SendPace, TheFirstSendOfAnObjectGoes) {
    SendPace pace;
    EXPECT_TRUE(pace.Allow(0x1000, At(0us), 166ms));
}

TEST(SendPace, AnEarlySendWaitsForItsInterval) {
    SendPace pace;
    ASSERT_TRUE(pace.Allow(0x1000, At(0us), 166ms));
    // 120 FPS: the game's counter says send again after 5 frames (41 ms).
    EXPECT_FALSE(pace.Allow(0x1000, At(41ms), 166ms));
    EXPECT_FALSE(pace.Allow(0x1000, At(141ms), 166ms));
    EXPECT_TRUE(pace.Allow(0x1000, At(166ms), 166ms));
    EXPECT_FALSE(pace.Allow(0x1000, At(200ms), 166ms));
}

TEST(SendPace, SixtyFpsFrameJitterNeverHoldsASend) {
    // At 60 FPS the game's own count already spaces the sends: a frame a little early must not
    // cost a whole frame more.
    SendPace pace;
    ASSERT_TRUE(pace.Allow(0x1000, At(0us), 166'667us));
    EXPECT_TRUE(pace.Allow(0x1000, At(160ms), 166'667us));
}

TEST(SendPace, EveryObjectHasItsOwnPace) {
    SendPace pace;
    ASSERT_TRUE(pace.Allow(0x1000, At(0us), 166ms));
    EXPECT_TRUE(pace.Allow(0x2000, At(10ms), 166ms));
    EXPECT_FALSE(pace.Allow(0x1000, At(20ms), 166ms));
}

TEST(SendPace, ObjectsNotSeenForAWhileAreForgotten) {
    SendPace pace;
    for (u64 object = 1; object <= 600; ++object) {
        ASSERT_TRUE(pace.Allow(object * 0x10, At(0us), 166ms));
    }
    EXPECT_EQ(pace.Size(), 600u);
    ASSERT_TRUE(pace.Allow(0x99990, At(10s), 166ms));
    EXPECT_EQ(pace.Size(), 1u);
}

// The player state counter (CS ChrSync, +0xC8): the game sends when the value it loads is 1 or
// less. Held back, it loads 2: the count goes on to 1 and the game asks again next frame.
TEST(SendPace, CountdownCounterIsHeldAtOneFrameLeft) {
    EXPECT_EQ(HeldCountdown(1, false), 2);
    EXPECT_EQ(HeldCountdown(0, false), 2);
    EXPECT_EQ(HeldCountdown(-3, false), 2);
    EXPECT_EQ(HeldCountdown(1, true), 1);
    EXPECT_EQ(HeldCountdown(4, false), 4);
}

// The second counter (+0xF8) is checked after its decrement: 0 or less sends.
TEST(SendPace, DecrementedCounterIsHeldAtOne) {
    EXPECT_EQ(HeldDecremented(0, false), 1);
    EXPECT_EQ(HeldDecremented(-1, false), 1);
    EXPECT_EQ(HeldDecremented(0, true), 0);
    EXPECT_EQ(HeldDecremented(5, false), 5);
}

// The enemy HP round (+0x680) sends when the count it loads has its low two bits clear. Held
// back, it loads one less: the stored count stays and the game asks again next frame.
TEST(SendPace, RoundCounterIsHeldOnItsSendFrame) {
    EXPECT_EQ(HeldRound(8, false), 7u);
    EXPECT_EQ(HeldRound(0, false), 0xFFFFFFFFu);
    EXPECT_EQ(HeldRound(8, true), 8u);
    EXPECT_EQ(HeldRound(9, false), 9u);
    EXPECT_TRUE(RoundSends(8));
    EXPECT_FALSE(RoundSends(7));
}

// The intervals are the 60 FPS mode's: 60 FPS++ sets the player state to every 10 frames and the
// second counter to every 20; the enemy HP round is every 4 frames in every mode.
TEST(SendPace, IntervalsAreTheSixtyFpsModes) {
    EXPECT_EQ(PlayerStatePaceInterval, std::chrono::microseconds{166'667});
    EXPECT_EQ(SecondCounterPaceInterval, std::chrono::microseconds{333'333});
    EXPECT_EQ(EnemyHpPaceInterval, std::chrono::microseconds{66'667});
}

// The host's event sync (channel 0x31, 0x012EE540): an event whose condition holds sends it every
// frame, so at most 60 a second per event (and slot) go, as in the 60 FPS mode. A rate, not an
// interval: at 70 FPS frame times vary, and 60 of the 70 still go.
TEST(SendPace, EventSyncGoesAtSixtyASecondPerEvent) {
    SendPace pace;
    int sent = 0;
    for (int frame = 0; frame < 120; ++frame) { // one second at 120 FPS
        sent +=
            pace.AllowRate(0x1000, At(std::chrono::microseconds{frame * 8'333}), EventSyncPerSecond)
                ? 1
                : 0;
    }
    EXPECT_GE(sent, 59);
    EXPECT_LE(sent, 62);
}

TEST(SendPace, EventSyncAtSixtyFpsAlwaysGoes) {
    SendPace pace;
    for (int frame = 0; frame < 600; ++frame) {
        // 60 FPS with a frame late and the next early: never held.
        const int jitter = frame % 2 == 0 ? 3'000 : -3'000;
        EXPECT_TRUE(pace.AllowRate(0x1000, At(std::chrono::microseconds{frame * 16'667 + jitter}),
                                   EventSyncPerSecond))
            << frame;
    }
}

TEST(SendPace, EveryEventSyncHasItsOwnRate) {
    // Rig 2026-10-22 at 60 FPS: event 9181 sent twice a frame, its condition group (+0x48) 3 and
    // 5; both must keep their 60 a second.
    SendPace pace;
    ASSERT_TRUE(pace.AllowRate(EventSyncKey(9181, 0, 3), At(0us), EventSyncPerSecond));
    EXPECT_TRUE(pace.AllowRate(EventSyncKey(9181, 0, 5), At(0us), EventSyncPerSecond));
    EXPECT_TRUE(pace.AllowRate(EventSyncKey(9181, 1, 3), At(1ms), EventSyncPerSecond));
    EXPECT_TRUE(pace.AllowRate(EventSyncKey(9182, 0, 3), At(1ms), EventSyncPerSecond));
    EXPECT_NE(EventSyncKey(9181, 0, 3), EventSyncKey(9181, 0, 5));
    EXPECT_NE(EventSyncKey(9181, 0, 3), EventSyncKey(9181, 1, 3));
}
