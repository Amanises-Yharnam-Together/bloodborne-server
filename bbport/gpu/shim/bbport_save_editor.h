// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the save editor page of the settings menu (Insert): the character's echoes, insight and
// attributes, and items given to it, changed in the running game (the save follows when the game
// saves). Offsets and functions of the 1.09 eboot:
//   GameDataMan (image+0x553B130) +0x08 -> the local PlayerGameData: +0x40 Vitality, +0x48
//   Endurance, +0x58 Strength, +0x60 Skill, +0x68 Bloodtinge, +0x70 Arcane, +0x80/+0x84 Insight,
//   +0x90 level, +0x94 echoes, +0x14/+0x18 HP (current, max).
//   0x1c13d00(request): sets level and attributes and recomputes HP, stamina and the rest (the
//   level-up commit's sequence); 0x131cb70(_, category, id, quantity): gives an item silently.
// Both run on the game's thread (RunPending, from its pad reads); reads work from any thread.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace BbSaveEditor {

enum Attribute { Vitality, Endurance, Strength, Skill, Bloodtinge, Arcane, AttributeCount };

struct Character {
    std::int32_t level = 0;
    std::int32_t echoes = 0;
    std::int32_t insight = 0;
    std::int32_t attributes[AttributeCount] = {};
    std::int32_t hp = 0;
    std::int32_t hp_max = 0;
};

// Item categories as the game encodes them in item ids' top bits.
constexpr std::uint32_t WeaponCategory = 0x00000000;
constexpr std::uint32_t ArmorCategory = 0x10000000;
constexpr std::uint32_t GoodsCategory = 0x40000000;

struct Item {
    std::uint32_t category = 0;
    std::uint32_t id = 0;
    std::string name;
};

inline std::int32_t ClampAttribute(std::int32_t v) {
    return v < 1 ? 1 : v > 99 ? 99 : v;
}
inline std::int32_t ClampEchoes(std::int64_t v) {
    return static_cast<std::int32_t>(v < 0 ? 0 : v > 999'999'999 ? 999'999'999 : v);
}
inline std::int32_t ClampInsight(std::int32_t v) {
    return v < 0 ? 0 : v > 99 ? 99 : v;
}

/// The level after changing the attributes: one level per point, as at the Doll; at least 1.
inline std::int32_t LevelAfter(const Character& now, const std::int32_t attributes[AttributeCount]) {
    std::int32_t level = now.level;
    for (int i = 0; i < AttributeCount; ++i) {
        level += attributes[i] - now.attributes[i];
    }
    return level < 1 ? 1 : level;
}

/// The request 0x1c13d00 reads: level at +0x08, the six attributes from +0x0c.
inline std::array<std::uint8_t, 0x24> StatsRequest(std::int32_t level,
                                                    const std::int32_t attributes[AttributeCount]) {
    std::array<std::uint8_t, 0x24> request{};
    const auto put = [&](std::size_t at, std::int32_t v) {
        for (int b = 0; b < 4; ++b) {
            request[at + b] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(v) >> (8 * b));
        }
    };
    put(0x08, level);
    for (int i = 0; i < AttributeCount; ++i) {
        put(0x0c + 4 * i, attributes[i]);
    }
    return request;
}

/// The PC save editor's item lists (resources/items.json, weapons.json, armors.json: goods in
/// groups, weapons by hand, armor by id), sorted by category and name; empty on bad input.
std::vector<Item> ParseItems(std::string_view goods, std::string_view weapons,
                             std::string_view armors);

/// Runtime (the GPU library): the loaded image, from bbgpu_patch_image.
void SetImage(unsigned char* image);
/// The local character, once it is in the world.
std::optional<Character> Read();
void SetEchoes(std::int32_t echoes);
void SetInsight(std::int32_t insight);
/// Queued for the game's thread: the attributes (and the level that follows).
void SetAttributes(const std::int32_t attributes[AttributeCount]);
/// Queued for the game's thread.
void GiveItem(std::uint32_t category, std::uint32_t id, std::int32_t quantity);
/// Runs what was queued (the game's thread).
void RunPending();
/// The item list, loaded once: BB_SAVE_EDITOR_DATA, else the shadPS4 kit's save editor beside
/// the port ("DATA/Bloodborne Save Editor/resources" up from the working folder). Empty if none.
const std::vector<Item>& Items();
/// Where the list was looked for (for the page's note).
const std::string& ItemsSource();

} // namespace BbSaveEditor
