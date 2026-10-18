// SPDX-License-Identifier: GPL-2.0-or-later
// bbport co-op: the map screen's pictures of the areas, drawn from the game's map files
// (bbport_map_art.h).
#include "bbport_map_art.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <thread>
#include <utility>

#include "bbport_game_art.h"
#include "miniz.h"

namespace BbMapArt {

namespace {

template <typename T>
bool Get(std::span<const std::uint8_t> bytes, std::size_t at, T& out) {
    if (at > bytes.size() || bytes.size() - at < sizeof(T)) {
        return false;
    }
    std::memcpy(&out, bytes.data() + at, sizeof(T));
    return true;
}

/// A UTF-16 name at `at` as the ASCII the game's file names are ("" past the end).
std::string Utf16At(std::span<const std::uint8_t> bytes, std::size_t at) {
    std::string text;
    for (std::uint16_t c = 0; at + 1 < bytes.size() && text.size() < 256; at += 2) {
        std::memcpy(&c, bytes.data() + at, 2);
        if (c == 0) {
            break;
        }
        text += c < 0x80 ? char(c) : '?';
    }
    return text;
}

/// The MSB's param lists: name -> the records' offsets.
std::map<std::string, std::vector<std::size_t>> ParamLists(std::span<const std::uint8_t> msb) {
    std::map<std::string, std::vector<std::size_t>> lists;
    std::int64_t at = 0x10;
    for (int guard = 0; at != 0 && guard < 64; ++guard) {
        std::int32_t version = 0, count = 0;
        std::int64_t name = 0;
        if (!Get(msb, std::size_t(at), version) || !Get(msb, std::size_t(at) + 4, count) ||
            !Get(msb, std::size_t(at) + 8, name) || count < 1 || count > 1'000'000) {
            break;
        }
        std::vector<std::size_t> records;
        std::int64_t next = 0;
        for (std::int32_t i = 0; i < count; ++i) {
            std::int64_t record = 0;
            if (!Get(msb, std::size_t(at) + 0x10 + std::size_t(i) * 8, record)) {
                return lists;
            }
            if (i + 1 == count) {
                next = record;
            } else {
                records.push_back(std::size_t(record));
            }
        }
        lists[Utf16At(msb, std::size_t(name))] = std::move(records);
        at = next;
    }
    return lists;
}

Vec3 Vec3At(std::span<const std::uint8_t> bytes, std::size_t at) {
    Vec3 v{};
    Get(bytes, at, v);
    return v;
}

/// Row vector times the rotation the game's map tools apply: x, then z, then y (degrees).
std::array<float, 9> RotationOf(const Vec3& degrees, const Vec3& scale) {
    const double rad = 3.14159265358979323846 / 180.0;
    const double cx = std::cos(degrees[0] * rad), sx = std::sin(degrees[0] * rad);
    const double cy = std::cos(degrees[1] * rad), sy = std::sin(degrees[1] * rad);
    const double cz = std::cos(degrees[2] * rad), sz = std::sin(degrees[2] * rad);
    const double rx[9] = {1, 0, 0, 0, cx, sx, 0, -sx, cx};
    const double rz[9] = {cz, sz, 0, -sz, cz, 0, 0, 0, 1};
    const double ry[9] = {cy, 0, -sy, 0, 1, 0, sy, 0, cy};
    const auto mul = [](const double* a, const double* b, double* out) {
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                out[r * 3 + c] = a[r * 3] * b[c] + a[r * 3 + 1] * b[3 + c] + a[r * 3 + 2] * b[6 + c];
            }
        }
    };
    double xz[9], xzy[9];
    mul(rx, rz, xz);
    mul(xz, ry, xzy);
    std::array<float, 9> m{};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            m[r * 3 + c] = float(xzy[r * 3 + c] * scale[r]);
        }
    }
    return m;
}

float Percentile(std::vector<float> values, double share) {
    const std::size_t at = std::min(values.size() - 1, std::size_t(share * double(values.size() - 1) + 0.5));
    std::nth_element(values.begin(), values.begin() + std::ptrdiff_t(at), values.end());
    return values[at];
}

constexpr std::uint32_t WaterColor = 0xFF4E6672u;

bool SkippedMaterial(const std::string& mtd) {
    // Kaniel 2026-10-18 ("a lot of noise"): also the alpha-cut decals (_al: litter, grime, leaves on
    // the ground), grass cards and blood splats, specks from above.
    static constexpr std::string_view Skipped[] = {"Sky", "sky", "Moon", "Sun_", "_al", "grass", "Bloody"};
    return std::ranges::any_of(Skipped, [&](std::string_view word) { return mtd.find(word) != std::string::npos; });
}

float TriangleArea(const float* t) {
    const float ax = t[3] - t[0], ay = t[4] - t[1], az = t[5] - t[2];
    const float bx = t[6] - t[0], by = t[7] - t[1], bz = t[8] - t[2];
    const float cx = ay * bz - az * by, cy = az * bx - ax * bz, cz = ax * by - ay * bx;
    return 0.5f * std::sqrt(cx * cx + cy * cy + cz * cz);
}

} // namespace

std::vector<float> Floors(std::span<const float> triangles) {
    std::vector<float> floors;
    for (std::size_t t = 0; t + 9 <= triangles.size(); t += 9) {
        const float* v = triangles.data() + t;
        const float ax = v[3] - v[0], ay = v[4] - v[1], az = v[5] - v[2];
        const float bx = v[6] - v[0], by = v[7] - v[1], bz = v[8] - v[2];
        const float nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
        const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (length > 0.0f && std::abs(ny) > 0.45f * length) {
            floors.insert(floors.end(), v, v + 9);
        }
    }
    return floors;
}

std::optional<std::vector<std::uint8_t>> ReadColorGrade(std::span<const std::uint8_t> tpf, const std::string& block) {
    const auto textures = BbGameArt::ParseTpf(tpf);
    if (!textures) {
        return std::nullopt;
    }
    const BbGameArt::TpfTexture* found = nullptr;
    for (const BbGameArt::TpfTexture& t : *textures) {
        if (t.name == block + "_CGrading0001" || (!found && t.name.starts_with(block.substr(0, 3)))) {
            found = &t;
        }
    }
    // 16 wide, 256 tall, RGBA8 (DXGI 28/29), 4 bytes a texel in 8x8 tiles of Morton order.
    if (!found || found->width != 16 || found->height != 256 || found->size < 16 * 256 * 4) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> grade(16 * 16 * 16 * 3);
    std::size_t source = found->offset;
    for (int ty = 0; ty < 256 / 8; ++ty) {
        for (int tx = 0; tx < 16 / 8; ++tx) {
            for (int i = 0; i < 64; ++i, source += 4) {
                int x = 0, y = 0;
                for (int bit = 0; bit < 3; ++bit) {
                    x |= ((i >> (2 * bit)) & 1) << bit;
                    y |= ((i >> (2 * bit + 1)) & 1) << bit;
                }
                const int row = ty * 8 + y, column = tx * 8 + x; // row: blue slice * 16 + green
                std::memcpy(grade.data() + (std::size_t(row) * 16 + std::size_t(column)) * 3, tpf.data() + source, 3);
            }
        }
    }
    return grade;
}

