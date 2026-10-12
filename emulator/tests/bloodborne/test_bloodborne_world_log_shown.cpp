// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The world log, step 2: the shown characters (docs/WORLD_SYNC.md, Stage C).

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

WorldLogEntry Reset(u64 seq, u32 map = 0) {
    return {seq, 0, map, WorldLogKind::Reset, 0};
}
WorldLogEntry Kill(u64 seq, u32 map, u32 handle) {
    return {seq, 0, map, WorldLogKind::Kill, handle};
}
WorldLogEntry ShownMap(u64 seq, u32 map) {
    return {seq, 0, map, WorldLogKind::ShownMap, map};
}
WorldLogEntry Shown(u64 seq, u32 map, u32 handle, u8 bits) {
    return {seq, 0, map, WorldLogKind::Shown, handle, bits};
}
WorldLogEntry Unshown(u64 seq, u32 map, u32 handle) {
    return {seq, 0, map, WorldLogKind::Unshown, handle};
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

// Iosefka's copy by her door switched off, a window NPC hidden, a woods enemy parked far away,
// one dead in the host's world.
const CharacterShown Iosefka{0x10014000, false, true, false, false};
const CharacterShown WindowNpc{0x10014005, true, false, false, false};
const CharacterShown Parked{0x10014066, true, true, true, false};
const CharacterShown DeadOne{0x1001400b, false, false, false, true};

} // namespace

TEST(WorldLogShown, BitsAreTheShownBoards) {
    for (u8 bits = 0; bits < 16; ++bits) {
        const CharacterShown character = ShownFromBits(0x10, bits);
        EXPECT_EQ(ShownBits(character), bits);
        EXPECT_EQ(EncodeShownCharacters(std::vector{character}).back(), bits);
    }
    EXPECT_EQ(ShownBits(Parked), 7);
    EXPECT_EQ(ShownBits(DeadOne), 8);
    const WorldLogShown shown{Yharnam, {{Parked.handle, 7}, {Iosefka.handle, 2}}};
    EXPECT_EQ(WorldLogShownCharacters(shown), (std::vector<CharacterShown>{Iosefka, Parked}));
}

// The bits ride in the kind byte (0x10 | bits); every entry is 13 bytes (kind, map, subject,
// value).
TEST(WorldLogShown, EntriesReadBack) {
    const u64 epoch = WorldLogEpoch(7, 1);
    const WorldLogBatch batch =
        Batch(epoch, 5,
              {Reset(1), ShownMap(2, Yharnam), Shown(3, Yharnam, Parked.handle, 7),
               Unshown(4, Yharnam, WindowNpc.handle), Kill(5, Yharnam, 9)});
    const std::vector<u8> payload = EncodeWorldLogBatch(batch);
    EXPECT_EQ(DecodeWorldLogBatch(payload), batch);
    const WorldLogBatch kills_only = Batch(epoch, 5, {Reset(1), Kill(2, Yharnam, 9)});
    EXPECT_EQ(payload.size() - EncodeWorldLogBatch(kills_only).size(), 3u * 13);
    // The kind bytes: name (1 + 4), epoch, last, count, first, then 13 bytes an entry.
    const size_t first_entry = 1 + 4 + 8 + 8 + 1 + 8;
    EXPECT_EQ(payload[first_entry + 13 * 1], 3);
    EXPECT_EQ(payload[first_entry + 13 * 2], 0x17);
    EXPECT_EQ(payload[first_entry + 13 * 3], 4);
}

