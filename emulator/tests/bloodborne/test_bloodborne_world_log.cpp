// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The world log (docs/WORLD_SYNC.md, Stage C, step 1: the kills).

#include <algorithm>
#include <deque>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/bloodborne_party.h"

using namespace Core::Bloodborne;

namespace {

constexpr u32 Yharnam = 0x18010000;
constexpr u32 Ward = 0x18000000;
constexpr u32 Dream = 0x15000000;

WorldLogProposal Proposal(std::string member, u64 epoch, u32 run, u32 number, WorldLogKills kills) {
    return {std::move(member), epoch, run, number, std::move(kills)};
}

std::vector<WorldLogEntry> Entries(u64 epoch, std::vector<WorldLogEntry> entries) {
    for (WorldLogEntry& entry : entries) {
        entry.epoch = epoch;
    }
    return entries;
}

WorldLogEntry Kill(u64 seq, u32 map, u32 handle) {
    return {seq, 0, map, WorldLogKind::Kill, handle};
}
WorldLogEntry Unkill(u64 seq, u32 map, u32 handle) {
    return {seq, 0, map, WorldLogKind::Unkill, handle};
}
WorldLogEntry Reset(u64 seq, u32 map = 0) {
    return {seq, 0, map, WorldLogKind::Reset, 0};
}

using Peers = std::vector<WorldLogPeer>;

WorldLogBatch Batch(u64 epoch, u64 last, std::vector<WorldLogEntry> entries) {
    return {"host", epoch, last, Entries(epoch, std::move(entries))};
}

} // namespace

TEST(WorldLogFrames, EveryFrameReadsBackAndACutOneDoesNot) {
    const WorldLogProposal proposal =
        Proposal("6b616e69656c", WorldLogEpoch(7, 2), 0x1234, 9,
                 {{Yharnam, {0x10014066, 0x1001400b}}, {Ward, {0x10010075}}});
    const WorldLogBatch batch{
        "686f7374", WorldLogEpoch(7, 2), 4,
        Entries(WorldLogEpoch(7, 2), {Kill(3, Yharnam, 0x10014066), Unkill(4, Ward, 0x10010075)})};
    const WorldLogSnapshotRequest request{"6b61", WorldLogEpoch(7, 1), 12};
    const WorldLogSnapshot snapshot{"686f7374", WorldLogEpoch(7, 2),      4, 1,
                                    2,          {{Yharnam, {0x10014066}}}};

    const auto check = [](const auto& value, const std::vector<u8>& payload, auto decode) {
        EXPECT_EQ(decode(payload), value);
        for (size_t size = 0; size < payload.size(); ++size) {
            EXPECT_FALSE(decode(std::span{payload}.first(size))) << "cut at " << size;
        }
        std::vector<u8> longer = payload;
        longer.push_back(0);
        EXPECT_FALSE(decode(longer)) << "a byte too many";
    };
    check(proposal, EncodeWorldLogProposal(proposal),
          [](std::span<const u8> bytes) { return DecodeWorldLogProposal(bytes); });
    check(batch, EncodeWorldLogBatch(batch),
          [](std::span<const u8> bytes) { return DecodeWorldLogBatch(bytes); });
    check(request, EncodeWorldLogSnapshotRequest(request),
          [](std::span<const u8> bytes) { return DecodeWorldLogSnapshotRequest(bytes); });
    check(snapshot, EncodeWorldLogSnapshot(snapshot),
          [](std::span<const u8> bytes) { return DecodeWorldLogSnapshot(bytes); });

    // A batch's entries are consecutive (only the first seq goes out) and end by the host's last.
    WorldLogBatch past_last = batch;
    past_last.last = 3;
    EXPECT_FALSE(DecodeWorldLogBatch(EncodeWorldLogBatch(past_last)));
    // A snapshot's part is one of its parts.
    WorldLogSnapshot part_past = snapshot;
    part_past.part = 2;
    EXPECT_FALSE(DecodeWorldLogSnapshot(EncodeWorldLogSnapshot(part_past)));
}

// Older builds (protocol 1) read 0x30-0x3F as co-op frames but take only the types they know: the
// rest reaches the damage decoder, which takes only its own type.
TEST(WorldLogFrames, TypesFollowTheHelloAndOlderBuildsDropThem) {
    for (const u8 type : {WorldLogProposeType, WorldLogEntriesType, WorldLogSnapshotRequestType,
                          WorldLogSnapshotType}) {
        EXPECT_TRUE(IsCoopFrameType(type));
        EXPECT_TRUE(IsWorldLogFrameType(type));
        EXPECT_GT(type, PeerHelloType);
        const std::vector<u8> frame =
            EncodeCoopFrame(type, EncodeWorldLogBatch(Batch(WorldLogEpoch(1, 1), 0, {})));
        EXPECT_EQ(DecodeDirectDamage(frame), std::nullopt) << int{type};
        EXPECT_EQ(DecodeCoopFrame(frame)->first, type);
    }
    for (const u8 type : {DirectDamageType, DirectVitalsType, DirectKillsType, PeerHelloType}) {
        EXPECT_FALSE(IsWorldLogFrameType(type));
    }
    EXPECT_EQ(CoopProtocolVersion, 2) << "the world log's frames are protocol 2";
}

