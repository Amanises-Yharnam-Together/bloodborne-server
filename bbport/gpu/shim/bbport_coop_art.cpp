// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_coop_art.h"

#include <cstring>

#include "miniz.h"

namespace BbCoopArt {

std::optional<Atlas> Decode(const std::uint8_t* data, std::size_t size) {
    constexpr std::size_t Header = 10;
    if (data == nullptr || size < Header || std::memcmp(data, "BBCA", 4) != 0) {
        return std::nullopt;
    }
    const auto u16 = [&](std::size_t at) { return int(data[at] | data[at + 1] << 8); };
    Atlas atlas{.width = u16(4), .height = u16(6), .rects = {}, .rgba = {}};
    const int count = u16(8);
    const std::size_t pixels_at = Header + std::size_t(count) * 8;
    if (count != int(Piece::Count) || size < pixels_at || atlas.width == 0 || atlas.height == 0) {
        return std::nullopt;
    }
    for (int i = 0; i < count; ++i) {
        const std::size_t at = Header + std::size_t(i) * 8;
        const Rect rect{u16(at), u16(at + 2), u16(at + 4), u16(at + 6)};
        if (rect.width == 0 || rect.height == 0 || rect.x + rect.width > atlas.width ||
            rect.y + rect.height > atlas.height) {
            return std::nullopt;
        }
        atlas.rects.push_back(rect);
    }
    std::size_t length = 0;
    void* pixels = tinfl_decompress_mem_to_heap(data + pixels_at, size - pixels_at, &length,
                                                TINFL_FLAG_PARSE_ZLIB_HEADER);
    if (pixels == nullptr) {
        return std::nullopt;
    }
    if (length == std::size_t(atlas.width) * std::size_t(atlas.height) * 4) {
        const auto* bytes = static_cast<const std::uint8_t*>(pixels);
        atlas.rgba.assign(bytes, bytes + length);
    }
    mz_free(pixels);
    if (atlas.rgba.empty()) {
        return std::nullopt;
    }
    return atlas;
}

} // namespace BbCoopArt
