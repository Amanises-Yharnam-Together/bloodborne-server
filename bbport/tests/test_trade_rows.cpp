// SPDX-License-Identifier: GPL-2.0-or-later
// bbport_trade_rows.cpp: trading's Give rows run for every item, weapons too (play test
// 2026-10-18: Give on a weapon did nothing, the game's select only ran rows of usable items).
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "gpu/shim/bbport_trade_rows.h"

using namespace BbTradeRows;

// A game text as MakeText leaves it: the characters inline (capacity 7) or on the heap.
static std::vector<unsigned char> Text(const char16_t* chars, bool heap,
                                       const char16_t* cached = nullptr) {
    std::vector<unsigned char> text(0x40, 0);
    std::memcpy(text.data(), &cached, 8);
    std::size_t length = 0;
    while (chars[length]) {
        ++length;
    }
    std::memcpy(text.data() + 0x20, &length, 8);
    const std::uint64_t capacity = heap ? 15 : 7;
    std::memcpy(text.data() + 0x28, &capacity, 8);
    if (heap) {
        std::memcpy(text.data() + 0x10, &chars, 8);
    } else {
        assert(length < 8);
        std::memcpy(text.data() + 0x10, chars, (length + 1) * 2);
    }
    return text;
}

int main() {
    // The label as the game's own compare reads it.
    const char16_t* long_label = u"Give to Лисара";
    auto heap = Text(long_label, true);
    assert(LabelText(heap.data()) == long_label);
    auto inline_text = Text(u"Use", false);
    assert(Utf8(LabelText(inline_text.data())) == "Use");
    const char16_t* cached = u"Discard";
    auto with_cache = Text(u"Use", false, cached);
    assert(LabelText(with_cache.data()) == cached);
    assert(Utf8(u"Give to Лисара") == "Give to Лисара");
    assert(Utf8(u"\U0001F5E1 x") == "\xF0\x9F\x97\xA1 x");

    // The rows the commands of an item got.
    GiveRows rows;
    rows.AddItem("Give to Лисара");
    rows.AddItem("Give to Ada");
    rows.AddEchoes("Give Blood Echoes to Ada");
    assert(rows.Find(LabelText(heap.data())) == Row::GiveItem);
    assert(rows.Find(u"Give to Ada") == Row::GiveItem);
    assert(rows.Find(u"Give Blood Echoes to Ada") == Row::GiveEchoes);
    assert(rows.Find(u"Use") == Row::Game);
    assert(rows.Find(u"Give to Adam") == Row::Game);
    assert(rows.Find(nullptr) == Row::Game);
    rows.Clear();
    assert(rows.Find(u"Give to Ada") == Row::Game);

    // A weapon is never usable: its Give row runs anyway, unless the player can't change
    // equipment now (as the game's Discard); the game's rows keep the game's check.
    int usable_asked = 0, equip_asked = 0;
    const auto never_usable = [&] { ++usable_asked; return false; };
    const auto free_hands = [&] { ++equip_asked; return 0; };
    const auto busy = [&] { ++equip_asked; return EquipBusy; };
    assert(Allowed(Row::GiveItem, never_usable, free_hands));
    assert(!Allowed(Row::GiveItem, never_usable, busy));
    assert(Allowed(Row::GiveEchoes, never_usable, busy));
    assert(usable_asked == 0);
    assert(!Allowed(Row::Game, never_usable, free_hands));
    assert(Allowed(Row::Game, [] { return true; }, busy));
    assert(usable_asked == 1);

    // The patch: the 1.09 eboot's call, sent through the thunk to the check.
    std::vector<unsigned char> image(0x01BA2E00, 0x90);
    const unsigned char call[] = {0xE8, 0x7A, 0x21, 0xB4, 0xFF}; // call 0x016E4F00
    std::memcpy(image.data() + SelectCheckCall, call, sizeof call);
    unsigned char* thunk = image.data() + 0x01BA2DC0;
    const void* check = reinterpret_cast<const void*>(std::uintptr_t(0x123456789ABCDEF0ull));
    std::vector<unsigned char> other = image;
    other[SelectCheckCall + 1] ^= 1;
    assert(!PatchSelectCheck(other.data(), other.size(), other.data() + 0x01BA2DC0, check));
    assert(other[SelectCheckCall + 1] == (0x7A ^ 1));
    assert(!PatchSelectCheck(image.data(), SelectCheckCall + 2, thunk, check));
    assert(PatchSelectCheck(image.data(), image.size(), thunk, check));
    assert(image[SelectCheckCall] == 0xE8);
    std::int32_t rel = 0;
    std::memcpy(&rel, image.data() + SelectCheckCall + 1, 4);
    assert(SelectCheckCall + 5 + std::int64_t(rel) == 0x01BA2DC0);
    // mov rsi, rbp (the frame); movabs rax, check; jmp rax.
    const unsigned char head[] = {0x48, 0x89, 0xEE, 0x48, 0xB8};
    assert(std::memcmp(thunk, head, sizeof head) == 0);
    const void* target = nullptr;
    std::memcpy(&target, thunk + sizeof head, 8);
    assert(target == check);
    assert(thunk[13] == 0xFF && thunk[14] == 0xE0);

    std::puts("trade-rows-test: ok");
    return 0;
}
