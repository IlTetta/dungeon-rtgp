#pragma once

// Tiny debug-only line drawer. For now it draws ONE thing: the wireframe of a camera frustum.
// The M3 spectator view uses it to show, from far above, the exact volume the player frustum
// culling keeps: everything outside this cage is skipped by the renderer.
//
// It is kept completely SEPARATE from Lorenzo's Renderer: it has its own little unlit shader
// (shaders/basic.vert/.frag: model/view/projection + a flat baseColor) and its own small VAO of
// line vertices. So the scene renderer stays untouched and this stays a pure Andrea-side tool.

#include <glad/glad.h>
#include <glm/glm.hpp>   // also brings in glm::inverse

#include "engine/shader.h"

class DebugDraw {
public:
    DebugDraw()
        : lineShader("shaders/basic.vert", "shaders/basic.frag")
    {
        // One VAO/VBO holding up to 24 line endpoints (the 12 edges of the frustum box, 2 points
        // each). The frustum changes every frame, so we re-upload the positions each draw ->
        // GL_DYNAMIC_DRAW. We only store vec3 positions (location 0, matching basic.vert).
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, 24 * sizeof(glm::vec3), nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
        glBindVertexArray(0);
    }

    // Draw the wireframe of the frustum described by `viewProj` (= projection * view of the camera
    // whose frustum we want to see, i.e. the frozen player camera). We render it from the SPECTATOR
    // camera (spectatorView + proj). `color` is the flat line color.
    void drawFrustum(const glm::mat4& viewProj, const glm::mat4& spectatorView,
                     const glm::mat4& proj, const glm::vec3& color) {
        // The 8 frustum corners ARE the 8 corners of the clip-space cube (NDC), each coordinate in
        // [-1, 1] for OpenGL. Transforming them back through inverse(viewProj) gives world-space
        // positions (with a perspective divide, because the far corners have w != 1).
        glm::mat4 inv = glm::inverse(viewProj);
        glm::vec3 corners[8];
        int idx = 0;
        for (int z = 0; z < 2; z++)          // z = 0 -> near plane, z = 1 -> far plane
            for (int y = 0; y < 2; y++)
                for (int x = 0; x < 2; x++) {
                    glm::vec4 p = inv * glm::vec4(x ? 1.0f : -1.0f,
                                                  y ? 1.0f : -1.0f,
                                                  z ? 1.0f : -1.0f, 1.0f);
                    corners[idx++] = glm::vec3(p) / p.w;   // perspective divide -> world position
                }

        // Corner index bit layout: bit0 = x, bit1 = y, bit2 = z. The 12 edges of the box connect
        // the corner pairs that differ in exactly one axis.
        static const int edges[12][2] = {
            {0,1},{2,3},{4,5},{6,7},   // edges along X
            {0,2},{1,3},{4,6},{5,7},   // edges along Y
            {0,4},{1,5},{2,6},{3,7}    // edges along Z (near -> far)
        };
        glm::vec3 verts[24];
        for (int e = 0; e < 12; e++) {
            verts[e * 2 + 0] = corners[edges[e][0]];
            verts[e * 2 + 1] = corners[edges[e][1]];
        }

        // upload this frame's line endpoints into the existing buffer
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

        // Draw the lines on top of everything (depth test OFF) so the whole cage is visible even
        // through walls; re-enable the depth test right after, so next frame's scene draws normally.
        glDisable(GL_DEPTH_TEST);
        lineShader.use();
        lineShader.setMat4("model", glm::mat4(1.0f));   // corners are already in world space
        lineShader.setMat4("view", spectatorView);
        lineShader.setMat4("projection", proj);
        lineShader.setVec3("baseColor", color);
        glDrawArrays(GL_LINES, 0, 24);
        glEnable(GL_DEPTH_TEST);

        glBindVertexArray(0);
    }

    // Free the GPU buffers and the shader. Call once, at shutdown.
    void clean() {
        glDeleteVertexArrays(1, &vao);
        glDeleteBuffers(1, &vbo);
        lineShader.clean();
    }

private:
    Shader lineShader;
    GLuint vao = 0, vbo = 0;
};
