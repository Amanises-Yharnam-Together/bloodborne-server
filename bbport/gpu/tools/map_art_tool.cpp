// SPDX-License-Identifier: GPL-2.0-or-later
// bbport co-op: an area's map picture as the map screen draws it (shim/bbport_map_art.h), to a PNG.
//   map-art-tool <dvdroot_ps4> <packed maps, hex: 18010000[,18000000...]> <out.png> [pixels a metre] [threads]
//                [floors at height]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../shim/bbport_map_art.h"
#include "miniz.h"

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "map-art-tool <dvdroot_ps4> <packed map hex> <out.png> [ppm]\n");
        return 2;
    }
    const std::filesystem::path root = argv[1];
    // One map, or several in one picture: 18000000,18010000.
    std::vector<std::uint32_t> maps;
    for (const char* at = argv[2]; *at;) {
        char* end = nullptr;
        maps.push_back(std::uint32_t(std::strtoul(at, &end, 16)));
        at = *end == ',' ? end + 1 : end;
    }
    const std::uint32_t packed = maps.front();
    const float ppm = argc > 4 ? std::strtof(argv[4], nullptr) : 3.0f;
    const auto start = std::chrono::steady_clock::now();
    const auto chart = BbMapArt::ChartAreas(maps, [&](const std::string& relative) {
        std::ifstream file(root / relative, std::ios::binary);
        return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(file), {}};
    }, ppm, 3072, argc > 5 ? std::atoi(argv[5]) : 4,
        [&](const std::string& relative, std::uint64_t offset, std::uint64_t size) {
            std::ifstream file(root / relative, std::ios::binary);
            std::vector<std::uint8_t> bytes(size);
            file.seekg(std::streamoff(offset));
            file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(size));
            bytes.resize(std::size_t(file.gcount()));
            return bytes;
        });
    if (!chart) {
        std::fprintf(stderr, "no map for %08x\n", packed);
        return 1;
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const auto write = [&](const std::vector<std::uint8_t>& rgba, const std::string& path) {
        size_t size = 0;
        void* png = tdefl_write_image_to_png_file_in_memory(rgba.data(), chart->width, chart->height, 4, &size);
        std::ofstream(path, std::ios::binary).write(static_cast<const char*>(png), std::streamsize(size));
        mz_free(png);
    };
    write(chart->rgba, argv[3]);
    // A sixth argument, heights (-12.5,3): also the floors as one standing at each sees them
    // (BbMapArt::FloorView), in <out>-floors<height>.png.
    for (const char* at = argc > 6 ? argv[6] : ""; *at;) {
        char* end = nullptr;
        const float height = std::strtof(at, &end);
        std::vector<std::uint8_t> rgba = chart->rgba;
        BbMapArt::FloorView(*chart, height, rgba.data());
        const std::string out = argv[3];
        write(rgba, out.substr(0, out.size() - 4) + "-floors" + std::to_string(int(height)) + ".png");
        at = *end == ',' ? end + 1 : end;
    }
    for (const auto& label : chart->labels) {
        std::printf("label %d at %.1f %.1f\n", label.name_id, label.x, label.z);
    }
    std::printf("%s: %dx%d at %.2f px/m, x0 %.2f z1 %.2f, drawn in %.1f s\n", argv[3], chart->width, chart->height,
                chart->ppm, chart->x0, chart->z1, seconds);
    return 0;
}
