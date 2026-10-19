// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: HDR10 output (host_passes/hdr_pass.h). The finished SDR frame (sRGB encoded, at the
// swapchain's size) and the overlay layer (premultiplied, sRGB encoded) become Rec.2020 PQ: SDR
// white sits at the paper white, decoded as gamma 2.2 (SdrToLinear). Highlights: with a gain map
// (vk_scene_hdr.h) as bright as the game's 16-bit scene had them, else the frame's own highlights
// are stretched toward the peak.

#version 450
#extension GL_GOOGLE_include_directive : require

#include "hdr_highlight.glsl"

layout (location = 0) in vec2 uv;
layout (location = 0) out vec4 color;

layout (binding = 0) uniform sampler2D game;
layout (binding = 1) uniform sampler2D overlay;
layout (binding = 2) uniform sampler2D gain_map; ///< r: gain over the linear color

layout (push_constant) uniform settings {
    float paper_white; ///< nits of SDR white
    float peak;        ///< nits the brightest highlight reaches
    float highlights;  ///< 0..1: how far highlights are stretched toward the peak
    float use_gain;    ///< 1: gain_map holds this frame's gains (the stretch is off)
} pc;

vec3 SdrToLinear(vec3 c) {
    return vec3(SdrToLinear(c.r), SdrToLinear(c.g), SdrToLinear(c.b));
}

// SMPTE ST.2084 (PQ) of absolute luminance.
vec3 Pq(vec3 nits) {
    const float m1 = 0.1593017578125, m2 = 78.84375;
    const float c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
    const vec3 p = pow(clamp(nits / 10000.0, 0.0, 1.0), vec3(m1));
    return pow((c1 + c2 * p) / (1.0 + c3 * p), vec3(m2));
}

// Rec.709 primaries to Rec.2020 (BT.2087), column-major.
const mat3 Bt709ToBt2020 = mat3(0.627404, 0.069097, 0.016391,
                                0.329283, 0.919540, 0.088013,
                                0.043313, 0.011362, 0.895595);

vec3 ExpandHighlights(vec3 linear) {
    const float luma = dot(linear, vec3(0.2126, 0.7152, 0.0722));
    return linear * HighlightGain(luma, pc.peak / pc.paper_white, pc.highlights);
}

void main() {
    const ivec2 texel = ivec2(gl_FragCoord.xy);
    const vec3 scene = texelFetch(game, texel, 0).rgb;
    const vec4 ui = texelFetch(overlay, texel, 0);
    // Under the overlay the SDR blend as it would be on an SDR display, without the stretch.
    vec3 linear;
    if (ui.a > 0.0) {
        linear = SdrToLinear(clamp(scene * (1.0 - ui.a) + ui.rgb, 0.0, 1.0));
    } else if (pc.use_gain > 0.5) {
        linear = SdrToLinear(scene) * max(texelFetch(gain_map, texel, 0).r, 1.0);
    } else {
        linear = ExpandHighlights(SdrToLinear(scene));
    }
    color = vec4(Pq(max(Bt709ToBt2020 * (linear * pc.paper_white), vec3(0.0))), 1.0);
}