std::array<float, 3> Graded(std::span<const std::uint8_t> grade, const float rgb[3]) {
    if (grade.size() < 16 * 16 * 16 * 3) {
        return {rgb[0], rgb[1], rgb[2]};
    }
    float f[3];
    int i0[3], i1[3];
    for (int c = 0; c < 3; ++c) {
        const float v = std::clamp(rgb[c], 0.0f, 255.0f) / 255.0f * 15.0f;
        i0[c] = std::min(14, int(v));
        i1[c] = i0[c] + 1;
        f[c] = v - float(i0[c]);
    }
    const auto at = [&](int r, int g, int b, int c) {
        return float(grade[(std::size_t(b) * 256 + std::size_t(g) * 16 + std::size_t(r)) * 3 + std::size_t(c)]);
    };
    std::array<float, 3> out{};
    for (int c = 0; c < 3; ++c) {
        float v = 0;
        for (int corner = 0; corner < 8; ++corner) {
            const int r = corner & 1 ? i1[0] : i0[0], g = corner & 2 ? i1[1] : i0[1], b = corner & 4 ? i1[2] : i0[2];
            const float w = (corner & 1 ? f[0] : 1 - f[0]) * (corner & 2 ? f[1] : 1 - f[1]) * (corner & 4 ? f[2] : 1 - f[2]);
            v += w * at(r, g, b, c);
        }
        out[std::size_t(c)] = v;
    }
    return out;
}

std::uint32_t AverageColor(std::span<const std::uint8_t> tpf) {
    const auto textures = BbGameArt::ParseTpf(tpf);
    if (!textures || textures->empty()) {
        return 0;
    }
    const BbGameArt::TpfTexture& texture = textures->front();
    // DXGI BC1 (71, 72 sRGB) and BC7 (98, 99 sRGB); the blocks in any order make the same average.
    const bool bc1 = texture.dxgi == 71 || texture.dxgi == 72, bc7 = texture.dxgi == 98 || texture.dxgi == 99;
    if (!bc1 && !bc7) {
        return 0;
    }
    const std::size_t block = bc1 ? 8 : 16;
    const std::size_t blocks = std::size_t(std::max(1, texture.width / 4)) * std::size_t(std::max(1, texture.height / 4));
    const std::size_t count = std::min<std::size_t>(blocks, texture.size / block);
    const std::size_t step = std::max<std::size_t>(1, count / 1024); // a thousand blocks at most
    double sum[3] = {}, weight = 0;
    for (std::size_t b = 0; b < count; b += step) {
        const std::uint8_t* at = tpf.data() + texture.offset + b * block;
        std::array<std::uint8_t, 64> pixels;
        if (bc1) {
            pixels = BbGameArt::DecodeBc1Block(std::span<const std::uint8_t, 8>(at, 8));
        } else {
            pixels = BbGameArt::DecodeBc7Block(std::span<const std::uint8_t, 16>(at, 16));
        }
        for (std::size_t p = 0; p < 64; p += 4) {
            const double a = pixels[p + 3] / 255.0;
            sum[0] += pixels[p] * a, sum[1] += pixels[p + 1] * a, sum[2] += pixels[p + 2] * a;
            weight += a;
        }
    }
    if (weight <= 0) {
        return 0;
    }
    const auto channel = [&](int c) { return std::uint32_t(std::clamp(sum[c] / weight, 0.0, 255.0)); };
    return 0xFF000000u | channel(0) << 16 | channel(1) << 8 | channel(2);
}

std::vector<ArchiveEntry> ReadArchiveHeader(std::span<const std::uint8_t> bhd) {
    std::vector<ArchiveEntry> entries;
    std::int32_t count = 0;
    std::int64_t entry_size = 0;
    if (bhd.size() < 0x40 || std::memcmp(bhd.data(), "BHF4", 4) != 0 || !Get(bhd, 0x0C, count) ||
        !Get(bhd, 0x20, entry_size) || entry_size < 0x24 || count < 0 || count > 1'000'000) {
        return entries;
    }
    for (std::int32_t i = 0; i < count; ++i) {
        const std::size_t at = 0x40 + std::size_t(i) * std::size_t(entry_size);
        std::int64_t size = 0;
        std::uint32_t offset = 0, name = 0;
        if (!Get(bhd, at + 8, size) || !Get(bhd, at + 0x18, offset) || !Get(bhd, at + 0x20, name)) {
            break;
        }
        // Names in UTF-16 when the header says so (+0x30), else in single bytes (a repacked
        // archive, as the rig's low-spec texture mod's).
        std::string text;
        if (bhd[0x30] != 0) {
            text = Utf16At(bhd, name);
        } else {
            for (std::size_t c = name; c < bhd.size() && bhd[c] != 0 && text.size() < 256; ++c) {
                text += char(bhd[c]);
            }
        }
        entries.push_back(ArchiveEntry{std::move(text), offset, std::uint64_t(size)});
    }
    return entries;
}

std::optional<MapStudio> ReadMapStudio(std::span<const std::uint8_t> msb) {
    if (msb.size() < 0x20 || std::memcmp(msb.data(), "MSB ", 4) != 0) {
        return std::nullopt;
    }
    const auto lists = ParamLists(msb);
    const auto models_it = lists.find("MODEL_PARAM_ST");
    const auto parts_it = lists.find("PARTS_PARAM_ST");
    if (models_it == lists.end() || parts_it == lists.end()) {
        return std::nullopt;
    }
    std::vector<std::string> models;
    for (const std::size_t record : models_it->second) {
        std::int64_t name = 0;
        Get(msb, record, name);
        models.push_back(Utf16At(msb, record + std::size_t(name)));
    }
    MapStudio studio;
    for (const std::size_t record : parts_it->second) {
        std::int64_t name_at = 0;
        std::int32_t model = -1;
        if (!Get(msb, record + 8, name_at) || !Get(msb, record + 0x1C, model)) {
            continue;
        }
        const std::string name = Utf16At(msb, record + std::size_t(name_at));
        const Vec3 position = Vec3At(msb, record + 0x28);
        if (name.starts_with('c')) {
            studio.spots.push_back(position);
            continue;
        }
        if (name.starts_with('h') && model >= 0 && std::size_t(model) < models.size() &&
            models[std::size_t(model)].starts_with('h')) {
            std::int64_t sub = 0;
            std::int16_t banner = -1;
            if (Get(msb, record + 0xB8, sub) && sub > 0) {
                Get(msb, record + std::size_t(sub) + 8, banner);
            }
            studio.collisions.push_back(MapStudio::Collision{
                Piece{models[std::size_t(model)], position, Vec3At(msb, record + 0x34), Vec3At(msb, record + 0x40)},
                banner == -1 ? 0 : std::abs(banner)});
            continue;
        }
        if (!name.starts_with('m') || model < 0 || std::size_t(model) >= models.size() ||
            !models[std::size_t(model)].starts_with('m')) {
            continue;
        }
        studio.pieces.push_back(Piece{models[std::size_t(model)], position, Vec3At(msb, record + 0x34),
                                      Vec3At(msb, record + 0x40)});
    }
    if (const auto points = lists.find("POINT_PARAM_ST"); points != lists.end()) {
        for (const std::size_t record : points->second) {
            studio.spots.push_back(Vec3At(msb, record + 0x14));
        }
    }
    return studio;
}

