// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The world log, step 3: the shared flags and the host's map state (docs/WORLD_SYNC.md, Stage C).

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
constexpr s32 GascoigneFog = 12414700;

WorldLogEntry Entry(u64 seq, WorldLogKind kind, u32 map, u32 subject) {
    return {seq, 0, map, kind, subject};
}

std::vector<WorldLogEntry> Entries(u64 epoch, std::vector<WorldLogEntry> entries) {
    for (WorldLogEntry& entry : entries) {
        entry.epoch = epoch;
    }
    return entries;
}

WorldLogBatch Batch(u64 epoch, u64 last, std::vector<WorldLogEntry> entries) {
    return {"host", epoch, last, Entries(epoch, std::move(entries))};
}

std::vector<u8> MapBits(std::initializer_list<s32> on) {
    std::vector<u8> bits((MapFlagCount + 7) / 8);
    for (const s32 index : on) {
        bits[index / 8] |= static_cast<u8>(0x80 >> (index % 8));
    }
    return bits;
}

WorldLogFlagProposal Flags(std::string member, u64 epoch, u32 run,
                           std::vector<WorldLogFlagEvent> events) {
    return {std::move(member), epoch, run, std::move(events)};
}

} // namespace

TEST(WorldLogFlags, FramesReadBackAndFitTheLink) {
    WorldLogFlagProposal proposal = Flags("6b616e69656c", WorldLogEpoch(7, 2), 0x1234,
                                          {{1, GascoigneFog, true}, {2, 12417830, false}});
    const auto payload = EncodeWorldLogFlagProposal(proposal);
    EXPECT_EQ(DecodeWorldLogFlagProposal(payload), proposal);
    for (size_t size = 0; size < payload.size(); ++size) {
        EXPECT_FALSE(DecodeWorldLogFlagProposal(std::span{payload}.first(size))) << size;
    }
    std::vector<u8> longer = payload;
    longer.push_back(0);
    EXPECT_FALSE(DecodeWorldLogFlagProposal(longer));
    for (u32 number = 3; number < 3 + WorldLogFlagEventsPerFrame; ++number) {
        proposal.events.push_back({number, 12000000 + static_cast<s32>(number), true});
    }
    proposal.member = std::string(64, 'a');
    const auto full = EncodeWorldLogFlagProposal(proposal);
    EXPECT_LE(full.size() + 5, 1400u);
    EXPECT_EQ(DecodeWorldLogFlagProposal(full)->events.size(), WorldLogFlagEventsPerFrame)
        << "the newest are kept";
    EXPECT_EQ(DecodeWorldLogFlagProposal(full)->events.back().number,
              2 + WorldLogFlagEventsPerFrame);

    const WorldLogMapStateSnapshot snapshot{
        std::string(64, 'h'), WorldLogEpoch(7, 2), 9, {Yharnam, MapBits({0, 7, 9999})}};
    const auto state = EncodeWorldLogMapStateSnapshot(snapshot);
    EXPECT_LE(state.size() + 5, 1400u);
    EXPECT_EQ(DecodeWorldLogMapStateSnapshot(state), snapshot);
    EXPECT_FALSE(DecodeWorldLogMapStateSnapshot(std::span{state}.first(state.size() - 1)));
    const WorldLogMapStateSnapshot none{"h", 1, 1, {}};
    EXPECT_EQ(DecodeWorldLogMapStateSnapshot(EncodeWorldLogMapStateSnapshot(none)), none);

    for (const u8 type : {WorldLogFlagsProposeType, WorldLogMapStateSnapshotType}) {
        EXPECT_TRUE(IsWorldLogFrameType(type));
        EXPECT_EQ(DecodeDirectDamage(EncodeCoopFrame(type, state)), std::nullopt)
            << "older builds drop it";
    }
}

// Kaniel's playtest 2026-10-24: a ritual altar's flags (94k) and the current altar's view (929)
// went to the team flag by flag, as diffs, and merged the records into ones that named no
// dungeon. Neither goes on the log now: a record goes whole (bloodborne_altar.h).
TEST(WorldLogFlags, NoAltarRecordGoesFlagByFlag) {
    FlagChanges changes;
    changes.set = {94105001, 94100003, 92900003, 92905001, 12417830};
    changes.cleared = {94100000, 92900000, 12417811};
    EXPECT_EQ(FlagsToShare(changes), (std::vector<std::pair<s32, bool>>{{12417830, true}}));
    EXPECT_FALSE(SharedWithParty(92900003)) << "the view is each game's own";
    EXPECT_FALSE(SharedWithParty(94100003));
}

