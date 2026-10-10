// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_game_art.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include "miniz.h"

namespace BbGameArt {

namespace {

std::uint32_t Le32(std::span<const std::uint8_t> bytes, std::size_t at) {
    return std::uint32_t(bytes[at]) | std::uint32_t(bytes[at + 1]) << 8 |
           std::uint32_t(bytes[at + 2]) << 16 | std::uint32_t(bytes[at + 3]) << 24;
}

std::uint16_t Le16(std::span<const std::uint8_t> bytes, std::size_t at) {
    return std::uint16_t(bytes[at] | bytes[at + 1] << 8);
}

// BC7 tables (the format's specification): two- and three-subset partitions, anchor indices and
// interpolation weights.
constexpr std::uint16_t Partitions2[64] = {
    0xCCCC, 0x8888, 0xEEEE, 0xECC8, 0xC880, 0xFEEC, 0xFEC8, 0xEC80, 0xC800, 0xFFEC, 0xFE80,
    0xE800, 0xFFE8, 0xFF00, 0xFFF0, 0xF000, 0xF710, 0x008E, 0x7100, 0x08CE, 0x008C, 0x7310,
    0x3100, 0x8CCE, 0x088C, 0x3110, 0x6666, 0x366C, 0x17E8, 0x0FF0, 0x718E, 0x399C, 0xAAAA,
    0xF0F0, 0x5A5A, 0x33CC, 0x3C3C, 0x55AA, 0x9696, 0xA55A, 0x73CE, 0x13C8, 0x324C, 0x3BDC,
    0x6996, 0xC33C, 0x9966, 0x0660, 0x0272, 0x04E4, 0x4E40, 0x2720, 0xC936, 0x936C, 0x39C6,
    0x639C, 0x9336, 0x9CC6, 0x817E, 0xE718, 0xCCF0, 0x0FCC, 0x7744, 0xEE22};
constexpr const char* Partitions3[64] = {
    "0011001102210222", "0001001122112221", "0000200122112211", "0222002200110111",
    "0000000011221122", "0011001100220022", "0022002211111111", "0011001122112211",
    "0000000011112222", "0000111111112222", "0000111122222222", "0012001200120012",
    "0112011201120112", "0122012201220122", "0011011211221222", "0011200122002220",
    "0001001101120112", "0111001120012200", "0000112211221122", "0022002200221111",
    "0111011102220222", "0001000122212221", "0000001101220122", "0000110011222120",
    "0122001200110000", "0012001211222222", "0110122112210110", "0000011012211221",
    "0022110211020022", "0110011020022222", "0011012201220011", "0000200022112221",
    "0000000211221222", "0222002200120011", "0011001200220222", "0120012001200120",
    "0000111122220000", "0120120120120120", "0120201212010120", "0011220011220011",
    "0011112222000011", "0101010122222222", "0000000021212121", "0022112200221122",
    "0022001100220011", "0220122102201221", "0101222222220101", "0000212121212121",
    "0101010101012222", "0222011102220111", "0002111200021112", "0000211221122112",
    "0222011101110222", "0002111211120002", "0110011001102222", "0000000021122112",
    "0110011022222222", "0022001100110022", "0022112211220022", "0000000000002112",
    "0002000100020001", "0222122202221222", "0101222222222222", "0111201122012220"};
constexpr std::uint8_t Anchor2[64] = {15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
                                      15, 15, 15, 15, 2,  8,  2,  2,  8,  8,  15, 2,  8,
                                      2,  2,  8,  8,  2,  2,  15, 15, 6,  8,  2,  8,  15,
                                      15, 2,  8,  2,  2,  2,  15, 15, 6,  6,  2,  6,  8,
                                      15, 15, 2,  2,  15, 15, 15, 15, 15, 2,  2,  15};
constexpr std::uint8_t Anchor3a[64] = {3,  3,  15, 15, 8,  3,  15, 15, 8,  8,  6,  6,  6,
                                       5,  3,  3,  3,  3,  8,  15, 3,  3,  6,  10, 5,  8,
                                       8,  6,  8,  5,  15, 15, 8,  15, 3,  5,  6,  10, 8,
                                       15, 15, 3,  15, 5,  15, 15, 15, 15, 3,  15, 5,  5,
                                       5,  8,  5,  10, 5,  10, 8,  13, 15, 12, 3,  3};
constexpr std::uint8_t Anchor3b[64] = {15, 8,  8,  3,  15, 15, 3,  8,  15, 15, 15, 15, 15,
                                       15, 15, 8,  15, 8,  15, 3,  15, 8,  15, 8,  3,  15,
                                       6,  10, 15, 15, 10, 8,  15, 3,  15, 10, 10, 8,  9,
                                       10, 6,  15, 8,  15, 3,  6,  6,  8,  15, 3,  15, 15,
                                       15, 15, 15, 15, 15, 15, 15, 15, 3,  15, 15, 8};
constexpr std::uint8_t Weights2[4] = {0, 21, 43, 64};
constexpr std::uint8_t Weights3[8] = {0, 9, 18, 27, 37, 46, 55, 64};
constexpr std::uint8_t Weights4[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};

const std::uint8_t* WeightsFor(int bits) {
    return bits == 2 ? Weights2 : bits == 3 ? Weights3 : Weights4;
}

struct Bc7Mode {
    int subsets, partition_bits, rotation_bits, selector_bits, color_bits, alpha_bits;
    bool endpoint_pbits, shared_pbits;
    int index_bits, index2_bits;
};
constexpr Bc7Mode Modes[8] = {
    {3, 4, 0, 0, 4, 0, true, false, 3, 0}, {2, 6, 0, 0, 6, 0, false, true, 3, 0},
    {3, 6, 0, 0, 5, 0, false, false, 2, 0}, {2, 6, 0, 0, 7, 0, true, false, 2, 0},
    {1, 0, 2, 1, 5, 6, false, false, 2, 3}, {1, 0, 2, 0, 7, 8, false, false, 2, 2},
    {1, 0, 0, 0, 7, 7, true, false, 4, 0},  {2, 6, 0, 0, 5, 5, true, false, 2, 0}};

class Bits {
public:
    explicit Bits(std::span<const std::uint8_t, 16> block) : block{block} {}
    int Take(int count) {
        int value = 0;
        for (int i = 0; i < count; ++i, ++at) {
            value |= ((block[at >> 3] >> (at & 7)) & 1) << i;
        }
        return value;
    }

private:
    std::span<const std::uint8_t, 16> block;
    int at = 0;
};

int SubsetOf(int subsets, int partition, int pixel) {
    if (subsets == 1) {
        return 0;
    }
    if (subsets == 2) {
        return (Partitions2[partition] >> pixel) & 1;
    }
    return Partitions3[partition][pixel] - '0';
}

int AnchorOf(int subsets, int partition, int subset) {
    if (subset == 0) {
        return 0;
    }
    if (subsets == 2) {
        return Anchor2[partition];
    }
    return subset == 1 ? Anchor3a[partition] : Anchor3b[partition];
}

} // namespace

std::optional<std::vector<std::uint8_t>> InflateDcx(std::span<const std::uint8_t> file) {
    if (file.size() < 16 || std::memcmp(file.data(), "DCX\0", 4) != 0) {
        return std::nullopt;
    }
    static constexpr std::uint8_t Dca[] = {'D', 'C', 'A', 0};
    const auto dca = std::search(file.begin(), file.end(), std::begin(Dca), std::end(Dca));
    if (dca == file.end() || std::size_t(file.end() - dca) < 8) {
        return std::nullopt;
    }
    const std::size_t start = std::size_t(dca - file.begin()) + 8;
    std::size_t size = 0;
    void* out = tinfl_decompress_mem_to_heap(file.data() + start, file.size() - start, &size,
                                             TINFL_FLAG_PARSE_ZLIB_HEADER);
    if (out == nullptr) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> payload(static_cast<std::uint8_t*>(out),
                                      static_cast<std::uint8_t*>(out) + size);
    mz_free(out);
    return payload;
}

std::optional<std::vector<TpfTexture>> ParseTpf(std::span<const std::uint8_t> tpf) {
    if (tpf.size() < 16 || std::memcmp(tpf.data(), "TPF\0", 4) != 0) {
        return std::nullopt;
    }
    const std::uint32_t count = Le32(tpf, 8);
    if (count > 4096 || 16 + std::size_t(count) * 0x24 > tpf.size()) {
        return std::nullopt;
    }
    std::vector<TpfTexture> textures;
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::size_t entry = 16 + std::size_t(index) * 0x24;
        TpfTexture texture{.name = {},
                           .dxgi = Le32(tpf, entry + 0x20),
                           .width = Le16(tpf, entry + 12),
                           .height = Le16(tpf, entry + 14),
                           .offset = Le32(tpf, entry),
                           .size = Le32(tpf, entry + 4)};
        if (std::size_t(texture.offset) + texture.size > tpf.size()) {
            return std::nullopt;
        }
        for (std::size_t at = Le32(tpf, entry + 0x18); at + 1 < tpf.size(); at += 2) {
            const std::uint16_t unit = Le16(tpf, at);
            if (unit == 0) {
                break;
            }
            texture.name += unit < 0x80 ? char(unit) : '?';
        }
        textures.push_back(std::move(texture));
    }
    return textures;
}

