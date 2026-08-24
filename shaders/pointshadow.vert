// pointshadow.vert
//
// One draw call renders all 6 faces of a point light's cubemap shadow (pointshadow.geom does
// the per-face projection), so this just passes the vertex position in world space along.

#version 410 core

layout (location = 0) in vec3 aPos;

uniform mat4 model;

out vec3 vWorldPos;

void main() {
    vWorldPos = vec3(model * vec4(aPos, 1.0));
}
