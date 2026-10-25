// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: hooks in the game's code that write GPU memory (BB_GUEST_IN_PLACE). Where the game
// copies resource data into memory the GPU uses, the GPU side is told once the data is there, as a
// driver is told of a resource update; the write needs no page protection to be seen. Each site
// is checked byte for byte against the expected code (another game version: not installed).
#pragma once

#include <cstdint>

namespace BbGuestHooks {
/// [address, address + size) of GPU memory the game has just handed out again: its old contents
/// are dead and whatever writes it next may be code the GPU side does not hear of.
using RangeCallback = void (*)(std::uint64_t address, std::uint64_t size);

/// Installs the hooks once (GPU side, after the game is loaded).
void Install(RangeCallback on_gpu_range_allocated);

/// The output's aspect ratio (the upscaler, when its size changes): the camera's from its next
/// update (wider: more to the sides, taller: more above and below), the HUD markers' places, the
/// game's UI movies laid out for it and the HUD's groups at its edges. 16:9: the game's.
void SetDisplayAspect(float aspect);
/// Whether the camera's aspect hooks are in (the 1.09 code there; not with BB_GUEST_HOOKS=0 or
/// BB_ASPECT_HOOKS=0): without them the output keeps the game's 16:9 picture.
bool AspectSupported();
/// How far the HUD's groups are moved toward the output's edges, in the game's 1920x1080 UI
/// pixels: the left and right groups by dx, the top and bottom ones by dy (0 at 16:9, without the
/// HUD hooks and with BB_HUD_ANCHOR=0). The port's co-op HUD follows them.
struct HudShift {
    float dx = 0.0f, dy = 0.0f;
};
HudShift HudEdgeShift();
} // namespace BbGuestHooks
