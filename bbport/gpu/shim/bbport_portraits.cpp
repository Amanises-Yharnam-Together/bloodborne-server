// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_portraits.h"

#include <algorithm>
#include <utility>

namespace BbPortraits {

int PlaceholderSlot(std::span<const std::uint8_t> level0) {
    if (level0.size() != std::size_t(Side) * Side * 4) {
        return -1;
    }
    for (int slot = 0; slot < SlotCount; ++slot) {
        const auto magic = Magic(slot);
        if (!std::equal(magic.begin(), magic.end(), level0.begin())) {
            continue;
        }
        for (std::size_t at = 4; at < level0.size(); at += 4) {
            if (!std::equal(magic.begin(), magic.end(), level0.begin() + std::ptrdiff_t(at))) {
                return -1;
            }
        }
        return slot;
    }
    return -1;
}

std::size_t MipOffset(int level) {
    std::size_t offset = 0;
    for (int m = 0, side = Side; m < level; ++m, side = std::max(side / 2, 1)) {
        offset += std::size_t(side) * std::size_t(side) * 4;
    }
    return offset;
}

std::vector<std::uint8_t> MipChain(std::span<const std::uint8_t> rgba, bool bgra, int levels) {
    std::vector<std::uint8_t> chain(MipOffset(levels), 0);
    if (rgba.size() != std::size_t(Side) * Side * 4 || levels < 1) {
        return chain;
    }
    std::copy(rgba.begin(), rgba.end(), chain.begin());
    for (int m = 1, side = Side / 2; m < levels; ++m, side = std::max(side / 2, 1)) {
        const int above = std::min(side * 2, Side);
        const std::uint8_t* from = chain.data() + MipOffset(m - 1);
        std::uint8_t* to = chain.data() + MipOffset(m);
        for (int y = 0; y < side; ++y) {
            for (int x = 0; x < side; ++x) {
                for (int c = 0; c < 4; ++c) {
                    int sum = 0;
                    for (int dy = 0; dy < 2; ++dy) {
                        for (int dx = 0; dx < 2; ++dx) {
                            const int sx = std::min(x * 2 + dx, above - 1);
                            const int sy = std::min(y * 2 + dy, above - 1);
                            sum += from[(std::size_t(sy) * above + sx) * 4 + c];
                        }
                    }
                    to[(std::size_t(y) * side + x) * 4 + c] = std::uint8_t((sum + 2) / 4);
                }
            }
        }
    }
    if (bgra) {
        for (std::size_t at = 0; at < chain.size(); at += 4) {
            std::swap(chain[at], chain[at + 2]);
        }
    }
    return chain;
}

} // namespace BbPortraits
