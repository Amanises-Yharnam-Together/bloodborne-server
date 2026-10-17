// SPDX-License-Identifier: GPL-2.0-or-later
// bbport co-op: an area's map picture as the map screen draws it (shim/bbport_map_art.h), to a PNG.
//   map-art-tool <dvdroot_ps4> <packed map, hex: 18010000> <out.png> [pixels a metre] [threads]
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
    const auto packed = std::uint32_t(std::strtoul(argv[2], nullptr, 16));
    const float ppm = argc > 4 ? std::strtof(argv[4], nullptr) : 3.0f;
    const auto start = std::chrono::steady_clock::now();
    const auto chart = BbMapArt::ChartArea(packed, [&](const std::string& relative) {
        std::ifstream file(root / relative, std::ios::binary);
        return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(file), {}};
    }, ppm, 3072, argc > 5 ? std::atoi(argv[5]) : 4);
    if (!chart) {
        std::fprintf(stderr, "no map for %08x\n", packed);
        return 1;
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    size_t size = 0;
    void* png = tdefl_write_image_to_png_file_in_memory(chart->rgba.data(), chart->width, chart->height, 4, &size);
    std::ofstream(argv[3], std::ios::binary).write(static_cast<const char*>(png), std::streamsize(size));
    mz_free(png);
    std::printf("%s: %dx%d at %.2f px/m, x0 %.2f z1 %.2f, drawn in %.1f s\n", argv[3], chart->width, chart->height,
                chart->ppm, chart->x0, chart->z1, seconds);
    return 0;
}