bool AppendTriangles(std::span<const std::uint8_t> flver, std::vector<float>& out, std::vector<std::uint32_t>* colors,
                     const MaterialColor& color_of) {
    if (flver.size() < 0x80 || std::memcmp(flver.data(), "FLVER\0L\0", 8) != 0) {
        return false;
    }
    std::uint32_t data = 0;
    std::int32_t dummies = 0, materials = 0, bones = 0, meshes = 0, buffers = 0, facesets = 0, layouts = 0;
    std::uint8_t header_index_size = 0;
    Get(flver, 0x0C, data);
    Get(flver, 0x14, dummies);
    Get(flver, 0x18, materials);
    Get(flver, 0x1C, bones);
    Get(flver, 0x20, meshes);
    Get(flver, 0x24, buffers);
    Get(flver, 0x48, header_index_size);
    Get(flver, 0x50, facesets);
    Get(flver, 0x54, layouts);
    if (dummies < 0 || materials < 0 || bones < 0 || meshes < 0 || buffers < 0 || facesets < 0 || layouts < 0) {
        return false;
    }
    const std::size_t material_base = 0x80 + std::size_t(dummies) * 0x40;
    const std::size_t mesh_base = material_base + std::size_t(materials) * 0x20 + std::size_t(bones) * 0x80;
    const std::size_t faceset_base = mesh_base + std::size_t(meshes) * 0x30;
    const std::size_t buffer_base = faceset_base + std::size_t(facesets) * 0x20;
    const std::size_t layout_base = buffer_base + std::size_t(buffers) * 0x20;
    const std::size_t texture_base = layout_base + std::size_t(layouts) * 0x10;
    // Each layout's position member: its offset in the vertex and whether it is a float3 or float4.
    std::vector<std::int32_t> position_of(std::size_t(layouts), -1);
    for (std::int32_t i = 0; i < layouts; ++i) {
        std::int32_t count = 0, members = 0;
        Get(flver, layout_base + std::size_t(i) * 0x10, count);
        Get(flver, layout_base + std::size_t(i) * 0x10 + 0xC, members);
        for (std::int32_t m = 0; m < count && m < 64; ++m) {
            const std::size_t at = std::size_t(members) + std::size_t(m) * 0x14;
            std::int32_t offset = 0;
            std::uint32_t type = 0, semantic = 0;
            Get(flver, at + 4, offset);
            Get(flver, at + 8, type);
            Get(flver, at + 0xC, semantic);
            if (semantic == 0 && (type == 2 || type == 3)) {
                position_of[std::size_t(i)] = offset;
                break;
            }
        }
    }
    std::vector<float> corners;
    std::vector<std::uint32_t> corner_colors;
    std::map<std::int32_t, std::uint32_t> material_colors;
    // A material's colour: its first diffuse texture's (path "...\m24_01_trims_003_a.tif").
    const auto material_color = [&](std::int32_t material) -> std::uint32_t {
        if (!colors || !color_of || material < 0 || material >= materials) {
            return 0;
        }
        if (const auto known = material_colors.find(material); known != material_colors.end()) {
            return known->second;
        }
        std::int32_t count = 0, first = 0;
        Get(flver, material_base + std::size_t(material) * 0x20 + 8, count);
        Get(flver, material_base + std::size_t(material) * 0x20 + 0xC, first);
        std::uint32_t color = 0;
        for (std::int32_t t = first; t < first + count && t >= 0 && t < first + 32; ++t) {
            std::int32_t path = 0, type = 0;
            Get(flver, texture_base + std::size_t(t) * 0x20, path);
            Get(flver, texture_base + std::size_t(t) * 0x20 + 4, type);
            if (Utf16At(flver, std::size_t(type)).find("Diffuse") == std::string::npos) {
                continue;
            }
            std::string name = Utf16At(flver, std::size_t(path));
            name = name.substr(name.find_last_of("\\/") + 1);
            name = name.substr(0, name.find('.'));
            if (!name.empty()) {
                color = color_of(name);
            }
            break;
        }
        material_colors[material] = color;
        return color;
    };
    for (std::int32_t mesh = 0; mesh < meshes; ++mesh) {
        const std::size_t at = mesh_base + std::size_t(mesh) * 0x30;
        std::int32_t material = 0, faceset_count = 0, faceset_list = 0, buffer_count = 0, buffer_list = 0;
        Get(flver, at + 4, material);
        Get(flver, at + 0x20, faceset_count);
        Get(flver, at + 0x24, faceset_list);
        Get(flver, at + 0x28, buffer_count);
        Get(flver, at + 0x2C, buffer_list);
        if (material >= 0 && material < materials) {
            std::int32_t mtd = 0;
            Get(flver, material_base + std::size_t(material) * 0x20 + 4, mtd);
            if (SkippedMaterial(Utf16At(flver, std::size_t(mtd)))) {
                continue;
            }
        }
        std::int32_t buffer = -1;
        if (buffer_count < 1 || !Get(flver, std::size_t(buffer_list), buffer) || buffer < 0 || buffer >= buffers) {
            continue;
        }
        std::int32_t layout = 0, vertex_size = 0, vertex_count = 0;
        std::uint32_t buffer_offset = 0;
        const std::size_t vb = buffer_base + std::size_t(buffer) * 0x20;
        Get(flver, vb + 4, layout);
        Get(flver, vb + 8, vertex_size);
        Get(flver, vb + 0xC, vertex_count);
        Get(flver, vb + 0x1C, buffer_offset);
        if (layout < 0 || layout >= layouts || position_of[std::size_t(layout)] < 0 || vertex_size <= 0 ||
            vertex_count <= 0) {
            continue;
        }
        // Kaniel 2026-10-18: water as a calm blue-grey surface (its textures are foam and ripples).
        std::uint32_t mesh_color = material_color(material);
        if (colors && material >= 0 && material < materials) {
            std::int32_t mtd = 0;
            Get(flver, material_base + std::size_t(material) * 0x20 + 4, mtd);
            const std::string name = Utf16At(flver, std::size_t(mtd));
            if (name.find("water") != std::string::npos || name.find("Water") != std::string::npos) {
                mesh_color = WaterColor;
            }
        }
        const std::size_t vertices = std::size_t(data) + buffer_offset + std::size_t(position_of[std::size_t(layout)]);
        if (vertices + std::size_t(vertex_count - 1) * std::size_t(vertex_size) + 12 > flver.size()) {
            continue;
        }
        const auto corner = [&](std::uint32_t index) {
            const std::uint8_t* p = flver.data() + vertices + std::size_t(index) * std::size_t(vertex_size);
            float xyz[3];
            std::memcpy(xyz, p, 12);
            corners.insert(corners.end(), xyz, xyz + 3);
        };
        for (std::int32_t f = 0; f < faceset_count; ++f) {
            std::int32_t faceset = -1;
            if (!Get(flver, std::size_t(faceset_list) + std::size_t(f) * 4, faceset) || faceset < 0 ||
                faceset >= facesets) {
                continue;
            }
            const std::size_t fs = faceset_base + std::size_t(faceset) * 0x20;
            std::uint32_t flags = 0, index_offset = 0;
            std::uint8_t strip = 0;
            std::int32_t index_count = 0, index_size = 0;
            Get(flver, fs, flags);
            Get(flver, fs + 4, strip);
            Get(flver, fs + 8, index_count);
            Get(flver, fs + 0xC, index_offset);
            Get(flver, fs + 0x18, index_size);
            if ((flags & 0x03000000u) != 0 || index_count < 3) {
                continue; // the lower levels of detail
            }
            if (index_size == 0) {
                index_size = header_index_size != 0 ? header_index_size : 16;
            }
            const std::size_t width = index_size == 32 ? 4 : 2;
            const std::size_t base = std::size_t(data) + index_offset;
            if (base + std::size_t(index_count) * width > flver.size()) {
                continue;
            }
            const auto index_at = [&](std::int32_t i) -> std::uint32_t {
                if (width == 4) {
                    std::uint32_t v;
                    std::memcpy(&v, flver.data() + base + std::size_t(i) * 4, 4);
                    return v;
                }
                std::uint16_t v;
                std::memcpy(&v, flver.data() + base + std::size_t(i) * 2, 2);
                return v == 0xFFFF ? 0xFFFFFFFFu : v;
            };
            const auto triangle = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
                const auto n = std::uint32_t(vertex_count);
                if (a >= n || b >= n || c >= n || a == b || b == c || a == c) {
                    return;
                }
                corner(a);
                corner(b);
                corner(c);
                corner_colors.push_back(mesh_color);
            };
            if (strip) {
                for (std::int32_t i = 0; i + 2 < index_count; ++i) {
                    triangle(index_at(i), index_at(i + 1), index_at(i + 2));
                }
            } else {
                for (std::int32_t i = 0; i + 2 < index_count; i += 3) {
                    triangle(index_at(i), index_at(i + 1), index_at(i + 2));
                }
            }
        }
    }
    out.insert(out.end(), corners.begin(), corners.end());
    if (colors) {
        colors->insert(colors->end(), corner_colors.begin(), corner_colors.end());
    }
    return true;
}