TEST(WorldLogShown, ASnapshotFrameReadsBackAndFitsTheLink) {
    WorldLogShownSnapshot snapshot{"686f7374", WorldLogEpoch(7, 2), 9, 1, 2, {Yharnam, {}}};
    snapshot.shown.bits = {{Parked.handle, 7}, {Iosefka.handle, 2}};
    const auto payload = EncodeWorldLogShownSnapshot(snapshot);
    EXPECT_EQ(DecodeWorldLogShownSnapshot(payload), snapshot);
    for (size_t size = 0; size < payload.size(); ++size) {
        EXPECT_FALSE(DecodeWorldLogShownSnapshot(std::span{payload}.first(size)))
            << "cut at " << size;
    }
    std::vector<u8> longer = payload;
    longer.push_back(0);
    EXPECT_FALSE(DecodeWorldLogShownSnapshot(longer));
    WorldLogShownSnapshot past = snapshot;
    past.part = 2;
    EXPECT_FALSE(DecodeWorldLogShownSnapshot(EncodeWorldLogShownSnapshot(past)));

    WorldLogShownSnapshot full{std::string(64, 'a'), 1, 1, 0, 1, {Yharnam, {}}};
    for (u32 handle = 0; handle < WorldLogShownPerFrame; ++handle) {
        full.shown.bits[0x10010000 + handle] = 15;
    }
    EXPECT_LE(EncodeWorldLogShownSnapshot(full).size() + 5, 1400u);
    EXPECT_TRUE(IsWorldLogFrameType(WorldLogShownSnapshotType));
    EXPECT_EQ(DecodeDirectDamage(EncodeCoopFrame(WorldLogShownSnapshotType, payload)), std::nullopt)
        << "older builds drop it";
}

TEST(WorldLogShown, TheHostLogsWhatChangedOnItsMap) {
    WorldLogHost host;
    const u64 epoch = WorldLogEpoch(7, 1);
    host.Start(epoch, false);
    EXPECT_EQ(host.Show(Yharnam, std::vector{WindowNpc, Parked}),
              Entries(epoch, {ShownMap(2, Yharnam), Shown(3, Yharnam, WindowNpc.handle, 1),
                              Shown(4, Yharnam, Parked.handle, 7)}));
    EXPECT_TRUE(host.Show(Yharnam, std::vector{Parked, WindowNpc}).empty()) << "nothing changed";
    // The window NPC shown again, a new one dead, the parked one unloaded.
    CharacterShown window_shown = WindowNpc;
    window_shown.hidden = false;
    EXPECT_EQ(
        host.Show(Yharnam, std::vector{window_shown, DeadOne}),
        Entries(epoch, {Unshown(5, Yharnam, Parked.handle), Shown(6, Yharnam, WindowNpc.handle, 0),
                        Shown(7, Yharnam, DeadOne.handle, 8)}));
    // Another map: it starts over.
    EXPECT_EQ(host.Show(Ward, std::vector{Iosefka}),
              Entries(epoch, {ShownMap(8, Ward), Shown(9, Ward, Iosefka.handle, 2)}));
    EXPECT_EQ(host.Shown(), (WorldLogShown{Ward, {{Iosefka.handle, 2}}}));
    // A new log (a wipe): no kills stand (#53), the host's shown characters again.
    host.Propose({"host", epoch, 1, 1, {{Ward, {0x77}}}});
    const u64 wipe = WorldLogEpoch(7, 2);
    WorldLogEntry reset = Reset(1);
    reset.subject = WorldLogResetAfterWipe;
    EXPECT_EQ(host.Start(wipe, true),
              Entries(wipe, {reset, ShownMap(2, Ward), Shown(3, Ward, Iosefka.handle, 2)}));
    // Off the log and back: from a ShownMap again.
    host.ForgetShown();
    EXPECT_EQ(host.Show(Ward, std::vector{Iosefka}),
              Entries(wipe, {ShownMap(4, Ward), Shown(5, Ward, Iosefka.handle, 2)}));
}

