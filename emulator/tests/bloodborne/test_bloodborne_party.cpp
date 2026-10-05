// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <string>

#include <gtest/gtest.h>

#include "core/bloodborne_party.h"

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