bool SkippedModel(std::string_view model, std::span<const float> triangles) {
    if (model.size() < 2) {
        return true;
    }
    int number = 0;
    for (const char c : model.substr(1)) {
        if (c < '0' || c > '9') {
            return true;
        }
        number = number * 10 + (c - '0');
    }
    if (number >= 900000 || (number >= 8000 && number < 10000)) {
        return true;
    }
    const std::size_t count = triangles.size() / 9;
    if (count == 0) {
        return true;
    }
    // Kaniel 2026-10-18 ("a lot of noise"): small props (bottles, rubble, bodies, candles) are
    // specks from above; a model within a metre and a half each way is left out.
    {
        float lo[3] = {triangles[0], triangles[1], triangles[2]}, hi[3] = {lo[0], lo[1], lo[2]};
        for (std::size_t i = 0; i + 3 <= triangles.size(); i += 3) {
            for (int c = 0; c < 3; ++c) {
                lo[c] = std::min(lo[c], triangles[i + std::size_t(c)]);
                hi[c] = std::max(hi[c], triangles[i + std::size_t(c)]);
            }
        }
        if (hi[0] - lo[0] < 1.5f && hi[1] - lo[1] < 1.5f && hi[2] - lo[2] < 1.5f) {
            return true;
        }
    }
    if (count > 64) {
        // A backdrop under or round the whole area (the Dream's sea of flowers, the woods' far
        // hills): its triangles spread thin, under one in 20 square metres over 200,000.
        float x0 = triangles[0], x1 = x0, z0 = triangles[2], z1 = z0;
        for (std::size_t i = 0; i + 3 <= triangles.size(); i += 3) {
            x0 = std::min(x0, triangles[i]), x1 = std::max(x1, triangles[i]);
            z0 = std::min(z0, triangles[i + 2]), z1 = std::max(z1, triangles[i + 2]);
        }
        const float spread = (x1 - x0) * (z1 - z0);
        return spread > 200'000.0f && float(count) < spread * 0.05f;
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (TriangleArea(triangles.data() + i * 9) > 100.0f) {
            return true;
        }
    }
    return false;
}

std::optional<PlayArea> PlayAreaOf(std::span<const Vec3> spots) {
    if (spots.empty()) {
        return std::nullopt;
    }
    std::vector<float> xs, ys, zs;
    for (const Vec3& s : spots) {
        xs.push_back(s[0]);
        ys.push_back(s[1]);
        zs.push_back(s[2]);
    }
    constexpr float Margin = 40.0f;
    return PlayArea{Percentile(xs, 0.01) - Margin, Percentile(xs, 0.99) + Margin, Percentile(zs, 0.01) - Margin,
                    Percentile(zs, 0.99) + Margin, Percentile(ys, 0.99) + 25.0f};
}

void Place(const Piece& piece, std::span<const float> model, const PlayArea& area, std::vector<float>& out,
           std::span<const std::uint32_t> colors, std::vector<std::uint32_t>* out_colors) {
    const std::array<float, 9> m = RotationOf(piece.rotation, piece.scale);
    const Vec3& p = piece.position;
    float tri[9];
    for (std::size_t t = 0; t + 9 <= model.size(); t += 9) {
        for (int k = 0; k < 3; ++k) {
            const float* v = model.data() + t + std::size_t(k) * 3;
            tri[k * 3 + 0] = v[0] * m[0] + v[1] * m[3] + v[2] * m[6] + p[0];
            tri[k * 3 + 1] = v[0] * m[1] + v[1] * m[4] + v[2] * m[7] + p[1];
            tri[k * 3 + 2] = v[0] * m[2] + v[1] * m[5] + v[2] * m[8] + p[2];
        }
        const float cx = (tri[0] + tri[3] + tri[6]) / 3.0f, cz = (tri[2] + tri[5] + tri[8]) / 3.0f;
        const float low = std::min({tri[1], tri[4], tri[7]});
        if (cx < area.x0 || cx > area.x1 || cz < area.z0 || cz > area.z1 || low >= area.ceiling) {
            continue;
        }
        out.insert(out.end(), tri, tri + 9);
        if (out_colors) {
            out_colors->push_back(t / 9 < colors.size() ? colors[t / 9] : 0u);
        }
    }
}

Canvas::Canvas(const PlayArea& area, float ppm_, int max_side) {
    const float w = std::max(1.0f, area.x1 - area.x0), h = std::max(1.0f, area.z1 - area.z0);
    ppm = std::min(ppm_, float(max_side) / std::max(w, h));
    width = std::max(1, int(std::ceil(w * ppm)));
    height = std::max(1, int(std::ceil(h * ppm)));
    x0 = area.x0;
    z1 = area.z1;
    top.assign(std::size_t(width) * std::size_t(height), -std::numeric_limits<float>::infinity());
    light.assign(top.size(), 0);
    color.assign(top.size(), 0);
    walk.assign(top.size(), 0);
}

void Canvas::AddWalkable(std::span<const float> floors) {
    has_walk = has_walk || !floors.empty();
    for (std::size_t t = 0; t + 9 <= floors.size(); t += 9) {
        const float* v = floors.data() + t;
        float px[3], py[3];
        for (int k = 0; k < 3; ++k) {
            px[k] = (v[k * 3] - x0) * ppm;
            py[k] = (z1 - v[k * 3 + 2]) * ppm;
        }
        const int bx0 = std::max(0, int(std::floor(std::min({px[0], px[1], px[2]}))));
        const int by0 = std::max(0, int(std::floor(std::min({py[0], py[1], py[2]}))));
        const int bx1 = std::min(width - 1, int(std::floor(std::max({px[0], px[1], px[2]}))));
        const int by1 = std::min(height - 1, int(std::floor(std::max({py[0], py[1], py[2]}))));
        const float den = (py[1] - py[2]) * (px[0] - px[2]) + (px[2] - px[1]) * (py[0] - py[2]);
        for (int y = by0; y <= by1; ++y) {
            for (int x = bx0; x <= bx1; ++x) {
                bool inside = bx0 == bx1 || by0 == by1 || std::abs(den) < 1e-9f;
                if (!inside) {
                    const float cx = float(x) + 0.5f, cy = float(y) + 0.5f;
                    const float l1 = ((py[1] - py[2]) * (cx - px[2]) + (px[2] - px[1]) * (cy - py[2])) / den;
                    const float l2 = ((py[2] - py[0]) * (cx - px[2]) + (px[0] - px[2]) * (cy - py[2])) / den;
                    inside = l1 >= -0.05f && l2 >= -0.05f && 1.0f - l1 - l2 >= -0.05f;
                }
                if (inside) {
                    walk[std::size_t(y) * std::size_t(width) + std::size_t(x)] = 1;
                }
            }
        }
    }
}

