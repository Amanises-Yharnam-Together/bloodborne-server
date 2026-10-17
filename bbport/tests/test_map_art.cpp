// SPDX-License-Identifier: GPL-2.0-or-later
// bbport co-op: the map screen's pictures of the areas (shim/bbport_map_art.h), on made-up data: an
// MSB's pieces and spots, a FLVER's highest-detail triangles, the pieces placed, the picture from
// above north up, the file kept in the user folder. With BLOODBORNE_EBOOT naming the game's
// eboot.bin, Central Yharnam is drawn from the game's own files and its lamps land on the picture.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "gpu/shim/bbport_map_art.h"

using namespace BbMapArt;

namespace {

struct Blob {
    std::vector<std::uint8_t> bytes;
    std::size_t Size() const { return bytes.size(); }
    template <typename T>
    void Put(std::size_t at, T value) {
        if (bytes.size() < at + sizeof(T)) {
            bytes.resize(at + sizeof(T));
        }
        std::memcpy(bytes.data() + at, &value, sizeof(T));
    }
    template <typename T>
    std::size_t Add(T value) {
        const std::size_t at = bytes.size();
        Put(at, value);
        return at;
    }
    std::size_t AddUtf16(const std::string& text) {
        const std::size_t at = bytes.size();
        for (const char c : text) {
            Add<std::uint16_t>(std::uint16_t(c));
        }
        Add<std::uint16_t>(0);
        return at;
    }
    void Align(std::size_t n) {
        while (bytes.size() % n) {
            bytes.push_back(0);
        }
    }
};

struct MsbPart {
    std::string name;
    int model;
    Vec3 position, rotation, scale;
};

/// An MSB of three lists: models, points and parts (as the game's, records after each list).
std::vector<std::uint8_t> MakeMsb(const std::vector<std::string>& models, const std::vector<Vec3>& points,
                                  const std::vector<MsbPart>& parts) {
    Blob msb;
    msb.bytes = {'M', 'S', 'B', ' ', 1, 0, 0, 0, 0x10, 0, 0, 0, 0, 0, 0, 0};
    std::size_t link = 0; // where the previous list's next offset goes
    const auto list = [&](const std::string& name, std::size_t count, auto&& write_record) {
        msb.Align(8);
        const std::size_t at = msb.Size();
        if (link != 0) {
            msb.Put<std::int64_t>(link, std::int64_t(at));
        }
        msb.Add<std::int32_t>(3);
        msb.Add<std::int32_t>(std::int32_t(count + 1));
        const std::size_t name_at = msb.Add<std::int64_t>(0);
        const std::size_t table = msb.Size();
        for (std::size_t i = 0; i <= count; ++i) {
            msb.Add<std::int64_t>(0);
        }
        link = table + count * 8;
        msb.Put<std::int64_t>(name_at, std::int64_t(msb.AddUtf16(name)));
        for (std::size_t i = 0; i < count; ++i) {
            msb.Align(8);
            const std::size_t record = msb.Size();
            msb.Put<std::int64_t>(table + i * 8, std::int64_t(record));
            write_record(i, record);
        }
    };
    list("MODEL_PARAM_ST", models.size(), [&](std::size_t i, std::size_t record) {
        msb.Add<std::int64_t>(0x10); // its name follows
        msb.Add<std::int32_t>(0);
        msb.Add<std::int32_t>(std::int32_t(i));
        msb.Put<std::int64_t>(record, std::int64_t(msb.AddUtf16(models[i]) - record));
    });
    list("POINT_PARAM_ST", points.size(), [&](std::size_t i, std::size_t record) {
        msb.Put<std::int64_t>(record, 0);
        msb.Put<Vec3>(record + 0x14, points[i]);
    });
    list("PARTS_PARAM_ST", parts.size(), [&](std::size_t i, std::size_t record) {
        const MsbPart& part = parts[i];
        msb.Put<std::int64_t>(record + 0x00, 0);
        msb.Put<std::int64_t>(record + 0x08, 0); // patched below
        msb.Put<std::int32_t>(record + 0x1C, part.model);
        msb.Put<Vec3>(record + 0x28, part.position);
        msb.Put<Vec3>(record + 0x34, part.rotation);
        msb.Put<Vec3>(record + 0x40, part.scale);
        msb.Put<std::uint8_t>(record + 0x5F, 0);
        msb.Put<std::int64_t>(record + 0x08, std::int64_t(msb.AddUtf16(part.name) - record));
    });
    return msb.bytes;
}

struct Faceset {
    std::uint32_t flags;
    bool strip;
    std::vector<std::uint16_t> indices;
};
struct Mesh {
    std::string mtd;
    std::vector<Vec3> vertices;
    std::vector<Faceset> facesets;
};

/// A FLVER2 with each mesh its own material, vertex buffer (float3 position then 8 bytes of
/// something else) and facesets.
std::vector<std::uint8_t> MakeFlver(const std::vector<Mesh>& meshes) {
    std::size_t facesets = 0;
    for (const Mesh& m : meshes) {
        facesets += m.facesets.size();
    }
    Blob f;
    f.bytes.resize(0x80);
    std::memcpy(f.bytes.data(), "FLVER\0L\0", 8);
    f.Put<std::uint32_t>(0x08, 0x20014);
    f.Put<std::int32_t>(0x14, 0);                          // dummies
    f.Put<std::int32_t>(0x18, std::int32_t(meshes.size())); // materials
    f.Put<std::int32_t>(0x1C, 0);                          // bones
    f.Put<std::int32_t>(0x20, std::int32_t(meshes.size()));
    f.Put<std::int32_t>(0x24, std::int32_t(meshes.size())); // vertex buffers
    f.Put<std::int32_t>(0x50, std::int32_t(facesets));
    f.Put<std::int32_t>(0x54, 1); // one layout
    const std::size_t materials = 0x80;
    const std::size_t mesh_base = materials + meshes.size() * 0x20;
    const std::size_t faceset_base = mesh_base + meshes.size() * 0x30;
    const std::size_t buffer_base = faceset_base + facesets * 0x20;
    const std::size_t layout_base = buffer_base + meshes.size() * 0x20;
    f.bytes.resize(layout_base + 0x10);
    // The layout: position (float3, semantic 0) at 0, then a normal (byte4, semantic 2) at 12.
    const std::size_t members = f.Size();
    f.Put<std::int32_t>(layout_base, 2);
    f.Put<std::int32_t>(layout_base + 0xC, std::int32_t(members));
    f.Add<std::int32_t>(0), f.Add<std::int32_t>(0), f.Add<std::uint32_t>(2), f.Add<std::uint32_t>(0), f.Add<std::int32_t>(0);
    f.Add<std::int32_t>(0), f.Add<std::int32_t>(12), f.Add<std::uint32_t>(0x11), f.Add<std::uint32_t>(2), f.Add<std::int32_t>(0);
    // Lists, names, then the data (offsets from the data's start).
    std::vector<std::size_t> faceset_lists, buffer_lists;
    std::size_t faceset_index = 0;
    for (std::size_t m = 0; m < meshes.size(); ++m) {
        faceset_lists.push_back(f.Size());
        for (std::size_t k = 0; k < meshes[m].facesets.size(); ++k) {
            f.Add<std::int32_t>(std::int32_t(faceset_index++));
        }
        buffer_lists.push_back(f.Add<std::int32_t>(std::int32_t(m)));
        f.Put<std::int32_t>(materials + m * 0x20 + 4, std::int32_t(f.AddUtf16("N:\\mtd\\" + meshes[m].mtd)));
    }
    f.Align(16);
    const std::size_t data = f.Size();
    f.Put<std::uint32_t>(0x0C, std::uint32_t(data));
    faceset_index = 0;
    for (std::size_t m = 0; m < meshes.size(); ++m) {
        const Mesh& mesh = meshes[m];
        const std::size_t at = mesh_base + m * 0x30;
        f.Put<std::int32_t>(at + 4, std::int32_t(m));
        f.Put<std::int32_t>(at + 0x20, std::int32_t(mesh.facesets.size()));
        f.Put<std::int32_t>(at + 0x24, std::int32_t(faceset_lists[m]));
        f.Put<std::int32_t>(at + 0x28, 1);
        f.Put<std::int32_t>(at + 0x2C, std::int32_t(buffer_lists[m]));
        for (const Faceset& fs : mesh.facesets) {
            const std::size_t entry = faceset_base + faceset_index++ * 0x20;
            const std::size_t indices = f.Size() - data;
            for (const std::uint16_t i : fs.indices) {
                f.Add<std::uint16_t>(i);
            }
            f.Align(16);
            f.Put<std::uint32_t>(entry, fs.flags);
            f.Put<std::uint8_t>(entry + 4, fs.strip ? 1 : 0);
            f.Put<std::int32_t>(entry + 8, std::int32_t(fs.indices.size()));
            f.Put<std::uint32_t>(entry + 0xC, std::uint32_t(indices));
            f.Put<std::int32_t>(entry + 0x18, 16);
        }
        const std::size_t vertices = f.Size() - data;
        for (const Vec3& v : mesh.vertices) {
            f.Add<Vec3>(v);
            f.Add<std::uint64_t>(0x7F7F7F7F7F7F7F7Full);
        }
        const std::size_t vb = buffer_base + m * 0x20;
        f.Put<std::int32_t>(vb + 4, 0);
        f.Put<std::int32_t>(vb + 8, 20);
        f.Put<std::int32_t>(vb + 0xC, std::int32_t(mesh.vertices.size()));
        f.Put<std::int32_t>(vb + 0x18, std::int32_t(mesh.vertices.size() * 20));
        f.Put<std::uint32_t>(vb + 0x1C, std::uint32_t(vertices));
    }
    return f.bytes;
}

bool Near(float a, float b, float within = 1e-3f) {
    return std::abs(a - b) <= within;
}

std::vector<std::uint8_t> ReadAll(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}

const std::uint8_t* PixelAt(const Chart& chart, float x, float z) {
    const auto [px, py] = chart.PixelOf(x, z);
    const int ix = int(px), iy = int(py);
    if (ix < 0 || iy < 0 || ix >= chart.width || iy >= chart.height) {
        return nullptr;
    }
    return chart.rgba.data() + (std::size_t(iy) * std::size_t(chart.width) + std::size_t(ix)) * 4;
}

} // namespace

