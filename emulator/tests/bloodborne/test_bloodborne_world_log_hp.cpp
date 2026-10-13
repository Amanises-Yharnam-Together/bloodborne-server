// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The world log, step 4: enemy HP (docs/WORLD_SYNC.md, Stage C).

#include <deque>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/bloodborne_party.h"

using namespace Core::Bloodborne;

namespace {

constexpr u32 Yharnam = 0x18010000;
constexpr u64 GuestA = 0x1111'0000'0000'0003; // a member's tag and visit
constexpr u64 GuestB = 0x2222'0000'0000'0005;
constexpr u32 Dog = 0x10014066;
constexpr u32 Wolf = 0x1001400b;

WorldLogEntry Entry(u64 seq, WorldLogKind kind, u32 map, u32 subject, u32 value = 0) {
    return {seq, 0, map, kind, subject, value};
}
WorldLogEntry Source(u64 seq, u64 source, u32 map = Yharnam) {
    return Entry(seq, WorldLogKind::HpSource, map, static_cast<u32>(source >> 32),
                 static_cast<u32>(source));
}
WorldLogEntry Value(u64 seq, u32 handle, s32 value, s32 max) {
    return Entry(seq, WorldLogKind::HpValue, static_cast<u32>(max), handle,
                 static_cast<u32>(value));
}

std::vector<WorldLogEntry> Entries(u64 epoch, std::vector<WorldLogEntry> entries) {
    for (WorldLogEntry& entry : entries) {
        entry.epoch = epoch;
    }
    return entries;
}

WorldLogHpProposal Hp(std::string member, u64 epoch, u32 number, u64 source,
                      std::vector<EnemyHp> enemies, u32 run = 5) {
    return {std::move(member), epoch, run, number, Yharnam, {source, std::move(enemies)}};
}

// A snapshot's other parts (empty) at `last`.
void TakeOtherParts(WorldLogMember& member, u64 epoch, u64 last, s64 now_ms) {
    member.Take(WorldLogSnapshot{"host", epoch, last, 0, 1, {}}, now_ms);
    member.Take(WorldLogShownSnapshot{"host", epoch, last, 0, 1, {}}, now_ms);
    member.Take(WorldLogMapStateSnapshot{"host", epoch, last, {}}, now_ms);
    member.Take(WorldLogFlagsSnapshot{"host", epoch, last, {}}, now_ms);
}

} // namespace

TEST(WorldLogHp, FramesReadBackAndFitTheLink) {
    WorldLogHpProposal proposal =
        Hp("6b616e69656c", WorldLogEpoch(7, 2), 9, GuestA, {{Dog, 49, 450}, {Wolf, 484, 484}});
    const auto payload = EncodeWorldLogHpProposal(proposal);
    EXPECT_EQ(DecodeWorldLogHpProposal(payload), proposal);
    for (size_t size = 0; size < payload.size(); ++size) {
        EXPECT_FALSE(DecodeWorldLogHpProposal(std::span{payload}.first(size))) << size;
    }
    std::vector<u8> longer = payload;
    longer.push_back(0);
    EXPECT_FALSE(DecodeWorldLogHpProposal(longer));
    proposal.member = std::string(64, 'a');
    proposal.report.enemies.clear();
    for (u32 index = 0; index < 150; ++index) {
        proposal.report.enemies.push_back({0x10010000 + index, 1, 2});
    }
    const auto full = EncodeWorldLogHpProposal(proposal);
    EXPECT_LE(full.size() + 5, 1400u);
    EXPECT_EQ(DecodeWorldLogHpProposal(full)->report.enemies.size(), WorldLogHpPerFrame);

    const WorldLogHpSnapshot snapshot{
        "686f7374", WorldLogEpoch(7, 2),
        12,         1,
        2,          {{GuestA, {Yharnam, {{Dog, {49, 450}}}}}, {GuestB, {Yharnam, {}}}}};
    const auto part = EncodeWorldLogHpSnapshot(snapshot);
    EXPECT_EQ(DecodeWorldLogHpSnapshot(part), snapshot);
    for (size_t size = 0; size < part.size(); ++size) {
        EXPECT_FALSE(DecodeWorldLogHpSnapshot(std::span{part}.first(size))) << size;
    }

    // An entries frame of 100 HP values fits, and values come back whole.
    std::vector<WorldLogEntry> many;
    for (u64 seq = 1; seq <= WorldLogEntriesPerFrame; ++seq) {
        many.push_back({seq, 5, 99999, WorldLogKind::HpValue, static_cast<u32>(seq), 0x7FFFFFFF});
    }
    const auto batch = BatchWorldLog(std::string(64, 'h'), 5, WorldLogEntriesPerFrame, many);
    ASSERT_EQ(batch.size(), 1u);
    const auto encoded = EncodeWorldLogBatch(batch[0]);
    EXPECT_LE(encoded.size() + 5, 1400u);
    EXPECT_EQ(DecodeWorldLogBatch(encoded), batch[0]);

    for (const u8 type : {WorldLogHpProposeType, WorldLogHpSnapshotType}) {
        EXPECT_TRUE(IsWorldLogFrameType(type));
        EXPECT_EQ(DecodeDirectDamage(EncodeCoopFrame(type, part)), std::nullopt);
    }
}

