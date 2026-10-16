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

using namespace Core::Bloodborne;

// Kaniel 2026-10-15: the host and a guest went to the Hunter's Dream and the guest left behind
// still saw them in Central Yharnam, frozen. Who shows comes from one place, the server's map
// board: another player's character shows while a member on this map stands near it.
TEST(MemberPresence, AnotherPlayerShowsOnlyWhileAMemberOnThisMapStandsThere) {
    constexpr u32 Yharnam = 0x18010000, Dream = 0x15000000;
    const auto member = [](const char* name, u32 map, float x) {
        return MemberVitals{.member = name, .map = map, .vital = Vital::Up, .at = {x, 0.0f, 0.0f}};
    };
    const std::vector<std::optional<LampPosition>> characters{LampPosition{100.0f, 0.0f, 0.0f},
                                                              LampPosition{200.0f, 0.0f, 0.0f}};
    // Both on this map, each near its character.
    std::vector<MemberVitals> board{member("me", Yharnam, 0.0f), member("a", Yharnam, 102.0f),
                                    member("b", Yharnam, 198.0f)};
    EXPECT_EQ(ShownOtherPlayers(characters, board, "me", Yharnam), (std::vector<bool>{true, true}));
    // Both in the Dream: neither shows.
    board = {member("me", Yharnam, 0.0f), member("a", Dream, 102.0f), member("b", Dream, 198.0f)};
    EXPECT_EQ(ShownOtherPlayers(characters, board, "me", Yharnam),
              (std::vector<bool>{false, false}));
    // One back: its character shows, the other's not.
    board = {member("me", Yharnam, 0.0f), member("a", Yharnam, 101.0f), member("b", Dream, 0.0f)};
    EXPECT_EQ(ShownOtherPlayers(characters, board, "me", Yharnam),
              (std::vector<bool>{true, false}));
    // A member loading (map 0) is on no map; this player's own entry never counts.
    board = {member("me", Yharnam, 100.0f), member("a", 0, 100.0f)};
    EXPECT_EQ(ShownOtherPlayers(characters, board, "me", Yharnam),
              (std::vector<bool>{false, false}));
    // A character without a position shows (nothing to tell).
    const std::vector<std::optional<LampPosition>> unknown{std::nullopt};
    EXPECT_EQ(ShownOtherPlayers(unknown, board, "me", Yharnam), (std::vector<bool>{true}));
}
