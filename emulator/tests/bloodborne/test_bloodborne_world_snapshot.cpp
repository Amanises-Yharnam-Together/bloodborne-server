// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The world snapshot on the server, and the flag events a snapshot brings (docs/WORLD_SYNC.md).

#include <string>
#include <vector>

#include <fmt/format.h>
#include <gtest/gtest.h>

#include "core/bloodborne_party.h"

using namespace Core::Bloodborne;

namespace {

constexpr u32 Yharnam = 0x18010000;
constexpr s32 Fog = 12414700;
constexpr s32 Lamp = 12417830;

std::vector<WorldLogEntry> Entries(u64 epoch, std::vector<WorldLogEntry> entries) {
    for (WorldLogEntry& entry : entries) {
        entry.epoch = epoch;
    }
    return entries;
}

WorldLogEntry FlagOn(u64 seq, s32 flag) {
    return {seq, 0, 0, WorldLogKind::FlagSet, static_cast<u32>(flag)};
}

// The frames a host sends a member that asks for a snapshot, as the glue builds them.
WorldSnapshotFrames HostFrames(const WorldLogHost& host) {
    WorldSnapshotFrames frames;
    for (const auto& part : host.Snapshot("h")) {
        frames.emplace_back(WorldLogSnapshotType, EncodeWorldLogSnapshot(part));
    }
    for (const auto& part : host.ShownSnapshot("h")) {
        frames.emplace_back(WorldLogShownSnapshotType, EncodeWorldLogShownSnapshot(part));
    }
    frames.emplace_back(WorldLogMapStateSnapshotType,
                        EncodeWorldLogMapStateSnapshot(host.MapStateSnapshot("h")));
    for (const auto& part : host.HpSnapshot("h")) {
        frames.emplace_back(WorldLogHpSnapshotType, EncodeWorldLogHpSnapshot(part));
    }
    frames.emplace_back(WorldLogFlagsSnapshotType,
                        EncodeWorldLogFlagsSnapshot(host.FlagsSnapshot("h")));
    return frames;
}

// A member takes the frames as the glue does.
std::vector<WorldLogEntry> TakeFrames(WorldLogMember& member, const WorldSnapshotFrames& frames,
                                      s64 now_ms) {
    std::vector<WorldLogEntry> applied;
    for (const auto& [type, payload] : frames) {
        WorldLogMember::Step step;
        if (type == WorldLogSnapshotType) {
            step = member.Take(*DecodeWorldLogSnapshot(payload), now_ms);
        } else if (type == WorldLogShownSnapshotType) {
            step = member.Take(*DecodeWorldLogShownSnapshot(payload), now_ms);
        } else if (type == WorldLogMapStateSnapshotType) {
            step = member.Take(*DecodeWorldLogMapStateSnapshot(payload), now_ms);
        } else if (type == WorldLogHpSnapshotType) {
            step = member.Take(*DecodeWorldLogHpSnapshot(payload), now_ms);
        } else if (type == WorldLogFlagsSnapshotType) {
            step = member.Take(*DecodeWorldLogFlagsSnapshot(payload), now_ms);
        }
        applied.insert(applied.end(), step.applied.begin(), step.applied.end());
    }
    return applied;
}

} // namespace

TEST(WorldSnapshot, TheFlagEventsFrameReadsBack) {
    const WorldLogFlagsSnapshot snapshot{
        "686f7374", WorldLogEpoch(7, 2), 9,
        Entries(WorldLogEpoch(7, 2), {FlagOn(4, Fog), FlagOn(9, Lamp)})};
    const auto payload = EncodeWorldLogFlagsSnapshot(snapshot);
    EXPECT_EQ(DecodeWorldLogFlagsSnapshot(payload), snapshot);
    for (size_t size = 0; size < payload.size(); ++size) {
        EXPECT_FALSE(DecodeWorldLogFlagsSnapshot(std::span{payload}.first(size))) << size;
    }
    EXPECT_TRUE(IsWorldLogFrameType(WorldLogFlagsSnapshotType));
    WorldLogFlagsSnapshot full{std::string(64, 'h'), 1, 1, {}};
    for (u64 seq = 1; seq <= WorldLogRecentFlags; ++seq) {
        full.events.push_back({seq, 1, 0, WorldLogKind::FlagClear, 7});
    }
    EXPECT_LE(EncodeWorldLogFlagsSnapshot(full).size() + 5, 1400u);
}

