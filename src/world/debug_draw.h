#pragma once

// Small debug line drawer. It draws one thing: the wireframe of a camera frustum.
// The spectator camera (V key) uses it to show, from above, the volume the player's frustum
// culling keeps: everything outside this cage is not drawn.
//
// It is separate from the Renderer: it has its own small unlit shader (shaders/basic.vert/.frag:
// model/view/projection + one flat baseColor) and its own VAO of line vertices.

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "engine/shader.h"

class DebugDraw {
public:
    DebugDraw()
        : lineShader("shaders/basic.vert", "shaders/basic.frag")
    {
        // One VAO/VBO with room for 24 points: the 12 edges of the frustum, 2 points each.
        // The frustum changes every frame, so we upload the points at every draw
        // (GL_DYNAMIC_DRAW). Only positions, at location 0 like in basic.vert.
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, 24 * sizeof(glm::vec3), nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
        glBindVertexArray(0);
    }

    // Draw the wireframe of the frustum of `viewProj` (projection * view of the camera we want to
    // show, the player's), as seen from the spectator camera (spectatorView + proj).
    void drawFrustum(const glm::mat4& viewProj, const glm::mat4& spectatorView,
                     const glm::mat4& proj, const glm::vec3& color) {
        // The frustum in NDC is just the cube [-1, 1]^3. So its 8 corners in world space are the
        // 8 cube corners taken back with inverse(viewProj), followed by the perspective divide
        // (after the inverse, w is not 1).
        glm::mat4 inv = glm::inverse(viewProj);
        glm::vec3 corners[8];
        int idx = 0;
        for (int z = 0; z < 2; z++)   // z = 0: near plane, z = 1: far plane
            for (int y = 0; y < 2; y++)
                for (int x = 0; x < 2; x++) {
                    glm::vec4 p = inv * glm::vec4(x ? 1.0f : -1.0f,
                                                  y ? 1.0f : -1.0f,
                                                  z ? 1.0f : -1.0f, 1.0f);
                    corners[idx++] = glm::vec3(p) / p.w;
                }

        // Corner index bits: bit0 = x, bit1 = y, bit2 = z. An edge joins two corners that differ
        // in exactly one bit (one axis).
        static const int edges[12][2] = {
            {0,1},{2,3},{4,5},{6,7},   // along X
            {0,2},{1,3},{4,6},{5,7},   // along Y
            {0,4},{1,5},{2,6},{3,7}    // along Z (near to far)
        };
        glm::vec3 verts[24];
        for (int e = 0; e < 12; e++) {
            verts[e * 2 + 0] = corners[edges[e][0]];
            verts[e * 2 + 1] = corners[edges[e][1]];
        }

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

        // depth test OFF so the cage is visible even through the walls, then back ON
        glDisable(GL_DEPTH_TEST);
        lineShader.use();
        lineShader.setMat4("model", glm::mat4(1.0f));   // the corners are already in world space
        lineShader.setMat4("view", spectatorView);
        lineShader.setMat4("projection", proj);
        lineShader.setVec3("baseColor", color);
        glDrawArrays(GL_LINES, 0, 24);
        glEnable(GL_DEPTH_TEST);

        glBindVertexArray(0);
    }

    // Free the buffers and the shader, once at shutdown.
    void clean() {
        glDeleteVertexArrays(1, &vao);
        glDeleteBuffers(1, &vbo);
        lineShader.clean();
    }

private:
    Shader lineShader;
    GLuint vao = 0, vbo = 0;
};
