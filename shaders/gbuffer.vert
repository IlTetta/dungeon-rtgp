// gbuffer.vert
//
// SSAO needs the view-space position and normal at every pixel, so this pass writes just
// those two things (no lighting, no textures) into a pair of textures before the real color
// pass runs.

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
