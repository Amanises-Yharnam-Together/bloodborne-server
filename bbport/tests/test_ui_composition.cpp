// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cmath>
#include <cstdio>
#include "gpu/shadps4/video_core/renderer_vulkan/ui_composition.h"

int main() {
    using namespace Vulkan::UiComposition;
    // Title/loading menus have no depth/camera. UI must still go to the native target.
    assert(Choose(true, true, false, true) == Background::Copy);
    assert(Choose(true, true, false, false) == Background::Copy);
    // In-game HUD: first reconstruct the scene, then draw the native UI over it.
    assert(Choose(true, true, true, true) == Background::Temporal);
    // Runtime FSR disable/failure must not change UI resolution.
    assert(Choose(true, true, true, false) == Background::Copy);
    assert(Choose(true, false, true, true) == Background::None);
    assert(Choose(false, true, true, true) == Background::None);
    assert(NativeViewport(1920, -1080));
    assert(!NativeViewport(960, -540));
    assert(MovieShader(0x34e8a281) && MovieShader(0x24042a9b));
    assert(!MovieShader(0x0b0acf50)); // fullscreen post/tonemap also has a native viewport
    assert(!MovieShader(0x6c62a79f)); // shadow geometry
    const auto display = Scale(960, 540, 1920, 1080, false);
    assert(display[0] == 2 && display[1] == 2);
    const auto ui = Scale(960, 540, 1920, 1080, true);
    assert(ui[0] == 1 && ui[1] == 1); // don't double an already native UI viewport
    const auto larger = Scale(960, 540, 3840, 2160, true);
    assert(larger[0] == 2 && larger[1] == 2);
    // Another aspect ratio: the game's UI is laid out for it already (its movies' viewport), so the
    // 1920x1080 frame fills the whole output, each axis by its own factor.
    const auto wide = Scale(1920, 1080, 2560, 1080, true);
    assert(wide[0] == 2560.0f / 1920.0f && wide[1] == 1.0f);
    // Where the game's 16:9 screen ends: x on a wider output, y on a taller one, 1 at 16:9.
    const auto e16 = ScreenEdges(3840, 2160);
    assert(e16[0] == 1.0f && e16[1] == 1.0f);
    const auto e21 = ScreenEdges(2560, 1080);
    assert(std::abs(e21[0] - 0.75f) < 1e-6f && e21[1] == 1.0f);
    const auto e32 = ScreenEdges(5120, 1440);
    assert(std::abs(e32[0] - 0.5f) < 1e-6f && e32[1] == 1.0f);
    const auto e43 = ScreenEdges(1600, 1200);
    assert(e43[0] == 1.0f && std::abs(e43[1] - 0.75f) < 1e-6f);
    // The edge parameter of the vertex shaders' corner snap: 0 never (that is "off"), x in the high
    // 16 bits, y in bits 1-15, bit 0 the viewport's y direction.
    const uint32_t p = EdgeParam(0.75f, 1.0f, true);
    assert(p != 0 && (p & 1u) == 1u);
    assert(std::abs(float(p >> 16) / 65535.0f - 0.75f) < 1e-4f);
    assert(std::abs(float((p & 0xfffeu) >> 1) / 32767.0f - 1.0f) < 1e-4f);
    assert((EdgeParam(0.75f, 1.0f, false) & 1u) == 0u);
    assert(EdgeParam(0.0f, 0.0f, false) != 0u);
    std::puts("UI composition: PASS (menu without camera, HUD, FSR off, viewport scaling, screen edges)");
}
