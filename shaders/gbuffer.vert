// gbuffer.vert
//
// M2 (Lorenzo, problem #5/E1 "F2": M2_shadows_plan.md, SSAO). SSAO needs to know, for every
// screen pixel, the VIEW-space position and normal of whatever is there - "view space"
// because ambient occlusion is inherently a camera-relative, screen-space technique (it
// samples a hemisphere around each pixel and re-projects those samples back to screen space
// to test against what is actually visible). This is a small extra pass, run once per frame
// BEFORE the real color pass, that writes exactly those two things (nothing else - no
// lighting, no textures) into two textures for ssao.frag to read.

#version 410 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 ViewPos;
out vec3 ViewNormal;

void main() {
    vec4 viewPos4 = view * model * vec4(aPos, 1.0);
    ViewPos = viewPos4.xyz;

    mat3 normalMatrix = transpose(inverse(mat3(view * model)));
    ViewNormal = normalize(normalMatrix * aNormal);

    gl_Position = projection * viewPos4;
}
