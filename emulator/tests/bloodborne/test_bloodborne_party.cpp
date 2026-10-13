// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <string>

#include <gtest/gtest.h>

#include "core/bloodborne_party.h"

using Core::Bloodborne::CoopTimeMeter;
using Core::Bloodborne::ParsePartySetting;

// SHADPS4_BLOODBORNE_PARTY=<name>:<secret> becomes the X-ShadPS4-Bloodborne-Party header value.
// The server applies the same rules (NormalizePartyKey), so anything rejected here would be
// rejected there too.

TEST(BloodborneParty, AcceptsNameAndSecret) {
    EXPECT_EQ(ParsePartySetting("hunters:blood"), std::optional<std::string>{"hunters:blood"});
}

TEST(BloodborneParty, TrimsSurroundingWhitespace) {
    EXPECT_EQ(ParsePartySetting("  hunters:blood \t"), std::optional<std::string>{"hunters:blood"});
}

TEST(BloodborneParty, KeepsColonsInsideTheSecret) {
    EXPECT_EQ(ParsePartySetting("hunters:a:b"), std::optional<std::string>{"hunters:a:b"});
}

TEST(BloodborneParty, RejectsEmptyOrMalformed) {
    EXPECT_FALSE(ParsePartySetting(""));
    EXPECT_FALSE(ParsePartySetting("   "));
    EXPECT_FALSE(ParsePartySetting("hunters"));
    EXPECT_FALSE(ParsePartySetting(":blood"));
    EXPECT_FALSE(ParsePartySetting("hunters:"));
}

TEST(BloodborneParty, RejectsControlCharactersSoHeadersCannotBeInjected) {
    EXPECT_FALSE(ParsePartySetting("hunters:blood\r\nX-Evil: 1"));
    EXPECT_FALSE(ParsePartySetting(std::string("hunters:bl\0od", 13)));
}

TEST(BloodborneParty, RejectsOversizedValues) {
    EXPECT_FALSE(ParsePartySetting(std::string(200, 'a') + ":b"));
    EXPECT_TRUE(ParsePartySetting(std::string(126, 'a') + ":b"));
}

// Ada's FPS drop (2026-10-12): the co-op work on the game's threads is measured and logged next
// to the port's Perf lines, once per period, per section: the average and the longest call.
TEST(BloodborneCoopTime, ReportsEachSectionOncePerPeriod) {
    CoopTimeMeter meter{30'000};
    EXPECT_EQ(meter.Report(0), std::nullopt) << "nothing measured";
    meter.Add("tick", 1'000'000, 0);
    meter.Add("tick", 3'000'000, 10);
    meter.Add("enemies", 500'000, 20);
    EXPECT_EQ(meter.Report(29'999), std::nullopt) << "the period is not over";
    EXPECT_EQ(meter.Report(30'000),
              std::optional<std::string>{
                  "tick avg 2.00 ms max 3.00 ms (2 calls), enemies avg 0.50 ms max 0.50 ms (1 "
                  "calls) over 30 s"});
    EXPECT_EQ(meter.Report(30'001), std::nullopt) << "counts start over";
}
