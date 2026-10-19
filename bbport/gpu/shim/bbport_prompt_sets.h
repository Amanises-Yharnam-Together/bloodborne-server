// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the prompt sets file (launcher/bbport_icons.py write_prompt_sets, BB_PROMPT_SETS): the
// game's button pictures in every icon set, for icons that follow the device in use
// (bbport_prompts.cpp). Per texture area (a KG_* glyph, a button baked into a menu atlas): the
// texture's size, the byte offsets of its 16-byte BC7 blocks in the texture, and those blocks in
// each set. Little endian: "BBPRMPT1", u32 set count, u32 entry count; per set a u8 length and its
// name; per entry a u8 length and its name, u32 width, height, piece count, the offsets, then per
// set the pieces. No game, GPU or window here (prompt-sets-test).
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace BbPromptSets {

/// The sets, in the file's order.
enum Set : int { PlayStation = 0, Xbox = 1, Keyboard = 2, Switch = 3, SetCount = 4 };
inline constexpr std::array<std::string_view, SetCount> SetNames = {"playstation", "xbox", "keyboard",
                                                                     "switch"};

struct Entry {
    std::string name; ///< "KG_OK", or "<atlas>:<glyph>" for an atlas button
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint32_t> offsets;                       ///< of the 16-byte pieces
    std::array<std::vector<std::uint8_t>, SetCount> pieces{}; ///< offsets.size() * 16 each
    /// The bytes the pieces span from the first offset (their range in guest memory).
    [[nodiscard]] std::uint32_t Span() const {
        std::uint32_t lo = UINT32_MAX, hi = 0;
        for (const std::uint32_t o : offsets) {
            lo = std::min(lo, o);
            hi = std::max(hi, o + 16);
        }
        return offsets.empty() ? 0 : hi - lo;
    }
    [[nodiscard]] std::uint32_t First() const {
        std::uint32_t lo = UINT32_MAX;
        for (const std::uint32_t o : offsets) {
            lo = std::min(lo, o);
        }
        return offsets.empty() ? 0 : lo;
    }
    [[nodiscard]] bool IsGlyph() const {
        return name.rfind("KG_", 0) == 0;
    }
};

/// The entries of a prompt sets file; nothing when it is not one or not in the four sets' order.
inline std::optional<std::vector<Entry>> Parse(std::span<const std::uint8_t> data) {
    std::size_t at = 0;
    const auto take = [&](std::size_t n) -> const std::uint8_t* {
        if (data.size() - at < n || at > data.size()) {
            return nullptr;
        }
        const std::uint8_t* p = data.data() + at;
        at += n;
        return p;
    };
    const auto u32 = [&](std::uint32_t& out) {
        const std::uint8_t* p = take(4);
        if (p) {
            std::memcpy(&out, p, 4);
        }
        return p != nullptr;
    };
    const auto text = [&](std::string& out) {
        const std::uint8_t* n = take(1);
        const std::uint8_t* p = n ? take(*n) : nullptr;
        if (p) {
            out.assign(reinterpret_cast<const char*>(p), *n);
        }
        return p != nullptr;
    };
    const std::uint8_t* magic = take(8);
    std::uint32_t sets = 0, count = 0;
    if (!magic || std::memcmp(magic, "BBPRMPT1", 8) != 0 || !u32(sets) || !u32(count) ||
        sets != SetCount || count > 4096) {
        return std::nullopt;
    }
    for (int s = 0; s < SetCount; ++s) {
        std::string name;
        if (!text(name) || name != SetNames[s]) {
            return std::nullopt;
        }
    }
    std::vector<Entry> entries(count);
    for (Entry& e : entries) {
        std::uint32_t pieces = 0;
        if (!text(e.name) || !u32(e.width) || !u32(e.height) || !u32(pieces) || pieces == 0 ||
            pieces > 4096) {
            return std::nullopt;
        }
        e.offsets.resize(pieces);
        for (std::uint32_t& o : e.offsets) {
            if (!u32(o) || o % 16 != 0) {
                return std::nullopt;
            }
        }
        for (auto& set : e.pieces) {
            const std::uint8_t* p = take(std::size_t(pieces) * 16);
            if (!p) {
                return std::nullopt;
            }
            set.assign(p, p + std::size_t(pieces) * 16);
        }
    }
    if (at != data.size()) {
        return std::nullopt;
    }
    return entries;
}

/// The set a controller's buttons are named by: SDL's gamepad type (SDL_GamepadType's values:
/// 4 PS3, 5 PS4, 6 PS5, 7 Switch Pro, 8-10 Joy-Cons left / right / pair, 1 standard, 2 Xbox 360,
/// 3 Xbox One). Any other controller names its buttons as an Xbox pad does.
constexpr Set ForGamepadType(int type) {
    if (type >= 4 && type <= 6) {
        return PlayStation;
    }
    if (type >= 7 && type <= 10) {
        return Switch;
    }
    return Xbox;
}

/// The overlay's letter of a set (Keys::IconSet): 'p', 'x', 'k' or 's'.
constexpr char Letter(Set set) {
    return set == Xbox ? 'x' : set == Keyboard ? 'k' : set == Switch ? 's' : 'p';
}

} // namespace BbPromptSets
