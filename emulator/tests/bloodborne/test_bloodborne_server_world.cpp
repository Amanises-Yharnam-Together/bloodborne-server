// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The party's world on the server (docs/SERVER_COOP_PLAN.md phase 3), the parts the game and
// shadNet share (server/coop): the reliable stream our messages take, coop.proto's messages built
// from the world log and back, and the host log's state as shadNet stores it.

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "coop/coop_messages.h"
#include "coop/coop_stream.h"
#include "core/bloodborne_party.h"

using namespace Core::Bloodborne;

namespace {

constexpr u32 Yharnam = 0x18010000;
constexpr u32 Ward = 0x18000000;

std::vector<u8> Bytes(size_t size, u8 seed) {
    std::vector<u8> bytes(size);
    for (size_t index = 0; index < size; ++index) {
        bytes[index] = static_cast<u8>(seed + index * 7);
    }
    return bytes;
}

// The frames from `from` to `to`, all delivered: the messages `to` completed.
std::vector<std::vector<u8>> Deliver(CoopStream& from, CoopStream& to, s64 now_ms) {
    std::vector<std::vector<u8>> messages;
    for (const std::vector<u8>& body : from.Flush(now_ms)) {
        for (std::vector<u8>& message : to.Receive(body, now_ms)) {
            messages.push_back(std::move(message));
        }
    }
    return messages;
}

// A host log with something of every kind in it.
WorldLogHost BusyHost() {
    WorldLogHost host;
    const u64 epoch = WorldLogEpoch(0x1234, 3);
    host.Start(epoch, true);
    host.Propose({"a", epoch, 5, 1, {{Yharnam, {0x10014066, 0x10014067}}, {Ward, {0x10010075}}}});
    host.Propose({"b", epoch, 6, 1, {{Yharnam, {0x10014066}}}});
    host.Show(Yharnam, std::vector<CharacterShown>{{0x10014000, false, true, false, false},
                                                   {0x10014005, true, false, false, true}});
    std::vector<u8> bits((MapFlagCount + 7) / 8);
    bits[3] = 0x81;
    host.MapState(Yharnam, bits);
    host.ReturnPoint(2102962, Yharnam);
    host.ProposeHp({"a", epoch, 5, 1, Yharnam, {0x1111000000000003, {{0x10014068, 49, 450}}}});
    host.ProposeFlags({"b", epoch, 6, {{1, 1001, true}, {2, 1002, false}}}, 100);
    return host;
}

} // namespace

TEST(ServerWorld, ALongMessageComesWholeAndInOrder) {
    CoopStream game{"game", 11};
    CoopStream server{"shadnet", 22};
    const std::vector<u8> big = Bytes(5 * CoopStream::ChunkBytes + 17, 3);
    const std::vector<u8> small = Bytes(5, 9);
    game.Send(big, 0);
    game.Send(small, 0);
    game.Send({}, 0); // an empty message is one too
    const auto messages = Deliver(game, server, 0);
    ASSERT_EQ(messages.size(), 3u);
    EXPECT_EQ(messages[0], big);
    EXPECT_EQ(messages[1], small);
    EXPECT_TRUE(messages[2].empty());
}

TEST(ServerWorld, ALostFrameIsSentAgainAndNothingComesTwice) {
    CoopStream game{"game", 11};
    CoopStream server{"shadnet", 22};
    game.Send(Bytes(10, 1), 0);
    game.Flush(0); // lost
    game.Send(Bytes(10, 2), 10);
    // The second one waits for the first: in order.
    EXPECT_TRUE(Deliver(game, server, 10).empty());
    const auto later = Deliver(game, server, 500);
    ASSERT_EQ(later.size(), 2u);
    EXPECT_EQ(later[0], Bytes(10, 1));
    EXPECT_EQ(later[1], Bytes(10, 2));
    Deliver(server, game, 510); // the acks
    EXPECT_EQ(game.Unacked(), 0u);
    EXPECT_TRUE(Deliver(game, server, 2000).empty()); // keepalives only
}

TEST(ServerWorld, ARestartedServerIsNoticedOnceAndHalfAMessageIsDropped) {
    CoopStream game{"game", 11};
    auto server = std::make_unique<CoopStream>("shadnet", 22);
    game.Send(Bytes(10, 1), 0);
    Deliver(game, *server, 0);
    Deliver(*server, game, 0);
    EXPECT_FALSE(game.TakeRestarted());
    // shadNet restarts: its new stream answers the game's frame, the game starts over.
    server = std::make_unique<CoopStream>("shadnet", 33);
    game.Send(Bytes(3 * CoopStream::ChunkBytes, 4), 100);
    const auto taken = Deliver(game, *server, 100);
    EXPECT_TRUE(taken.empty()); // frames meant for the old server
    Deliver(*server, game, 100);
    EXPECT_TRUE(game.TakeRestarted());
    EXPECT_FALSE(game.TakeRestarted());
    EXPECT_EQ(game.Unacked(), 0u); // what was in flight is gone: the game says hello again
    game.Send(Bytes(10, 5), 200);
    const auto after = Deliver(game, *server, 200);
    ASSERT_EQ(after.size(), 1u);
    EXPECT_EQ(after[0], Bytes(10, 5));
}

