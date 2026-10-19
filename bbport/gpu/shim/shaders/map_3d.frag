// SPDX-License-Identifier: GPL-2.0-or-later
// bbport co-op: the live 3D map's models (bbport_map_3d.cpp). Kaniel 2026-10-19 ("it looks much
// worse"): what faces up and lies at the map picture's top there (roofs, streets, floors) takes the
// map picture's own pixel (the game's textures, light and grade, at full detail); the rest (walls)
// takes its colour, brightened as the picture's are, lit from the north-west, its normal turned
// toward the viewer.
#version 450

layout(location = 0) in vec4 in_color;
layout(location = 1) in vec3 in_world;

layout(set = 0, binding = 0) uniform sampler2D picture;
layout(set = 0, binding = 1) uniform isampler2D tops;

layout(push_constant) uniform Camera {
    vec4 middle;
    vec4 turn;
    vec4 frame;   // half width, half height, the area's place on the atlas (dx, dz)
    vec4 chart;   // the picture's frame: x0, z1, width and height in metres
} camera;

layout(location = 0) out vec4 out_color;

void main() {
    vec3 normal = normalize(cross(dFdx(in_world), dFdy(in_world)));
    const vec3 toward = vec3(-camera.turn.w * camera.turn.y, camera.turn.z, -camera.turn.w * camera.turn.x);
    if (dot(normal, toward) < 0.0) {
        normal = -normal;
    }
    const vec3 local = in_world - vec3(camera.frame.z, 0.0, camera.frame.w);
    const vec2 uv = vec2((local.x - camera.chart.x) / camera.chart.z, (camera.chart.y - local.z) / camera.chart.w);
    if (normal.y > 0.55 && all(greaterThanEqual(uv, vec2(0.0))) && all(lessThan(uv, vec2(1.0)))) {
        const ivec2 size = textureSize(tops, 0);
        const int top = texelFetch(tops, clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1), 0).r;
        const vec4 seen = texture(picture, uv);
        if (top != -32768 && abs(local.y - float(top) / 10.0) < 1.5 && seen.a > 0.5) {
            out_color = vec4(seen.rgb, 1.0);
            return;
        }
    }
    const vec3 light = normalize(vec3(-0.45, 0.78, 0.43));
    const float lit = 0.62 + 0.55 * max(dot(normal, light), 0.0);
    out_color = vec4(min(in_color.rgb * 1.45 * lit, vec3(1.0)), 1.0);
}
