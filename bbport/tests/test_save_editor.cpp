// SPDX-License-Identifier: GPL-2.0-or-later
// The save editor page's pure parts: the request the game's stat setter takes, the level that
// follows from changed attributes, value limits and the item list from the save editor's files.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "gpu/shim/bbport_save_editor.h"

using namespace BbSaveEditor;

namespace {

void Put32(std::vector<std::uint8_t>& out, std::size_t at, std::uint32_t value) {
    std::memcpy(out.data() + at, &value, 4);
}
void Put64(std::vector<std::uint8_t>& out, std::size_t at, std::uint64_t value) {
    std::memcpy(out.data() + at, &value, 8);
}
void PutUtf16(std::vector<std::uint8_t>& out, const std::u16string& text) {
    for (const char16_t c : text) {
        out.push_back(static_cast<std::uint8_t>(c & 0xFF));
        out.push_back(static_cast<std::uint8_t>(c >> 8));
    }
    out.push_back(0);
    out.push_back(0);
}

// A version 2 FMG as the game's: one group per run of ids.
std::vector<std::uint8_t> Fmg(const std::map<std::uint32_t, std::u16string>& strings) {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> groups; // first, last
    for (const auto& [id, text] : strings) {
        if (!groups.empty() && groups.back().second + 1 == id) {
            groups.back().second = id;
        } else {
            groups.push_back({id, id});
        }
    }
    const std::size_t table = 0x28 + groups.size() * 16;
    std::vector<std::uint8_t> out(table + strings.size() * 8);
    out[0] = 0;
    out[2] = 2; // version 2
    Put32(out, 0x0C, static_cast<std::uint32_t>(groups.size()));
    Put32(out, 0x10, static_cast<std::uint32_t>(strings.size()));
    Put64(out, 0x18, table);
    std::uint32_t index = 0;
    for (std::size_t g = 0; g < groups.size(); ++g) {
        Put32(out, 0x28 + g * 16, index);
        Put32(out, 0x28 + g * 16 + 4, groups[g].first);
        Put32(out, 0x28 + g * 16 + 8, groups[g].second);
        index += groups[g].second - groups[g].first + 1;
    }
    std::size_t slot = 0;
    for (const auto& [id, text] : strings) {
        Put64(out, table + slot++ * 8, out.size());
        PutUtf16(out, text);
    }
    Put32(out, 0x04, static_cast<std::uint32_t>(out.size()));
    return out;
}

// A BND4 as the game's item.msgbnd (entries of 0x24 bytes: size, offset, id, name).
std::vector<std::uint8_t> Bnd4(const std::map<std::int32_t, std::vector<std::uint8_t>>& files) {
    std::vector<std::uint8_t> out(0x40 + files.size() * 0x24);
    std::memcpy(out.data(), "BND4", 4);
    Put32(out, 0x0C, static_cast<std::uint32_t>(files.size()));
    Put64(out, 0x20, 0x24);
    out[0x30] = 1;
    out[0x31] = 0x74;
    std::size_t entry = 0x40;
    for (const auto& [id, bytes] : files) {
        Put64(out, entry + 0x08, bytes.size());
        Put64(out, entry + 0x10, bytes.size());
        Put32(out, entry + 0x18, static_cast<std::uint32_t>(out.size()));
        Put32(out, entry + 0x1C, static_cast<std::uint32_t>(id));
        out.insert(out.end(), bytes.begin(), bytes.end());
        Put32(out, entry + 0x20, static_cast<std::uint32_t>(out.size()));
        PutUtf16(out, u"x.fmg");
        entry += 0x24;
    }
    return out;
}

const Item* Find(const std::vector<Item>& items, std::uint32_t category, std::uint32_t id) {
    for (const Item& item : items) {
        if (item.category == category && item.id == id) {
            return &item;
        }
    }
    return nullptr;
}

} // namespace