TEST(WorldLogFrames, AFullSetFitsTheLink) {
    WorldLogProposal big = Proposal("6b616e69656c6b616e69656c6b616e69", 1, 1, 1, {});
    for (u32 handle = 0; handle < 400; ++handle) {
        big.kills[Yharnam + (handle % 7)].insert(0x10010000 + handle);
    }
    const auto proposal = EncodeWorldLogProposal(big);
    EXPECT_LE(proposal.size() + 5, 1400u);
    EXPECT_EQ(WorldLogKillCount(DecodeWorldLogProposal(proposal)->kills), WorldLogHandlesPerFrame)
        << "cut as the direct kills frame was";

    std::vector<WorldLogEntry> many;
    for (u64 seq = 1; seq <= 250; ++seq) {
        many.push_back({seq, 5, Yharnam, WorldLogKind::Kill, static_cast<u32>(seq)});
    }
    const auto batches = BatchWorldLog("686f7374", 5, 250, many);
    ASSERT_EQ(batches.size(), 3u);
    u64 next = 1;
    for (const WorldLogBatch& batch : batches) {
        EXPECT_LE(batch.entries.size(), WorldLogEntriesPerFrame);
        EXPECT_LE(EncodeWorldLogBatch(batch).size() + 5, 1400u);
        EXPECT_EQ(batch.last, 250u);
        EXPECT_EQ(batch.entries.front().seq, next);
        next = batch.entries.back().seq + 1;
    }
    EXPECT_EQ(next, 251u);
    const auto heartbeat = BatchWorldLog("686f7374", 5, 250, {});
    ASSERT_EQ(heartbeat.size(), 1u);
    EXPECT_TRUE(heartbeat[0].entries.empty());
}

TEST(WorldLogEpochs, AnotherRunIsFollowedAndAnOlderLogIsNot) {
    EXPECT_TRUE(WorldLogEpochFollows(WorldLogEpoch(7, 3), WorldLogEpoch(7, 2)));
    EXPECT_FALSE(WorldLogEpochFollows(WorldLogEpoch(7, 2), WorldLogEpoch(7, 3)));
    EXPECT_FALSE(WorldLogEpochFollows(WorldLogEpoch(7, 3), WorldLogEpoch(7, 3)));
    EXPECT_TRUE(WorldLogEpochFollows(WorldLogEpoch(9, 1), WorldLogEpoch(7, 3)))
        << "the host's game restarted";
    EXPECT_TRUE(WorldLogEpochFollows(WorldLogEpoch(7, 1), 0));
    EXPECT_FALSE(WorldLogEpochFollows(0, WorldLogEpoch(7, 1)));
}

TEST(BloodbornePeerHello, AHelloAdvertisesTheWorldLog) {
    const PeerHello hello{.revision = "abc", .member = "6b61", .features = WorldLogFeature};
    const auto payload = EncodePeerHello(hello);
    EXPECT_EQ(DecodePeerHello(payload), hello);
    // A protocol 1 hello ends after the member: it has no features.
    const auto old = DecodePeerHello(std::span{payload}.first(payload.size() - 1));
    ASSERT_TRUE(old);
    EXPECT_EQ(old->features, 0);
}

TEST(WorldLogHost, KillsAreOrderedOncePerEnemy) {
    WorldLogHost host;
    const u64 epoch = WorldLogEpoch(7, 1);
    EXPECT_FALSE(host.Started());
    EXPECT_EQ(host.Start(epoch, false), Entries(epoch, {Reset(1)}));
    EXPECT_EQ(host.Last(), 1u);

    auto taken = host.Propose(Proposal("guest", 0, 5, 1, {{Yharnam, {0x10, 0x11}}}));
    EXPECT_FALSE(taken.stale);
    EXPECT_EQ(taken.entries, Entries(epoch, {Kill(2, Yharnam, 0x10), Kill(3, Yharnam, 0x11)}));
    // The same kill by a second member: still one entry.
    taken = host.Propose(Proposal("host", epoch, 6, 1, {{Yharnam, {0x11, 0x12}}}));
    EXPECT_EQ(taken.entries, Entries(epoch, {Kill(4, Yharnam, 0x12)}));
    // The guest's visit ends: 0x10 goes, 0x11 stands for the host's kill.
    taken = host.Propose(Proposal("guest", epoch, 5, 2, {}));
    EXPECT_EQ(taken.entries, Entries(epoch, {Unkill(5, Yharnam, 0x10)}));
    taken = host.Propose(Proposal("host", epoch, 6, 2, {{Yharnam, {0x12}}}));
    EXPECT_EQ(taken.entries, Entries(epoch, {Unkill(6, Yharnam, 0x11)}));
    // The same set again: nothing.
    EXPECT_TRUE(host.Propose(Proposal("host", epoch, 6, 3, {{Yharnam, {0x12}}})).entries.empty());
    EXPECT_EQ(host.Kills(), (WorldLogKills{{Yharnam, {0x12}}}));
    EXPECT_EQ(host.Last(), 6u);
}

