// SPDX-License-Identifier: GPL-2.0-or-later
// bbport co-op: the live 3D map's models (bbport_map_3d.cpp). The map screen's own camera: the spot
// turned by the yaw round the middle (cx, cz), seen from the pitch, `zoom` pixels a metre, in an
// orthographic view (as the map screen's Frame places its marks).
#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;

layout(push_constant) uniform Camera {
    vec4 middle;   // cx, cy, cz, zoom
    vec4 turn;     // cos(yaw), sin(yaw), sin(pitch), cos(pitch)
    vec4 frame;    // half width, half height (pixels), the area's place on the atlas (dx, dz)
} camera;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec3 out_world;

void main() {
    const vec3 world = vec3(position.x + camera.frame.z, position.y, position.z + camera.frame.w);
    const vec3 d = world - camera.middle.xyz;
    const float u = d.x * camera.turn.x - d.z * camera.turn.y;
    const float v = d.x * camera.turn.y + d.z * camera.turn.x;
    const float up = v * camera.turn.z + d.y * camera.turn.w;
    const float near = -v * camera.turn.w + d.y * camera.turn.z;
    gl_Position = vec4(u * camera.middle.w / camera.frame.x, -up * camera.middle.w / camera.frame.y,
                       clamp(0.5 - near / 8000.0, 0.0, 1.0), 1.0);
    out_color = color;
    out_world = world;
}
