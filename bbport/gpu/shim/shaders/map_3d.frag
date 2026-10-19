// SPDX-License-Identifier: GPL-2.0-or-later
// bbport co-op: the live 3D map's models (bbport_map_3d.cpp): each face in its colour, lit from the
// north-west as the map pictures are, its normal turned toward the viewer.
#version 450

layout(location = 0) in vec4 in_color;
layout(location = 1) in vec3 in_world;

layout(push_constant) uniform Camera {
    vec4 middle;
    vec4 turn;
    vec4 frame;
} camera;

layout(location = 0) out vec4 out_color;

void main() {
    vec3 normal = normalize(cross(dFdx(in_world), dFdy(in_world)));
    const vec3 toward = vec3(-camera.turn.w * camera.turn.y, camera.turn.z, -camera.turn.w * camera.turn.x);
    if (dot(normal, toward) < 0.0) {
        normal = -normal;
    }
    const vec3 light = normalize(vec3(-0.45, 0.78, 0.43));
    const float lit = 0.55 + 0.6 * max(dot(normal, light), 0.0);
    out_color = vec4(in_color.rgb * lit, 1.0);
}
