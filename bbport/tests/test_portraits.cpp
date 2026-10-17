// SPDX-License-Identifier: GPL-2.0-or-later
// bbport co-op: the party portraits' textures (shim/bbport_portraits.h): the party HUD's placeholder
// bitmaps are told by their colour, in either channel order, and the member's picture goes into
// every mip level, fitted to the size the screen shows it at.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

#include "gpu/shim/bbport_portraits.h"

using namespace BbPortraits;

namespace {

std::vector<std::uint8_t> Uniform(std::array<std::uint8_t, 4> pixel, int side = Side) {
    std::vector<std::uint8_t> bytes;
    for (int i = 0; i < side * side; ++i) {
        bytes.insert(bytes.end(), pixel.begin(), pixel.end());
    }
    return bytes;
}

} // namespace

int main() {
    // hud_patch.py portrait_magic: red and blue alike, green the slot.
    for (int slot = 0; slot < SlotCount; ++slot) {
        const auto magic = Magic(slot);
        assert(magic[0] == 0x3D && magic[1] == 0xC0 + slot && magic[2] == 0x3D && magic[3] == 0xFF);
        assert(PlaceholderSlot(Uniform(magic)) == slot);
    }
    // Anything else is no placeholder: another colour, one pixel off, another size.
    assert(PlaceholderSlot(Uniform({0x3D, 0xC0, 0x3E, 0xFF})) == -1);
    assert(PlaceholderSlot(Uniform({0x3D, 0xC3, 0x3D, 0xFF})) == -1);
    auto speck = Uniform(Magic(1));
    speck[speck.size() - 2] ^= 1;
    assert(PlaceholderSlot(speck) == -1);
    assert(PlaceholderSlot(Uniform(Magic(0), 32)) == -1);
    assert(PlaceholderSlot({}) == -1);
    // An older HUD's placeholders (64 px, a mod layer may still hold one) are told by their side.
    assert(PlaceholderSlot(Uniform(Magic(2), 64), 64) == 2);
    assert(PlaceholderSlot(Uniform(Magic(2), 64)) == -1);

    // Kaniel 2026-10-17 ("really poor quality"): the picture is 128 px (Core's PortraitSide) and the
    // HUD shows it 42 px tall on its 1080p stage (36 until Kaniel's "a little bit bigger"), at the
    // output's size: 84 px at 4K.
    static_assert(Side == 128 && PictureSide == 128 && StageSize == 42);
    assert(ShownPixels(2160) == 84 && ShownPixels(1080) == 42 && ShownPixels(1440) == 56);
    assert(ShownPixels(0) == StageSize);

    // The mip chain: level 0 is the picture, each level after it half as wide, averaged.
    std::vector<std::uint8_t> picture(std::size_t(Side) * Side * 4);
    for (std::size_t i = 0; i < picture.size(); i += 4) {
        const int x = int(i / 4) % Side;
        picture[i] = x % 2 ? 200 : 100; // columns alternate: their average at level 1
        picture[i + 1] = 10;
        picture[i + 2] = 20;
        picture[i + 3] = 255;
    }
    const auto chain = MipChain(picture, false, 3);
    assert(chain.size() == MipOffset(3));
    assert(MipOffset(1) == std::size_t(Side) * Side * 4);
    assert(MipOffset(2) == MipOffset(1) + std::size_t(Side / 2) * (Side / 2) * 4);
    assert(chain[0] == 100 && chain[4] == 200 && chain[1] == 10 && chain[2] == 20);
    assert(chain[MipOffset(1)] == 150 && chain[MipOffset(2)] == 150);
    // BGRA targets get red and blue swapped.
    const auto swapped = MipChain(picture, true, 1);
    assert(swapped[0] == 20 && swapped[2] == 100 && swapped[3] == 255);
    // No picture: clear.
    const auto clear = MipChain({}, false, 2);
    assert(clear.size() == MipOffset(2));
    for (const auto byte : clear) {
        assert(byte == 0);
    }
    // Fitted to the screen: the texture has one level, so a picture shown smaller than its side
    // is averaged down to the shown size first (no shimmer of single texels), then drawn back up.
    // Fine stripes (one texel each) shown at a quarter of the side are their average, grey.
    std::vector<std::uint8_t> stripes(std::size_t(Side) * Side * 4);
    for (std::size_t i = 0; i < stripes.size(); i += 4) {
        const bool on = (i / 4) % 2 == 1;
        stripes[i] = stripes[i + 1] = stripes[i + 2] = on ? 255 : 0;
        stripes[i + 3] = 255;
    }
    const auto fitted = FitToScreen(stripes, PictureSide, Side, Side / 4);
    assert(fitted.size() == stripes.size());
    for (std::size_t i = 0; i < fitted.size(); i += 4) {
        assert(std::abs(int(fitted[i]) - 128) <= 12 && fitted[i + 3] == 255);
    }
    // Shown at its side or larger: as it is.
    assert(FitToScreen(stripes, PictureSide, Side, Side) == stripes);
    assert(FitToScreen(stripes, PictureSide, Side, 2 * Side) == stripes);
    // A smooth picture keeps its shape: a left-to-right ramp stays one (each column brighter).
    std::vector<std::uint8_t> ramp(std::size_t(Side) * Side * 4);
    for (std::size_t i = 0; i < ramp.size(); i += 4) {
        ramp[i] = std::uint8_t((i / 4) % Side * 2);
        ramp[i + 3] = 255;
    }
    const auto smooth = FitToScreen(ramp, PictureSide, Side, 40);
    for (int x = 8; x + 8 < Side; ++x) {
        assert(smooth[std::size_t(x + 1) * 4] >= smooth[std::size_t(x) * 4]);
    }
    assert(smooth[8 * 4] < 40 && smooth[std::size_t(Side - 8) * 4] > 210);
    // Into an older HUD's 64 px texture: the picture is averaged to its side.
    const auto small = FitToScreen(stripes, PictureSide, 64, 200);
    assert(small.size() == std::size_t(64) * 64 * 4);
    assert(std::abs(int(small[0]) - 128) <= 2);
    // No picture: nothing.
    assert(FitToScreen({}, PictureSide, Side, 36).empty());
    std::puts("Portraits: PASS (placeholders by colour, mip chain, channel order, clear, fitted to the "
              "screen)");
    return 0;
}
