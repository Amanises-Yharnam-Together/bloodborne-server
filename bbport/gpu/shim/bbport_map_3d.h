// SPDX-License-Identifier: GPL-2.0-or-later
// bbport co-op: the map screen's live 3D view (Kaniel 2026-10-19: "an opt in option to view rendered
// in real time 3d map"). The areas' thinned models (BbMapArt::AreaMesh) kept on the GPU and drawn
// with depth into a picture of their own, before the overlay draws, with the map screen's camera;
// the map screen shows that picture where its map goes.
#pragma once

#include <cstdint>
#include <memory>

#include "imgui.h"

#include "bbport_map_art.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {
class Instance;
}

namespace BbMap3d {

/// The map screen's camera this frame: the frame's place on screen (pixels), the spot in its
/// middle (atlas metres), its height, the turn and tilt (radians) and the zoom (pixels a metre).
struct Camera {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float cx = 0, cy = 0, cz = 0;
    float yaw = 0, pitch = 0, zoom = 1;
};

/// With the overlay's Vulkan instance (once); the pass itself is made when the live view is first used.
bool Init(const Vulkan::Instance& instance);
/// An area's mesh on the GPU, placed at (dx, dz) on the atlas (kept until Clear).
void Upload(std::uint32_t map, const std::shared_ptr<const BbMapArt::AreaMesh>& mesh, float dx, float dz);
bool Has(std::uint32_t map);
/// The meshes go (the live view was turned off): waits for the GPU first.
void Clear();
/// Asks for this frame's picture (from Map::Screen); its texture for ImGui, null until the pass
/// can draw.
ImTextureRef Request(const Camera& camera, ImVec2& uv1);
/// Draws the asked-for picture into `cmdbuf` (before the overlay's own pass), if one was asked.
void Draw(vk::CommandBuffer cmdbuf);

} // namespace BbMap3d
