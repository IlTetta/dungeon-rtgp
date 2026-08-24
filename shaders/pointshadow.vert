// pointshadow.vert
//
// Vertex shader for the SINGLE-PASS point-light cubemap shadow (M2, item #6/S1 "F2":
// M2_shadows_plan.md). Earlier version of this pass issued 6 separate draw calls, one per
// cube face, each with its own view-projection; this one issues ONE draw call per light and
// lets pointshadow.geom re-emit each triangle into all 6 faces via gl_Layer. So all this
// vertex shader does is put the vertex in world space - the actual per-face projection
// happens in the geometry shader, which needs the un-projected position anyway.

#version 410 core

layout (location = 0) in vec3 aPos;

uniform mat4 model;

out vec3 vWorldPos;   // to the geometry shader

void main() {
    vWorldPos = vec3(model * vec4(aPos, 1.0));
}
