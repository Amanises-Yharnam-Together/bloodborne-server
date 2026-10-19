// SPDX-License-Identifier: GPL-2.0-or-later
// host_shaders/hdr_highlight.glsl, compiled as C++: the HDR highlight curve on values measured
// in a frame at Central Yharnam's first lamp (Kaniel 2026-10-19, PG27UCDM at white 250, peak
// 1000: headroom 4). Linear luminance of the UI-free SDR scene, 95th percentiles: lamp core
// 0.69, wall sconce 0.47, lamp halo 0.24, floor 0.06; the game's lights stop near 0.7.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>

// GLSL's float functions (its 1.0 literals are float there, double here).
namespace glsl {
inline float max(float a, double b) {
    return std::max(a, float(b));
}
inline float min(float a, double b) {
    return std::min(a, float(b));
}
inline float clamp(float x, double lo, double hi) {
    return std::min(std::max(x, float(lo)), float(hi));
}
inline float mix(double a, double b, float t) {
    return float(a + (b - a) * t);
}
inline float log2(float x) {
    return std::log2(x);
}
inline float pow(float x, double y) {
    return std::pow(x, float(y));
}
#include "hdr_highlight.glsl"
} // namespace glsl

int main() {
    using glsl::HighlightGain;
    const float headroom = 4.0f;
    // Off: the SDR picture.
    assert(HighlightGain(0.69f, headroom, 0.0f) == 1.0f);
    // The game's dark mid-tones keep its grading.
    assert(HighlightGain(0.06f, headroom, 0.5f) == 1.0f && "floor");
    assert(HighlightGain(0.01f, headroom, 1.0f) == 1.0f);
    // Its lights reach the peak: the lamp's core, though the game's tonemap stops it at 0.69.
    const float lamp = 0.69f * HighlightGain(0.69f, headroom, 0.5f);
    assert(lamp > 0.9f * headroom && lamp <= headroom && "lamp core near the peak");
    // A sconce clearly brighter, the halo a little; brighter in stays brighter out.
    const float sconce = 0.47f * HighlightGain(0.47f, headroom, 0.5f);
    const float halo = 0.24f * HighlightGain(0.24f, headroom, 0.5f);
    assert(sconce > 2.0f * 0.47f && sconce < lamp);
    assert(halo > 0.24f && halo < sconce);
    for (float l = 0.0f; l <= 1.0f; l += 0.01f) {
        const float a = l * HighlightGain(l, headroom, 0.5f);
        const float b = (l + 0.01f) * HighlightGain(l + 0.01f, headroom, 0.5f);
        assert(b >= a - 1e-5f && "monotonic (to float rounding at the peak)");
        assert(a <= headroom * 1.0001f && "never past the peak");
    }
    // More highlights: more of the picture brightens (the halo), no more than the peak.
    assert(0.24f * HighlightGain(0.24f, headroom, 1.0f) > halo);
    // No headroom (white at the peak): nothing changes.
    assert(HighlightGain(0.69f, 1.0f, 1.0f) == 1.0f);
    // Kaniel 2026-10-19, third look: gamma 2.2 made "the whole picture too dark". The SDR
    // picture is decoded with the sRGB curve, as Windows shows SDR in HDR mode: everything that
    // is not a light looks as it does on the desktop.
    assert(std::fabs(glsl::SdrToLinear(0.02f) - 0.02f / 12.92f) < 1e-7f && "the sRGB toe");
    assert(std::fabs(glsl::SdrToLinear(0.5f) - std::pow((0.5f + 0.055f) / 1.055f, 2.4f)) < 1e-6f);
    assert(glsl::SdrToLinear(0.0f) == 0.0f && std::fabs(glsl::SdrToLinear(1.0f) - 1.0f) < 1e-6f);

    // Harsh edges: the gain follows a small area's luminance (LocalLuma), so a one-pixel bright
    // edge beside dark pixels gets far less than a lamp's core, and its neighbours ramp.
    const float edge = glsl::LocalLuma(0.69f, 0.0f);
    const float core = glsl::LocalLuma(0.69f, 0.69f);
    assert(core == 0.69f && "a lamp's core keeps its own luminance");
    assert(edge < 0.69f * 0.6f && HighlightGain(edge, headroom, 0.5f) < 0.5f * HighlightGain(core, headroom, 0.5f));
    assert(glsl::LocalLuma(0.1f, 0.69f) >= 0.1f && "a dark pixel by a lamp is not darkened");

    // Kaniel 2026-10-19, fourth look ("definitely better ... maybe with outside lighting"):
    // Central Yharnam at dusk, white 480 and peak 1000 (headroom 2.08), highlights 10: the sky
    // (0.09), lit windows (0.11) and lit walls (0.15) rise by a third or more; the dark grass
    // (0.007) stays. At the default 5 the sky stays as it was.
    const float desk = 1000.0f / 480.0f;
    assert(HighlightGain(0.09f, desk, 1.0f) > 1.3f && "the sky");
    assert(HighlightGain(0.11f, desk, 1.0f) > 1.4f && "lit windows");
    assert(HighlightGain(0.15f, desk, 1.0f) > 1.5f && "lit walls");
    assert(HighlightGain(0.007f, desk, 1.0f) == 1.0f && "the grass");
    assert(HighlightGain(0.09f, desk, 0.5f) < 1.1f && "the default keeps the sky");
    for (float l = 0.0f; l <= 1.0f; l += 0.01f) {
        const float a = l * HighlightGain(l, desk, 1.0f);
        const float b = (l + 0.01f) * HighlightGain(l + 0.01f, desk, 1.0f);
        assert(b >= a - 1e-5f && a <= desk * 1.0001f);
    }
    // The lifted lights' colour: more saturated the more they rise (into Rec.2020's wider gamut),
    // at most a quarter more; untouched pixels keep the game's colour.
    assert(glsl::ChromaScale(1.0f, desk) == 1.0f);
    assert(glsl::ChromaScale(desk / 0.7f, desk) > 1.2f && glsl::ChromaScale(desk / 0.7f, desk) <= 1.25f);
    assert(glsl::ChromaScale(1.5f, desk) > 1.0f && glsl::ChromaScale(1.5f, desk) < glsl::ChromaScale(2.5f, desk));
    std::puts("PASS: HDR highlights lift the game's lights, keep its mid-tones");
    return 0;
}
