// basic.vert
// Minimal vertex shader, now used only by DebugDraw (the frustum wireframe of the spectator
// camera). It only moves each vertex from object space to clip space.

#version 410 core

// Attribute locations must match the ones we set up on the CPU side in Mesh::setupMesh()
// (see src/engine/mesh.h): 0 = Position, 1 = Normal, 2 = TexCoords.
// Only aPos is needed; the others are not read.
layout (location = 0) in vec3 aPos;

// One matrix per object (position/rotation/scale) and two shared by the whole frame
// (they depend on the camera, not on the single object).
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    // model -> world -> view -> clip space, all in one line: matrices are applied
    // right to left, so aPos is first placed in the world by "model", then seen from
    // the camera by "view", then projected by "projection".
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