TEST(WorldLogHp, TheHostLogsWhatChangedInEachReport) {
    WorldLogHost host;
    const u64 epoch = WorldLogEpoch(7, 1);
    host.Start(epoch, false);
    auto taken = host.ProposeHp(Hp("a", epoch, 1, GuestA, {{Dog, 49, 450}}));
    EXPECT_EQ(taken.entries, Entries(epoch, {Source(2, GuestA), Value(3, Dog, 49, 450)}));
    EXPECT_TRUE(host.ProposeHp(Hp("a", epoch, 2, GuestA, {{Dog, 49, 450}})).entries.empty())
        << "the same report again (every 0.5 s)";
    // A second hit, and the wolf hurt.
    taken = host.ProposeHp(Hp("a", epoch, 3, GuestA, {{Dog, 98, 450}, {Wolf, 10, 484}}));
    EXPECT_EQ(taken.entries,
              Entries(epoch, {Source(4, GuestA), Value(5, Wolf, 10, 484), Value(6, Dog, 98, 450)}));
    // The dog left the report (its death).
    taken = host.ProposeHp(Hp("a", epoch, 4, GuestA, {{Wolf, 10, 484}}));
    EXPECT_EQ(taken.entries,
              Entries(epoch, {Source(7, GuestA), Entry(8, WorldLogKind::HpGone, 0, Dog)}));
    // A late one, and one made for another log: dropped.
    EXPECT_TRUE(host.ProposeHp(Hp("a", epoch, 2, GuestA, {{Dog, 1, 450}})).stale);
    EXPECT_TRUE(host.ProposeHp(Hp("a", WorldLogEpoch(7, 9), 9, GuestA, {})).stale);
    // A new visit is a new source: the old one goes.
    const u64 next_visit = GuestA + 1;
    taken = host.ProposeHp(Hp("a", epoch, 5, next_visit, {{Dog, 5, 450}}));
    EXPECT_EQ(taken.entries,
              Entries(epoch, {Entry(9, WorldLogKind::HpSourceGone, 0,
                                    static_cast<u32>(GuestA >> 32), static_cast<u32>(GuestA)),
                              Source(10, next_visit), Value(11, Dog, 5, 450)}));
    EXPECT_EQ(host.Hp().size(), 1u);
}

// Ada's run (2026-10-12): the host's dog at 438 of 1093 reached the guest as 182 (438 - 256),
// and its copy flipped between the two all fight. HP and a source's visit go on the log whole.
TEST(WorldLogHp, TheHostLogsWholeValues) {
    WorldLogHost host;
    const u64 epoch = WorldLogEpoch(7, 1);
    host.Start(epoch, false);
    const u64 source = HostHpReport | 0x2ECE'20BD'0000'0101; // a visit past 255
    const auto taken = host.ProposeHp(Hp("h", epoch, 1, source, {{Dog, 438, 1093}}));
    EXPECT_EQ(taken.entries, Entries(epoch, {Source(2, source), Value(3, Dog, 438, 1093)}));
    std::vector<WorldLogEntry> log = Entries(epoch, {Entry(1, WorldLogKind::Reset, 0, 0)});
    log.insert(log.end(), taken.entries.begin(), taken.entries.end());
    WorldLogMember member;
    member.Take(BatchWorldLog("host", epoch, 3, log)[0], 0);
    EXPECT_EQ(WorldLogHpReports(member.Hp()), (std::vector<std::pair<u32, EnemyHpReport>>{
                                                  {Yharnam, {source, {{Dog, 438, 1093}}}}}));
}