// The log carries what the flag boards carried: the flag table's Shared flags that turned on (a
// lamp's active flag, a boss fog's entry), and the host's map flags that a guest takes by
// TakesHostMapFlag. Never the bell's, a boss's "found", the Dream doors, the badge lines, a
// headstone or lamp trip's request, nor a map pickup.
TEST(WorldLogFlags, OnlyTheFlagBoardsFlagsAreShared) {
    const s32 lamp_active = 10007810 + 241 * 10000 + 1 * 20; // Central Yharnam's first lamp
    ASSERT_TRUE(SharedWithParty(lamp_active));
    const std::vector<s32> never{9185,     9300,     9401,     9403,     12101000, 12101020,
                                 72101000, 72102000, 72110100, 50000001, 4000};
    FlagChanges changes;
    changes.set = never;
    changes.set.push_back(GascoigneFog);
    changes.set.push_back(lamp_active);
    changes.cleared = {lamp_active + 1};
    EXPECT_EQ(FlagsToShare(changes),
              (std::vector<std::pair<s32, bool>>{{GascoigneFog, true}, {lamp_active, true}}));
    EXPECT_FALSE(TakesHostMapFlag(12101000)) << "each player's own badge line";
    EXPECT_FALSE(TakesHostMapFlag(50000001)) << "a pickup, kept in the guest's own list";
    EXPECT_TRUE(TakesHostMapFlag(12410000));
}

