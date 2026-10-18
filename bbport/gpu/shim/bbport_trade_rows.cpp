// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: trading's Give rows and the item commands' select check (bbport_trade_rows.h).
#include "bbport_trade_rows.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace BbTradeRows {

const char16_t* LabelText(const unsigned char* text) {
    const char16_t* cached = nullptr;
    std::memcpy(&cached, text, sizeof cached);
    if (cached) {
        return cached;
    }
    std::uint64_t capacity = 0;
    std::memcpy(&capacity, text + 0x28, sizeof capacity);
    if (capacity >= 8) {
        const char16_t* chars = nullptr;
        std::memcpy(&chars, text + 0x10, sizeof chars);
        return chars;
    }
    return reinterpret_cast<const char16_t*>(text + 0x10);
}

std::string Utf8(const char16_t* text) {
    std::string out;
    for (const char16_t* c = text; c && *c; ++c) {
        std::uint32_t code = *c;
        if (code >= 0xD800 && code < 0xDC00 && c[1] >= 0xDC00 && c[1] < 0xE000) {
            code = 0x10000 + ((code - 0xD800) << 10) + (c[1] - 0xDC00);
            ++c;
        }
        if (code < 0x80) {
            out += char(code);
        } else if (code < 0x800) {
            out += char(0xC0 | (code >> 6));
            out += char(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += char(0xE0 | (code >> 12));
            out += char(0x80 | ((code >> 6) & 0x3F));
            out += char(0x80 | (code & 0x3F));
        } else {
            out += char(0xF0 | (code >> 18));
            out += char(0x80 | ((code >> 12) & 0x3F));
            out += char(0x80 | ((code >> 6) & 0x3F));
            out += char(0x80 | (code & 0x3F));
        }
    }
    return out;
}

void GiveRows::Clear() {
    items.clear();
    echoes.clear();
}

void GiveRows::AddItem(std::string label) {
    items.push_back(std::move(label));
}

void GiveRows::AddEchoes(std::string label) {
    echoes.push_back(std::move(label));
}

Row GiveRows::Find(const char16_t* label) const {
    if (!label || (items.empty() && echoes.empty())) {
        return Row::Game;
    }
    const std::string text = Utf8(label);
    if (std::ranges::find(items, text) != items.end()) {
        return Row::GiveItem;
    }
    if (std::ranges::find(echoes, text) != echoes.end()) {
        return Row::GiveEchoes;
    }
    return Row::Game;
}

bool PatchSelectCheck(unsigned char* image, std::uint64_t size, unsigned char* thunk,
                      const void* check) {
    const std::int32_t to_usable = std::int32_t(UsableCheck - (SelectCheckCall + 5));
    unsigned char call[5] = {0xE8};
    std::memcpy(call + 1, &to_usable, 4);
    if (SelectCheckCall + sizeof call > size ||
        std::memcmp(image + SelectCheckCall, call, sizeof call) != 0) {
        return false;
    }
    const std::int64_t to = std::int64_t(thunk - (image + SelectCheckCall + 5));
    if (to != std::int32_t(to)) {
        return false;
    }
    // mov rsi, rbp (the select's frame); movabs rax, check; jmp rax (the index stays in edi).
    const unsigned char head[] = {0x48, 0x89, 0xEE, 0x48, 0xB8};
    std::memcpy(thunk, head, sizeof head);
    std::memcpy(thunk + sizeof head, &check, 8);
    thunk[13] = 0xFF;
    thunk[14] = 0xE0;
    thunk[15] = 0xCC;
    const std::int32_t to32 = std::int32_t(to);
    std::memcpy(image + SelectCheckCall + 1, &to32, 4);
    return true;
}

} // namespace BbTradeRows