void Canvas::Add(std::span<const float> triangles, std::span<const std::uint32_t> colors) {
    // The light from the north-west, high, as an old map's relief.
    const float lx = -0.45f, ly = 0.78f, lz = 0.43f;
    for (std::size_t t = 0; t + 9 <= triangles.size(); t += 9) {
        const float* v = triangles.data() + t;
        const float ax = v[3] - v[0], ay = v[4] - v[1], az = v[5] - v[2];
        const float bx = v[6] - v[0], by = v[7] - v[1], bz = v[8] - v[2];
        float nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
        const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (length <= 0.0f) {
            continue;
        }
        if (ny < 0.0f) {
            nx = -nx, ny = -ny, nz = -nz;
        }
        const float lambert = std::clamp((nx * lx + ny * ly + nz * lz) / length, 0.0f, 1.0f);
        const auto shade = std::uint8_t(std::lround(lambert * 255.0f));
        const std::uint32_t tint = t / 9 < colors.size() ? colors[t / 9] : 0u;
        // Pixels of the corners (z north up).
        float px[3], py[3], h[3];
        for (int k = 0; k < 3; ++k) {
            px[k] = (v[k * 3] - x0) * ppm;
            py[k] = (z1 - v[k * 3 + 2]) * ppm;
            h[k] = v[k * 3 + 1];
        }
        const int bx0 = std::max(0, int(std::floor(std::min({px[0], px[1], px[2]}))));
        const int by0 = std::max(0, int(std::floor(std::min({py[0], py[1], py[2]}))));
        const int bx1 = std::min(width - 1, int(std::floor(std::max({px[0], px[1], px[2]}))));
        const int by1 = std::min(height - 1, int(std::floor(std::max({py[0], py[1], py[2]}))));
        if (bx0 > bx1 || by0 > by1) {
            continue;
        }
        const auto plot = [&](int x, int y, float z) {
            const std::size_t at = std::size_t(y) * std::size_t(width) + std::size_t(x);
            if (z > top[at]) {
                top[at] = z;
                light[at] = shade;
                color[at] = tint;
            }
        };
        if (bx0 == bx1 && by0 == by1) {
            plot(bx0, by0, std::max({h[0], h[1], h[2]})); // under a pixel: its top
            continue;
        }
        const float den = (py[1] - py[2]) * (px[0] - px[2]) + (px[2] - px[1]) * (py[0] - py[2]);
        if (std::abs(den) < 1e-9f) {
            // Edge-on from above (a wall): its line, at its top.
            const float z = std::max({h[0], h[1], h[2]});
            for (int k = 0; k < 3; ++k) {
                const int j = (k + 1) % 3;
                const int steps = int(std::max(std::abs(px[j] - px[k]), std::abs(py[j] - py[k]))) + 1;
                for (int s = 0; s <= steps; ++s) {
                    const float f = float(s) / float(steps);
                    const int x = int(px[k] + (px[j] - px[k]) * f), y = int(py[k] + (py[j] - py[k]) * f);
                    if (x >= 0 && y >= 0 && x < width && y < height) {
                        plot(x, y, z);
                    }
                }
            }
            continue;
        }
        const float inv = 1.0f / den;
        for (int y = by0; y <= by1; ++y) {
            const float cy = float(y) + 0.5f;
            for (int x = bx0; x <= bx1; ++x) {
                const float cx = float(x) + 0.5f;
                const float l1 = ((py[1] - py[2]) * (cx - px[2]) + (px[2] - px[1]) * (cy - py[2])) * inv;
                const float l2 = ((py[2] - py[0]) * (cx - px[2]) + (px[0] - px[2]) * (cy - py[2])) * inv;
                const float l3 = 1.0f - l1 - l2;
                constexpr float Slack = -0.02f;
                if (l1 < Slack || l2 < Slack || l3 < Slack) {
                    continue;
                }
                plot(x, y, l1 * h[0] + l2 * h[1] + l3 * h[2]);
            }
        }
    }
}

void Canvas::Merge(const Canvas& other) {
    if (other.width != width || other.height != height) {
        return;
    }
    for (std::size_t i = 0; i < top.size(); ++i) {
        if (other.top[i] > top[i]) {
            top[i] = other.top[i];
            light[i] = other.light[i];
            color[i] = other.color[i];
        }
        walk[i] = walk[i] | other.walk[i];
    }
    has_walk = has_walk || other.has_walk;
}

