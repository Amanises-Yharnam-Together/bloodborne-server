// SPDX-License-Identifier: GPL-2.0-or-later
// bbport co-op: the party portraits' textures. The party HUD (scripts/hud_patch.py) holds a
// placeholder bitmap per party list slot (CoopPartyFace0-2), all one colour (Magic); the texture
// cache knows a slot's texture by it and uploads the member's picture in its place
// (Core::Bloodborne::PartyPortraits, through bbcoop_portrait_slot). The status page's model target
// (ModelWidth x ModelHeight) is read back as the page samples it, for this player's own portrait.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace BbPortraits {

// A slot texture's side (Core::Bloodborne::PortraitSide, hud_patch.py PORTRAIT_PIXELS).
constexpr int Side = 64;
constexpr int SlotCount = 3;
// hud_patch.py portrait_magic: red and blue alike (the same in RGBA and BGRA), green the slot.
constexpr std::array<std::uint8_t, 4> Magic(int slot) {
    return {0x3D, static_cast<std::uint8_t>(0xC0 + slot), 0x3D, 0xFF};
}
// The slot whose placeholder `level0` (a Side square of 32-bit pixels) is; -1 for anything else.
int PlaceholderSlot(std::span<const std::uint8_t> level0);

// The status page's model, the character in its outfit (Core::Bloodborne::StatusModelWidth/Height;
// its face target, 560x256, is the bare head).
constexpr std::uint32_t ModelWidth = 560;
constexpr std::uint32_t ModelHeight = 768;

// Bytes before mip `level` of a Side square of 32-bit pixels.
std::size_t MipOffset(int level);
// `levels` mip levels of the picture (`rgba`, a Side square), each half the one before (box
// filtered), in BGRA order when `bgra`; clear (all zero) when `rgba` is empty.
std::vector<std::uint8_t> MipChain(std::span<const std::uint8_t> rgba, bool bgra, int levels);

} // namespace BbPortraits
