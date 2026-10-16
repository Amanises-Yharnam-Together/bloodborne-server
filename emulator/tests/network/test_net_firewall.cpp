// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtest/gtest.h>

#include "core/libraries/network/net_firewall.h"

using namespace Libraries::Net;

namespace {
constexpr u32 Loopback = 0x7F000001;
} // namespace

// Phase 9's proof test: the rig gives each game the server's addresses, and the game's sockets
// reach nothing else (a firewall per game, inside it: Windows does not filter loopback).
TEST(NetFirewall, ReachesOnlyWhatTheSettingNames) {
    const auto firewall = NetFirewall::Parse("127.0.0.1:32313, 127.0.0.1:32314,10.0.0.5:*");
    ASSERT_TRUE(firewall.has_value());
    EXPECT_TRUE(firewall->Allows(Loopback, 32313));
    EXPECT_TRUE(firewall->Allows(Loopback, 32314));
    EXPECT_FALSE(firewall->Allows(Loopback, 51601)) << "another game's port";
    EXPECT_FALSE(firewall->Allows(0x7F000002, 32313));
    EXPECT_TRUE(firewall->Allows(0x0A000005, 1));
    EXPECT_TRUE(firewall->Allows(0x0A000005, 65535));
    EXPECT_FALSE(firewall->Allows(0x0A000006, 1));
}

TEST(NetFirewall, NoSettingMeansNoFirewall) {
    EXPECT_FALSE(NetFirewall::Parse("").has_value());
    EXPECT_FALSE(NetFirewall::Parse("  ").has_value());
}

// A setting that does not read lets nothing through: a broken proof never passes.
TEST(NetFirewall, ASettingThatDoesNotReadBlocksEverything) {
    for (const char* setting : {"127.0.0.1", "127.0.0.1:x", "localhost:80", "1.2.3:80",
                                "127.0.0.1:70000", "127.0.0.1:80,,"}) {
        const auto firewall = NetFirewall::Parse(setting);
        ASSERT_TRUE(firewall.has_value()) << setting;
        EXPECT_FALSE(firewall->Allows(Loopback, 80)) << setting;
        EXPECT_FALSE(firewall->Allows(0x01020300, 80)) << setting;
    }
}
