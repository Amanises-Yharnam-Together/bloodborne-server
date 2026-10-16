// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <vector>

#include <gtest/gtest.h>

#include "core/bloodborne_party.h"

using namespace Core::Bloodborne;

// The channel table is shadNet's since phase 9 (coop/packet_routes.h, tested in
// server/tests/test_packet_routes.cpp); the game reads the same one.
TEST(ChannelTable, TheGameReadsShadNetsTable) {
    EXPECT_EQ(NativeChannelPolicy(0x06), ChannelPolicy::SameMap);
    EXPECT_EQ(NativeChannelPolicy(0x2C), ChannelPolicy::Ours);
    EXPECT_EQ(NativeChannelPolicy(0x27), ChannelPolicy::Drop);
    EXPECT_EQ(NativeChannelPolicy(0x10), ChannelPolicy::Pass);
    EXPECT_EQ(ChannelPolicyName(ChannelPolicy::SameMap), "same map");
}

// A reliable packet shadNet drops keeps its place in the member's stream as a channel past the
// game's last (0x43), which the game link hands to nobody.
TEST(ChannelTable, ADroppedPacketReadsAsAChannelTheGameDoesNotHave) {
    EXPECT_GT(DroppedNativeChannel, 0x43u);
    EXPECT_EQ(DroppedNativeChannel, NativeChannelCount);
    EXPECT_EQ(NativePacketChannel(DroppedNativePacket()), DroppedNativeChannel);
    EXPECT_EQ(NativePacketChannel(std::vector<u8>{6, 0, 0, 0, 0, 0, 0, 0}), 6u);
    EXPECT_EQ(NativePacketChannel(std::vector<u8>{6, 0, 0}), NativeChannelCount);
}
