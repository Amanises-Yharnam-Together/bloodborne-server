// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <optional>

#include <gtest/gtest.h>

#include "core/bloodborne_party.h"

using namespace Core::Bloodborne;

namespace {

constexpr u32 CentralYharnam = 0x18000000;
constexpr u32 CathedralWard = 0x18010000;

bool KeptIn(u32 channel, u32 own_map, std::optional<u32> peer_map) {
    return KeepsNativePacket(channel, PacketWay::In, own_map, peer_map);
}

bool KeptOut(u32 channel, u32 own_map, std::optional<u32> peer_map) {
    return KeepsNativePacket(channel, PacketWay::Out, own_map, peer_map);
}

} // namespace

TEST(ChannelTable, OffUnlessAskedFor) {
    EXPECT_FALSE(ChannelTableEnabled(nullptr));
    EXPECT_FALSE(ChannelTableEnabled(""));
    EXPECT_FALSE(ChannelTableEnabled("0"));
    EXPECT_FALSE(ChannelTableEnabled("off"));
    EXPECT_TRUE(ChannelTableEnabled("1"));
    EXPECT_TRUE(ChannelTableEnabled("on"));
    EXPECT_FALSE(ChannelTableEnabled("yes please"));
}

// docs/SERVER_COOP_PLAN.md "Traffic and who owns it", one row per area.
TEST(ChannelTable, EveryChannelHasThePlansPolicy) {
    // The join handshake and its snapshots, character data, announcements: the game's own.
    for (const u32 channel : {0x07u, 0x19u, 0x42u, 0x10u, 0x11u, 0x3Fu, 0x08u, 0x0Au, 0x0Bu, 0x0Cu,
                              0x0Du, 0x13u, 0x3Bu, 0x3Cu, 0x43u, 0x32u, 0x33u}) {
        EXPECT_EQ(NativeChannelPolicy(channel), ChannelPolicy::Pass) << std::hex << channel;
    }
    // Player state, hits, throws, forced animations, bullets, enemies, events, objects: only
    // between members on one map.
    for (const u32 channel : {0x04u, 0x05u, 0x06u, 0x14u, 0x12u, 0x37u, 0x30u, 0x24u, 0x25u,
                              0x3Eu, 0x40u, 0x15u, 0x16u, 0x17u, 0x29u, 0x36u, 0x3Du, 0x41u,
                              0x34u, 0x0Eu, 0x0Fu, 0x31u, 0x2Au, 0x2Bu, 0x2Du, 0x2Eu}) {
        EXPECT_EQ(NativeChannelPolicy(channel), ChannelPolicy::SameMap) << std::hex << channel;
    }
    // Our code keeps each player's map items (0x2C) and the doors' state (0x2F, the host's
    // correction); the boss "thanks" kick (0x27) is nobody's.
    EXPECT_EQ(NativeChannelPolicy(0x2C), ChannelPolicy::Ours);
    EXPECT_EQ(NativeChannelPolicy(0x2F), ChannelPolicy::Ours);
    EXPECT_EQ(NativeChannelPolicy(0x27), ChannelPolicy::Drop);
    // Not understood yet, or not a channel: untouched.
    for (const u32 channel : {0x00u, 0x01u, 0x02u, 0x03u, 0x22u, 0x38u, 0x39u, 0x44u, 0xFFFFu}) {
        EXPECT_EQ(NativeChannelPolicy(channel), ChannelPolicy::Pass) << std::hex << channel;
    }
}

TEST(ChannelTable, PoliciesHaveNames) {
    EXPECT_EQ(ChannelPolicyName(ChannelPolicy::Pass), "pass");
    EXPECT_EQ(ChannelPolicyName(ChannelPolicy::SameMap), "same map");
    EXPECT_EQ(ChannelPolicyName(ChannelPolicy::Ours), "ours");
    EXPECT_EQ(ChannelPolicyName(ChannelPolicy::Drop), "drop");
}

TEST(ChannelTable, PassedChannelsAlwaysGoThrough) {
    EXPECT_TRUE(KeptIn(0x10, CentralYharnam, CathedralWard));
    EXPECT_TRUE(KeptOut(0x10, CentralYharnam, CathedralWard));
    EXPECT_TRUE(KeptIn(0x0A, 0, std::nullopt));
    EXPECT_TRUE(KeptIn(0x44, CentralYharnam, CathedralWard));
}

TEST(ChannelTable, OursAndDroppedChannelsNeverReachTheGame) {
    for (const u32 channel : {0x2Cu, 0x2Fu, 0x27u}) {
        EXPECT_FALSE(KeptIn(channel, CentralYharnam, CentralYharnam)) << std::hex << channel;
        EXPECT_FALSE(KeptOut(channel, CentralYharnam, CentralYharnam)) << std::hex << channel;
        EXPECT_FALSE(KeptIn(channel, 0, std::nullopt)) << std::hex << channel;
    }
}

TEST(ChannelTable, SameMapChannelsStopBetweenMembersApart) {
    EXPECT_TRUE(KeptIn(0x06, CentralYharnam, CentralYharnam));
    EXPECT_FALSE(KeptIn(0x06, CentralYharnam, CathedralWard));
    EXPECT_FALSE(KeptIn(0x15, CathedralWard, CentralYharnam));
    EXPECT_FALSE(KeptIn(0x31, CathedralWard, CentralYharnam));
}

// The receiver decides: a sent packet keeps the transport busy (the game's idle timer counts it),
// and the board's map of the other member is the same at both ends.
TEST(ChannelTable, SameMapChannelsAreAlwaysSent) {
    EXPECT_TRUE(KeptOut(0x06, CentralYharnam, CathedralWard));
    EXPECT_TRUE(KeptOut(0x15, CentralYharnam, std::nullopt));
}

// Whoever's map is not known (not on the map board yet, a load) gets the game's own behavior.
TEST(ChannelTable, AnUnknownMapLetsSameMapChannelsThrough) {
    EXPECT_TRUE(KeptIn(0x06, CentralYharnam, std::nullopt));
    EXPECT_TRUE(KeptIn(0x06, CentralYharnam, 0u));
    EXPECT_TRUE(KeptIn(0x06, 0, CathedralWard));
}

// The receive hooks (see InstallChannelTable) make the game take its own drop branch: a channel
// past 0x43 frees the packet instead of queueing it.
TEST(ChannelTable, ADroppedPacketReadsAsAChannelTheGameDoesNotHave) {
    EXPECT_GT(DroppedNativeChannel, 0x43u);
    EXPECT_EQ(DroppedNativeChannel, NativeChannelCount);
}
