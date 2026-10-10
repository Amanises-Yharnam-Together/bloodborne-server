// SPDX-License-Identifier: GPL-2.0-or-later
// bbport co-op: the game's own HUD art for the party panel, read at runtime from the player's game
// files (menu/common.tpf.dcx); nothing of it is shipped. A DCX (zlib) holds a PS4 TPF whose
// textures are BC7 or BC1 blocks in the PS4's tiled order; the gauge strips the game's HP bar is
// drawn with sit in one of them (GaugeTexture, GaugeFrame/Fill/Regain).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace BbGameArt {

// The game's HP bar parts in MENU_Common_00091 (1024x512, BC7), in its pixels: the dark framed
// track, the red HP fill and the orange regain strip, drawn inside the track.
inline constexpr std::string_view GaugeTexture = "MENU_Common_00091";
struct PixelRect {
    int x0, y0, x1, y1; // x1/y1 one past the last pixel
};
inline constexpr PixelRect GaugeFrame{4, 4, 652, 21};
inline constexpr PixelRect GaugeFill{10, 26, 648, 35};
inline constexpr PixelRect GaugeRegain{10, 39, 648, 48};
// Pixels at each end of the frame kept unstretched (its end caps).
inline constexpr int GaugeFrameCap = 8;

// A DCX's payload: "DCX\0" ... "DCA\0" <u32 size> then a zlib stream. Nothing if it is not one.
std::optional<std::vector<std::uint8_t>> InflateDcx(std::span<const std::uint8_t> file);

struct TpfTexture {
    std::string name;
    std::uint32_t dxgi = 0; // 98 BC7, 71 BC1
    int width = 0, height = 0;
    std::uint32_t offset = 0, size = 0;
};
// A PS4 TPF's textures (UTF-16 names, entries of 0x24 bytes); nothing if it is not one.
std::optional<std::vector<TpfTexture>> ParseTpf(std::span<const std::uint8_t> tpf);

// The PS4's block order: 8x8-block tiles in row order, Morton (x, y interleaved) inside a tile.
// Returns the blocks in plain row order.
std::vector<std::uint8_t> DeswizzleBlocks(std::span<const std::uint8_t> data, int width_blocks,
                                          int height_blocks, int block_bytes);

// One BC7 block to 4x4 RGBA pixels (row by row).
std::array<std::uint8_t, 64> DecodeBc7Block(std::span<const std::uint8_t, 16> block);

// The RGBA pixels of `rect` of a BC7 texture whose blocks are in plain row order.
std::vector<std::uint8_t> DecodeBc7Rect(std::span<const std::uint8_t> blocks, int width,
                                        const PixelRect& rect);

// The gauge art: GaugeTexture's top rows (0 to `rows`) as RGBA, `width` wide; nothing when the
// file is missing or not as expected.
struct GaugeArt {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};
std::optional<GaugeArt> LoadGaugeArt(std::span<const std::uint8_t> common_tpf_dcx);

} // namespace BbGameArt
