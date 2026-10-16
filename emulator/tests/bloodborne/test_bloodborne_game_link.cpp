// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtest/gtest.h>

#include "core/bloodborne_game_link.h"

using namespace Core::Bloodborne;

namespace {

// A game packet: {u32 channel, u32 size, payload}.
std::vector<u8> Packet(u32 channel, u8 mark, size_t size = 1) {
    std::vector<u8> packet(8 + size, mark);
    for (int index = 0; index < 4; ++index) {
        packet[index] = static_cast<u8>(channel >> (8 * index));
        packet[4 + index] = static_cast<u8>(size >> (8 * index));
    }
    return packet;
}

std::vector<u8> Marks(const std::vector<std::vector<u8>>& packets) {
    std::vector<u8> marks;
    for (const auto& packet : packets) {
        marks.push_back(packet.back());
    }
    return marks;
}

// Everything `from` has to send now, received by `to`: the packets `to` hands its game.
std::vector<std::vector<u8>> Carry(GameLinkPeer& from, GameLinkPeer& to, s64 now,
                                   std::optional<size_t> lose = std::nullopt) {
    std::vector<std::vector<u8>> delivered;
    const auto frames = from.Flush(now);
    for (size_t index = 0; index < frames.size(); ++index) {
        if (lose && *lose == index) {
            continue;
        }
        const auto decoded = DecodeGameFrame(EncodeGameFrame(frames[index]));
        EXPECT_TRUE(decoded.has_value());
        if (decoded) {
            for (auto& packet : to.Receive(*decoded, now)) {
                delivered.push_back(std::move(packet));
            }
        }
    }
    return delivered;
}

} // namespace

TEST(GameLink, AFrameReadsBack) {
    GameFrame frame{.sender = "rig_host", .instance = 7, .peer_instance = 9, .ack = 12};
    frame.entries.push_back({PacketClass::Reliable, 11, Packet(0x31, 0xAB, 24)});
    frame.entries.push_back({PacketClass::Unreliable, 400, Packet(6, 0xCD, 80)});
    const auto bytes = EncodeGameFrame(frame);
    const auto back = DecodeGameFrame(bytes);
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(*back, frame);
    for (size_t size = 0; size < bytes.size(); ++size) {
        EXPECT_FALSE(DecodeGameFrame({bytes.data(), size}).has_value()) << size;
    }
}

TEST(GameLink, ReliablePacketsArriveInOrderThoughOneIsLost) {
    GameLinkPeer host{"rig_host", 1};
    GameLinkPeer guest{"rig_guest", 2};
    // One frame each, so one can be lost.
    host.Send(PacketClass::Reliable, Packet(0x10, 1, 2000), 0);
    host.Send(PacketClass::Reliable, Packet(0x11, 2, 2000), 0);
    host.Send(PacketClass::Reliable, Packet(0x42, 3, 2000), 0);
    EXPECT_EQ(Marks(Carry(host, guest, 0, 1)), (std::vector<u8>{1})) << "3 waits for 2";
    EXPECT_TRUE(Carry(guest, host, 10).empty()) << "the ack";
    EXPECT_TRUE(Carry(host, guest, 20).empty()) << "nothing due yet";
    EXPECT_EQ(Marks(Carry(host, guest, 200)), (std::vector<u8>{2, 3}));
    Carry(guest, host, 210);
    EXPECT_EQ(host.Unacked(), 0u);
    EXPECT_TRUE(host.Flush(1000).empty()) << "all acked: nothing more to send";
}

TEST(GameLink, AnAckThatIsLostMeansAResendTheReceiverIgnores) {
    GameLinkPeer host{"rig_host", 1};
    GameLinkPeer guest{"rig_guest", 2};
    host.Send(PacketClass::Reliable, Packet(0x31, 5), 0);
    EXPECT_EQ(Marks(Carry(host, guest, 0)), (std::vector<u8>{5}));
    guest.Flush(10); // the ack, lost
    EXPECT_TRUE(Carry(host, guest, 200).empty()) << "a duplicate";
    EXPECT_EQ(host.Resends(), 1u);
    Carry(guest, host, 210);
    EXPECT_EQ(host.Unacked(), 0u);
}

TEST(GameLink, UnreliablePacketsAreNeverResentAndTheNewestWinsPerChannel) {
    GameLinkPeer host{"rig_host", 1};
    GameLinkPeer guest{"rig_guest", 2};
    host.Send(PacketClass::Unreliable, Packet(6, 1, 900), 0);
    host.Send(PacketClass::Unreliable, Packet(6, 2, 900), 0);
    const auto frames = host.Flush(0);
    ASSERT_EQ(frames.size(), 2u);
    // The second arrives first: the first is older on its channel and goes.
    EXPECT_EQ(Marks(guest.Receive(frames[1], 0)), (std::vector<u8>{2}));
    EXPECT_TRUE(guest.Receive(frames[0], 0).empty());
    EXPECT_EQ(guest.DroppedOld(), 1u);
    // Another channel keeps its own order.
    host.Send(PacketClass::Unreliable, Packet(4, 3), 10);
    EXPECT_EQ(Marks(Carry(host, guest, 10)), (std::vector<u8>{3}));
    EXPECT_TRUE(host.Flush(500).empty()) << "never resent";
}

