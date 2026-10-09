// SPDX-License-Identifier: GPL-2.0-or-later
// The save editor page's pure parts: the request the game's stat setter takes, the level that
// follows from changed attributes, value limits and the item list from the save editor's files.
#include <cassert>
#include <cstdio>
#include <cstring>
#include "gpu/shim/bbport_save_editor.h"

using namespace BbSaveEditor;

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
    std::puts("save editor: ok");
    return 0;
}
