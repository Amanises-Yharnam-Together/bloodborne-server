// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_portraits.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace BbPortraits {

namespace {

// `rgba` (a `from` square) averaged to a `to` square (`to` <= `from`): each pixel the mean of the
// source area it covers, partly covered texels by their share.
std::vector<float> AreaDown(std::span<const std::uint8_t> rgba, int from, int to) {
    std::vector<float> out(std::size_t(to) * to * 4, 0.0f);
    const float step = float(from) / float(to);
    for (int y = 0; y < to; ++y) {
        const float y0 = y * step, y1 = y0 + step;
        for (int x = 0; x < to; ++x) {
            const float x0 = x * step, x1 = x0 + step;
            float sum[4]{}, weight = 0.0f;
            for (int sy = int(y0); sy < std::min(int(std::ceil(y1)), from); ++sy) {
                const float wy = std::min(y1, float(sy + 1)) - std::max(y0, float(sy));
                for (int sx = int(x0); sx < std::min(int(std::ceil(x1)), from); ++sx) {
                    const float w = wy * (std::min(x1, float(sx + 1)) - std::max(x0, float(sx)));
                    const std::uint8_t* p = &rgba[(std::size_t(sy) * from + sx) * 4];
                    for (int c = 0; c < 4; ++c) {
                        sum[c] += w * p[c];
                    }
                    weight += w;
                }
            }
            for (int c = 0; c < 4; ++c) {
                out[(std::size_t(y) * to + x) * 4 + c] = weight > 0.0f ? sum[c] / weight : 0.0f;
            }
        }
    }
    return out;
}

} // namespace

int ShownPixels(int output_height) {
    return output_height > 0 ? int(std::lround(double(StageSize) * output_height / 1080.0))
                             : StageSize;
}

std::vector<std::uint8_t> FitToScreen(std::span<const std::uint8_t> rgba, int from, int to,
                                      int shown) {
    if (from < 1 || to < 1 || rgba.size() != std::size_t(from) * from * 4) {
        return {};
    }
    if (shown >= to && from == to) {
        return {rgba.begin(), rgba.end()};
    }
    // Down to what shows (never more than the texture holds, never less than a pixel).
    const int fine = std::clamp(std::min(shown, to), 1, from);
    const auto small = AreaDown(rgba, from, fine);
    std::vector<std::uint8_t> out(std::size_t(to) * to * 4);
    if (fine == to) {
        for (std::size_t i = 0; i < out.size(); ++i) {
            out[i] = std::uint8_t(std::clamp(std::lround(small[i]), 0L, 255L));
        }
        return out;
    }
    // Back up to the texture's side, bilinear between the small picture's pixel centres.
    const float scale = float(fine) / float(to);
    for (int y = 0; y < to; ++y) {
        const float fy = std::clamp((y + 0.5f) * scale - 0.5f, 0.0f, float(fine - 1));
        const int y0 = int(fy), y1 = std::min(y0 + 1, fine - 1);
        const float ty = fy - y0;
        for (int x = 0; x < to; ++x) {
            const float fx = std::clamp((x + 0.5f) * scale - 0.5f, 0.0f, float(fine - 1));
            const int x0 = int(fx), x1 = std::min(x0 + 1, fine - 1);
            const float tx = fx - x0;
            for (int c = 0; c < 4; ++c) {
                const auto at = [&](int px, int py) {
                    return small[(std::size_t(py) * fine + px) * 4 + c];
                };
                const float top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * tx;
                const float bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * tx;
                out[(std::size_t(y) * to + x) * 4 + c] =
                    std::uint8_t(std::clamp(std::lround(top + (bottom - top) * ty), 0L, 255L));
            }
        }
    }
    return out;
}

int PlaceholderSlot(std::span<const std::uint8_t> level0, int side) {
    if (level0.size() != std::size_t(side) * side * 4) {
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

std::size_t MipOffset(int level, int side) {
    std::size_t offset = 0;
    for (int m = 0; m < level; ++m, side = std::max(side / 2, 1)) {
        offset += std::size_t(side) * std::size_t(side) * 4;
    }
    return offset;
}

std::vector<std::uint8_t> MipChain(std::span<const std::uint8_t> rgba, bool bgra, int levels,
                                   int side0) {
    std::vector<std::uint8_t> chain(MipOffset(levels, side0), 0);
    if (rgba.size() != std::size_t(side0) * side0 * 4 || levels < 1) {
        return chain;
    }
    std::copy(rgba.begin(), rgba.end(), chain.begin());
    for (int m = 1, side = std::max(side0 / 2, 1); m < levels; ++m, side = std::max(side / 2, 1)) {
        const int above = std::min(side * 2, side0);
        const std::uint8_t* from = chain.data() + MipOffset(m - 1, side0);
        std::uint8_t* to = chain.data() + MipOffset(m, side0);
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