// No stale pre-wipe damage: a new log (a wipe, a new session) starts with no HP at all.
TEST(WorldLogHp, ANewLogHasNoHp) {
    WorldLogHost host;
    const u64 epoch = WorldLogEpoch(7, 1);
    host.Start(epoch, false);
    host.ProposeHp(Hp("a", epoch, 1, GuestA, {{Dog, 49, 450}}));
    const u64 wiped = WorldLogEpoch(7, 2);
    EXPECT_EQ(host.Start(wiped, true).size(), 1u) << "a Reset alone";
    EXPECT_TRUE(host.Hp().empty());
    EXPECT_TRUE(host.ProposeHp(Hp("a", epoch, 2, GuestA, {{Dog, 49, 450}})).stale)
        << "made for the old log";
    // The member's report after its own wipe (a new visit, a new source).
    const auto taken = host.ProposeHp(Hp("a", wiped, 3, GuestA + 1, {}));
    EXPECT_TRUE(taken.entries.empty()) << "nothing hurt yet";
    EXPECT_TRUE(host.Hp().empty()) << "a report with no enemies is kept as none";
    EXPECT_TRUE(host.HpSnapshot("h")[0].hp.empty());
}

TEST(WorldLogHp, AMemberKeepsEveryReport) {
    WorldLogMember member;
    const u64 epoch = WorldLogEpoch(7, 1);
    member.Take(
        BatchWorldLog("host", epoch, 6,
                      Entries(epoch, {Entry(1, WorldLogKind::Reset, 0, 0), Source(2, GuestA),
                                      Value(3, Dog, 49, 450), Source(4, GuestB),
                                      Value(5, Dog, 20, 450), Value(6, Wolf, 7, 484)}))[0],
        0);
    EXPECT_EQ(member.Hp(), (WorldLogHp{{GuestA, {Yharnam, {{Dog, {49, 450}}}}},
                                       {GuestB, {Yharnam, {{Dog, {20, 450}}, {Wolf, {7, 484}}}}}}));
    EXPECT_EQ(WorldLogHpReports(member.Hp()),
              (std::vector<std::pair<u32, EnemyHpReport>>{
                  {Yharnam, {GuestA, {{Dog, 49, 450}}}},
                  {Yharnam, {GuestB, {{Wolf, 7, 484}, {Dog, 20, 450}}}}}));
    member.Take(
        BatchWorldLog(
            "host", epoch, 9,
            Entries(epoch, {Source(7, GuestB), Entry(8, WorldLogKind::HpGone, 0, Wolf),
                            Entry(9, WorldLogKind::HpSourceGone, 0, static_cast<u32>(GuestA >> 32),
                                  static_cast<u32>(GuestA))}))[0],
        10);
    EXPECT_EQ(member.Hp(), (WorldLogHp{{GuestB, {Yharnam, {{Dog, {20, 450}}}}}}));
    member.Take(BatchWorldLog("host", WorldLogEpoch(7, 2), 1,
                              Entries(WorldLogEpoch(7, 2), {Entry(1, WorldLogKind::Reset, 0,
                                                                  WorldLogResetAfterWipe)}))[0],
                20);
    EXPECT_TRUE(member.Hp().empty()) << "the wipe's new log";
}

TEST(WorldLogHp, ASnapshotWaitsForTheHpParts) {
    const u64 epoch = WorldLogEpoch(7, 3);
    WorldLogMember member;
    member.Take(BatchWorldLog("host", epoch, 30, {})[0], 0);
    TakeOtherParts(member, epoch, 30, 10);
    EXPECT_FALSE(member.Fresh(10)) << "the HP part is not in yet";
    const auto whole = member.Take(
        WorldLogHpSnapshot{"host", epoch, 30, 0, 1, {{GuestA, {Yharnam, {{Dog, {49, 450}}}}}}}, 20);
    EXPECT_TRUE(whole.snapshot);
    EXPECT_EQ(member.Hp().at(GuestA).enemies.at(Dog), (std::pair<s32, s32>{49, 450}));
    // Entries go on from the snapshot: the source they are of comes first.
    member.Take(BatchWorldLog("host", epoch, 32,
                              Entries(epoch, {Source(31, GuestA), Value(32, Dog, 98, 450)}))[0],
                30);
    EXPECT_EQ(member.Hp().at(GuestA).enemies.at(Dog), (std::pair<s32, s32>{98, 450}));
}