TEST(ServerWorld, ASnapshotBringsAMemberTheWholeWorld) {
    const WorldLogHost host = BusyHost();
    const std::vector<u8> wire = EncodeEnvelope([&] {
        bbcoop::Envelope envelope;
        *envelope.mutable_snapshot() = SnapshotOf(host);
        return envelope;
    }());
    const auto envelope = DecodeEnvelope(wire);
    ASSERT_TRUE(envelope && envelope->has_snapshot());
    const SnapshotParts parts = PartsOf(envelope->snapshot());
    WorldLogMember member;
    member.Take(parts.kills, 0);
    member.Take(parts.shown, 0);
    member.Take(parts.map_state, 0);
    member.Take(parts.hp, 0);
    const WorldLogMember::Step step = member.Take(parts.flags, 0);
    EXPECT_TRUE(step.snapshot);
    EXPECT_TRUE(step.after_wipe);
    EXPECT_EQ(member.Epoch(), host.Epoch());
    EXPECT_EQ(member.Applied(), host.Last());
    EXPECT_EQ(member.Kills(), host.Kills());
    EXPECT_EQ(member.Shown(), host.Shown());
    EXPECT_EQ(member.MapState(), host.MapStateNow());
    EXPECT_EQ(member.HostReturnPoint(), host.ReturnPointNow());
    EXPECT_EQ(member.Hp(), host.Hp());
    EXPECT_TRUE(member.Fresh(0));
}

TEST(ServerWorld, EntriesReadBackAsTheHostWroteThem) {
    WorldLogHost host;
    const u64 epoch = WorldLogEpoch(0x77, 1);
    std::vector<WorldLogEntry> entries = host.Start(epoch, false);
    const auto kills = host.Propose({"a", 0, 1, 1, {{Yharnam, {5, 6}}}});
    entries.insert(entries.end(), kills.entries.begin(), kills.entries.end());
    const bbcoop::Entries message = ToMessage(host.Epoch(), host.Last(), entries);
    bbcoop::Entries read;
    ASSERT_TRUE(read.ParseFromString(message.SerializeAsString()));
    const WorldLogBatch batch = BatchOf(read);
    EXPECT_EQ(batch.epoch, epoch);
    EXPECT_EQ(batch.last, host.Last());
    EXPECT_EQ(batch.entries, entries);
    WorldLogMember member;
    EXPECT_EQ(member.Take(batch, 0).applied, entries);
    EXPECT_EQ(member.Kills(), host.Kills());
}

TEST(ServerWorld, AProposalKeepsItsSetButTakesTheServersName) {
    const WorldLogProposal proposal{"spoofed", 9, 3, 4, {{Ward, {1, 2}}, {Yharnam, {3}}}};
    const WorldLogProposal read = ProposalOf(ToMessage(proposal), "real");
    EXPECT_EQ(read.member, "real");
    EXPECT_EQ(read.epoch, 9u);
    EXPECT_EQ(read.run, 3u);
    EXPECT_EQ(read.number, 4u);
    EXPECT_EQ(read.kills, proposal.kills);
}

TEST(ServerWorld, TheStoredStateIsTheLogAgain) {
    WorldLogHost host = BusyHost();
    const WorldLogHost::State saved = host.Save();
    bbcoop::StoredHost stored;
    ASSERT_TRUE(stored.ParseFromString(ToMessage(saved).SerializeAsString()));
    EXPECT_EQ(StateOf(stored), saved);

    // A log restored from it goes on as the old one would: the same entries for the same input.
    WorldLogHost again;
    again.Restore(StateOf(stored));
    const WorldLogProposal next{"b", host.Epoch(), 6, 2, {}}; // b's kill goes, a's stands
    const auto before = host.Propose(next);
    const auto after = again.Propose(next);
    EXPECT_EQ(before.entries, after.entries);
    EXPECT_TRUE(after.entries.empty()); // a still has 0x10014066
    EXPECT_EQ(again.Drop("a"), host.Drop("a"));
    EXPECT_EQ(again.Save(), host.Save());
    EXPECT_TRUE(again.Kills().empty());
}

TEST(ServerWorld, ADroppedMemberTakesItsKillsAndHpOut) {
    WorldLogHost host = BusyHost();
    const std::vector<WorldLogEntry> entries = host.Drop("a");
    EXPECT_EQ(WorldLogHandles(host.Kills()), (std::unordered_set<u32>{0x10014066}));
    EXPECT_TRUE(host.Hp().empty());
    EXPECT_TRUE(std::ranges::any_of(entries, [](const WorldLogEntry& entry) {
        return entry.kind == WorldLogKind::HpSourceGone;
    }));
    EXPECT_TRUE(host.Drop("nobody").empty());
}