int main() {
    // The setter (0x1c13d00) reads level at +0x08 and the six attributes from +0x0c.
    const std::int32_t attributes[AttributeCount] = {10, 11, 12, 13, 14, 15};
    const auto request = StatsRequest(30, attributes);
    assert(request.size() == 0x24);
    std::int32_t value = 0;
    std::memcpy(&value, request.data() + 0x08, 4);
    assert(value == 30);
    std::memcpy(&value, request.data() + 0x0c, 4);
    assert(value == 10);
    std::memcpy(&value, request.data() + 0x20, 4);
    assert(value == 15);
    for (int i = 0; i < 8; ++i) {
        assert(request[i] == 0);
    }

    // Each attribute point is a level, as at the Doll.
    Character now{};
    now.level = 10;
    for (int i = 0; i < AttributeCount; ++i) {
        now.attributes[i] = 10;
    }
    std::int32_t raised[AttributeCount] = {12, 10, 10, 10, 10, 9};
    assert(LevelAfter(now, raised) == 11);
    std::int32_t lowered[AttributeCount] = {1, 1, 1, 1, 1, 1};
    assert(LevelAfter(now, lowered) == 1) ; // never below 1

    assert(ClampAttribute(0) == 1 && ClampAttribute(100) == 99 && ClampAttribute(50) == 50);
    assert(ClampEchoes(-5) == 0 && ClampEchoes(2'000'000'000) == 999'999'999);
    assert(ClampInsight(-1) == 0 && ClampInsight(120) == 99);

    // The PC save editor's lists: goods (four groups), weapons (both hands), armor.
    const char* goods = R"({"consumable": {"1000": {"item_name": "Blood Vial"}},
                            "material": {"7000": {"item_name": "Blood Stone Shard"}},
                            "key": {}, "chalice": {}})";
    const char* weapons = R"({"rightHand": {"2000000": {"item_name": "Chikage"}},
                              "leftHand": {"6000000": {"item_name": "Hunter Pistol"}}})";
    const char* armors = R"({"10000": {"item_name": "Grey Wolf Cap"}})";
    const auto items = ParseItems(goods, weapons, armors);
    assert(items.size() == 5);
    bool vial = false, chikage = false, cap = false;
    for (const Item& item : items) {
        vial |= item.category == GoodsCategory && item.id == 1000 && item.name == "Blood Vial";
        chikage |= item.category == WeaponCategory && item.id == 2000000 && item.name == "Chikage";
        cap |= item.category == ArmorCategory && item.id == 10000 && item.name == "Grey Wolf Cap";
    }
    assert(vial && chikage && cap);
    assert(ParseItems("not json", "", "").empty());

    // Kaniel 2026-10-10: weapons given upgraded. The game's EquipParamWeapon has a row per level:
    // Chikage 2000000 .. 2001000 (+10), the Hunter Pistol 6000000 .. 6001000.
    assert(UpgradedWeaponId(2000000, 0) == 2000000);
    assert(UpgradedWeaponId(2000000, 10) == 2001000);
    assert(UpgradedWeaponId(6000000, 3) == 6000300);
    assert(UpgradedWeaponId(2000000, 12) == 2001000 && UpgradedWeaponId(2000000, -1) == 2000000);
    assert(UpgradedWeaponId(2000700, 2) == 2000200) ; // a level is from the weapon's base

    // Kaniel 2026-10-15: no items in a release installed from a plain game folder (the list came
    // from the shadPS4 kit's save editor). The names come from the game's own text instead
    // (msg/<language>/item.msgbnd.dcx: FMG 10 goods, 11 weapons, 12 armor), nothing shipped.
    const auto bnd = Bnd4({
        {10, Fmg({{100, u"Hunter's Mark"}, {800, u"Point Forward"}, {1000, u"Blood Vial"},
                  {7111, u"Vermiculum (not needed)"}, {7112, u""}})},
        {11, Fmg({{1000000, u"Bare Fists"}, {2000000, u"Chikage"}, {2000100, u"Chikage +1"},
                  {2010000, u"Uncanny Chikage"}, {6000000, u"*"}})},
        {12, Fmg({{1000, u"NOT NEEDED"}, {10000, u"Grey Wolf Cap"}, {20000, u"en5304"},
                  {30000, u"Charred Hunter Garb \u00e9"}})},
        {20, Fmg({{100, u"Sacrifice all Blood Echoes"}})},
    });
    const auto game = ItemsFromGameText(bnd);
    assert(Find(game, GoodsCategory, 100) &&
           Find(game, GoodsCategory, 100)->name == "Hunter's Mark");
    assert(Find(game, GoodsCategory, 1000));
    assert(!Find(game, GoodsCategory, 800));  // gestures are no items
    assert(!Find(game, GoodsCategory, 7111)); // "(not needed)"
    assert(!Find(game, GoodsCategory, 7112)); // no name
    assert(Find(game, WeaponCategory, 2000000) && Find(game, WeaponCategory, 2010000));
    assert(!Find(game, WeaponCategory, 2000100)); // levels are given from the base
    assert(!Find(game, WeaponCategory, 1000000)); // bare fists
    assert(!Find(game, WeaponCategory, 6000000)); // "*"
    assert(Find(game, ArmorCategory, 10000) &&
           Find(game, ArmorCategory, 10000)->name == "Grey Wolf Cap");
    assert(!Find(game, ArmorCategory, 1000) && !Find(game, ArmorCategory, 20000));
    assert(Find(game, ArmorCategory, 30000)->name == "Charred Hunter Garb \xc3\xa9"); // UTF-8
    assert(game.size() == 6);
    assert(ItemsFromGameText(std::vector<std::uint8_t>{'B', 'N', 'D', '3'}).empty());
    assert(ItemsFromGameText(std::vector<std::uint8_t>{}).empty());

    // Kaniel 2026-10-15, trading any item: each item's name by its full item id, weapons at every
    // level, gems and runes from the menu text's FMG 31.
    const auto menu = Bnd4({{31, Fmg({{1000000, u"Tempering Blood Gemstone (1)"},
                                      {11000002, u"Clockwise Metamorphosis"}, {0, u"*"}})}});
    const auto names = ItemNamesFromGameText(bnd, menu);
    assert(names.at(0x40000000 | 1000) == "Blood Vial");
    assert(names.at(2000100) == "Chikage +1");
    assert(names.at(0x10000000 | 30000) == "Charred Hunter Garb \xc3\xa9");
    assert(names.at(0x80000000 | 1000000) == "Tempering Blood Gemstone (1)");
    assert(names.at(0x80000000 | 11000002) == "Clockwise Metamorphosis");
    assert(!names.contains(0x80000000) && !names.contains(6000000) &&
           !names.contains(0x10000000 | 1000));
    assert(ItemNamesFromGameText({}, {}).empty());
    std::puts("save editor: ok");
    return 0;
}