TEST(WorldLogHost, LateAndOtherLogsProposalsAreDropped) {
    WorldLogHost host;
    const u64 epoch = WorldLogEpoch(7, 1);
    host.Start(epoch, false);
    host.Propose(Proposal("guest", epoch, 5, 4, {{Yharnam, {0x10}}}));
    // Number 3 left before 4 and came after it.
    const auto late = host.Propose(Proposal("guest", epoch, 5, 3, {}));
    EXPECT_TRUE(late.stale);
    EXPECT_TRUE(late.entries.empty());
    // A proposal made for another log (before the host's wipe) is not this log's.
    const auto other = host.Propose(Proposal("guest", WorldLogEpoch(7, 9), 5, 5, {}));
    EXPECT_TRUE(other.stale);
    EXPECT_EQ(host.Kills(), (WorldLogKills{{Yharnam, {0x10}}}));
}

// PR #40: a game restarted under the same member name never knew its old kills to take them
// back. Its new run's first set replaces them.
TEST(WorldLogHost, ARestartedMembersOldKillsGo) {
    WorldLogHost host;
    const u64 epoch = WorldLogEpoch(7, 1);
    host.Start(epoch, false);
    host.Propose(Proposal("guest", epoch, 5, 40, {{Yharnam, {0x10, 0x11}}}));
    const auto restarted = host.Propose(Proposal("guest", 0, 8, 1, {{Ward, {0x20}}}));
    EXPECT_FALSE(restarted.stale) << "a new run counts from 1 again";
    EXPECT_EQ(restarted.entries, Entries(epoch, {Unkill(4, Yharnam, 0x10), Unkill(5, Yharnam, 0x11),
                                                 Kill(6, Ward, 0x20)}));
}

// A party wipe respawns the map for everyone (rig smoke 2026-10-12, run 191219: the kept sets
// stood the host's own kill of 0x1001400b again at seq 2): the new log has no kills at all, nor
// does a new session's.
TEST(WorldLogHost, ANewEpochStartsWithAResetAndNoKills) {
    WorldLogHost host;
    const u64 first = WorldLogEpoch(7, 1);
    host.Start(first, false);
    host.Propose(Proposal("guest", first, 5, 1, {{Dream, {0x30}}}));
    host.Propose(Proposal("host", first, 6, 1, {{Yharnam, {0x10}}}));
    const u64 wipe = WorldLogEpoch(7, 2);
    WorldLogEntry reset = Reset(1);
    reset.subject = WorldLogResetAfterWipe;
    EXPECT_EQ(host.Start(wipe, true), Entries(wipe, {reset}));
    EXPECT_TRUE(host.Kills().empty());
    EXPECT_TRUE(StartsAfterAWipe(Entries(wipe, {reset})));
    EXPECT_FALSE(StartsAfterAWipe(Entries(wipe, {Reset(1)})));
    // The host's own set after its wipe: nothing.
    EXPECT_TRUE(host.Propose(Proposal("host", wipe, 6, 2, {})).entries.empty());
    EXPECT_TRUE(host.Propose(Proposal("guest", first, 5, 2, {{Dream, {0x30}}})).stale)
        << "made before the guest heard of the wipe";
    // A kill made after the wipe stands.
    EXPECT_EQ(host.Propose(Proposal("guest", wipe, 5, 3, {{Yharnam, {0x11}}})).entries,
              Entries(wipe, {Kill(2, Yharnam, 0x11)}));
    const u64 session = WorldLogEpoch(7, 3);
    EXPECT_EQ(host.Start(session, false), Entries(session, {Reset(1)}));
    EXPECT_TRUE(host.Kills().empty());
    EXPECT_FALSE(host.Propose(Proposal("guest", 0, 5, 4, {{Dream, {0x30}}})).stale)
        << "a member that has not heard of the new log yet";
}

