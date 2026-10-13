// SPDX-License-Identifier: GPL-2.0-or-later
// bbport co-op: the party portraits' textures (shim/bbport_portraits.h): the party HUD's placeholder
// bitmaps are told by their colour, in either channel order, and the member's picture goes into
// every mip level.
#include <cassert>
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
    std::puts("Portraits: PASS (placeholders by colour, mip chain, channel order, clear)");
    return 0;
}