TEST(WorldSnapshot, TheHostKeepsTheRecentFlagEvents) {
    WorldLogHost host;
    const u64 epoch = WorldLogEpoch(7, 1);
    host.Start(epoch, false);
    for (u32 number = 1; number <= WorldLogRecentFlags + 6; ++number) {
        host.ProposeFlags({"g", epoch, 5, {{number, static_cast<s32>(number), true}}}, 0);
    }
    const auto snapshot = host.FlagsSnapshot("h");
    ASSERT_EQ(snapshot.events.size(), WorldLogRecentFlags);
    EXPECT_EQ(snapshot.events.front().subject, 7u) << "the oldest went";
    EXPECT_EQ(snapshot.last, host.Last());
    host.Start(WorldLogEpoch(7, 2), true);
    EXPECT_TRUE(host.FlagsSnapshot("h").events.empty()) << "a new log has none";
}

// A member that lost entries (its link was down) gets the flag events it missed from the snapshot,
// once; one that joins gets none of the earlier ones.
TEST(WorldSnapshot, ASnapshotBringsTheMissedFlagEventsOnly) {
    WorldLogHost host;
    const u64 epoch = WorldLogEpoch(7, 1);
    WorldLogMember member;
    const auto relay = [&](const std::vector<WorldLogEntry>& entries, s64 now) {
        for (const auto& batch : BatchWorldLog("h", host.Epoch(), host.Last(), entries)) {
            member.Take(batch, now);
        }
    };
    relay(host.Start(epoch, false), 0);
    relay(host.ProposeFlags({"g", epoch, 5, {{1, Fog, true}}}, 0).entries, 10);
    // Its link goes down: the lamp is lost.
    host.ProposeFlags({"g", epoch, 5, {{2, Lamp, true}}}, 20);
    EXPECT_TRUE(member.Take(BatchWorldLog("h", epoch, host.Last(), {})[0], 30).gap);
    const auto missed = TakeFrames(member, HostFrames(host), 40);
    EXPECT_EQ(missed, Entries(epoch, {FlagOn(3, Lamp)})) << "the fog it had, the lamp it missed";
    EXPECT_TRUE(member.Fresh(40));
    // Another member joins now: the snapshot sets none of the earlier events.
    WorldLogMember late;
    late.Take(BatchWorldLog("h", epoch, host.Last(), {})[0], 50);
    EXPECT_TRUE(TakeFrames(late, HostFrames(host), 60).empty());
    EXPECT_TRUE(late.Fresh(60));
}

TEST(WorldSnapshot, ABlobReadsBack) {
    const WorldSnapshotFrames frames{{WorldLogSnapshotType, {1, 2, 3}},
                                     {WorldLogFlagsSnapshotType, {}},
                                     {WorldLogHpSnapshotType, std::vector<u8>(1300, 7)}};
    const auto blob = EncodeWorldSnapshot(frames);
    EXPECT_EQ(DecodeWorldSnapshot(blob), frames);
    for (size_t size = 0; size + 1 < blob.size(); size += 97) {
        EXPECT_FALSE(DecodeWorldSnapshot(std::span{blob}.first(size + 1))) << size;
    }
    std::vector<u8> other = blob;
    other[0] = 99;
    EXPECT_FALSE(DecodeWorldSnapshot(other)) << "another version";
    EXPECT_FALSE(DecodeWorldSnapshot({}));
}

