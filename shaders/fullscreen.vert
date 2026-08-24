// fullscreen.vert
//
// Shared by every full-screen pass (M2: ssao.frag, ssaoblur.frag): draws a single triangle
// pair covering the whole screen in NDC coordinates, so the fragment shader runs once per
// pixel. aPos/aUV come from Renderer::initSSAO's small dedicated quad VAO (not the Mesh
// class - a 2-triangle screen quad does not need position/normal/tangent/bitangent).

#version 410 core

layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;

out vec2 TexCoords;

void main() {
    TexCoords = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
