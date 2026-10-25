// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <string_view>

#include <gtest/gtest.h>

#include "core/bloodborne_altar.h"
#include "core/bloodborne_party.h"

using namespace Core::Bloodborne;

namespace {

AltarRecord RecordStarting(std::initializer_list<u8> bytes) {
    AltarRecord record{};
    std::ranges::copy(bytes, record.begin());
    return record;
}

// Lisara's story Pthumeru Chalice record (1D 0A 5A 00, ChannelId 2000000010) and the one the
// team world held after the merges (Kaniel's playtest 2026-10-24).
const AltarRecord Pthumeru = RecordStarting({0x1D, 0x0A, 0x5A, 0x00, 0x77, 0x35, 0x94, 0x0A});
const AltarRecord Merged = RecordStarting({0x1D, 0x0F, 0xFF, 0xFF, 0x77, 0x35, 0x94, 0x0A});

// The rig's altar-record-capture (2026-10-24): the Pthumeru Chalice ritual's record (altar 1, the
// same after the ritual, in the dungeon and back in the Dream) and an untouched altar's; and
// Kaniel's altar 6 in the team world after the merges (team_world.pb, 2026-10-24).
constexpr std::string_view CapturedPthumeru =
    "1d0a5a007735940a000000001d0a5a00000017dd0097370effffffff00973713000000230097372200000032ffffff"
    "ff"
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff746e75"
    "48"
    "000072650000000000000000746e754800007265000000000000000002003000000000063273686300000063000000"
    "00"
    "0000000000000000000000000000000000000000001837df001837df00000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0060000000000000000000000008000000000000000000000000800000000000000000000000080000000100020000"
    "00"
    "0000000000000000000000000000000800000000000000000000000000000000000000000000000000000000000000"
    "00"
    "000000000000000000000000000000000000000000000000000000000000";
constexpr std::string_view CapturedEmpty =
    "ffffffff0000000080000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    "ff"
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000002003000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "000000000000000000000000000000000000000000000000000000000000";
constexpr std::string_view KanielsAltar6 =
    "1d0fffff7735940a80000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    "ff"
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff746e75"
    "48"
    "000072650000000000000000746e754800007265000000000000000002003000000000063273686300000063000000"
    "00"
    "0000000000000000000000000000000000000000001837df001837df00000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0060000000000000000000000008000000000000000000000000800000000000000000000000080000000100020000"
    "00"
    "0000000000000000000000000000000800000000000000000000000000000000000000000000000000000000000000"
    "00"
    "000000000000000000000000000000000000000000000000000000000000";

AltarRecord FromHex(std::string_view hex) {
    AltarRecord record{};
    for (size_t byte = 0; byte < record.size(); ++byte) {
        record[byte] =
            static_cast<u8>(std::stoi(std::string{hex.substr(byte * 2, 2)}, nullptr, 16));
    }
    return record;
}

} // namespace

TEST(BloodborneAltar, TheRigsRecordsAreTheBuiltOnes) {
    EXPECT_EQ(EmptyAltarRecord(), FromHex(CapturedEmpty));
    EXPECT_EQ(StandardAltarRecord(10), FromHex(CapturedPthumeru));
    EXPECT_EQ(ClassifyAltarRecord(FromHex(CapturedPthumeru)), AltarRecordKind::Dungeon);
    EXPECT_EQ(ClassifyAltarRecord(FromHex(CapturedEmpty)), AltarRecordKind::Empty);
}

// Kaniel's playtest 2026-10-24: his altar 6 (1D 0F FF FF) crashed every member's game; repaired
// it is the ritual's record again, its state kept.
TEST(BloodborneAltar, KanielsAltarIsTheRitualsAgain) {
    const AltarRecord broken = FromHex(KanielsAltar6);
    EXPECT_EQ(ClassifyAltarRecord(broken), AltarRecordKind::Broken);
    const AltarRepair repair = RepairAltarRecord(broken);
    ASSERT_EQ(repair.action, AltarRepair::Action::Rebuild);
    EXPECT_EQ(repair.record, FromHex(CapturedPthumeru));
}

TEST(BloodborneAltar, AnAltarsFlagsAreItsRecord) {
    EXPECT_EQ(AltarFlagBase(0), 94000000);
    EXPECT_EQ(AltarFlagBase(6), 94600000);
    EXPECT_EQ(AltarOfFlag(94600000), 6);
    EXPECT_EQ(AltarOfFlag(94605999), 6);
    EXPECT_EQ(AltarOfFlag(94006000), std::nullopt);
    EXPECT_EQ(AltarOfFlag(94700000), std::nullopt) << "the Dream has seven altars";
    EXPECT_EQ(AltarOfFlag(92900000), std::nullopt) << "the view is no altar's";
}