int main() {
    // The MSB: map pieces (an "m" part of an "m" model), characters ("c" parts) and points are
    // spots; anything else (an object's "o" part) is neither.
    {
        const auto msb = MakeMsb({"m000100", "c1000", "o0001"}, {{5, 6, 7}},
                                 {{"m000100_0000", 0, {1, 2, 3}, {0, 90, 0}, {1, 1, 2}},
                                  {"c1000_0000", 1, {10, 0, -10}, {}, {1, 1, 1}},
                                  {"o0001_0000", 2, {99, 99, 99}, {}, {1, 1, 1}}});
        const auto studio = ReadMapStudio(msb);
        assert(studio);
        assert(studio->pieces.size() == 1);
        const Piece& p = studio->pieces[0];
        assert(p.model == "m000100");
        assert(p.position == (Vec3{1, 2, 3}) && p.rotation == (Vec3{0, 90, 0}) && p.scale == (Vec3{1, 1, 2}));
        assert(studio->spots.size() == 2);
        assert(studio->spots[0] == (Vec3{10, 0, -10}) && studio->spots[1] == (Vec3{5, 6, 7}));
        assert(!ReadMapStudio(std::vector<std::uint8_t>(64, 0)));
    }

    // The FLVER: the highest detail's triangles (a list, and a strip with its restart index); the
    // lower levels and the water's mesh left out.
    {
        Mesh ground{"M[ARSN]_l.mtd",
                    {{0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {1, 0, 1}},
                    {{0, false, {0, 1, 2, 1, 3, 2}}, {0x01000000, false, {0, 1, 2}}, {0x02000000, false, {0, 1, 2}}}};
        Mesh stairs{"M[ARSN].mtd", {{0, 1, 0}, {1, 1, 0}, {0, 2, 1}, {1, 2, 1}}, {{0, true, {0, 1, 2, 3, 0xFFFF, 0, 1, 2}}}};
        Mesh water{"M[ARSN]_water.mtd", {{0, 5, 0}, {9, 5, 0}, {0, 5, 9}}, {{0, false, {0, 1, 2}}}};
        std::vector<float> out;
        assert(AppendTriangles(MakeFlver({ground, stairs, water}), out));
        // ground 2, stairs: (0 1 2) (1 2 3) then the restart drops the windows over it, then (0 1 2).
        assert(out.size() == 9 * 5);
        assert(out[0] == 0 && out[3] == 1 && out[8] == 1); // the ground's first corner set
        for (std::size_t i = 1; i < out.size(); i += 3) {
            assert(out[i] < 5.0f); // nothing of the water's
        }
        std::vector<float> none;
        assert(!AppendTriangles(std::vector<std::uint8_t>(0x100, 0), none) && none.empty());
    }

    // Models left out: the sky (m9xxxxx), the distant views (m008xxx), a quad of huge triangles.
    {
        const std::vector<float> small{0, 0, 0, 1, 0, 0, 0, 0, 1};
        const std::vector<float> huge{0, 0, 0, 30, 0, 0, 0, 0, 30};
        assert(SkippedModel("m900000", small) && SkippedModel("m008020", small));
        assert(!SkippedModel("m000100", small) && SkippedModel("m000100", huge));
        assert(SkippedModel("m000100", {}));
        std::vector<float> many;
        for (int i = 0; i < 70; ++i) {
            many.insert(many.end(), huge.begin(), huge.end());
        }
        assert(!SkippedModel("m000100", many)); // a big model's big floor stays
        // A backdrop: 70 triangles spread over a kilometre square (the Dream's sea of flowers).
        std::vector<float> backdrop = many;
        backdrop.insert(backdrop.end(), {0, 0, 0, 1000, 0, 0, 0, 0, 1000});
        assert(SkippedModel("m002000", backdrop));
    }

    // The play area: the middle of the spots, 40 m round, the ceiling 25 m over the highest.
    {
        std::vector<Vec3> spots;
        for (int i = 0; i <= 100; ++i) {
            spots.push_back({float(i), float(i % 10), float(-i)});
        }
        spots.push_back({5000, 0, 0}); // a stray point far out
        const auto area = PlayAreaOf(spots);
        assert(area && Near(area->x0, -39.0f) && area->x1 < 200.0f && Near(area->z1, 40.0f));
        assert(Near(area->ceiling, 34.0f));
        assert(!PlayAreaOf({}));
    }

    // Placing: scale, rotation (y by 90 degrees turns +x to -z), then the position; triangles out of
    // the play area or above its ceiling stay out.
    {
        const PlayArea area{-100, 100, -100, 100, 50};
        const std::vector<float> model{1, 0, 0, 2, 0, 0, 1, 0, 1};
        std::vector<float> out;
        Place(Piece{"m000100", {10, 1, 20}, {0, 90, 0}, {2, 1, 1}}, model, area, out);
        assert(out.size() == 9);
        assert(Near(out[0], 10.0f) && Near(out[1], 1.0f) && Near(out[2], 18.0f)); // (2, 0, 0) -> (0, 0, -2)
        assert(Near(out[3], 10.0f) && Near(out[5], 16.0f));
        assert(Near(out[6], 11.0f) && Near(out[8], 18.0f)); // (2, 0, 1) -> (1, 0, -2)
        out.clear();
        Place(Piece{"m000100", {500, 0, 0}, {}, {1, 1, 1}}, model, area, out);
        Place(Piece{"m000100", {0, 60, 0}, {}, {1, 1, 1}}, model, area, out);
        assert(out.empty());
        // About x by 90 degrees: +y goes to +z (the order x, z, y as the map tools turn).
        Place(Piece{"m000100", {}, {90, 0, 0}, {1, 1, 1}}, std::vector<float>{0, 1, 0, 0, 1, 0, 0, 1, 0}, area, out);
        assert(Near(out[0], 0.0f) && Near(out[1], 0.0f) && Near(out[2], 1.0f));
    }

    // The picture: north up, the highest surface wins, nothing is transparent, the shading lighter
    // on high ground than low.
    {
        const PlayArea area{0, 30, 0, 10, 100};
        Canvas canvas(area, 2.0f, 4096);
        // Low ground over the west half, a roof 6 m up over the east half's north.
        canvas.Add(std::vector<float>{0, 0, 0, 10, 0, 0, 0, 0, 10, 10, 0, 0, 10, 0, 10, 0, 0, 10});
        canvas.Add(std::vector<float>{12, 6, 6, 18, 6, 6, 12, 6, 9, 18, 6, 6, 18, 6, 9, 12, 6, 9});
        canvas.Add(std::vector<float>{12, -3, 6, 18, -3, 6, 12, -3, 9}); // under the roof: hidden
        const Chart chart = canvas.Finish();
        // Cropped to what is drawn: the east edge's empty strip goes.
        assert(chart.width < 50 && chart.height == 20 && Near(chart.ppm, 2.0f));
        assert(Near(chart.PixelOf(0, 10)[0], 0.0f) && Near(chart.PixelOf(0, 10)[1], 0.0f)); // north-west corner
        const std::uint8_t* low = PixelAt(chart, 5, 5);
        const std::uint8_t* roof = PixelAt(chart, 15, 8);
        const std::uint8_t* south_east = PixelAt(chart, 15, 2);
        assert(low && low[3] == 255 && roof && roof[3] == 255);
        assert(south_east && south_east[3] == 0);
        assert(roof[0] > low[0]); // pale high ground, darker low ground
        // Two canvases of one area (two threads' models) merge: the higher surface wins.
        Canvas first(area, 2.0f, 4096), second(area, 2.0f, 4096);
        first.Add(std::vector<float>{0, 0, 0, 10, 0, 0, 0, 0, 10, 10, 0, 0, 10, 0, 10, 0, 0, 10});
        second.Add(std::vector<float>{12, 6, 6, 18, 6, 6, 12, 6, 9, 18, 6, 6, 18, 6, 9, 12, 6, 9});
        second.Add(std::vector<float>{0, -3, 0, 10, -3, 0, 0, -3, 10}); // under the first's ground
        first.Merge(second);
        const Chart merged = first.Finish();
        assert(merged.rgba == chart.rgba);
        // A picture wider than max_side has fewer pixels a metre.
        Canvas wide(PlayArea{0, 1000, 0, 100, 10}, 3.0f, 500);
        assert(wide.Finish().width == 500);
    }

    // The file in the user folder round trips; anything else is no chart.
    {
        Chart chart;
        chart.width = 3;
        chart.height = 2;
        chart.x0 = -5.5f;
        chart.z1 = 7.25f;
        chart.ppm = 2.5f;
        for (int i = 0; i < 24; ++i) {
            chart.rgba.push_back(std::uint8_t(i * 10));
        }
        const auto file = Save(chart);
        const auto back = Load(file);
        assert(back && back->width == 3 && back->height == 2 && back->x0 == -5.5f && back->z1 == 7.25f &&
               back->ppm == 2.5f && back->rgba == chart.rgba);
        auto broken = file;
        broken[0] = 'X';
        assert(!Load(broken));
        assert(!Load(std::vector<std::uint8_t>(file.begin(), file.begin() + 40)));
    }

    assert(MapNameOf(0x18010000) == "m24_01_00_00" && MapNameOf(0x15000000) == "m21_00_00_00");

    // The game's own Central Yharnam: its lamps stand on the picture.
    if (const char* eboot = std::getenv("BLOODBORNE_EBOOT")) {
        const auto dvdroot = std::filesystem::path(eboot).parent_path() / "dvdroot_ps4";
        if (std::filesystem::exists(dvdroot / "map" / "MapStudio" / "m24_01_00_00.msb.dcx")) {
            const auto chart = ChartArea(
                0x18010000, [&](const std::string& relative) { return ReadAll(dvdroot / relative); }, 2.0f, 2048,
                4);
            assert(chart && chart->width > 500 && chart->height > 500);
            // 1st Floor Sickroom, Central Yharnam, Great Bridge (bloodborne_lamp_points.h).
            for (const auto& [x, z] : {std::pair{-199.74f, 179.42f}, {-193.4f, 68.5f}, {-124.8f, 64.3f}}) {
                const std::uint8_t* px = PixelAt(*chart, x, z);
                assert(px && px[3] == 255);
            }
            std::printf("map-art: Central Yharnam drawn %dx%d\n", chart->width, chart->height);
        }
    }
    std::printf("map-art: all passed\n");
    return 0;
}