TEST(WorldLogHp, TheHostsSnapshotIsInParts) {
    WorldLogHost host;
    const u64 epoch = WorldLogEpoch(7, 1);
    host.Start(epoch, false);
    std::vector<EnemyHp> many;
    for (u32 index = 0; index < 150; ++index) {
        many.push_back({0x10010000 + index, static_cast<s32>(index), 500});
    }
    host.ProposeHp(Hp("a", epoch, 1, GuestA, std::vector(many.begin(), many.begin() + 100)));
    host.ProposeHp(Hp("b", epoch, 1, GuestB, std::vector(many.begin() + 100, many.end())));
    const auto parts = host.HpSnapshot("h");
    ASSERT_EQ(parts.size(), 2u);
    WorldLogHp joined;
    for (const WorldLogHpSnapshot& part : parts) {
        EXPECT_EQ(part.parts, 2);
        EXPECT_LE(EncodeWorldLogHpSnapshot(part).size() + 5, 1400u);
        for (const auto& [source, report] : part.hp) {
            joined[source].map = report.map;
            joined[source].enemies.insert(report.enemies.begin(), report.enemies.end());
        }
    }
    EXPECT_EQ(joined, host.Hp());
}

// Rig hit3: each hit counted once. A guest's hits reach another guest's copy through the host's
// log as its report (cumulative damage), and SharedDamage takes each report's damage once, however
// often the same report comes.
TEST(WorldLogHp, EachHitCountsOnceThroughTheLog) {
    WorldLogHost host;
    WorldLogMember b;
    const u64 epoch = WorldLogEpoch(7, 1);
    const auto relay = [&](const std::vector<WorldLogEntry>& entries, s64 now) {
        for (const WorldLogBatch& batch : BatchWorldLog("h", host.Epoch(), host.Last(), entries)) {
            b.Take(batch, now);
        }
    };
    relay(host.Start(epoch, false), 0);
    SharedDamage damage;
    const u64 b_visit = GuestB;
    const auto reports = [&] {
        std::vector<EnemyHpReport> out;
        for (auto& [map, report] : WorldLogHpReports(b.Hp())) {
            if (report.visit != b_visit) {
                out.push_back(report);
            }
        }
        return out;
    };
    std::vector<EnemyCopy> copy{{Dog, 450, 450, 0}};
    damage.Update(b_visit, true, copy, reports());
    // A hits twice (49 each); its report goes out after each hit and again every 0.5 s.
    relay(host.ProposeHp(Hp("a", epoch, 1, GuestA, {{Dog, 49, 450}})).entries, 100);
    auto step = damage.Update(b_visit, true, copy, reports());
    ASSERT_EQ(step.writes.size(), 1u);
    EXPECT_EQ(step.writes[0].hp, 401);
    copy[0].hp = 401;
    relay(host.ProposeHp(Hp("a", epoch, 2, GuestA, {{Dog, 49, 450}})).entries, 600);
    EXPECT_TRUE(damage.Update(b_visit, true, copy, reports()).writes.empty()) << "counted once";
    relay(host.ProposeHp(Hp("a", epoch, 3, GuestA, {{Dog, 98, 450}})).entries, 700);
    step = damage.Update(b_visit, true, copy, reports());
    ASSERT_EQ(step.writes.size(), 1u);
    EXPECT_EQ(step.writes[0].hp, 352);
}

TEST(WorldLogHp, MembersConvergeOverABadLink) {
    std::mt19937 random{44};
    for (int round = 0; round < 20; ++round) {
        WorldLogHost host;
        std::vector<WorldLogMember> members(2);
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
        u64 epoch = WorldLogEpoch(9, 1);
        send(host.Start(epoch, false));
        std::vector<std::vector<EnemyHp>> reports(3);
        std::vector<u64> sources{GuestA, GuestB, 0x3333'0000'0000'0001};
        std::vector<u32> numbers(3);
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
        for (int tick = 0; tick < 150; ++tick) {
            now += 100;
            const size_t who = random() % 3;
            const u32 handle = 0x10 + random() % 10;
            auto& report = reports[who];
            const auto at = std::ranges::find(report, handle, &EnemyHp::handle);
            if (at == report.end()) {
                report.push_back({handle, 10, 500});
            } else if (random() % 5 == 0) {
                report.erase(at);
            } else {
                at->hp += 10;
            }
            if (random() % 30 == 0) {
                ++sources[who]; // a new visit
                report.clear();
            }
            if (tick == 100) {
                epoch = WorldLogEpoch(9, 2); // a wipe
                send(host.Start(epoch, true));
            }
            send(
                host.ProposeHp(Hp(std::to_string(who), epoch, ++numbers[who], sources[who], report))
                    .entries);
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
            EXPECT_EQ(member.Hp(), host.Hp()) << "round " << round;
        }
    }
}