// Rig smoke 2026-10-13: the host's log started over at the wipe while its own copy of 0x1001400b
// still lay dead (its wipe trip had not loaded the map yet); the new log showed it dead, and the
// second guest, back from its own wipe trip, killed its respawned copy as "dead in the host's
// world". A wipe respawns the map: the new log shows nothing dead.
TEST(WorldLogShown, AWipesLogShowsNothingDead) {
    WorldLogHost host;
    host.Start(WorldLogEpoch(7, 1), false);
    host.Show(Yharnam, std::vector{WindowNpc, DeadOne});
    const u64 wipe = WorldLogEpoch(7, 2);
    WorldLogEntry reset = Reset(1);
    reset.subject = WorldLogResetAfterWipe;
    EXPECT_EQ(host.Start(wipe, true),
              Entries(wipe, {reset, ShownMap(2, Yharnam), Shown(3, Yharnam, WindowNpc.handle, 1),
                             Shown(4, Yharnam, DeadOne.handle, 0)}));
    EXPECT_EQ(host.Shown(), (WorldLogShown{Yharnam, {{WindowNpc.handle, 1}, {DeadOne.handle, 0}}}));
    // A new session's log keeps what is dead in the host's world (its save).
    host.Show(Yharnam, std::vector{WindowNpc, DeadOne});
    const u64 session = WorldLogEpoch(8, 1);
    EXPECT_EQ(host.Start(session, false), Entries(session, {Reset(1), ShownMap(2, Yharnam),
                                                            Shown(3, Yharnam, WindowNpc.handle, 1),
                                                            Shown(4, Yharnam, DeadOne.handle, 8)}));
}

TEST(WorldLogShown, TheHostsSnapshotIsInParts) {
    WorldLogHost host;
    host.Start(WorldLogEpoch(7, 1), false);
    const auto empty = host.ShownSnapshot("h");
    ASSERT_EQ(empty.size(), 1u);
    EXPECT_EQ(empty[0].shown, WorldLogShown{});
    std::vector<CharacterShown> many;
    for (u32 index = 0; index < 500; ++index) {
        many.push_back(ShownFromBits(0x10010000 + index, index % 16));
    }
    host.Show(Yharnam, many);
    const auto parts = host.ShownSnapshot("h");
    ASSERT_EQ(parts.size(), 3u);
    WorldLogShown joined{Yharnam, {}};
    for (const WorldLogShownSnapshot& part : parts) {
        EXPECT_EQ(part.parts, 3);
        EXPECT_EQ(part.last, host.Last());
        EXPECT_EQ(part.shown.map, Yharnam);
        EXPECT_LE(part.shown.bits.size(), WorldLogShownPerFrame);
        joined.bits.insert(part.shown.bits.begin(), part.shown.bits.end());
    }
    EXPECT_EQ(joined, host.Shown());
}

TEST(WorldLogShown, AMemberFollowsTheEntries) {
    WorldLogMember member;
    const u64 epoch = WorldLogEpoch(7, 1);
    EXPECT_FALSE(member.ShownKnown());
    member.Take(Batch(epoch, 4,
                      {Reset(1), ShownMap(2, Yharnam), Shown(3, Yharnam, WindowNpc.handle, 1),
                       Shown(4, Yharnam, Parked.handle, 7)}),
                0);
    EXPECT_TRUE(member.ShownKnown()) << "the log from its start";
    EXPECT_EQ(member.Shown(),
              (WorldLogShown{Yharnam, {{WindowNpc.handle, 1}, {Parked.handle, 7}}}));
    member.Take(
        Batch(epoch, 6, {Unshown(5, Yharnam, WindowNpc.handle), Shown(6, Ward, Iosefka.handle, 2)}),
        10);
    EXPECT_EQ(member.Shown(), (WorldLogShown{Yharnam, {{Parked.handle, 7}}}))
        << "another map's entry is not the host's map";
    member.Take(Batch(epoch, 7, {ShownMap(7, Ward)}), 20);
    EXPECT_EQ(member.Shown(), (WorldLogShown{Ward, {}}));
    member.Take(Batch(epoch, 8, {Reset(8, Ward)}), 30);
    EXPECT_EQ(member.Shown(), WorldLogShown{});
    EXPECT_TRUE(member.ShownKnown());
    member.Reset();
    EXPECT_FALSE(member.ShownKnown());
}

