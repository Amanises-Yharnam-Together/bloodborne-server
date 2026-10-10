// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: HDR10 output (host_passes/hdr_pass.h). The finished SDR frame (sRGB encoded, at the
// swapchain's size) and the overlay layer (premultiplied, sRGB encoded) become Rec.2020 PQ: SDR
// white sits at the paper white. Highlights: with a gain map (vk_scene_hdr.h) as bright as the
// game's 16-bit scene had them, else the frame's own highlights are stretched toward the peak.

#version 450

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

vec3 SrgbToLinear(vec3 c) {
    return mix(pow((c + 0.055) / 1.055, vec3(2.4)), c / 12.92, lessThanEqual(c, vec3(0.04045)));
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

// Luminance above the knee goes up to peak / paper white times at full white; the hue stays.
vec3 ExpandHighlights(vec3 linear) {
    const float knee = 0.5; // linear; sRGB ~0.74
    const float luma = dot(linear, vec3(0.2126, 0.7152, 0.0722));
    if (pc.highlights <= 0.0 || luma <= knee) {
        return linear;
    }
    const float headroom = max(pc.peak / pc.paper_white, 1.0);
    const float t = clamp((luma - knee) / (1.0 - knee), 0.0, 1.0);
    return linear * mix(1.0, headroom, pc.highlights * t * t);
}

void main() {
    const ivec2 texel = ivec2(gl_FragCoord.xy);
    const vec3 scene = texelFetch(game, texel, 0).rgb;
    const vec4 ui = texelFetch(overlay, texel, 0);
    // Under the overlay the SDR blend as it would be on an SDR display, without the stretch.
    vec3 linear;
    if (ui.a > 0.0) {
        linear = SrgbToLinear(clamp(scene * (1.0 - ui.a) + ui.rgb, 0.0, 1.0));
    } else if (pc.use_gain > 0.5) {
        linear = SrgbToLinear(scene) * max(texelFetch(gain_map, texel, 0).r, 1.0);
    } else {
        linear = ExpandHighlights(SrgbToLinear(scene));
    }
    color = vec4(Pq(max(Bt709ToBt2020 * (linear * pc.paper_white), vec3(0.0))), 1.0);
}