TEST(BloodborneAltar, TheFirstWordNamesTheDungeon) {
    EXPECT_EQ(AltarRecordDungeon(Pthumeru), (AltarDungeon{0x0A, 90, 0}));
    EXPECT_EQ(AltarRecordDungeon(RecordStarting({0xFF, 0xFF, 0xFF, 0xFF})), std::nullopt);
    EXPECT_EQ(AltarRecordDungeon(std::span<const u8>{}), std::nullopt);
}

// The chalice maps in the game's files (dvdroot_ps4/map/mapstudio, patch 1.09): each standard
// key's story variant 90 and its root variants, layouts 00-99 for a root dungeon.
TEST(BloodborneAltar, OnlyTheGamesDungeonsExist) {
    EXPECT_TRUE(ChaliceDungeonExists({10, 90, 0}));
    EXPECT_TRUE(ChaliceDungeonExists({10, 90, 1}));
    EXPECT_FALSE(ChaliceDungeonExists({10, 90, 2}));
    EXPECT_TRUE(ChaliceDungeonExists({10, 0, 99}));
    EXPECT_TRUE(ChaliceDungeonExists({40, 11, 37}));
    EXPECT_FALSE(ChaliceDungeonExists({40, 0, 0})) << "no m29_40_00_00";
    EXPECT_TRUE(ChaliceDungeonExists({53, 21, 5}));
    EXPECT_TRUE(ChaliceDungeonExists({21, 90, 0}));
    EXPECT_FALSE(ChaliceDungeonExists({21, 90, 1}));
    EXPECT_FALSE(ChaliceDungeonExists({15, 0xFF, 0xFF})) << "the merged record's";
    EXPECT_FALSE(ChaliceDungeonExists({1, 0, 0})) << "m29_01_*: no chalice's";
    EXPECT_FALSE(ChaliceDungeonExists({10, 0, 100}));
}

TEST(BloodborneAltar, ARecordIsEmptyADungeonOrBroken) {
    EXPECT_EQ(ClassifyAltarRecord(Pthumeru), AltarRecordKind::Dungeon);
    EXPECT_EQ(ClassifyAltarRecord(Merged), AltarRecordKind::Broken);
    EXPECT_EQ(ClassifyAltarRecord(RecordStarting({0xFF, 0xFF, 0xFF, 0xFF})),
              AltarRecordKind::Empty);
    EXPECT_EQ(ClassifyAltarRecord(RecordStarting({0x1D, 0x0A, 0x5A, 0x03})),
              AltarRecordKind::Broken);
    EXPECT_EQ(ClassifyAltarRecord(RecordStarting({0x00, 0x0A, 0x5A, 0x00})),
              AltarRecordKind::Broken);
    EXPECT_EQ(ClassifyAltarRecord(std::span<const u8>{Pthumeru}.first(3)), AltarRecordKind::Broken);
}

TEST(BloodborneAltar, AStandardChalicesChannelNamesItsKey) {
    EXPECT_EQ(StandardChannelKey(Pthumeru), 10);
    EXPECT_EQ(StandardChannelKey(Merged), 10) << "the second word was not merged";
    // 2000000053 (Isz), 2000000011 (no chalice), a stored dungeon's 17.
    EXPECT_EQ(StandardChannelKey(RecordStarting({0x1D, 0x35, 0x5A, 0, 0x77, 0x35, 0x94, 0x35})),
              53);
    EXPECT_EQ(StandardChannelKey(RecordStarting({0x1D, 0x0B, 0x5A, 0, 0x77, 0x35, 0x94, 0x0B})),
              std::nullopt);
    EXPECT_EQ(StandardChannelKey(RecordStarting({0x1D, 0x0A, 0, 0, 0, 0, 0, 17})), std::nullopt);
}

// A record goes whole: every flag of the altar, on and off, so the other altar ends exactly so.
TEST(BloodborneAltar, ARecordGoesWhole) {
    const auto changes = AltarRecordChanges(6, Pthumeru);
    ASSERT_EQ(changes.size(), static_cast<size_t>(AltarRecordFlags));
    EXPECT_EQ(changes.front(), (std::pair<s32, bool>{94600000, false}));
    // 0x1D = 00011101: flags 3, 4, 5, 7 on.
    for (s32 bit = 0; bit < 8; ++bit) {
        EXPECT_EQ(changes[bit].second, bit == 3 || bit == 4 || bit == 5 || bit == 7) << bit;
    }
    // 0x0A, the key: 12 and 14.
    EXPECT_EQ(changes[12], (std::pair<s32, bool>{94600012, true}));
    EXPECT_EQ(changes[13], (std::pair<s32, bool>{94600013, false}));
    EXPECT_EQ(changes.back(), (std::pair<s32, bool>{94605999, false}));
    EXPECT_TRUE(std::ranges::is_sorted(changes));
}