TEST(GameLink, SmallPacketsShareAFrameAndALargeOneGoesAlone) {
    GameLinkPeer host{"rig_host", 1};
    for (u8 mark = 0; mark < 10; ++mark) {
        host.Send(PacketClass::Unreliable, Packet(6, mark, 80), 0);
    }
    host.Send(PacketClass::Reliable, Packet(0x11, 99, 0x6000), 0);
    const auto frames = host.Flush(0);
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[0].entries.size(), 10u);
    EXPECT_EQ(frames[1].entries.size(), 1u);
}

TEST(GameLink, APeerThatRestartedStartsBothStreamsAgain) {
    GameLinkPeer host{"rig_host", 1};
    GameLinkPeer guest{"rig_guest", 2};
    host.Send(PacketClass::Reliable, Packet(7, 1), 0);
    guest.Send(PacketClass::Reliable, Packet(0x42, 2), 0);
    Carry(host, guest, 0);
    Carry(guest, host, 0);
    host.Send(PacketClass::Reliable, Packet(0x31, 3), 50); // never reaches the old guest
    host.Flush(50);

    // The guest's game crashed and came back: a new instance, its streams from the start.
    GameLinkPeer restarted{"rig_guest", 3};
    restarted.Send(PacketClass::Reliable, Packet(0x42, 4), 1000);
    EXPECT_EQ(Marks(Carry(restarted, host, 1000)), (std::vector<u8>{4}));
    EXPECT_EQ(host.Unacked(), 0u) << "what the old guest missed is dropped";
    host.Send(PacketClass::Reliable, Packet(7, 5), 1010);
    EXPECT_EQ(Marks(Carry(host, restarted, 1010)), (std::vector<u8>{5}));
}

// Rig 2026-10-13 (crash-rejoin-hp): the host kept sending its old streams to the guest's new
// game until it heard from it; the new game took those (98 player states dropped as late once the
// host's streams started again, reliable ones held under numbers the new streams reuse) and its
// join timed out.
TEST(GameLink, ARestartedMemberTakesNothingOfTheOldStreams) {
    GameLinkPeer host{"rig_host", 1};
    GameLinkPeer guest{"rig_guest", 2};
    for (u8 mark = 0; mark < 5; ++mark) {
        host.Send(PacketClass::Unreliable, Packet(6, mark), 0);
        host.Send(PacketClass::Reliable, Packet(0x31, mark), 0);
    }
    Carry(host, guest, 0);
    Carry(guest, host, 10);

    GameLinkPeer restarted{"rig_guest", 3};
    host.Send(PacketClass::Unreliable, Packet(6, 50), 1000);
    host.Send(PacketClass::Reliable, Packet(0x31, 51), 1000);
    EXPECT_TRUE(Carry(host, restarted, 1000).empty()) << "meant for the old game";
    // The new game answers at once, so the host learns of it without waiting for its packets.
    EXPECT_TRUE(Carry(restarted, host, 1010).empty());
    host.Send(PacketClass::Unreliable, Packet(6, 60), 1020);
    host.Send(PacketClass::Reliable, Packet(0x31, 61), 1020);
    EXPECT_EQ(Marks(Carry(host, restarted, 1020)), (std::vector<u8>{60, 61}));
    EXPECT_EQ(restarted.DroppedOld(), 0u);
}

// An idle member still hears from this game once a second: its silence means a dead link, and a
// restarted game learns of the other's instance without waiting for a packet.
TEST(GameLink, AnIdleLinkSaysItIsThereOnceASecond) {
    GameLinkPeer host{"rig_host", 1};
    host.Send(PacketClass::Unreliable, Packet(6, 1), 0);
    EXPECT_EQ(host.Flush(0).size(), 1u);
    EXPECT_TRUE(host.Flush(999).empty());
    const auto frames = host.Flush(1000);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_TRUE(frames[0].entries.empty());
    EXPECT_TRUE(host.Flush(1500).empty());
}

TEST(GameLink, SaysHowLongThePeerHasBeenSilent) {
    GameLinkPeer host{"rig_host", 1};
    GameLinkPeer guest{"rig_guest", 2};
    EXPECT_FALSE(host.SilentFor(0).has_value()) << "never heard";
    guest.Send(PacketClass::Unreliable, Packet(6, 1), 100);
    Carry(guest, host, 100);
    EXPECT_EQ(host.SilentFor(1600), 1500);
}
