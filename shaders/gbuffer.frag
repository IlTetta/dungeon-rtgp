// gbuffer.frag
//
// See gbuffer.vert for why this pass exists. Two outputs, nothing else: the view-space
// position and normal of the closest surface at this pixel, into two separate floating-point
// textures (Renderer::initSSAO) that ssao.frag reads back next.

#version 410 core

in vec3 ViewPos;
in vec3 ViewNormal;

layout (location = 0) out vec3 gPosition;
layout (location = 1) out vec3 gNormal;

void main() {
    gPosition = ViewPos;
    gNormal = normalize(ViewNormal);
}