Chart Canvas::Finish() const {
    Chart chart;
    chart.width = width;
    chart.height = height;
    chart.x0 = x0;
    chart.z1 = z1;
    chart.ppm = ppm;
    chart.rgba.assign(std::size_t(width) * std::size_t(height) * 4, 0);
    std::vector<float> heights;
    heights.reserve(top.size() / 4);
    for (std::size_t i = 0; i < top.size(); i += 4) {
        if (std::isfinite(top[i])) {
            heights.push_back(top[i]);
        }
    }
    if (heights.empty()) {
        return chart;
    }
    const float low = Percentile(heights, 0.02), high = Percentile(heights, 0.98);
    const float span = std::max(1.0f, high - low);
    // Kaniel 2026-10-18 ("a lot of noise"): each drawn pixel takes the median light and the most
    // common colour of the 5x5 pixels round it on its own surface (within 0.75 m of its height), so
    // grime and cobble specks blend while walls and roof edges stay sharp; a pixel with few such
    // neighbours is a speck itself (a bottle, a stray leaf) and takes its whole window's median.
    std::vector<float> smooth_top = top;
    std::vector<std::uint8_t> smooth_light = light;
    std::vector<std::uint32_t> smooth_color = color;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t at = std::size_t(y) * std::size_t(width) + std::size_t(x);
            if (!std::isfinite(top[at])) {
                continue;
            }
            float hs[25];
            std::uint8_t ls[25];
            std::uint32_t cs[25];
            int n = 0;
            const auto gather = [&](bool same_surface) {
                n = 0;
                for (int dy = -2; dy <= 2; ++dy) {
                    for (int dx = -2; dx <= 2; ++dx) {
                        const int nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
                            continue;
                        }
                        const std::size_t o = std::size_t(ny) * std::size_t(width) + std::size_t(nx);
                        if (std::isfinite(top[o]) && (!same_surface || std::abs(top[o] - top[at]) < 0.75f)) {
                            hs[n] = top[o], ls[n] = light[o], cs[n] = color[o];
                            ++n;
                        }
                    }
                }
            };
            gather(true);
            if (n < 8) {
                gather(false);
            }
            std::nth_element(hs, hs + n / 2, hs + n);
            std::nth_element(ls, ls + n / 2, ls + n);
            smooth_top[at] = hs[n / 2];
            smooth_light[at] = ls[n / 2];
            int best = 0;
            for (int i = 0; i < n; ++i) {
                const int votes = int(std::count(cs, cs + n, cs[i]));
                if (votes > best) {
                    best = votes;
                    smooth_color[at] = cs[i];
                }
            }
        }
    }
    // Kaniel 2026-10-18 ("a lot of extra stuff outside the map"): with the walkable floors known,
    // what lies more than Reach metres from all of them goes (two passes of a distance sweep).
    std::vector<std::uint8_t> kept(top.size(), 1);
    if (has_walk) {
        const int reach = std::max(1, int(Reach * ppm));
        std::vector<int> far(top.size(), 1 << 20);
        for (std::size_t i = 0; i < top.size(); ++i) {
            if (walk[i]) {
                far[i] = 0;
            }
        }
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int& d = far[std::size_t(y) * std::size_t(width) + std::size_t(x)];
                if (x > 0) d = std::min(d, far[std::size_t(y) * std::size_t(width) + std::size_t(x - 1)] + 1);
                if (y > 0) d = std::min(d, far[std::size_t(y - 1) * std::size_t(width) + std::size_t(x)] + 1);
            }
        }
        for (int y = height - 1; y >= 0; --y) {
            for (int x = width - 1; x >= 0; --x) {
                int& d = far[std::size_t(y) * std::size_t(width) + std::size_t(x)];
                if (x + 1 < width) d = std::min(d, far[std::size_t(y) * std::size_t(width) + std::size_t(x + 1)] + 1);
                if (y + 1 < height) d = std::min(d, far[std::size_t(y + 1) * std::size_t(width) + std::size_t(x)] + 1);
            }
        }
        for (std::size_t i = 0; i < top.size(); ++i) {
            kept[i] = far[i] <= reach ? 1 : 0;
        }
    }
    // Kaniel 2026-10-18 (shadows): the light from the north-west, 40 degrees up; a pixel with a
    // surface rising above that line within 15 m toward the light lies in its shadow.
    const float rise = std::tan(40.0f * 3.14159265f / 180.0f) / ppm * 1.41421356f; // metres a diagonal pixel
    const int shadow_steps = int(15.0f * ppm / 1.41421356f);
    const auto in_shadow = [&](int x, int y, float h) {
        for (int k = 1; k <= shadow_steps; ++k) {
            const int sx = x - k, sy = y - k;
            if (sx < 0 || sy < 0) {
                return false;
            }
            const float other = smooth_top[std::size_t(sy) * std::size_t(width) + std::size_t(sx)];
            if (std::isfinite(other) && other - h > float(k) * rise) {
                return true;
            }
        }
        return false;
    };
    const auto height_at = [&](int x, int y) {
        x = std::clamp(x, 0, width - 1);
        y = std::clamp(y, 0, height - 1);
        const float h = smooth_top[std::size_t(y) * std::size_t(width) + std::size_t(x)];
        return std::isfinite(h) ? h : low - 8.0f;
    };
    // Ink on paper: low ground dark umber, high ground pale parchment, the light's relief over it,
    // ink lines where the height drops away (walls, roof edges, the shore of the void).
    const float paper[3] = {214.0f, 198.0f, 162.0f}, umber[3] = {84.0f, 70.0f, 54.0f}, ink[3] = {38.0f, 29.0f, 22.0f};
    // ppm-independent: a metre and a half of drop inks fully over 2 m.
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t at = std::size_t(y) * std::size_t(width) + std::size_t(x);
            if (!std::isfinite(top[at]) || !kept[at]) {
                continue;
            }
            const float h = smooth_top[at];
            const float t = std::clamp((h - low) / span, 0.0f, 1.0f);
            const float relief = 0.50f + 0.62f * float(smooth_light[at]) / 255.0f;
            const float drop = std::max({std::abs(height_at(x + 1, y) - height_at(x - 1, y)),
                                         std::abs(height_at(x, y + 1) - height_at(x, y - 1))});
            const float line = std::clamp((drop - 2.0f) / 2.0f, 0.0f, 1.0f) * 0.55f;
            // A faint grain, as paper has (a hash of the pixel: the same picture every time).
            std::uint32_t hash = std::uint32_t(x) * 73856093u ^ std::uint32_t(y) * 19349663u;
            hash = (hash ^ (hash >> 13)) * 0x5bd1e995u;
            const float grain = float((hash >> 24) & 0xFF) / 255.0f * 3.0f - 1.5f;
            std::uint8_t* px = chart.rgba.data() + at * 4;
            float base[3];
            for (int c = 0; c < 3; ++c) {
                base[c] = umber[c] + (paper[c] - umber[c]) * t;
            }
            if (const std::uint32_t tint = smooth_color[at]; tint != 0) {
                // Kaniel 2026-10-17 ("can we make it colored?"): the material's own colour,
                // livelier than the dark textures and as bright as the height says, a little of
                // the paper's tone kept.
                float rgb[3] = {float((tint >> 16) & 0xFF), float((tint >> 8) & 0xFF), float(tint & 0xFF)};
                const float lum = std::max(1.0f, 0.299f * rgb[0] + 0.587f * rgb[1] + 0.114f * rgb[2]);
                const float want = 95.0f + 120.0f * t;
                for (int c = 0; c < 3; ++c) {
                    const float vivid = lum + (rgb[c] - lum) * 1.25f;
                    const float paper_tone = (umber[c] + (paper[c] - umber[c]) * t);
                    base[c] = std::clamp(vivid * want / lum, 0.0f, 255.0f) * 0.8f + paper_tone * 0.2f;
                }
            }
            const float shade = in_shadow(x, y, h) ? 0.72f : 1.0f;
            float lit[3];
            for (int c = 0; c < 3; ++c) {
                lit[c] = std::clamp(base[c] * relief * shade, 0.0f, 255.0f);
            }
            // Kaniel 2026-10-18 ("the location feels in different colours"): the area's own grade.
            if (!grade.empty()) {
                // The table expects the lit, tone-mapped scene, darker than these flat colours.
                const float dim[3] = {lit[0] * 0.8f, lit[1] * 0.8f, lit[2] * 0.8f};
                const auto g = Graded(grade, dim);
                for (int c = 0; c < 3; ++c) {
                    lit[c] = lit[c] * 0.35f + g[std::size_t(c)] * 0.65f;
                }
            }
            for (int c = 0; c < 3; ++c) {
                float v = lit[c] + grain;
                v = v * (1.0f - line) + ink[c] * line;
                px[c] = std::uint8_t(std::clamp(v, 0.0f, 255.0f));
            }
            px[3] = 255;
        }
    }
    return Cropped(chart);
}