TEST(WorldSnapshot, TheServersMessages) {
    const std::vector<u8> blob{0x01, 0xab};
    EXPECT_EQ(EncodeWorldSnapshotUpdate("4b61", blob), "4b61 01ab");
    EXPECT_EQ(EncodeWorldSnapshotUpdate("4b61", {}), "4b61");
    EXPECT_EQ(ParseWorldSnapshotAnswer("1500 01ab\n"), (WorldSnapshotAnswer{1500, blob}));
    EXPECT_EQ(ParseWorldSnapshotAnswer(""), WorldSnapshotAnswer{}) << "none yet";
    EXPECT_FALSE(ParseWorldSnapshotAnswer("x 01ab\n"));
    EXPECT_FALSE(ParseWorldSnapshotAnswer("12 0g\n"));
    EXPECT_FALSE(ParseWorldSnapshotAnswer("12 011\n"));
}

// A member back from a restart (or with its link down) takes the server's copy: the same state as
// the host's.
TEST(WorldSnapshot, AMemberTakesTheHostsWorldFromTheServer) {
    WorldLogHost host;
    const u64 epoch = WorldLogEpoch(7, 1);
    host.Start(epoch, false);
    host.Propose({"g", epoch, 5, 1, {{Yharnam, {0x10, 0x11}}}});
    host.Show(Yharnam, std::vector<CharacterShown>{{0x10014000, false, true, false, false}});
    std::vector<u8> bits((MapFlagCount + 7) / 8);
    bits[3] = 0x80;
    host.MapState(Yharnam, bits);
    host.ProposeHp({"g", epoch, 5, 1, Yharnam, {0x1111000000000003, {{0x10014066, 49, 450}}}});
    const auto answer = ParseWorldSnapshotAnswer("700 " + [&] {
        std::string hex;
        for (const u8 byte : EncodeWorldSnapshot(HostFrames(host))) {
            hex += fmt::format("{:02x}", byte);
        }
        return hex;
    }() + "\n");
    ASSERT_TRUE(answer);
    const auto frames = DecodeWorldSnapshot(answer->blob);
    ASSERT_TRUE(frames);
    WorldLogMember member;
    TakeFrames(member, *frames, 0);
    EXPECT_EQ(member.Epoch(), epoch);
    EXPECT_EQ(member.Applied(), host.Last());
    EXPECT_EQ(member.Kills(), host.Kills());
    EXPECT_EQ(member.Shown(), host.Shown());
    EXPECT_EQ(member.MapState(), host.MapStateNow());
    EXPECT_EQ(member.Hp(), host.Hp());
    EXPECT_TRUE(member.Fresh(0));
}

// A member that joins after the host's return point changed gets it from the snapshot (the map
// state part carries it), over the link or from the server.
TEST(WorldSnapshot, ASnapshotCarriesTheHostsReturnPoint) {
    const WorldLogMapStateSnapshot part{"686f7374", WorldLogEpoch(7, 2), 9, {}, {2412951, Yharnam}};
    const auto payload = EncodeWorldLogMapStateSnapshot(part);
    EXPECT_EQ(DecodeWorldLogMapStateSnapshot(payload), part);
    for (size_t size = 0; size < payload.size(); ++size) {
        EXPECT_FALSE(DecodeWorldLogMapStateSnapshot(std::span{payload}.first(size))) << size;
    }

    WorldLogHost host;
    const u64 epoch = WorldLogEpoch(7, 1);
    host.Start(epoch, false);
    host.ReturnPoint(2412951, Yharnam);
    host.Propose({"g", epoch, 5, 1, {{Yharnam, {0x10}}}});
    EXPECT_EQ(host.MapStateSnapshot("h").return_point, (WorldLogReturnPoint{2412951, Yharnam}));
    WorldLogMember late;
    late.Take(BatchWorldLog("h", epoch, host.Last(), {})[0], 0);
    const auto frames = DecodeWorldSnapshot(EncodeWorldSnapshot(HostFrames(host)));
    ASSERT_TRUE(frames);
    TakeFrames(late, *frames, 10);
    EXPECT_TRUE(late.Fresh(10));
    EXPECT_EQ(late.HostReturnPoint(), (WorldLogReturnPoint{2412951, Yharnam}));
}
