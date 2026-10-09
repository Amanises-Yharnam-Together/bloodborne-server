// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Autoplay (SHADPS4_BLOODBORNE_AUTOPLAY=<script>): the emulator presses the buttons itself, from
// the title screen into the world, then runs a test script, so a test needs no one at the pad.

#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/bloodborne_party.h"

using namespace Core::Bloodborne;

TEST(BloodborneAutoplay, ReadsAScriptOneCommandPerLine) {
    const auto script = ParseAutoplayScript(
        "# the host\nwait-session 2 300\n\n  travel 2102962  \r\nkill nearest\n");
    ASSERT_EQ(script.size(), 3u);
    EXPECT_EQ(script[0], (AutoplayCommand{"wait-session", {"2", "300"}}));
    EXPECT_EQ(script[1], (AutoplayCommand{"travel", {"2102962"}}));
    EXPECT_EQ(script[2], (AutoplayCommand{"kill", {"nearest"}}));
    EXPECT_TRUE(ParseAutoplayScript("").empty());
    EXPECT_TRUE(ParseAutoplayScript("# only a comment\r\n   \n").empty());
}

TEST(BloodborneAutoplay, NamesThePadButtons) {
    EXPECT_EQ(AutoplayButton("cross"), 0x4000u);
    EXPECT_EQ(AutoplayButton("circle"), 0x2000u);
    EXPECT_EQ(AutoplayButton("square"), 0x8000u);
    EXPECT_EQ(AutoplayButton("triangle"), 0x1000u);
    EXPECT_EQ(AutoplayButton("options"), 0x8u);
    EXPECT_EQ(AutoplayButton("up"), 0x10u);
    EXPECT_EQ(AutoplayButton("right"), 0x20u);
    EXPECT_EQ(AutoplayButton("down"), 0x40u);
    EXPECT_EQ(AutoplayButton("left"), 0x80u);
    EXPECT_EQ(AutoplayButton("l1"), 0x400u);
    EXPECT_EQ(AutoplayButton("r1"), 0x800u);
    EXPECT_EQ(AutoplayButton("l2"), 0x100u);
    EXPECT_EQ(AutoplayButton("r2"), 0x200u);
    EXPECT_EQ(AutoplayButton("l3"), 0x2u);
    EXPECT_EQ(AutoplayButton("r3"), 0x4u) << "lock on";
    EXPECT_EQ(AutoplayButton("start"), 0u) << "not a PS4 button";
}

// "Press any button", then Continue (the title menu's first entry): Cross for 150 ms every two
// seconds, from two seconds in, until the player is in the world.
TEST(BloodborneAutoplay, TheTitleScreenGetsCrossNowAndThen) {
    EXPECT_FALSE(TitlePressesCross(0));
    EXPECT_FALSE(TitlePressesCross(1999));
    EXPECT_TRUE(TitlePressesCross(2000));
    EXPECT_TRUE(TitlePressesCross(2149));
    EXPECT_FALSE(TitlePressesCross(2150));
    EXPECT_TRUE(TitlePressesCross(4100));
    EXPECT_FALSE(TitlePressesCross(5000));
}

TEST(BloodborneAutoplay, ReadsTargetsAndComparisons) {
    EXPECT_EQ(ParseAutoplayTarget("nearest"), std::optional<u32>{0}) << "0 means the nearest";
    EXPECT_EQ(ParseAutoplayTarget("self"), std::optional<u32>{AutoplaySelf}) << "the player";
    EXPECT_EQ(ParseAutoplayTarget("marked"), std::optional<u32>{AutoplayMarked})
        << "the one a mark command picked";
    EXPECT_EQ(ParseAutoplayTarget("0x1001409c"), std::optional<u32>{0x1001409c});
    EXPECT_EQ(ParseAutoplayTarget("1001409c"), std::optional<u32>{0x1001409c});
    EXPECT_EQ(ParseAutoplayTarget("zz"), std::nullopt);
    EXPECT_EQ(CompareAutoplay(5, "<=", 5), std::optional<bool>{true});
    EXPECT_EQ(CompareAutoplay(5, "<", 5), std::optional<bool>{false});
    EXPECT_EQ(CompareAutoplay(6, ">", 5), std::optional<bool>{true});
    EXPECT_EQ(CompareAutoplay(5, ">=", 6), std::optional<bool>{false});
    EXPECT_EQ(CompareAutoplay(5, "==", 5), std::optional<bool>{true});
    EXPECT_EQ(CompareAutoplay(5, "!=", 5), std::optional<bool>{false});
    EXPECT_EQ(CompareAutoplay(5, "~", 5), std::nullopt);
}

// attack <target>: a real hit (the kill reward and the death run as in play). The teleport that
// puts the player beside it also turns the player to heading 0 (written headings do not stay),
// which faces -z: the rig's first swing, from the +z side, killed. So each cycle tries the next
// side: +z first, then +x, -z and -x, in case a map or a teleport turns the player otherwise.
TEST(BloodborneAutoplay, AnAttackCirclesTheTarget) {
    const AttackStance first = AttackStanceFor(10.0f, 20.0f, 0);
    EXPECT_NEAR(first.x, 10.0f, 1e-4f);
    EXPECT_NEAR(first.z, 20.0f + AttackDistance, 1e-4f) << "first on the +z side";
    const AttackStance second = AttackStanceFor(10.0f, 20.0f, 1);
    EXPECT_NEAR(second.x, 10.0f + AttackDistance, 1e-4f) << "then the +x side";
    EXPECT_NEAR(second.z, 20.0f, 1e-4f);
    EXPECT_NEAR(AttackStanceFor(10.0f, 20.0f, 2).z, 20.0f - AttackDistance, 1e-4f);
    EXPECT_NEAR(AttackStanceFor(10.0f, 20.0f, 3).x, 10.0f - AttackDistance, 1e-4f);
    EXPECT_NEAR(AttackStanceFor(10.0f, 20.0f, 4).z, first.z, 1e-4f) << "then all over again";
    EXPECT_NEAR(AttackStanceFor(10.0f, 20.0f, 4).x, first.x, 1e-4f);
}

TEST(BloodborneAutoplay, AnAttackPressesR1OncePerCycle) {
    EXPECT_EQ(AttackCycleAt(0.0), 0u);
    EXPECT_EQ(AttackCycleAt(AttackCycleSeconds - 0.01), 0u);
    EXPECT_EQ(AttackCycleAt(AttackCycleSeconds), 1u);
    EXPECT_EQ(AttackCycleAt(-1.0), 0u);
    EXPECT_FALSE(AttackHoldsR1(0.0)) << "standing there first";
    EXPECT_FALSE(AttackHoldsR1(0.29));
    EXPECT_TRUE(AttackHoldsR1(0.3));
    EXPECT_TRUE(AttackHoldsR1(0.49));
    EXPECT_FALSE(AttackHoldsR1(0.5)) << "released";
    EXPECT_FALSE(AttackHoldsR1(AttackCycleSeconds - 0.01));
    EXPECT_TRUE(AttackHoldsR1(AttackCycleSeconds + 0.4)) << "again in the next cycle";
}
