// SPDX-License-Identifier: GPL-2.0-or-later
// bbport co-op: the map screen's pictures of the areas (Kaniel 2026-10-17: an Elden Ring style map).
// Bloodborne has no map, so each area's is drawn from the player's own game files the first time
// it is wanted and kept in the user folder; nothing of the game's is shipped. An area's MSB
// (map/MapStudio/mAA_BB_00_00.msb.dcx) places its map pieces, each a FLVER model
// (map/mAA_BB_00_00/mAA_BB_00_00_NNNNNN.flver.dcx); their triangles seen from above, the highest
// one at each pixel, shaded by its slope and height, make a picture of the area in ink on paper.
// Positions are the game's metres on that map: x east-west, y up, z north-south; the picture has
// +z up and +x right, as the camera's heading turns (bloodborne_compass.h: 0 looks along +z,
// turning right goes toward +x).
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace BbMapArt {

using Vec3 = std::array<float, 3>;

/// A map piece placed by the MSB: its model ("m000100"), where it stands, its rotation in degrees
/// about x, y and z, and its scale.
struct Piece {
    std::string model;
    Vec3 position{}, rotation{}, scale{1.0f, 1.0f, 1.0f};
};
/// What an area's MSB says for its picture: the map pieces, and the spots players reach (its
/// characters and its points: where the picture is wanted).
struct MapStudio {
    std::vector<Piece> pieces;
    std::vector<Vec3> spots;
};
/// The MSB (inflated); nothing if it is not one.
std::optional<MapStudio> ReadMapStudio(std::span<const std::uint8_t> msb);

/// The highest-detail triangles of a FLVER2 model (inflated), three corners of x, y, z each,
/// appended to `out` in the model's space. Left out: the lower levels of detail and the meshes
/// whose material is the sky, the moon, the sun or water (a flat disc over a river). False if it
/// is not a FLVER.
bool AppendTriangles(std::span<const std::uint8_t> flver, std::vector<float>& out);

/// Models left out of the picture: the sky domes (m9xxxxx), the distant views (m008xxx, m009xxx)
/// and loose effect quads (a few huge flat triangles: fog, light shafts).
bool SkippedModel(std::string_view model, std::span<const float> triangles);

/// Where the picture is wanted: the middle of the spots (the outer 1% left out each way) and 40 m
/// round them, and a ceiling 25 m over the highest spots (a roof over the play area stays, the
/// cloud ring and the far towers go).
struct PlayArea {
    float x0 = 0, x1 = 0, z0 = 0, z1 = 0, ceiling = 0;
};
std::optional<PlayArea> PlayAreaOf(std::span<const Vec3> spots);

/// The piece's model triangles (`model`, its space) put where the piece stands, appended to `out`;
/// the ones whose middle is outside the play area or wholly above its ceiling are left out.
void Place(const Piece& piece, std::span<const float> model, const PlayArea& area, std::vector<float>& out);

/// An area's picture: RGBA rows, `ppm` pixels a metre; the pixel (px, py) shows x = x0 + px / ppm,
/// z = z1 - py / ppm (north up). Transparent where nothing is.
struct Chart {
    int width = 0, height = 0;
    float x0 = 0, z1 = 0, ppm = 1;
    std::vector<std::uint8_t> rgba;
    /// Where the world spot (x, z) is in the picture, in pixels.
    std::array<float, 2> PixelOf(float x, float z) const { return {(x - x0) * ppm, (z1 - z) * ppm}; }
};

/// The chart cut to what is drawn in it (a few pixels round it); its frame follows.
Chart Cropped(const Chart& chart);

/// The picture drawn up: world triangles added a model at a time (an area has tens of millions),
/// the highest at each pixel kept with its slope's light; Finish shades it in ink on paper. `ppm`
/// pixels a metre, fewer when the play area would be wider or taller than `max_side`; the picture
/// cropped to what was drawn.
class Canvas {
public:
    Canvas(const PlayArea& area, float ppm, int max_side);
    void Add(std::span<const float> triangles);
    /// Another canvas of the same play area and size drawn into this one (the higher surface wins):
    /// an area's models drawn by several threads at once.
    void Merge(const Canvas& other);
    Chart Finish() const;

private:
    int width = 0, height = 0;
    float x0 = 0, z1 = 0, ppm = 1;
    std::vector<float> top;          // the highest surface's height at each pixel (-inf: none)
    std::vector<std::uint8_t> light; // its slope's light, 0 to 255
};

/// A chart as kept in the user folder ("BBMAP1", the frame, then the pixels zlib-compressed), and
/// read back (nothing when it is not one).
std::vector<std::uint8_t> Save(const Chart& chart);
std::optional<Chart> Load(std::span<const std::uint8_t> file);

/// The folder name of a packed map (0x18010000 -> "m24_01_00_00").
std::string MapNameOf(std::uint32_t packed_map);

/// An area's picture from the game's files: `read` gives a file of dvdroot_ps4 by its relative
/// path ("map/MapStudio/m24_01_00_00.msb.dcx"), inflated or not as the file is, empty when it is
/// missing (called from `threads` threads at once). Nothing when the MSB is missing or places
/// nothing.
using ReadFile = std::function<std::vector<std::uint8_t>(const std::string& relative)>;
std::optional<Chart> ChartArea(std::uint32_t packed_map, const ReadFile& read, float ppm = 3.0f,
                               int max_side = 3072, int threads = 1);

} // namespace BbMapArt
