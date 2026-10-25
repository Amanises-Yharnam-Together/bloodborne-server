// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <string>
#include <string_view>

#include "core/bloodborne_altar.h"
#include "core/bloodborne_party.h"

namespace Core::Bloodborne {

namespace {

// The chalice dungeons' map folders of the game's files (dvdroot_ps4/map/mapstudio/
// m29_<key>_<variant>_00, patch 1.09) and the highest layout file each has (m29_<key>_<variant>_
// <layout>.msb.dcx, every one from 00): the story variant 90 of each standard key, the root
// variants of each depth and area. The m29_0x_* folders are no chalice's.
struct ChaliceMaps {
    u8 key;
    u8 variant;
    u8 last_layout;
};
constexpr std::array<ChaliceMaps, 33> ChaliceMapFolders{{
    {10, 0, 99},  {10, 90, 1}, {20, 0, 99},  {20, 90, 1},  {21, 0, 99},  {21, 90, 0},  {30, 0, 99},
    {30, 20, 99}, {30, 90, 1}, {31, 0, 99},  {31, 20, 99}, {31, 90, 0},  {40, 10, 99}, {40, 11, 99},
    {40, 90, 1},  {42, 0, 99}, {42, 1, 99},  {42, 90, 1},  {50, 0, 99},  {50, 1, 99},  {50, 20, 99},
    {50, 21, 99}, {50, 90, 1}, {52, 0, 99},  {52, 1, 99},  {52, 20, 99}, {52, 21, 99}, {52, 90, 1},
    {53, 0, 99},  {53, 1, 99}, {53, 20, 99}, {53, 21, 99}, {53, 90, 1},
}};

constexpr std::array<u8, 10> StandardKeys{10, 20, 21, 30, 31, 40, 42, 50, 52, 53};
constexpr u32 StandardChannelBase = 2'000'000'000;
constexpr u8 StoryVariant = 90;

u32 BigEndianWord(std::span<const u8> record, size_t word) {
    u32 value = 0;
    for (size_t byte = 0; byte < 4; ++byte) {
        value = (value << 8) | record[word * 4 + byte];
    }
    return value;
}

// A ritual's record mirrors the server's FormData (bloodborne_backend.cpp, StandardDungeon) from
// its fourth byte on, every 32-bit word big endian: the rig's altar-record-capture (Pthumeru
// Chalice, 2026-10-24) logged 1D 0A 5A 00, the ChannelId, 0, then FormData's 1D 0A 5A 00, the goods
// (6109), its nine layers (id, code), "Hunter" twice, 02 00 30 00, the glyph's length and text
// ("chs2c"), and the server's date (00 18 37 DF) twice. The same for every standard chalice
// (StandardChalices there): each depth and area's goods and layer codes, 0xFF for none.
struct StandardChalice {
    u8 key;
    u32 goods;
    std::array<u8, 5> codes;
    size_t layers;
};
constexpr std::array<StandardChalice, 10> StandardChalices{{
    {10, 6109, {0xFF, 0x23, 0x32}, 3},
    {20, 6209, {0xFF, 0x24, 0x32, 0x3C}, 4},
    {21, 6219, {0xFF, 0x23, 0x33, 0x3C, 0x0A}, 5},
    {30, 6309, {0xFF, 0x24, 0x32, 0x3D}, 4},
    {31, 6319, {0xFF, 0x25, 0x33, 0x3C, 0x0A}, 5},
    {40, 6409, {0xFF, 0x24, 0x32, 0x3D, 0x0E}, 5},
    {42, 6429, {0xFF, 0x23, 0x34, 0x3C}, 4},
    {50, 6509, {0xFF, 0x24, 0x32, 0x3D, 0x0E}, 5},
    {52, 6529, {0xFF, 0x26, 0x34, 0x3C}, 4},
    {53, 6539, {0x4F, 0x27, 0x35, 0x43, 0x0F}, 5},
}};
constexpr std::array<u32, 5> StandardLayerIds{30, 35, 50, 60, 11};
constexpr std::string_view GlyphAlphabet = "23456789abcdefghijkmnpqrstuvwxyz";
// An empty altar's record: FF FF FF FF, 0, 80 00 00 00, FF up to the conductor, zeros from there
// but 02 00 30 00 at byte 124 (the capture's untouched altars).
constexpr size_t ConductorAt = 92;
constexpr size_t SettingsAt = 124;
constexpr size_t GlyphAt = 128;
constexpr size_t DatesAt = 164;
// Bytes from 625 (flags 5000-5999: "a chalice on it" 5001, the layers' switches) are the
// dungeon's state, not its name: a rebuild keeps them (the merge of 2026-10-24 left them right).
constexpr size_t StateAt = 625;

void PutBigEndian(AltarRecord& record, size_t at, u32 value) {
    for (size_t byte = 0; byte < 4; ++byte) {
        record[at + byte] = static_cast<u8>(value >> (24 - 8 * byte));
    }
}

// Text the way the record holds it: each 4-byte group reversed (a little endian word put big
// endian).
void PutText(AltarRecord& record, size_t at, std::string_view text, size_t size) {
    for (size_t index = 0; index < size; ++index) {
        const size_t source = (index & ~size_t{3}) + (3 - (index & 3));
        record[at + index] = source < text.size() ? static_cast<u8>(text[source]) : 0;
    }
}

} // namespace

std::optional<s32> AltarOfFlag(s32 flag) {
    if (flag < AltarFlagBase(0) || flag >= AltarFlagBase(RitualAltarCount)) {
        return std::nullopt;
    }
    const s32 altar = (flag - AltarFlagBase(0)) / 100000;
    return flag - AltarFlagBase(altar) < AltarRecordFlags ? std::optional{altar} : std::nullopt;
}

std::optional<AltarDungeon> AltarRecordDungeon(std::span<const u8> record) {
    if (record.size() < 4 || record[0] != 0x1D) {
        return std::nullopt;
    }
    return AltarDungeon{record[1], record[2], record[3]};
}

bool ChaliceDungeonExists(const AltarDungeon& dungeon) {
    return std::ranges::any_of(ChaliceMapFolders, [&dungeon](const ChaliceMaps& folder) {
        return folder.key == dungeon.key && folder.variant == dungeon.variant &&
               dungeon.layout <= folder.last_layout;
    });
}

AltarRecordKind ClassifyAltarRecord(std::span<const u8> record) {
    if (record.size() < 8) {
        return AltarRecordKind::Broken;
    }
    if (std::ranges::all_of(record.first(4), [](u8 byte) { return byte == 0xFF; })) {
        return AltarRecordKind::Empty;
    }
    const auto dungeon = AltarRecordDungeon(record);
    return dungeon && ChaliceDungeonExists(*dungeon) ? AltarRecordKind::Dungeon
                                                     : AltarRecordKind::Broken;
}

std::optional<u8> StandardChannelKey(std::span<const u8> record) {
    if (record.size() < 8) {
        return std::nullopt;
    }
    const u32 channel = BigEndianWord(record, 1);
    for (const u8 key : StandardKeys) {
        if (channel == StandardChannelBase + key) {
            return key;
        }
    }
    return std::nullopt;
}

std::vector<std::pair<s32, bool>> AltarRecordChanges(s32 altar, const AltarRecord& record) {
    std::vector<std::pair<s32, bool>> changes;
    changes.reserve(AltarRecordFlags);
    const s32 base = AltarFlagBase(altar);
    for (size_t byte = 0; byte < record.size(); ++byte) {
        for (u32 bit = 0; bit < 8; ++bit) {
            changes.emplace_back(base + static_cast<s32>(byte * 8 + bit),
                                 (record[byte] & (0x80 >> bit)) != 0);
        }
    }
    return changes;
}

bool SharesAltarRecord(const AltarShareFrame& frame) {
    return frame.playing && frame.map == HuntersDreamMap &&
           frame.since_place_ms >= AltarShareSettleMs &&
           frame.since_own_write_ms >= AltarOwnWriteQuietMs;
}

AltarRepair RepairAltarRecord(std::span<const u8> record) {
    AltarRepair repair;
    if (ClassifyAltarRecord(record) != AltarRecordKind::Broken) {
        return repair;
    }
    if (const auto key = StandardChannelKey(record)) {
        if (const auto rebuilt = StandardAltarRecord(*key, record)) {
            repair.action = AltarRepair::Action::Rebuild;
            repair.record = *rebuilt;
            return repair;
        }
    }
    repair.action = AltarRepair::Action::Clear;
    repair.record = EmptyAltarRecord();
    // The ritual took the chalice the record names (FormData's goods, byte 16).
    if (record.size() >= 20) {
        const u32 goods = BigEndianWord(record, 4);
        if (goods >= 6100 && goods <= 6599) {
            repair.chalice = goods;
        }
    }
    return repair;
}

std::optional<AltarRecord> StandardAltarRecord(u8 key, std::span<const u8> state) {
    const auto chalice = std::ranges::find(StandardChalices, key, &StandardChalice::key);
    if (chalice == StandardChalices.end()) {
        return std::nullopt;
    }
    AltarRecord record = EmptyAltarRecord();
    const u32 name = 0x1D000000u | u32{key} << 16 | u32{StoryVariant} << 8;
    PutBigEndian(record, 0, name);
    PutBigEndian(record, 4, StandardChannelBase + key);
    PutBigEndian(record, 8, 0);
    PutBigEndian(record, 12, name);
    PutBigEndian(record, 16, chalice->goods);
    for (size_t slot = 0; slot < 9; ++slot) {
        const bool layer = slot < chalice->layers;
        const u8 code = layer ? chalice->codes[slot] : 0xFF;
        PutBigEndian(record, 20 + slot * 8,
                     layer ? 9'900'000 + u32{key} * 1000 + StandardLayerIds[slot] : 0xFFFFFFFFu);
        PutBigEndian(record, 24 + slot * 8, code == 0xFF ? 0xFFFFFFFFu : code);
    }
    PutText(record, ConductorAt, "Hunter", 16);
    PutText(record, ConductorAt + 16, "Hunter", 16);
    PutBigEndian(record, GlyphAt, 6);
    const std::string glyph{'c', 'h', 's', GlyphAlphabet[key / 32], GlyphAlphabet[key % 32]};
    PutText(record, GlyphAt + 4, glyph, 8);
    PutBigEndian(record, DatesAt, 0x001837DF);
    PutBigEndian(record, DatesAt + 4, 0x001837DF);
    if (state.size() == AltarRecordBytes) {
        std::copy(state.begin() + StateAt, state.end(), record.begin() + StateAt);
    } else {
        // A ritual's own (Pthumeru's three layers): a chalice on it, its layers.
        for (const s32 flag : {5001, 5002, 5100, 5200, 5300, 5335, 5350, 5500}) {
            record[flag / 8] |= static_cast<u8>(0x80 >> (flag % 8));
        }
    }
    return record;
}

AltarRecord EmptyAltarRecord() {
    AltarRecord record{};
    std::fill(record.begin(), record.begin() + ConductorAt, u8{0xFF});
    PutBigEndian(record, 4, 0);
    PutBigEndian(record, 8, 0x80000000u);
    PutBigEndian(record, SettingsAt, 0x02003000u);
    return record;
}

} // namespace Core::Bloodborne
