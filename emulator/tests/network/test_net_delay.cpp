// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>

#include <gtest/gtest.h>

#include "core/libraries/network/net_delay.h"

using namespace Libraries::Net;
using namespace std::chrono_literals;

TEST(NetDelay, ParsesOneValueOrARange) {
    const auto fixed = ParseDelayRange("40");
    ASSERT_TRUE(fixed.has_value());
    EXPECT_EQ(fixed->min_ms, 40u);
    EXPECT_EQ(fixed->max_ms, 40u);

    const auto range = ParseDelayRange("25-50");
    ASSERT_TRUE(range.has_value());
    EXPECT_EQ(range->min_ms, 25u);
    EXPECT_EQ(range->max_ms, 50u);
}

TEST(NetDelay, NothingOrNonsenseMeansNoDelay) {
    EXPECT_FALSE(ParseDelayRange("").has_value());
    EXPECT_FALSE(ParseDelayRange("0").has_value());
    EXPECT_FALSE(ParseDelayRange("0-0").has_value());
    EXPECT_FALSE(ParseDelayRange("abc").has_value());
    EXPECT_FALSE(ParseDelayRange("50-25").has_value());
    EXPECT_FALSE(ParseDelayRange("10-").has_value());
    EXPECT_FALSE(ParseDelayRange("-10").has_value());
    EXPECT_FALSE(ParseDelayRange("10-20x").has_value());
    EXPECT_FALSE(ParseDelayRange("99999").has_value());
}

TEST(NetDelay, EveryPacketIsDueWithinTheRange) {
    DelayLine line({25, 50}, 1);
    const auto start = DelayLine::Clock::time_point{} + 10s;
    for (int i = 0; i < 200; ++i) {
        const auto sent = start + i * 100ms; // far apart: the order rule never applies
        const auto due = line.DueAt(sent);
        EXPECT_GE(due - sent, 25ms);
        EXPECT_LE(due - sent, 50ms);
    }
}

TEST(NetDelay, PacketsKeepTheirOrder) {
    DelayLine line({25, 50}, 7);
    auto sent = DelayLine::Clock::time_point{} + 10s;
    auto last_due = DelayLine::Clock::time_point{};
    bool varied = false;
    std::chrono::nanoseconds first_delay{};
    for (int i = 0; i < 500; ++i) {
        sent += 1ms; // closer than the range is wide: a random delay alone would reorder them
        const auto due = line.DueAt(sent);
        EXPECT_GE(due, last_due);
        EXPECT_GE(due - sent, 25ms);
        EXPECT_LE(due - sent, 50ms);
        if (i == 0) {
            first_delay = due - sent;
        } else if (due - sent != first_delay) {
            varied = true;
        }
        last_due = due;
    }
    EXPECT_TRUE(varied); // jitter, not one fixed delay
}