TEST(WorldLogFlags, AnOutboxResendsEachEventForAWhile) {
    WorldLogFlagOutbox outbox;
    EXPECT_FALSE(outbox.TakeNew());
    outbox.Add(std::vector<std::pair<s32, bool>>{{GascoigneFog, true}, {7, false}}, 0);
    EXPECT_TRUE(outbox.TakeNew());
    EXPECT_FALSE(outbox.TakeNew());
    EXPECT_EQ(outbox.Pending(100),
              (std::vector<WorldLogFlagEvent>{{1, GascoigneFog, true}, {2, 7, false}}));
    outbox.Add(std::vector<std::pair<s32, bool>>{{8, true}}, 5'000);
    EXPECT_EQ(outbox.Pending(WorldLogFlagResendWindowMs + 1),
              (std::vector<WorldLogFlagEvent>{{3, 8, true}}));
    EXPECT_TRUE(outbox.Pending(5'000 + WorldLogFlagResendWindowMs + 1).empty());
    for (s32 flag = 0; flag < 100; ++flag) {
        outbox.Add(std::vector<std::pair<s32, bool>>{{flag, true}}, 20'000);
    }
    // A burst goes out a frame at a time, oldest first (the host takes rising numbers), then the
    // newest frame repeats (2026-10-18: an altar record of 311 flags lost all but the last 64).
    const auto first = outbox.Pending(20'000);
    EXPECT_EQ(first.size(), WorldLogFlagEventsPerFrame);
    EXPECT_EQ(first.front().flag, 0);
    EXPECT_EQ(first.back().flag, 63);
    const auto second = outbox.Pending(20'500);
    EXPECT_EQ(second.front().flag, 64);
    EXPECT_EQ(second.back().flag, 99);
    const auto again = outbox.Pending(21'000);
    EXPECT_EQ(again.size(), WorldLogFlagEventsPerFrame);
    EXPECT_EQ(again.back().flag, 99);
}

TEST(WorldLogFlags, AnEventCountsOnceFromEitherCopy) {
    FlagEventOnce once;
    EXPECT_TRUE(once.First(GascoigneFog, true, 0));
    EXPECT_FALSE(once.First(GascoigneFog, true, WorldLogFlagRepeatMs - 1));
    EXPECT_TRUE(once.First(GascoigneFog, false, 10));
    EXPECT_TRUE(once.First(GascoigneFog, true, 2 * WorldLogFlagRepeatMs));
}

TEST(WorldLogFlags, TheHostLogsEachEventOnce) {
    WorldLogHost host;
    const u64 epoch = WorldLogEpoch(7, 1);
    host.Start(epoch, false);
    auto taken = host.ProposeFlags(Flags("guest", epoch, 5, {{1, GascoigneFog, true}}), 0);
    EXPECT_EQ(taken.entries, Entries(epoch, {Entry(2, WorldLogKind::FlagSet, 0, GascoigneFog)}));
    // Resent (the frame repeats for a while): nothing new.
    EXPECT_TRUE(host.ProposeFlags(Flags("guest", epoch, 5, {{1, GascoigneFog, true}}), 500)
                    .entries.empty());
    // Another member lit the same lamp at the same time: one entry.
    EXPECT_TRUE(
        host.ProposeFlags(Flags("host", epoch, 6, {{1, GascoigneFog, true}}), 600).entries.empty());
    // A later visit's fog: again.
    taken = host.ProposeFlags(
        Flags("guest", epoch, 5, {{1, GascoigneFog, true}, {2, GascoigneFog, true}}),
        WorldLogFlagRepeatMs + 1);
    EXPECT_EQ(taken.entries, Entries(epoch, {Entry(3, WorldLogKind::FlagSet, 0, GascoigneFog)}));
    taken = host.ProposeFlags(Flags("guest", epoch, 5, {{3, 77, false}}), 20'000);
    EXPECT_EQ(taken.entries, Entries(epoch, {Entry(4, WorldLogKind::FlagClear, 0, 77)}));
    // A restarted game counts from 1 again.
    taken = host.ProposeFlags(Flags("guest", 0, 9, {{1, 78, true}}), 21'000);
    EXPECT_EQ(taken.entries, Entries(epoch, {Entry(5, WorldLogKind::FlagSet, 0, 78)}));
    EXPECT_TRUE(host.ProposeFlags(Flags("guest", WorldLogEpoch(7, 9), 9, {{2, 79, true}}), 0).stale)
        << "made for another log";
}

TEST(WorldLogFlags, TheHostLogsItsMapState) {
    WorldLogHost host;
    const u64 epoch = WorldLogEpoch(7, 1);
    host.Start(epoch, false);
    EXPECT_EQ(host.MapState(Yharnam, MapBits({3, 900})),
              Entries(epoch, {Entry(2, WorldLogKind::MapStateMap, Yharnam, Yharnam),
                              Entry(3, WorldLogKind::MapFlagOn, Yharnam, 3),
                              Entry(4, WorldLogKind::MapFlagOn, Yharnam, 900)}));
    EXPECT_TRUE(host.MapState(Yharnam, MapBits({3, 900})).empty());
    EXPECT_EQ(host.MapState(Yharnam, MapBits({900, 9999})),
              Entries(epoch, {Entry(5, WorldLogKind::MapFlagOff, Yharnam, 3),
                              Entry(6, WorldLogKind::MapFlagOn, Yharnam, 9999)}));
    EXPECT_EQ(host.MapState(Ward, MapBits({1})),
              Entries(epoch, {Entry(7, WorldLogKind::MapStateMap, Ward, Ward),
                              Entry(8, WorldLogKind::MapFlagOn, Ward, 1)}));
    EXPECT_TRUE(host.MapState(Ward, std::vector<u8>{1, 2}).empty()) << "not a whole map";
    const u64 wipe = WorldLogEpoch(7, 2);
    EXPECT_EQ(host.Start(wipe, true),
              Entries(wipe, {Entry(1, WorldLogKind::Reset, 0, WorldLogResetAfterWipe),
                             Entry(2, WorldLogKind::MapStateMap, Ward, Ward),
                             Entry(3, WorldLogKind::MapFlagOn, Ward, 1)}));
    EXPECT_EQ(host.MapStateSnapshot("h"),
              (WorldLogMapStateSnapshot{"h", wipe, 3, {Ward, MapBits({1})}}));
    host.ForgetMapState();
    EXPECT_EQ(host.MapStateSnapshot("h").state, WorldLogMapState{});
    EXPECT_EQ(host.MapState(Ward, MapBits({1})).size(), 2u) << "from a MapStateMap again";
}

TEST(WorldLogFlags, AMemberKeepsTheHostsMapState) {
    WorldLogMember member;
    const u64 epoch = WorldLogEpoch(7, 1);
    EXPECT_FALSE(member.MapStateKnown());
    const auto step = member.Take(Batch(epoch, 5,
                                        {Entry(1, WorldLogKind::Reset, 0, 0),
                                         Entry(2, WorldLogKind::MapStateMap, Yharnam, Yharnam),
                                         Entry(3, WorldLogKind::MapFlagOn, Yharnam, 3),
                                         Entry(4, WorldLogKind::FlagSet, 0, GascoigneFog),
                                         Entry(5, WorldLogKind::MapFlagOn, Ward, 4)}),
                                  0);
    EXPECT_EQ(step.applied.size(), 5u) << "the flag events are the glue's to set";
    EXPECT_TRUE(member.MapStateKnown());
    EXPECT_EQ(member.MapState(), (WorldLogMapState{Yharnam, MapBits({3})}))
        << "another map's flag is not the host's map";
    member.Take(Batch(epoch, 7,
                      {Entry(6, WorldLogKind::MapFlagOff, Yharnam, 3),
                       Entry(7, WorldLogKind::MapFlagOn, Yharnam, MapFlagCount)}),
                10);
    EXPECT_EQ(member.MapState(), (WorldLogMapState{Yharnam, MapBits({})}))
        << "an index past the map's flags is dropped";
    member.Reset();
    EXPECT_FALSE(member.MapStateKnown());
    EXPECT_EQ(member.MapState(), WorldLogMapState{});
}

TEST(WorldLogFlags, ASnapshotWaitsForTheMapState) {
    const u64 epoch = WorldLogEpoch(7, 3);
    WorldLogMember member;
    member.Take(Batch(epoch, 20, {}), 0);
    member.Take(WorldLogSnapshot{"host", epoch, 20, 0, 1, {}}, 10);
    const auto shown =
        member.Take(WorldLogShownSnapshot{"host", epoch, 20, 0, 1, {Yharnam, {}}}, 20);
    EXPECT_FALSE(shown.snapshot) << "the map state is not in yet";
    member.Take(WorldLogHpSnapshot{"host", epoch, 20, 0, 1, {}}, 25);
    member.Take(WorldLogFlagsSnapshot{"host", epoch, 20, {}}, 26);
    const auto whole =
        member.Take(WorldLogMapStateSnapshot{"host", epoch, 20, {Yharnam, MapBits({5})}}, 30);
    EXPECT_TRUE(whole.snapshot);
    EXPECT_TRUE(member.MapStateKnown());
    EXPECT_EQ(member.MapState(), (WorldLogMapState{Yharnam, MapBits({5})}));
}

// Review 2026-10-12: a member that joins mid-log holds the live events it hears while it waits
// for the snapshot; the lamp lit in that time is set once the snapshot is in, not dropped with
// the entries the snapshot covers (on the log path nothing else would set it).
TEST(WorldLogFlags, ASnapshotKeepsTheEventsHeardWhileWaiting) {
    const u64 epoch = WorldLogEpoch(7, 3);
    WorldLogMember member;
    const auto heard = member.Take(Batch(epoch, 21, {Entry(21, WorldLogKind::FlagSet, 0, 78)}), 0);
    EXPECT_TRUE(heard.applied.empty()) << "not based yet";
    member.Take(WorldLogSnapshot{"host", epoch, 22, 0, 1, {}}, 10);
    member.Take(WorldLogShownSnapshot{"host", epoch, 22, 0, 1, {Yharnam, {}}}, 20);
    member.Take(WorldLogHpSnapshot{"host", epoch, 22, 0, 1, {}}, 25);
    member.Take(WorldLogFlagsSnapshot{"host", epoch, 22, {}}, 26);
    const auto whole = member.Take(WorldLogMapStateSnapshot{"host", epoch, 22, {}}, 30);
    ASSERT_TRUE(whole.snapshot);
    EXPECT_EQ(whole.applied, Entries(epoch, {Entry(21, WorldLogKind::FlagSet, 0, 78)}));
}

TEST(WorldLogFlags, MembersConvergeOnTheMapStateOverABadLink) {
    std::mt19937 random{4};
    for (int round = 0; round < 20; ++round) {
        WorldLogHost host;
        std::vector<WorldLogMember> members(2);
        for (WorldLogMember& member : members) {
        }
        std::deque<std::pair<size_t, WorldLogBatch>> link;
        const auto send = [&](const std::vector<WorldLogEntry>& entries) {
            for (const WorldLogBatch& batch :
                 BatchWorldLog("h", host.Epoch(), host.Last(), entries)) {
                for (size_t to = 0; to < members.size(); ++to) {
                    if (random() % 8 != 0) {
                        link.emplace_back(to, batch);
                    }
                }
            }
        };
        send(host.Start(WorldLogEpoch(9, 1), false));
        std::vector<u8> bits = MapBits({});
        u32 map = Yharnam;
        s64 now = 0;
        const auto snapshot = [&](WorldLogMember& member) {
            for (const auto& part : host.Snapshot("h")) {
                member.Take(part, now);
            }
            for (const auto& part : host.ShownSnapshot("h")) {
                member.Take(part, now);
            }
            member.Take(host.MapStateSnapshot("h"), now);
            for (const auto& part : host.HpSnapshot("h")) {
                member.Take(part, now);
            }
            member.Take(host.FlagsSnapshot("h"), now);
        };
        for (int tick = 0; tick < 120; ++tick) {
            now += 100;
            const s32 index = static_cast<s32>(random() % MapFlagCount);
            bits[index / 8] ^= static_cast<u8>(0x80 >> (index % 8));
            if (tick == 60) {
                map = Ward;
            }
            if (tick == 90) {
                send(host.Start(WorldLogEpoch(9, 2), true));
            }
            send(host.MapState(map, bits));
            std::shuffle(link.begin(), link.begin() + std::min<size_t>(link.size(), 3), random);
            while (!link.empty()) {
                auto [to, batch] = link.front();
                link.pop_front();
                members[to].Take(batch, now);
            }
            for (WorldLogMember& member : members) {
                if (member.SnapshotDue(now)) {
                    snapshot(member);
                }
            }
        }
        for (WorldLogMember& member : members) {
            member.Take(BatchWorldLog("h", host.Epoch(), host.Last(), {})[0], now);
            if (member.SnapshotDue(now + 10'000)) {
                snapshot(member);
            }
            EXPECT_EQ(member.MapState(), host.MapStateNow()) << "round " << round;
            EXPECT_TRUE(member.MapStateKnown());
        }
    }
}

TEST(WorldLogFlags, AMapStateReadsBackFromItsTraceRecord) {
    const auto payload = EncodeWorldLogMapStateInput(Ward, MapBits({2}));
    EXPECT_EQ(DecodeWorldLogMapStateInput(payload), (WorldLogMapState{Ward, MapBits({2})}));
    EXPECT_FALSE(DecodeWorldLogMapStateInput(std::span{payload}.first(3)));
}

// Review 2026-10-12: the after-wipe mark came only with the Reset entry; a member that took the
// new log from a snapshot (the Reset's frame lost, or the server's world snapshot) proposed its
// pre-wipe kills into it. The flags part says whether the log started at a wipe.
TEST(WorldLogFlags, ASnapshotSaysTheLogStartedAtAWipe) {
    WorldLogHost host;
    const u64 epoch = WorldLogEpoch(7, 4);
    host.Start(epoch, true);
    const auto flags =
        DecodeWorldLogFlagsSnapshot(EncodeWorldLogFlagsSnapshot(host.FlagsSnapshot("host")));
    ASSERT_TRUE(flags);
    EXPECT_TRUE(flags->after_wipe);
    WorldLogMember member;
    member.Take(WorldLogSnapshot{"host", epoch, flags->last, 0, 1, {}}, 10);
    member.Take(WorldLogShownSnapshot{"host", epoch, flags->last, 0, 1, {Yharnam, {}}}, 20);
    member.Take(WorldLogHpSnapshot{"host", epoch, flags->last, 0, 1, {}}, 25);
    member.Take(WorldLogMapStateSnapshot{"host", epoch, flags->last, {}}, 26);
    const auto whole = member.Take(*flags, 30);
    ASSERT_TRUE(whole.snapshot);
    EXPECT_TRUE(whole.after_wipe);

    WorldLogHost fresh;
    fresh.Start(WorldLogEpoch(7, 5), false);
    EXPECT_FALSE(fresh.FlagsSnapshot("host").after_wipe);
}
