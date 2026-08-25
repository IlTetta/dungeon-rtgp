// gbuffer.frag
//
// Two outputs, nothing else: view-space position and normal of the closest surface at this
// pixel, for ssao.frag to read back.

#version 410 core

in vec3 ViewPos;
in vec3 ViewNormal;

layout (location = 0) out vec3 gPosition;
layout (location = 1) out vec3 gNormal;

void main() {
    gPosition = ViewPos;
    gNormal = normalize(ViewNormal);
}