std::vector<std::uint8_t> DeswizzleBlocks(std::span<const std::uint8_t> data, int width_blocks,
                                          int height_blocks, int block_bytes) {
    std::vector<std::uint8_t> out(std::size_t(width_blocks) * height_blocks * block_bytes);
    const int tiles_x = (width_blocks + 7) / 8, tiles_y = (height_blocks + 7) / 8;
    std::size_t source = 0;
    for (int tile_y = 0; tile_y < tiles_y; ++tile_y) {
        for (int tile_x = 0; tile_x < tiles_x; ++tile_x) {
            for (int i = 0; i < 64; ++i, source += block_bytes) {
                int x = 0, y = 0;
                for (int bit = 0; bit < 3; ++bit) {
                    x |= ((i >> (2 * bit)) & 1) << bit;
                    y |= ((i >> (2 * bit + 1)) & 1) << bit;
                }
                x += tile_x * 8;
                y += tile_y * 8;
                if (x < width_blocks && y < height_blocks && source + block_bytes <= data.size()) {
                    std::memcpy(out.data() + (std::size_t(y) * width_blocks + x) * block_bytes,
                                data.data() + source, block_bytes);
                }
            }
        }
    }
    return out;
}

std::array<std::uint8_t, 64> DecodeBc7Block(std::span<const std::uint8_t, 16> block) {
    std::array<std::uint8_t, 64> out{};
    int mode = 0;
    while (mode < 8 && ((block[0] >> mode) & 1) == 0) {
        ++mode;
    }
    if (mode == 8) {
        return out; // reserved: transparent black
    }
    const Bc7Mode& m = Modes[mode];
    Bits bits{block};
    bits.Take(mode + 1);
    const int partition = bits.Take(m.partition_bits);
    const int rotation = bits.Take(m.rotation_bits);
    const int selector = bits.Take(m.selector_bits);
    const int endpoints = m.subsets * 2;
    int color[6][4] = {};
    for (int channel = 0; channel < 3; ++channel) {
        for (int e = 0; e < endpoints; ++e) {
            color[e][channel] = bits.Take(m.color_bits);
        }
    }
    for (int e = 0; e < endpoints; ++e) {
        color[e][3] = m.alpha_bits ? bits.Take(m.alpha_bits) : 255;
    }
    int pbit[6] = {};
    if (m.endpoint_pbits) {
        for (int e = 0; e < endpoints; ++e) {
            pbit[e] = bits.Take(1);
        }
    } else if (m.shared_pbits) {
        for (int s = 0; s < m.subsets; ++s) {
            pbit[2 * s] = pbit[2 * s + 1] = bits.Take(1);
        }
    }
    const bool with_pbits = m.endpoint_pbits || m.shared_pbits;
    for (int e = 0; e < endpoints; ++e) {
        for (int channel = 0; channel < 4; ++channel) {
            if (channel == 3 && m.alpha_bits == 0) {
                continue;
            }
            int value = color[e][channel];
            int width = channel < 3 ? m.color_bits : m.alpha_bits;
            if (with_pbits) {
                value = value << 1 | pbit[e];
                ++width;
            }
            color[e][channel] = width < 8 ? (value << (8 - width)) | (value >> (2 * width - 8))
                                          : value;
        }
    }
    int index[16] = {}, index2[16] = {};
    for (int pixel = 0; pixel < 16; ++pixel) {
        const int subset = SubsetOf(m.subsets, partition, pixel);
        index[pixel] =
            bits.Take(m.index_bits - (pixel == AnchorOf(m.subsets, partition, subset) ? 1 : 0));
    }
    if (m.index2_bits) {
        for (int pixel = 0; pixel < 16; ++pixel) {
            index2[pixel] = bits.Take(m.index2_bits - (pixel == 0 ? 1 : 0));
        }
    }
    for (int pixel = 0; pixel < 16; ++pixel) {
        const int subset = SubsetOf(m.subsets, partition, pixel);
        const int* a = color[2 * subset];
        const int* b = color[2 * subset + 1];
        int color_index = index[pixel], alpha_index = index[pixel];
        const std::uint8_t* color_weights = WeightsFor(m.index_bits);
        const std::uint8_t* alpha_weights = color_weights;
        if (m.index2_bits) {
            if (selector) {
                color_index = index2[pixel];
                color_weights = WeightsFor(m.index2_bits);
            } else {
                alpha_index = index2[pixel];
                alpha_weights = WeightsFor(m.index2_bits);
            }
        }
        int px[4];
        for (int channel = 0; channel < 3; ++channel) {
            const int w = color_weights[color_index];
            px[channel] = ((64 - w) * a[channel] + w * b[channel] + 32) >> 6;
        }
        const int w = alpha_weights[alpha_index];
        px[3] = ((64 - w) * a[3] + w * b[3] + 32) >> 6;
        if (rotation) {
            std::swap(px[3], px[rotation - 1]);
        }
        for (int channel = 0; channel < 4; ++channel) {
            out[pixel * 4 + channel] = std::uint8_t(px[channel]);
        }
    }
    return out;
}