// Kaniel's playtest 2026-10-24: each load into a dungeon (and the team world's sync) changed the
// view back to an older record and that went to the team. Only a ritual's or a join's: in the
// Dream, playing, settled.
TEST(BloodborneAltar, OnlyARitualInTheDreamShares) {
    const AltarShareFrame ritual{.playing = true,
                                 .map = HuntersDreamMap,
                                 .since_place_ms = 40'000,
                                 .since_own_write_ms = 1'000'000};
    EXPECT_TRUE(SharesAltarRecord(ritual));
    AltarShareFrame frame = ritual;
    frame.playing = false;
    EXPECT_FALSE(SharesAltarRecord(frame)) << "loading";
    frame = ritual;
    frame.map = 0x1d000000;
    EXPECT_FALSE(SharesAltarRecord(frame)) << "in a chalice dungeon";
    frame = ritual;
    frame.since_place_ms = AltarShareSettleMs - 1;
    EXPECT_FALSE(SharesAltarRecord(frame)) << "just loaded into the Dream";
    frame = ritual;
    frame.since_own_write_ms = AltarOwnWriteQuietMs - 1;
    EXPECT_FALSE(SharesAltarRecord(frame)) << "this game just wrote the team's record";
}

// Kaniel 2026-10-24: the altars the merges broke are repaired as a save loads.
TEST(BloodborneAltar, ABrokenStandardRecordIsMadeAgain) {
    const AltarRepair repair = RepairAltarRecord(Merged);
    ASSERT_EQ(repair.action, AltarRepair::Action::Rebuild);
    EXPECT_EQ(std::optional{repair.record}, StandardAltarRecord(10, Merged));
    EXPECT_EQ(ClassifyAltarRecord(repair.record), AltarRecordKind::Dungeon);
    EXPECT_EQ(repair.chalice, std::nullopt);
}

TEST(BloodborneAltar, AnotherBrokenRecordIsCleared) {
    // A root dungeon's (a stored ChannelId) with the key merged; its chalice (FormData's goods,
    // byte 16: the Pthumeru Root Chalice 6100) goes back.
    const AltarRecord root = RecordStarting(
        {0x1D, 0x0F, 0x00, 0x07, 0, 0, 0, 17, 0, 0, 0, 0, 0x1D, 0x0A, 0, 7, 0, 0, 0x17, 0xD4});
    AltarRepair repair = RepairAltarRecord(root);
    ASSERT_EQ(repair.action, AltarRepair::Action::Clear);
    EXPECT_EQ(repair.record, EmptyAltarRecord());
    EXPECT_EQ(repair.chalice, 6100u);
    // Its goods merged too: nothing to give.
    repair =
        RepairAltarRecord(RecordStarting({0x1D, 0x0A, 0x00, 0xFF, 0,    0,    0,    17,   0,   0, 0,
                                          0,    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}));
    ASSERT_EQ(repair.action, AltarRepair::Action::Clear);
    EXPECT_EQ(repair.chalice, std::nullopt);
    // A standard chalice's ChannelId whose key is not one: the record says nothing to rebuild.
    repair = RepairAltarRecord(RecordStarting({0x1D, 0x0F, 0xFF, 0xFF, 0x77, 0x35, 0x94, 0x0F}));
    ASSERT_EQ(repair.action, AltarRepair::Action::Clear);
}

TEST(BloodborneAltar, GoodRecordsStayAsTheyAre) {
    EXPECT_EQ(RepairAltarRecord(Pthumeru).action, AltarRepair::Action::None);
    EXPECT_EQ(RepairAltarRecord(EmptyAltarRecord()).action, AltarRepair::Action::None);
    for (const u8 key : {10, 20, 21, 30, 31, 40, 42, 50, 52, 53}) {
        const auto record = StandardAltarRecord(key);
        ASSERT_TRUE(record) << int{key};
        EXPECT_EQ(ClassifyAltarRecord(*record), AltarRecordKind::Dungeon) << int{key};
        EXPECT_EQ(StandardChannelKey(*record), key);
        EXPECT_EQ(RepairAltarRecord(*record).action, AltarRepair::Action::None) << int{key};
    }
    EXPECT_EQ(StandardAltarRecord(11), std::nullopt);
    EXPECT_EQ(ClassifyAltarRecord(EmptyAltarRecord()), AltarRecordKind::Empty);
}