// A snapshot is whole only with the shown characters' parts too, in either order.
TEST(WorldLogShown, ASnapshotWaitsForTheShownParts) {
    const u64 epoch = WorldLogEpoch(7, 3);
    const WorldLogSnapshot kills{"host", epoch, 40, 0, 1, {{Yharnam, {0x9}}}};
    const WorldLogShownSnapshot shown{"host", epoch, 40, 0, 1, {Yharnam, {{Parked.handle, 7}}}};
    for (const bool shown_first : {false, true}) {
        WorldLogMember member;
        member.Take(Batch(epoch, 41, {Shown(41, Yharnam, Iosefka.handle, 2)}), 0);
        member.Take(WorldLogMapStateSnapshot{"host", epoch, 40, {}}, 5);
        member.Take(WorldLogHpSnapshot{"host", epoch, 40, 0, 1, {}}, 5);
        member.Take(WorldLogFlagsSnapshot{"host", epoch, 40, {}}, 5);
        const auto first = shown_first ? member.Take(shown, 10) : member.Take(kills, 10);
        EXPECT_FALSE(first.snapshot);
        EXPECT_TRUE(member.Kills().empty());
        const auto second = shown_first ? member.Take(kills, 20) : member.Take(shown, 20);
        EXPECT_TRUE(second.snapshot);
        EXPECT_EQ(second.applied.size(), 1u);
        EXPECT_EQ(member.Kills(), (WorldLogKills{{Yharnam, {0x9}}}));
        EXPECT_EQ(member.Shown(),
                  (WorldLogShown{Yharnam, {{Parked.handle, 7}, {Iosefka.handle, 2}}}));
        EXPECT_TRUE(member.ShownKnown());
        EXPECT_TRUE(member.Fresh(20));
    }
}

TEST(WorldLogShown, MembersConvergeOverABadLink) {
    std::mt19937 random{1012};
    for (int round = 0; round < 20; ++round) {
        WorldLogHost host;
        std::vector<WorldLogMember> members(2);
        for (WorldLogMember& member : members) {
        }
        u64 epoch = WorldLogEpoch(9, 1);
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
        send(host.Start(epoch, false));
        std::vector<CharacterShown> world;
        u32 map = Yharnam;
        s64 now = 0;
        for (int tick = 0; tick < 150; ++tick) {
            now += 100;
            const u32 handle = 0x10 + random() % 12;
            const auto at = std::ranges::find(world, handle, &CharacterShown::handle);
            if (at != world.end() && random() % 4 == 0) {
                world.erase(at);
            } else if (at != world.end()) {
                *at = ShownFromBits(handle, random() % 16);
            } else {
                world.push_back(ShownFromBits(handle, random() % 16));
            }
            if (tick == 70) {
                map = Ward; // the host's load
            }
            if (tick == 110) {
                epoch = WorldLogEpoch(9, 2); // its wipe
                send(host.Start(epoch, true));
            }
            send(host.Show(map, world));
            std::shuffle(link.begin(), link.begin() + std::min<size_t>(link.size(), 3), random);
            while (!link.empty()) {
                auto [to, batch] = link.front();
                link.pop_front();
                members[to].Take(batch, now);
            }
            for (WorldLogMember& member : members) {
                if (member.SnapshotDue(now)) {
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
                }
            }
        }
        for (WorldLogMember& member : members) {
            member.Take(BatchWorldLog("h", host.Epoch(), host.Last(), {})[0], now);
            if (member.SnapshotDue(now + 10'000)) {
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
            }
            EXPECT_EQ(member.Shown(), host.Shown()) << "round " << round;
            EXPECT_TRUE(member.ShownKnown());
        }
    }
}

TEST(WorldLogShown, AShowReadsBackFromItsTraceRecord) {
    const std::vector<CharacterShown> characters{Iosefka, Parked};
    const auto payload = EncodeWorldLogShow(Ward, characters);
    const auto back = DecodeWorldLogShow(payload);
    ASSERT_TRUE(back);
    EXPECT_EQ(back->first, Ward);
    EXPECT_EQ(back->second, characters);
    EXPECT_FALSE(DecodeWorldLogShow(std::span{payload}.first(3)));
    EXPECT_FALSE(DecodeWorldLogShow(std::span{payload}.first(payload.size() - 1)));
}
