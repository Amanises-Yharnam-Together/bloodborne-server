// SPDX-License-Identifier: GPL-2.0-or-later
// bbport co-op: reading the game's HUD art from its files (bbport_game_art.h), on made-up data:
// the PS4 block order, BC7 blocks, a TPF's entries and a DCX's zlib payload.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "gpu/shim/bbport_game_art.h"
#include "miniz.h"

using namespace BbGameArt;

namespace {

/// BC7 bits written least significant first, as the format reads them.
struct BitWriter {
    std::array<std::uint8_t, 16> block{};
    int at = 0;
    void Put(int value, int count) {
        for (int i = 0; i < count; ++i, ++at) {
            block[at >> 3] |= std::uint8_t(((value >> i) & 1) << (at & 7));
        }
    }
};

/// A mode 6 block (one subset, 7-bit RGBA endpoints with a p-bit each, 4-bit indices): black and
/// opaque at endpoint 0, white at endpoint 1, every pixel's index `index_of(pixel)`.
template <typename IndexOf>
std::array<std::uint8_t, 16> Mode6Block(IndexOf index_of) {
    BitWriter bits;
    bits.Put(1 << 6, 7);
    for (int channel = 0; channel < 3; ++channel) {
        bits.Put(0, 7);
        bits.Put(127, 7);
    }
    bits.Put(127, 7); // alpha 0
    bits.Put(127, 7); // alpha 1
    bits.Put(0, 1);   // p-bit 0: 0x00 for colour, 0xFE for alpha
    bits.Put(1, 1);   // p-bit 1: 0xFF
    for (int pixel = 0; pixel < 16; ++pixel) {
        bits.Put(index_of(pixel), pixel == 0 ? 3 : 4);
    }
    return bits.block;
}

void PutLe32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        bytes[at + i] = std::uint8_t(value >> (8 * i));
    }
}

void DecodesBc7() {
    const auto block = Mode6Block([](int pixel) { return pixel == 0 ? 0 : pixel == 1 ? 15 : 7; });
    const auto pixels = DecodeBc7Block(block);
    assert(pixels[0] == 0 && pixels[1] == 0 && pixels[2] == 0 && pixels[3] == 0xFE);
    assert(pixels[4] == 255 && pixels[5] == 255 && pixels[6] == 255 && pixels[7] == 255);
    // Index 7 of 15: weight 30 of 64 between 0 and 255.
    assert(pixels[8] == 120 && pixels[11] == 0xFE + 0);
    // A reserved mode (no bit set in the first byte's mode field) is transparent black.
    const std::array<std::uint8_t, 16> reserved{};
    for (const std::uint8_t value : DecodeBc7Block(reserved)) {
        assert(value == 0);
    }
}

void PutsBlocksBackInRowOrder() {
    // 16x8 one-byte blocks numbered in the PS4's order: two 8x8 tiles, Morton inside each.
    std::vector<std::uint8_t> tiled(16 * 8);
    for (std::size_t i = 0; i < tiled.size(); ++i) {
        tiled[i] = std::uint8_t(i);
    }
    const auto rows = DeswizzleBlocks(tiled, 16, 8, 1);
    assert(rows[0] == 0);           // (0, 0)
    assert(rows[1] == 1);           // (1, 0): x is the low bit
    assert(rows[16] == 2);          // (0, 1)
    assert(rows[17] == 3);          // (1, 1)
    assert(rows[2] == 4);           // (2, 0)
    assert(rows[7 * 16 + 7] == 63); // the first tile's last block
    assert(rows[8] == 64);          // the second tile starts at x 8
    // A texture narrower than a tile keeps only its own blocks; the tile's padding is skipped.
    const auto narrow = DeswizzleBlocks(tiled, 4, 2, 1);
    assert(narrow.size() == 8 && narrow[4] == 2 && narrow[7] == 7);
}