Chart Cropped(const Chart& chart) {
    int cx0 = chart.width, cy0 = chart.height, cx1 = -1, cy1 = -1;
    for (int y = 0; y < chart.height; ++y) {
        for (int x = 0; x < chart.width; ++x) {
            if (chart.rgba[(std::size_t(y) * std::size_t(chart.width) + std::size_t(x)) * 4 + 3] != 0) {
                cx0 = std::min(cx0, x), cx1 = std::max(cx1, x), cy0 = std::min(cy0, y), cy1 = std::max(cy1, y);
            }
        }
    }
    if (cx1 < 0) {
        return chart;
    }
    constexpr int Margin = 6;
    cx0 = std::max(0, cx0 - Margin), cy0 = std::max(0, cy0 - Margin);
    cx1 = std::min(chart.width - 1, cx1 + Margin), cy1 = std::min(chart.height - 1, cy1 + Margin);
    Chart out;
    out.labels = chart.labels;
    out.width = cx1 - cx0 + 1;
    out.height = cy1 - cy0 + 1;
    out.ppm = chart.ppm;
    out.x0 = chart.x0 + float(cx0) / chart.ppm;
    out.z1 = chart.z1 - float(cy0) / chart.ppm;
    out.rgba.resize(std::size_t(out.width) * std::size_t(out.height) * 4);
    for (int y = 0; y < out.height; ++y) {
        std::memcpy(out.rgba.data() + std::size_t(y) * std::size_t(out.width) * 4,
                    chart.rgba.data() + ((std::size_t(y + cy0) * std::size_t(chart.width)) + std::size_t(cx0)) * 4,
                    std::size_t(out.width) * 4);
    }
    return out;
}

std::vector<std::uint8_t> Save(const Chart& chart) {
    std::vector<std::uint8_t> file(32);
    std::memcpy(file.data(), "BBMAP5\0\0", 8);
    std::memcpy(file.data() + 8, &chart.width, 4);
    std::memcpy(file.data() + 12, &chart.height, 4);
    std::memcpy(file.data() + 16, &chart.x0, 4);
    std::memcpy(file.data() + 20, &chart.z1, 4);
    std::memcpy(file.data() + 24, &chart.ppm, 4);
    mz_ulong size = mz_compressBound(mz_ulong(chart.rgba.size()));
    std::vector<std::uint8_t> packed(size);
    if (mz_compress2(packed.data(), &size, chart.rgba.data(), mz_ulong(chart.rgba.size()), 6) != MZ_OK) {
        return {};
    }
    const auto stored = std::uint32_t(size);
    std::memcpy(file.data() + 28, &stored, 4);
    file.insert(file.end(), packed.begin(), packed.begin() + std::ptrdiff_t(size));
    // The labels after the pixels: a count, then (name id, x, z) each.
    const auto count = std::uint32_t(chart.labels.size());
    const auto put = [&](const void* v) {
        const auto* b = static_cast<const std::uint8_t*>(v);
        file.insert(file.end(), b, b + 4);
    };
    put(&count);
    for (const Chart::Label& label : chart.labels) {
        put(&label.name_id);
        put(&label.x);
        put(&label.z);
    }
    return file;
}

std::optional<Chart> Load(std::span<const std::uint8_t> file) {
    if (file.size() < 32 || std::memcmp(file.data(), "BBMAP5\0\0", 8) != 0) {
        return std::nullopt;
    }
    Chart chart;
    std::uint32_t stored = 0;
    std::memcpy(&chart.width, file.data() + 8, 4);
    std::memcpy(&chart.height, file.data() + 12, 4);
    std::memcpy(&chart.x0, file.data() + 16, 4);
    std::memcpy(&chart.z1, file.data() + 20, 4);
    std::memcpy(&chart.ppm, file.data() + 24, 4);
    std::memcpy(&stored, file.data() + 28, 4);
    if (chart.width <= 0 || chart.height <= 0 || chart.width > 8192 || chart.height > 8192 ||
        !(chart.ppm > 0.0f) || stored > file.size() - 32) {
        return std::nullopt;
    }
    chart.rgba.resize(std::size_t(chart.width) * std::size_t(chart.height) * 4);
    mz_ulong size = mz_ulong(chart.rgba.size());
    if (mz_uncompress(chart.rgba.data(), &size, file.data() + 32, stored) != MZ_OK || size != chart.rgba.size()) {
        return std::nullopt;
    }
    std::size_t at = 32 + stored;
    std::uint32_t count = 0;
    if (at + 4 <= file.size()) {
        std::memcpy(&count, file.data() + at, 4);
        at += 4;
    }
    for (std::uint32_t i = 0; i < count && at + 12 <= file.size() && i < 10000; ++i, at += 12) {
        Chart::Label label;
        std::memcpy(&label.name_id, file.data() + at, 4);
        std::memcpy(&label.x, file.data() + at + 4, 4);
        std::memcpy(&label.z, file.data() + at + 8, 4);
        chart.labels.push_back(label);
    }
    return chart;
}

std::string MapNameOf(std::uint32_t packed_map) {
    char name[16];
    std::snprintf(name, sizeof(name), "m%02u_%02u_00_00", (packed_map >> 24) & 0xFF, (packed_map >> 16) & 0xFF);
    return name;
}

std::optional<Chart> ChartArea(std::uint32_t packed_map, const ReadFile& read, float ppm, int max_side, int threads,
                               const ReadRange& read_range) {
    const std::array<std::uint32_t, 1> one{packed_map};
    return ChartAreas(one, read, ppm, max_side, threads, read_range);
}

namespace {

/// The material colours of an area's textures (map/mAA/*.tpfbhd, their low-resolution "_l"
/// copies), worked out when first asked; safe from several threads.
class TextureColors {
public:
    TextureColors(const ReadFile& read, const ReadRange& read_range) : read{read}, read_range{read_range} {}

    /// The archives of area folder `folder` ("m24") in the index.
    void AddFolder(const std::string& folder, std::span<const std::string> archives) {
        std::scoped_lock lock{mutex};
        for (const std::string& archive : archives) {
            const std::string bhd = "map/" + folder + "/" + archive + ".tpfbhd";
            for (const ArchiveEntry& entry : ReadArchiveHeader(read(bhd))) {
                index.try_emplace(entry.name, Where{"map/" + folder + "/" + archive + ".tpfbdt", entry.offset, entry.size});
            }
        }
    }

    std::uint32_t Of(std::string_view texture) {
        if (!read_range) {
            return 0;
        }
        const std::string name(texture);
        Where where;
        {
            std::scoped_lock lock{mutex};
            if (const auto known = colors.find(name); known != colors.end()) {
                return known->second;
            }
            auto found = index.find(name + "_l.tpf.dcx");
            if (found == index.end()) {
                found = index.find(name + ".tpf.dcx");
            }
            if (found == index.end()) {
                colors[name] = 0;
                return 0;
            }
            where = found->second;
        }
        const auto packed = read_range(where.file, where.offset, where.size);
        const auto tpf = BbGameArt::InflateDcx(packed);
        const std::uint32_t color = tpf ? AverageColor(*tpf) : 0;
        std::scoped_lock lock{mutex};
        colors[name] = color;
        return color;
    }

private:
    struct Where {
        std::string file;
        std::uint64_t offset = 0, size = 0;
    };
    const ReadFile& read;
    const ReadRange& read_range;
    std::mutex mutex;
    std::map<std::string, Where> index;
    std::map<std::string, std::uint32_t> colors;
};

/// The collision meshes of an area (map/mAA_BB_00_00/hAA_BB_00_00.hkxbhd/.hkxbdt, the .patch ones
/// over them) by their file names' stems ("h24_01_00_00_000100").
std::map<std::string, std::vector<std::uint8_t>> CollisionFiles(const std::string& map_name, const ReadFile& read) {
    std::map<std::string, std::vector<std::uint8_t>> files;
    const std::string stem = "map/" + map_name + "/h" + map_name.substr(1);
    for (const std::string suffix : {"", ".patch"}) {
        const auto bhd = read(stem + ".hkxbhd" + suffix);
        if (bhd.empty()) {
            continue;
        }
        const auto bdt = read(stem + ".hkxbdt" + suffix);
        for (const ArchiveEntry& entry : ReadArchiveHeader(bhd)) {
            if (entry.offset + entry.size > bdt.size()) {
                continue;
            }
            std::string name = entry.name.substr(entry.name.find_last_of("\\/") + 1);
            name = name.substr(0, name.find('.'));
            files[name].assign(bdt.begin() + std::ptrdiff_t(entry.offset),
                               bdt.begin() + std::ptrdiff_t(entry.offset + entry.size));
        }
    }
    return files;
}

/// The texture archives of area AA's folder: mAA_0000 .. mAA_0009 and mAA_BB_0000 .. (those
/// that are there).
std::vector<std::string> ArchivesOf(const std::string& map_name, const ReadFile& exists) {
    std::vector<std::string> archives;
    const std::string area = map_name.substr(0, 3), block = map_name.substr(0, 6);
    for (const std::string& prefix : {area, block}) {
        for (int i = 0; i < 10; ++i) {
            char name[32];
            std::snprintf(name, sizeof(name), "%s_%04d", prefix.c_str(), i);
            if (std::find(archives.begin(), archives.end(), name) == archives.end() &&
                !exists("map/" + area + "/" + name + ".tpfbhd").empty()) {
                archives.emplace_back(name);
            }
        }
    }
    return archives;
}

} // namespace