TEST(WorldLogWipe, AMemberProposesAgainOnlyAfterItsOwnWipe) {
    WorldLogWipeHold hold;
    EXPECT_FALSE(hold.HoldsProposals(0));
    EXPECT_FALSE(hold.BoardStale(0));
    // The host's wiped log came first: hold until this member's own wipe.
    hold.HostWiped(1'000);
    EXPECT_TRUE(hold.HoldsProposals(1'100));
    EXPECT_TRUE(hold.BoardStale(1'100));
    hold.OwnWipe(1'300);
    EXPECT_FALSE(hold.HoldsProposals(1'300));
    EXPECT_TRUE(hold.BoardStale(1'300 + WorldLogWipeGraceMs - 1));
    EXPECT_FALSE(hold.BoardStale(1'300 + WorldLogWipeGraceMs));
    // Its own wipe first, then the host's log: nothing held.
    WorldLogWipeHold first;
    first.OwnWipe(500);
    first.HostWiped(900);
    EXPECT_FALSE(first.HoldsProposals(900));
    // A member elsewhere (no wipe of its own): held for the grace time only.
    WorldLogWipeHold elsewhere;
    elsewhere.OwnWipe(0);
    elsewhere.HostWiped(100'000);
    EXPECT_TRUE(elsewhere.HoldsProposals(100'000 + WorldLogWipeGraceMs - 1));
    EXPECT_FALSE(elsewhere.HoldsProposals(100'000 + WorldLogWipeGraceMs));
}

// Rig smoke 2026-10-12, run 191219, in order: the host killed 0x1001400b; the party wiped; the
// host's new log stood it again from the kept sets (seq 2) and took it back (seq 3); after the
// wipe's trip a member's proposal stood it a third time (seq 4) and every game showed it dead.
TEST(WorldLogWipe, AWipeLeavesNoKillStanding) {
    constexpr u32 Dog = 0x1001400b;
    WorldLogHost host;
    WorldLogMember guest;
    WorldLogWipeHold guest_hold;
    const u64 first = WorldLogEpoch(0x3237, 1);
    host.Start(first, false);
    guest.Take(BatchWorldLog("h", first, host.Last(), std::vector{Reset(1)})[0], 0);
    // The host's kill; a guest that applied it near and counted it as its own too.
    const auto killed = host.Propose(Proposal("host", first, 6, 1, {{Yharnam, {Dog}}})).entries;
    guest.Take(BatchWorldLog("h", first, host.Last(), killed)[0], 100);
    host.Propose(Proposal("guest", first, 5, 1, {{Yharnam, {Dog}}}));
    // The wipe on the host: its kill step forgets its own kills, then the new log starts.
    host.Propose(Proposal("host", first, 6, 2, {}));
    const u64 wiped = WorldLogEpoch(0x3237, 2);
    const auto started = host.Start(wiped, true);
    EXPECT_EQ(started.size(), 1u) << "a Reset alone: no kill stands again";
    // The guest's last proposal before it heard of the new log: another log's, dropped.
    EXPECT_TRUE(host.Propose(Proposal("guest", first, 5, 2, {{Yharnam, {Dog}}})).stale);
    // The guest follows the new log before its own wipe ran: it holds its proposals.
    const auto step = guest.Take(BatchWorldLog("h", wiped, host.Last(), started)[0], 1'000);
    EXPECT_TRUE(StartsAfterAWipe(step.applied));
    guest_hold.HostWiped(1'000);
    EXPECT_TRUE(guest_hold.HoldsProposals(1'050)) << "its set still has the dog";
    // Its wipe: the kills forgotten, then proposals again (nothing to stand).
    guest_hold.OwnWipe(1'100);
    EXPECT_FALSE(guest_hold.HoldsProposals(1'100));
    EXPECT_TRUE(host.Propose(Proposal("guest", wiped, 5, 3, {})).entries.empty());
    // The trip after the wipe: still nothing.
    EXPECT_TRUE(host.Propose(Proposal("guest", wiped, 5, 4, {})).entries.empty());
    EXPECT_TRUE(host.Kills().empty());
    EXPECT_TRUE(guest.Kills().empty());
}

TEST(WorldLogHost, ASnapshotHoldsEveryKillInParts) {
    WorldLogHost host;
    const u64 epoch = WorldLogEpoch(7, 1);
    host.Start(epoch, false);
    const auto empty = host.Snapshot("h");
    ASSERT_EQ(empty.size(), 1u);
    EXPECT_EQ(empty[0], (WorldLogSnapshot{"h", epoch, 1, 0, 1, {}}));
    WorldLogKills all;
    for (u32 index = 0; index < 600; ++index) {
        all[index % 2 ? Yharnam : Ward].insert(0x10010000 + index);
    }
    // In pieces, as several members' kills.
    WorldLogKills a, b;
    for (const auto& [map, handles] : all) {
        for (const u32 handle : handles) {
            (handle % 3 ? a : b)[map].insert(handle);
        }
    }
    host.Propose(Proposal("a", epoch, 1, 1, a));
    host.Propose(Proposal("b", epoch, 2, 1, b));
    const auto parts = host.Snapshot("h");
    ASSERT_EQ(parts.size(), 3u);
    WorldLogKills joined;
    for (const WorldLogSnapshot& part : parts) {
        EXPECT_EQ(part.parts, 3);
        EXPECT_EQ(part.last, host.Last());
        EXPECT_LE(WorldLogKillCount(part.kills), WorldLogHandlesPerFrame);
        EXPECT_LE(EncodeWorldLogSnapshot(part).size() + 5, 1400u);
        for (const auto& [map, handles] : part.kills) {
            joined[map].insert(handles.begin(), handles.end());
        }
    }
    EXPECT_EQ(joined, all);
}

TEST(WorldLogMember, EntriesApplyInOrderExactlyOnce) {
    WorldLogMember member;
    const u64 epoch = WorldLogEpoch(7, 1);
    auto step = member.Take(Batch(epoch, 2, {Reset(1), Kill(2, Yharnam, 0x10)}), 0);
    EXPECT_TRUE(step.new_epoch);
    EXPECT_EQ(step.applied.size(), 2u);
    EXPECT_FALSE(step.gap);
    // Again (a resent frame): nothing.
    EXPECT_TRUE(
        member.Take(Batch(epoch, 2, {Reset(1), Kill(2, Yharnam, 0x10)}), 10).applied.empty());
    // Seq 4 before 3: kept, then both in order.
    step = member.Take(Batch(epoch, 4, {Unkill(4, Yharnam, 0x10)}), 20);
    EXPECT_TRUE(step.applied.empty());
    EXPECT_TRUE(step.gap);
    EXPECT_FALSE(member.Fresh(20)) << "a gap: the board's copy is used meanwhile";
    step = member.Take(Batch(epoch, 4, {Kill(3, Ward, 0x20)}), 30);
    EXPECT_EQ(step.applied, Entries(epoch, {Kill(3, Ward, 0x20), Unkill(4, Yharnam, 0x10)}));
    EXPECT_FALSE(step.gap);
    EXPECT_EQ(member.Kills(), (WorldLogKills{{Ward, {0x20}}}));
    EXPECT_EQ(member.Applied(), 4u);
    EXPECT_TRUE(member.Fresh(30));
    EXPECT_TRUE(member.Fresh(30 + WorldLogLifeMs));
    EXPECT_FALSE(member.Fresh(31 + WorldLogLifeMs)) << "the host went quiet";
    EXPECT_EQ(member.Host(), "host");
}

TEST(WorldLogMember, AGapAsksForASnapshotThatMendsIt) {
    WorldLogMember member;
    const u64 epoch = WorldLogEpoch(7, 1);
    member.Take(Batch(epoch, 2, {Reset(1), Kill(2, Yharnam, 0x10)}), 0);
    EXPECT_FALSE(member.SnapshotDue(0));
    // 3 and 4 were lost; 5 came.
    const auto step = member.Take(Batch(epoch, 5, {Kill(5, Yharnam, 0x13)}), 100);
    EXPECT_TRUE(step.gap);
    EXPECT_TRUE(member.SnapshotDue(100));
    EXPECT_FALSE(member.SnapshotDue(100 + WorldLogSnapshotRetryMs - 1)) << "asked already";
    EXPECT_TRUE(member.SnapshotDue(100 + WorldLogSnapshotRetryMs)) << "asked again";
    // The snapshot is of seq 4; the waiting 5 goes on top.
    const auto mended = member.Take(
        WorldLogSnapshot{"host", epoch, 4, 0, 1, {{Yharnam, {0x10, 0x11}}, {Ward, {0x12}}}}, 2'000);
    EXPECT_TRUE(mended.snapshot);
    EXPECT_EQ(mended.applied, Entries(epoch, {Kill(5, Yharnam, 0x13)}));
    EXPECT_FALSE(mended.gap);
    EXPECT_EQ(member.Kills(), (WorldLogKills{{Yharnam, {0x10, 0x11, 0x13}}, {Ward, {0x12}}}));
    EXPECT_FALSE(member.SnapshotDue(10'000));
    // An older snapshot than what the member has: nothing changes.
    EXPECT_FALSE(member.Take(WorldLogSnapshot{"host", epoch, 3, 0, 1, {}}, 2'100).snapshot);
    EXPECT_EQ(member.Applied(), 5u);
}

TEST(WorldLogMember, AHeartbeatShowsALostTail) {
    WorldLogMember member;
    const u64 epoch = WorldLogEpoch(7, 1);
    member.Take(Batch(epoch, 1, {Reset(1)}), 0);
    EXPECT_TRUE(member.Fresh(0));
    EXPECT_TRUE(member.Take(Batch(epoch, 3, {}), 500).gap) << "2 and 3 never came";
    EXPECT_FALSE(member.Fresh(500));
    EXPECT_TRUE(member.SnapshotDue(500));
}

TEST(WorldLogMember, ALateJoinerStartsFromASnapshotInParts) {
    WorldLogMember member;
    const u64 epoch = WorldLogEpoch(7, 4);
    EXPECT_FALSE(member.SnapshotDue(0)) << "nobody to ask yet";
    const auto heard = member.Take(Batch(epoch, 40, {}), 0);
    EXPECT_TRUE(heard.new_epoch);
    EXPECT_TRUE(heard.gap);
    EXPECT_TRUE(member.SnapshotDue(0));
    // Entries after the snapshot's seq wait for it.
    EXPECT_TRUE(member.Take(Batch(epoch, 41, {Kill(41, Ward, 0x50)}), 10).applied.empty());
    EXPECT_FALSE(
        member.Take(WorldLogSnapshot{"host", epoch, 40, 1, 2, {{Ward, {0x2}}}}, 20).snapshot)
        << "one part of two";
    EXPECT_TRUE(member.Kills().empty());
    const auto whole = member.Take(WorldLogSnapshot{"host", epoch, 40, 0, 2, {{Ward, {0x1}}}}, 30);
    EXPECT_TRUE(whole.snapshot);
    EXPECT_EQ(whole.applied, Entries(epoch, {Kill(41, Ward, 0x50)}));
    EXPECT_EQ(member.Kills(), (WorldLogKills{{Ward, {0x1, 0x2, 0x50}}}));
    EXPECT_TRUE(member.Fresh(30));
}

TEST(WorldLogMember, ANewEpochStartsOverAndAnOlderOneIsDropped) {
    WorldLogMember member;
    const u64 first = WorldLogEpoch(7, 1);
    member.Take(Batch(first, 3, {Reset(1), Kill(2, Yharnam, 0x10), Kill(3, Ward, 0x20)}), 0);
    // The host's wipe: a new log from its start.
    const u64 wipe = WorldLogEpoch(7, 2);
    auto step = member.Take(Batch(wipe, 2, {Reset(1), Kill(2, Ward, 0x20)}), 10);
    EXPECT_TRUE(step.new_epoch);
    EXPECT_FALSE(step.gap);
    EXPECT_EQ(member.Kills(), (WorldLogKills{{Ward, {0x20}}}));
    // A frame of the old log that came late.
    step = member.Take(Batch(first, 4, {Kill(4, Yharnam, 0x11)}), 20);
    EXPECT_TRUE(step.stale);
    EXPECT_EQ(member.Kills(), (WorldLogKills{{Ward, {0x20}}}));
    EXPECT_EQ(member.Epoch(), wipe);
    EXPECT_TRUE(member.Take(WorldLogSnapshot{"host", first, 9, 0, 1, {}}, 30).stale);
    // The host's game restarted: another run's log is followed even with a lower count.
    step = member.Take(Batch(WorldLogEpoch(8, 1), 1, {Reset(1)}), 40);
    EXPECT_TRUE(step.new_epoch);
    EXPECT_TRUE(member.Kills().empty());
}

TEST(WorldLogMember, ResetsOnlyItsMapAndSkipsKindsItDoesNotKnow) {
    WorldLogMember member;
    const u64 epoch = WorldLogEpoch(7, 1);
    member.Take(Batch(epoch, 4,
                      {Reset(1), Kill(2, Yharnam, 0x10), Kill(3, Ward, 0x20),
                       WorldLogEntry{4, 0, Ward, static_cast<WorldLogKind>(9), 0x20}}),
                0);
    EXPECT_EQ(member.Applied(), 4u) << "a later kind still counts";
    member.Take(Batch(epoch, 5, {Reset(5, Yharnam)}), 10);
    EXPECT_EQ(member.Kills(), (WorldLogKills{{Ward, {0x20}}}));
    member.Take(Batch(epoch, 6, {Unkill(6, Ward, 0x20)}), 20);
    EXPECT_TRUE(member.Kills().empty()) << "no empty map left behind";
}

TEST(WorldLogMember, LeavingForgetsTheLog) {
    WorldLogMember member;
    const u64 epoch = WorldLogEpoch(7, 1);
    member.Take(Batch(epoch, 2, {Reset(1), Kill(2, Yharnam, 0x10)}), 0);
    member.Reset();
    EXPECT_TRUE(member.Kills().empty());
    EXPECT_EQ(member.Epoch(), 0u);
    EXPECT_FALSE(member.Fresh(0));
    // The same log again (rejoined the same host): from a snapshot, the start is behind it.
    EXPECT_TRUE(member.Take(Batch(epoch, 2, {}), 10).gap);
    EXPECT_TRUE(member.SnapshotDue(10));
}

// A host and two members over a link that drops, repeats and reorders frames, with snapshots on
// request: the members end with the host's kills.
TEST(WorldLogMember, MembersConvergeOverABadLink) {
    std::mt19937 random{20261012};
    for (int round = 0; round < 20; ++round) {
        WorldLogHost host;
        std::vector<WorldLogMember> members(2);
        u32 count = 1;
        u64 epoch = WorldLogEpoch(7, count);
        std::vector<WorldLogEntry> sent = host.Start(epoch, false);
        std::deque<std::pair<size_t, WorldLogBatch>> link;
        const auto send = [&](const std::vector<WorldLogEntry>& entries) {
            for (const WorldLogBatch& batch :
                 BatchWorldLog("h", host.Epoch(), host.Last(), entries)) {
                for (size_t to = 0; to < members.size(); ++to) {
                    const u32 fate = random() % 10;
                    if (fate == 0) {
                        continue; // lost
                    }
                    link.emplace_back(to, batch);
                    if (fate == 1) {
                        link.emplace_back(to, batch); // twice
                    }
                }
            }
        };
        send(sent);
        WorldLogKills sets[3];
        s64 now = 0;
        for (int tick = 0; tick < 200; ++tick) {
            now += 100;
            const size_t who = random() % 3;
            const u32 handle = 0x10 + random() % 30;
            const u32 map = random() % 2 ? Yharnam : Ward;
            if (random() % 2) {
                sets[who][map].insert(handle);
            } else {
                sets[who][map].erase(handle);
            }
            if (tick == 120) {
                epoch = WorldLogEpoch(7, ++count); // a wipe
                send(host.Start(epoch, true));
            }
            send(
                host.Propose(Proposal(std::to_string(who), epoch, 1, tick + 1, sets[who])).entries);
            if (tick % 5 == 0) {
                send({}); // heartbeat
            }
            std::shuffle(link.begin(), link.begin() + std::min<size_t>(link.size(), 4), random);
            while (!link.empty()) {
                auto [to, batch] = link.front();
                link.pop_front();
                members[to].Take(batch, now);
            }
            for (WorldLogMember& member : members) {
                if (member.SnapshotDue(now)) {
                    for (const WorldLogSnapshot& part : host.Snapshot("h")) {
                        member.Take(part, now);
                    }
                }
            }
        }
        send({});
        for (auto& [to, batch] : link) {
            members[to].Take(batch, now);
        }
        for (WorldLogMember& member : members) {
            if (member.SnapshotDue(now + 10'000)) {
                for (const WorldLogSnapshot& part : host.Snapshot("h")) {
                    member.Take(part, now);
                }
            }
            WorldLogKills expected = host.Kills();
            EXPECT_EQ(member.Kills(), expected) << "round " << round;
            EXPECT_EQ(member.Epoch(), host.Epoch());
            EXPECT_EQ(member.Applied(), host.Last());
        }
    }
}

TEST(WorldLogPath, OnUnlessSwitchedOff) {
    EXPECT_TRUE(WorldLogEnabled(nullptr));
    EXPECT_TRUE(WorldLogEnabled(""));
    EXPECT_TRUE(WorldLogEnabled("1"));
    EXPECT_FALSE(WorldLogEnabled("0"));
    EXPECT_FALSE(WorldLogEnabled("off"));
}

TEST(WorldLogPath, OnlyWhenEveryPeerHasIt) {
    const PeerHello with{.member = "a", .features = WorldLogFeature};
    const PeerHello off{.member = "b", .features = 0};
    PeerHello older{.member = "c", .features = WorldLogFeature};
    older.protocol = 1;
    WorldLogPath path;
    EXPECT_FALSE(path.Decide(true, Peers{}, 0)) << "no peers: the board";
    const std::vector<WorldLogPeer> both{{"a", with}, {"b", with}};
    EXPECT_TRUE(path.Decide(true, both, 0));
    auto line = path.TakeChange();
    ASSERT_TRUE(line);
    EXPECT_EQ(*line, "Co-op world log: on, kills go through the host's log (2 peers have it)");
    EXPECT_FALSE(path.TakeChange()) << "once per change";
    EXPECT_TRUE(path.Decide(true, both, 100));
    EXPECT_FALSE(path.TakeChange());

    EXPECT_FALSE(path.Decide(true, Peers{{"a", with}, {"b", off}}, 200));
    EXPECT_EQ(path.TakeChange(), "Co-op world log: off, kills go through the kill board (b has it "
                                 "switched off)");
    EXPECT_TRUE(path.Decide(true, both, 300));
    EXPECT_FALSE(path.Decide(true, Peers{{"a", with}, {"c", older}}, 400));
    EXPECT_EQ(path.TakeChange(), "Co-op world log: off, kills go through the kill board (c runs "
                                 "co-op protocol 1)");
    EXPECT_TRUE(path.Decide(true, both, 500));
    EXPECT_FALSE(path.Decide(false, both, 600));
    path.TakeChange();
    EXPECT_FALSE(path.Decide(false, both, 700));
    EXPECT_FALSE(path.TakeChange());
}

TEST(WorldLogPath, AnUnsurePeerKeepsThePathForAWhile) {
    const PeerHello with{.member = "a", .features = WorldLogFeature};
    WorldLogPath path;
    EXPECT_TRUE(path.Decide(true, Peers{{"a", with}}, 0));
    path.TakeChange();
    // A third member's link came up; its hello is on the way.
    const std::vector<WorldLogPeer> new_peer{{"a", with}, {"d", std::nullopt}};
    EXPECT_TRUE(path.Decide(true, new_peer, 1'000));
    EXPECT_TRUE(path.Decide(true, new_peer, 1'000 + WorldLogUnsureMs - 1));
    EXPECT_FALSE(path.TakeChange());
    EXPECT_FALSE(path.Decide(true, new_peer, 1'000 + WorldLogUnsureMs));
    EXPECT_EQ(path.TakeChange(), "Co-op world log: off, kills go through the kill board (no hello "
                                 "from d)");
    // Its hello came: on again at once.
    EXPECT_TRUE(path.Decide(true, Peers{{"a", with}, {"d", with}}, 7'000));
    path.TakeChange();
    // Every link dropped for a moment: the log stays, then the board.
    EXPECT_TRUE(path.Decide(true, Peers{}, 8'000));
    EXPECT_TRUE(path.Decide(true, Peers{{"a", with}}, 9'000));
    EXPECT_TRUE(path.Decide(true, Peers{}, 10'000));
    EXPECT_FALSE(path.Decide(true, Peers{}, 10'000 + WorldLogUnsureMs));
    EXPECT_EQ(path.TakeChange(), "Co-op world log: off, kills go through the kill board (no "
                                 "peers)");
    // Not decided yet and unsure: the board.
    WorldLogPath fresh;
    EXPECT_FALSE(fresh.Decide(true, Peers{{"d", std::nullopt}}, 0));
}

// The path in use is logged at the first decision, and again when it or its reason changes (a
// mixed party stays on the board but says why).
TEST(WorldLogPath, TheFirstDecisionAndEachNewReasonAreLogged) {
    PeerHello older{.member = "c", .features = WorldLogFeature};
    older.protocol = 1;
    WorldLogPath path;
    path.Decide(true, Peers{}, 0);
    EXPECT_EQ(path.TakeChange(), "Co-op world log: off, kills go through the kill board (no "
                                 "peers)");
    path.Decide(true, Peers{{"c", older}}, 10);
    EXPECT_EQ(path.TakeChange(), "Co-op world log: off, kills go through the kill board (c runs "
                                 "co-op protocol 1)");
    path.Decide(true, Peers{{"c", older}}, 20);
    EXPECT_FALSE(path.TakeChange());
}

TEST(VisitKillsStanding, ThisVisitsAndTheHeldOnesByMap) {
    VisitKills kills;
    const u64 yharnam_visit = (u64{Yharnam} << 32) | 1;
    kills.Observe(yharnam_visit, std::vector<SeenCharacter>{{0x10, 400, 0xA0}, {0x11, 300, 0xB0}});
    kills.Observe(yharnam_visit, std::vector<SeenCharacter>{{0x10, 0, 0xA0}, {0x11, 300, 0xB0}});
    EXPECT_EQ(kills.Standing(), (WorldLogKills{{Yharnam, {0x10}}}));
    // A trip while another member stays on Yharnam: its kill is held there.
    const std::vector<u32> others{Yharnam};
    const u64 dream_visit = (u64{Dream} << 32) | 2;
    kills.Observe(dream_visit, std::vector<SeenCharacter>{{0x40, 50, 0xC0}}, others);
    kills.Observe(dream_visit, std::vector<SeenCharacter>{{0x40, 0, 0xC0}}, others);
    EXPECT_EQ(kills.Standing(), (WorldLogKills{{Yharnam, {0x10}}, {Dream, {0x40}}}));
    kills.Forget();
    EXPECT_TRUE(kills.Standing().empty());
}

TEST(PartyKillsBoard, TheHostsEntriesCanBeLeftOut) {
    PartyKills kills;
    kills.Apply(std::vector<KillChange>{{0x10, true, "host"}, {0x11, true, "out"}});
    EXPECT_EQ(kills.AllExcept("host"), (std::unordered_set<u32>{0x11}));
    EXPECT_EQ(kills.All(), (std::unordered_set<u32>{0x10, 0x11}));
    kills.Forget("out");
    EXPECT_EQ(kills.All(), (std::unordered_set<u32>{0x10}));
}