std::vector<std::uint8_t> OneTextureTpf(const char* name, std::uint32_t dxgi, int w, int h,
                                        const std::vector<std::uint8_t>& data) {
    const std::size_t name_at = 16 + 0x24, data_at = name_at + (std::strlen(name) + 1) * 2;
    std::vector<std::uint8_t> tpf(data_at + data.size());
    std::memcpy(tpf.data(), "TPF\0", 4);
    PutLe32(tpf, 8, 1);
    tpf[12] = 4; // PS4
    tpf[14] = 1; // UTF-16 names
    PutLe32(tpf, 16, std::uint32_t(data_at));
    PutLe32(tpf, 20, std::uint32_t(data.size()));
    tpf[16 + 12] = std::uint8_t(w);
    tpf[16 + 13] = std::uint8_t(w >> 8);
    tpf[16 + 14] = std::uint8_t(h);
    tpf[16 + 15] = std::uint8_t(h >> 8);
    PutLe32(tpf, 16 + 0x18, std::uint32_t(name_at));
    PutLe32(tpf, 16 + 0x20, dxgi);
    for (std::size_t i = 0; name[i]; ++i) {
        tpf[name_at + i * 2] = std::uint8_t(name[i]);
    }
    std::memcpy(tpf.data() + data_at, data.data(), data.size());
    return tpf;
}

std::vector<std::uint8_t> Dcx(const std::vector<std::uint8_t>& payload) {
    mz_ulong size = mz_compressBound(mz_ulong(payload.size()));
    std::vector<std::uint8_t> zlib(size);
    assert(mz_compress(zlib.data(), &size, payload.data(), mz_ulong(payload.size())) == MZ_OK);
    std::vector<std::uint8_t> file(0x48); // the header up to "DCA" and its size 8; then the stream
    std::memcpy(file.data(), "DCX\0", 4);
    std::memcpy(file.data() + 0x40, "DCA\0", 4);
    file[0x47] = 8;
    file.insert(file.end(), zlib.begin(), zlib.begin() + std::ptrdiff_t(size));
    return file;
}

void ReadsTheTpfInsideTheDcx() {
    const std::vector<std::uint8_t> data(32, 0xAB);
    const auto tpf = OneTextureTpf("MENU_Test", 98, 8, 4, data);
    const auto payload = InflateDcx(Dcx(tpf));
    assert(payload && *payload == tpf);
    const auto textures = ParseTpf(*payload);
    assert(textures && textures->size() == 1);
    const TpfTexture& texture = (*textures)[0];
    assert(texture.name == "MENU_Test" && texture.dxgi == 98);
    assert(texture.width == 8 && texture.height == 4 && texture.size == 32);
    assert((*payload)[texture.offset] == 0xAB);
    // Not a DCX or TPF, or one whose data runs past its end: nothing.
    assert(!InflateDcx(tpf));
    assert(!ParseTpf(data));
    auto cut = tpf;
    cut.resize(cut.size() - 1);
    assert(!ParseTpf(cut));
}

void FindsTheGaugeArt() {
    // A 1024x512 BC7 MENU_Common_00091 of white blocks, tiled: its top rows come back white.
    const auto white = Mode6Block([](int) { return 15; });
    std::vector<std::uint8_t> blocks;
    for (int i = 0; i < 256 * 128; ++i) {
        blocks.insert(blocks.end(), white.begin(), white.end());
    }
    const auto art = LoadGaugeArt(Dcx(OneTextureTpf("MENU_Common_00091", 98, 1024, 512, blocks)));
    assert(art && art->width == 656 && art->height == 48);
    assert(art->rgba.size() == std::size_t(656 * 48 * 4));
    // (A block's first pixel is its anchor: its index has a bit less and can't be 15.)
    assert(art->rgba[4] == 255 && art->rgba[art->rgba.size() - 1] == 255);
    // The parts sit in those rows.
    for (const PixelRect& part : {GaugeFrame, GaugeFill, GaugeRegain}) {
        assert(part.x0 >= 0 && part.x1 <= art->width && part.y0 >= 0 && part.y1 <= art->height);
    }
    // Another texture, or the same name in another format: nothing.
    assert(!LoadGaugeArt(Dcx(OneTextureTpf("MENU_Common_00090", 98, 1024, 512, blocks))));
    assert(!LoadGaugeArt(Dcx(OneTextureTpf("MENU_Common_00091", 71, 1024, 512, blocks))));
}

} // namespace

int main() {
    DecodesBc7();
    PutsBlocksBackInRowOrder();
    ReadsTheTpfInsideTheDcx();
    FindsTheGaugeArt();
    std::puts("Game art: PASS (BC7, PS4 block order, TPF in DCX, gauge texture)");
    return 0;
}