std::vector<std::uint8_t> DecodeBc7Rect(std::span<const std::uint8_t> blocks, int width,
                                        const PixelRect& rect) {
    const int w = rect.x1 - rect.x0, h = rect.y1 - rect.y0, width_blocks = (width + 3) / 4;
    std::vector<std::uint8_t> rgba(std::size_t(std::max(w, 0)) * std::max(h, 0) * 4);
    for (int y = rect.y0; y < rect.y1; ++y) {
        for (int x = rect.x0; x < rect.x1; ++x) {
            const std::size_t at = (std::size_t(y / 4) * width_blocks + x / 4) * 16;
            if (at + 16 > blocks.size()) {
                continue;
            }
            // Decoding a block per pixel is wasteful but this runs once, on a few thousand pixels.
            const auto pixels = DecodeBc7Block(std::span<const std::uint8_t, 16>{blocks.data() + at, 16});
            std::memcpy(rgba.data() + (std::size_t(y - rect.y0) * w + (x - rect.x0)) * 4,
                        pixels.data() + ((y % 4) * 4 + x % 4) * 4, 4);
        }
    }
    return rgba;
}

std::optional<GaugeArt> LoadGaugeArt(std::span<const std::uint8_t> common_tpf_dcx) {
    const auto tpf = InflateDcx(common_tpf_dcx);
    const auto textures = tpf ? ParseTpf(*tpf) : std::nullopt;
    if (!textures) {
        return std::nullopt;
    }
    const auto texture = std::ranges::find(*textures, GaugeTexture, &TpfTexture::name);
    if (texture == textures->end() || texture->dxgi != 98 || texture->width != 1024 ||
        texture->height != 512) {
        return std::nullopt;
    }
    const int width_blocks = texture->width / 4, height_blocks = texture->height / 4;
    if (texture->size < std::uint32_t(width_blocks * height_blocks * 16)) {
        return std::nullopt;
    }
    const auto blocks = DeswizzleBlocks(
        std::span<const std::uint8_t>{tpf->data() + texture->offset, texture->size}, width_blocks,
        height_blocks, 16);
    const PixelRect rows{0, 0, 656, 48};
    return GaugeArt{.width = rows.x1, .height = rows.y1,
                    .rgba = DecodeBc7Rect(blocks, texture->width, rows)};
}

} // namespace BbGameArt