std::optional<Chart> ChartAreas(std::span<const std::uint32_t> packed_maps, const ReadFile& read, float ppm,
                                int max_side, int threads, const ReadRange& read_range) {
    TextureColors textures(read, read_range);
    const auto inflate = [](std::vector<std::uint8_t> file) {
        if (file.size() >= 4 && std::memcmp(file.data(), "DCX\0", 4) == 0) {
            return BbGameArt::InflateDcx(file).value_or(std::vector<std::uint8_t>{});
        }
        return file;
    };
    // Each area's pieces, filtered by its own play area; the picture spans them all.
    struct Source {
        std::string name;
        MapStudio studio;
        PlayArea area;
    };
    std::vector<Source> sources;
    std::optional<PlayArea> all;
    for (const std::uint32_t packed_map : packed_maps) {
        const std::string name = MapNameOf(packed_map);
        auto studio = ReadMapStudio(inflate(read("map/MapStudio/" + name + ".msb.dcx")));
        const auto area = studio && !studio->pieces.empty() ? PlayAreaOf(studio->spots) : std::nullopt;
        if (!area) {
            continue;
        }
        all = all ? PlayArea{std::min(all->x0, area->x0), std::max(all->x1, area->x1), std::min(all->z0, area->z0),
                             std::max(all->z1, area->z1), std::max(all->ceiling, area->ceiling)}
                  : *area;
        sources.push_back(Source{name, std::move(*studio), *area});
        if (read_range) {
            textures.AddFolder(name.substr(0, 3), ArchivesOf(name, read));
        }
    }
    if (sources.empty()) {
        return std::nullopt;
    }
    // A model at a time, every piece of it: tens of millions of triangles never at once. Each
    // thread draws every `threads`-th model on its own canvas; the canvases are merged.
    struct Model {
        const Source* source;
        std::string name;
        std::vector<const Piece*> pieces;
    };
    std::vector<Model> models;
    for (const Source& source : sources) {
        std::map<std::string, std::vector<const Piece*>> by_model;
        for (const Piece& piece : source.studio.pieces) {
            by_model[piece.model].push_back(&piece);
        }
        for (auto& [name, pieces] : by_model) {
            models.push_back(Model{&source, name, std::move(pieces)});
        }
    }
    threads = std::clamp(threads, 1, 16);
    std::vector<Canvas> canvases(std::size_t(threads), Canvas(*all, ppm, max_side));
    // Kaniel 2026-10-18: the collision meshes' floors: where one can walk (what lies far from it
    // goes) and where each place the game names lies (its label).
    std::map<std::int32_t, std::array<double, 3>> places; // name id -> area-weighted x, z, area
    for (const Source& source : sources) {
        const auto files = CollisionFiles(source.name, read);
        std::map<std::string, std::vector<float>> meshes;
        for (const MapStudio::Collision& collision : source.studio.collisions) {
            const std::string& model = collision.piece.model;
            auto mesh = meshes.find(model);
            if (mesh == meshes.end()) {
                std::vector<float> triangles;
                auto file = files.find(model);
                if (file == files.end()) {
                    file = files.find("h" + source.name.substr(1) + "_" + model.substr(1));
                }
                if (file != files.end()) {
                    AppendCollisionTriangles(inflate(file->second), triangles);
                }
                mesh = meshes.emplace(model, Floors(triangles)).first;
            }
            std::vector<float> world;
            Place(collision.piece, mesh->second, PlayArea{-1e9f, 1e9f, -1e9f, 1e9f, 1e9f}, world);
            canvases[0].AddWalkable(world);
            // Area names (ids ending in 0) are the title already.
            if (collision.place_name > 0 && collision.place_name % 10 != 0) {
                auto& place = places[collision.place_name];
                for (std::size_t t = 0; t + 9 <= world.size(); t += 9) {
                    const float a = TriangleArea(world.data() + t);
                    place[0] += a * (world[t] + world[t + 3] + world[t + 6]) / 3.0;
                    place[1] += a * (world[t + 2] + world[t + 5] + world[t + 8]) / 3.0;
                    place[2] += a;
                }
            }
        }
    }
    // The game's colour grade of the (first) area's block.
    if (const auto tpf = BbGameArt::InflateDcx(read("map/" + sources[0].name.substr(0, 3) + "/" +
                                                   sources[0].name.substr(0, 3) + "_cgrading.tpf.dcx"))) {
        if (auto grade = ReadColorGrade(*tpf, sources[0].name.substr(0, 6))) {
            canvases[0].SetGrade(std::move(*grade));
        }
    }
    const auto draw = [&](int part) {
        std::vector<float> model, world;
        std::vector<std::uint32_t> model_colors, world_colors;
        const MaterialColor color_of = [&](std::string_view texture) { return textures.Of(texture); };
        for (std::size_t m = std::size_t(part); m < models.size(); m += std::size_t(threads)) {
            const Model& it = models[m];
            const std::string& map = it.source->name;
            model.clear();
            model_colors.clear();
            const auto flver = inflate(read("map/" + map + "/" + map + "_" + it.name.substr(1) + ".flver.dcx"));
            if (!AppendTriangles(flver, model, &model_colors, color_of) || SkippedModel(it.name, model)) {
                continue;
            }
            for (const Piece* piece : it.pieces) {
                world.clear();
                world_colors.clear();
                Place(*piece, model, it.source->area, world, model_colors, &world_colors);
                canvases[std::size_t(part)].Add(world, world_colors);
            }
        }
    };
    std::vector<std::thread> workers;
    for (int part = 1; part < threads; ++part) {
        workers.emplace_back(draw, part);
    }
    draw(0);
    for (std::thread& worker : workers) {
        worker.join();
    }
    for (std::size_t part = 1; part < canvases.size(); ++part) {
        canvases[0].Merge(canvases[part]);
    }
    Chart chart = canvases[0].Finish();
    for (const auto& [id, place] : places) {
        if (place[2] > 0) {
            chart.labels.push_back(Chart::Label{id, float(place[0] / place[2]), float(place[1] / place[2])});
        }
    }
    return chart;
}

} // namespace BbMapArt
