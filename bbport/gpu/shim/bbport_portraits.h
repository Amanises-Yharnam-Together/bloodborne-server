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

// A slot texture's side (hud_patch.py PORTRAIT_PIXELS; an older HUD's are 64) and the member's
// picture's (Core::Bloodborne::PortraitSide). Kaniel 2026-10-17 ("really poor quality"): 128, so
// that the picture is sharp on a 4K screen.
constexpr int Side = 128;
constexpr int PictureSide = 128;
constexpr int SlotCount = 3;
// The portrait's size on the HUD's 1920x1080 stage (hud_patch.py PORTRAIT_SIZE): the HUD is drawn
// at the output's size, so the screen shows it ShownPixels(output height) across.
constexpr int StageSize = 42;
int ShownPixels(int output_height);
// hud_patch.py portrait_magic: red and blue alike (the same in RGBA and BGRA), green the slot.
constexpr std::array<std::uint8_t, 4> Magic(int slot) {
    return {0x3D, static_cast<std::uint8_t>(0xC0 + slot), 0x3D, 0xFF};
}
// The slot whose placeholder `level0` (a `side` square of 32-bit pixels) is; -1 for anything else.
int PlaceholderSlot(std::span<const std::uint8_t> level0, int side = Side);

// The status page's model, the character in its outfit (Core::Bloodborne::StatusModelWidth/Height;
// its face target, 560x256, is the bare head).
constexpr std::uint32_t ModelWidth = 560;
constexpr std::uint32_t ModelHeight = 768;

// Bytes before mip `level` of a `side` square of 32-bit pixels.
std::size_t MipOffset(int level, int side = Side);
// `levels` mip levels of the picture (`rgba`, a `side` square), each half the one before (box
// filtered), in BGRA order when `bgra`; clear (all zero) when `rgba` is empty.
std::vector<std::uint8_t> MipChain(std::span<const std::uint8_t> rgba, bool bgra, int levels,
                                   int side = Side);
// The picture (`rgba`, a `from` square, premultiplied) for a `to` square texture the screen shows
// `shown` pixels across. The HUD's textures have one level, so the GPU cannot filter a picture
// shown smaller than its texture (single texels shimmer and the face looks coarse): it is averaged
// down to the size it shows at (area filter) and drawn back up to `to` (bilinear). Nothing for no
// picture.
std::vector<std::uint8_t> FitToScreen(std::span<const std::uint8_t> rgba, int from, int to,
                                      int shown);

} // namespace BbPortraits
