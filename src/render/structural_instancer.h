#pragma once

// Draws the dungeon's STRUCTURAL geometry (floor / wall / ceiling slabs) with instancing.
//
// Every slab is the same unit cube, scaled/placed by its own model matrix, and uses one of just
// three materials (floor / wall / ceiling). Normally the color pass draws one call per slab -
// hundreds of calls. Here we instead group the visible slabs by material and issue ONE
// glDrawElementsInstanced per material (so 3 calls at most), feeding each slab's model matrix
// through a per-instance vertex attribute. Same cube, same shader, same pixels as the per-object
// path: only the number of draw calls changes. That is exactly what the benchmark's instancing
// A/B measures (like the particle system) - draw_calls collapses while triangles stay the same.
//
// This is the second measurable optimization of the project (the first being frustum culling): it
// targets draw-call / CPU-submission overhead, NOT triangle throughput, which is why it pays off
// here even though the scene is not triangle-heavy (see analysis_notes).
//
// The helper owns its OWN copy of the unit cube (built once), so it does not depend on the scene's
// mesh lifetime (the scene is rebuilt on every "Generate"). Only the per-instance model matrices
// are re-uploaded each frame, after culling.

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <vector>

#include "core/scene.h"
#include "core/metrics.h"
#include "engine/mesh.h"             // Vertex layout (must match ggx.vert locations 0..4)
#include "engine/shader.h"
#include "world/frustum_culling.h"   // isAABBVisible, Frustum

// The three structural surface kinds. Kept together so both the instancer and the renderer's
// per-object loop can agree on "is this slab drawn by the instancer?".
inline bool isStructural(MaterialId m) {
    return m == MAT_FLOOR || m == MAT_WALL || m == MAT_CEILING;
}

class StructuralInstancer {
public:
    // Build the cube VAO/VBO/EBO + the per-instance VBO. Needs an active OpenGL context, so call
    // it once from the Renderer constructor (after GLAD is loaded).
    void init() {
        // Same unit cube as makeCubeMesh() in dungeon_geometry.h: 6 faces, each with its own 4
        // vertices and flat normal, going from -0.5 to +0.5 on each axis. We rebuild it here (not
        // reuse scene.meshes[0]) to stay independent of the scene's lifetime and to own a VAO we
        // can add the instance attributes to.
        std::vector<Vertex> vertices;
        std::vector<GLuint> indices;
        auto addFace = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 n) {
            GLuint start = (GLuint)vertices.size();
            Vertex v0; v0.Position = a; v0.Normal = n; v0.TexCoords = glm::vec2(0.0f, 0.0f); v0.Tangent = glm::vec3(0.0f); v0.Bitangent = glm::vec3(0.0f);
            Vertex v1; v1.Position = b; v1.Normal = n; v1.TexCoords = glm::vec2(1.0f, 0.0f); v1.Tangent = glm::vec3(0.0f); v1.Bitangent = glm::vec3(0.0f);
            Vertex v2; v2.Position = c; v2.Normal = n; v2.TexCoords = glm::vec2(1.0f, 1.0f); v2.Tangent = glm::vec3(0.0f); v2.Bitangent = glm::vec3(0.0f);
            Vertex v3; v3.Position = d; v3.Normal = n; v3.TexCoords = glm::vec2(0.0f, 1.0f); v3.Tangent = glm::vec3(0.0f); v3.Bitangent = glm::vec3(0.0f);
            vertices.push_back(v0); vertices.push_back(v1); vertices.push_back(v2); vertices.push_back(v3);
            indices.push_back(start + 0); indices.push_back(start + 1); indices.push_back(start + 2);
            indices.push_back(start + 0); indices.push_back(start + 2); indices.push_back(start + 3);
        };
        addFace({-0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, { 0, 0, 1}); // front  +Z
        addFace({ 0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, { 0, 0,-1}); // back   -Z
        addFace({ 0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f, 0.5f}, { 1, 0, 0}); // right  +X
        addFace({-0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f,-0.5f}, {-1, 0, 0}); // left   -X
        addFace({-0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}, { 0, 1, 0}); // top    +Y
        addFace({-0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f, 0.5f}, {-0.5f,-0.5f, 0.5f}, { 0,-1, 0}); // bottom -Y

        indexCount = (int)indices.size();

        glGenVertexArrays(1, &cubeVAO);
        glGenBuffers(1, &cubeVBO);
        glGenBuffers(1, &cubeEBO);
        glGenBuffers(1, &instanceVBO);

        glBindVertexArray(cubeVAO);

        glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cubeEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), indices.data(), GL_STATIC_DRAW);

        // per-vertex attributes 0..4, exactly like Mesh::setupMesh() so ggx.vert reads them the same
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Normal));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Tangent));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Bitangent));

        // per-INSTANCE model matrix in attributes 5..8 (a mat4 is 4 vec4 columns). glVertexAttribDivisor
        // = 1 means "advance once per instance, not per vertex", so every vertex of one cube gets that
        // instance's whole matrix. ggx.vert declares this as "layout(location = 5) in mat4".
        glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
        for (int col = 0; col < 4; ++col) {
            glEnableVertexAttribArray(5 + col);
            glVertexAttribPointer(5 + col, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4),
                                  (void*)(col * sizeof(glm::vec4)));
            glVertexAttribDivisor(5 + col, 1);
        }

        glBindVertexArray(0);
    }

    // Draw every VISIBLE structural slab, grouped by material, using `shader` (already active, with
    // all its per-frame uniforms - lights, shadows, SSAO - already set). Switches the vertex shader
    // to the per-instance matrix (useInstanceModel = 1), and back to the uniform path at the end so
    // the caller's per-object loop keeps working. Culls each slab against cullFrustum when
    // cullingEnabled, and skips ceilings when hideCeiling (overhead debug view). Adds to the
    // drawCalls / trianglesDrawn / objectsDrawn counters, same as the per-object path would.
    void drawStructural(Shader& shader, const Scene& scene, const Frustum& cullFrustum,
                        bool cullingEnabled, bool hideCeiling, int albedoUnit, FrameMetrics& metrics) {
        // one bucket of model matrices per structural material (0 = floor, 1 = wall, 2 = ceiling).
        // Structural objects always use materialIndex 0/1/2 (see buildScene), so we can index by it.
        for (int b = 0; b < 3; ++b) buckets[b].clear();
        for (const RenderObject& obj : scene.objects) {
            if (!isStructural(obj.material)) continue;
            if (hideCeiling && obj.material == MAT_CEILING) continue;
            if (cullingEnabled && !isAABBVisible(cullFrustum, obj.worldBounds)) continue;
            buckets[obj.materialIndex].push_back(obj.modelMatrix);
        }

        shader.setInt("useInstanceModel", 1);   // read the model from the per-instance attribute
        glBindVertexArray(cubeVAO);
        glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);

        for (int b = 0; b < 3; ++b) {
            if (buckets[b].empty()) continue;

            // bind this material exactly like the per-object path (albedo + GGX params)
            const Material& mat = scene.materials[b];
            glActiveTexture(GL_TEXTURE0 + albedoUnit);
            glBindTexture(GL_TEXTURE_2D, mat.albedo);
            shader.setFloat("uvScale", mat.uvScale);
            shader.setFloat("roughness", mat.roughness);
            shader.setVec3("F0", mat.F0);

            // stream this frame's visible matrices, then draw the whole bucket in ONE call
            glBufferData(GL_ARRAY_BUFFER, buckets[b].size() * sizeof(glm::mat4),
                         buckets[b].data(), GL_DYNAMIC_DRAW);
            glDrawElementsInstanced(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0,
                                    (GLsizei)buckets[b].size());

            metrics.drawCalls += 1;
            metrics.trianglesDrawn += (indexCount / 3) * (int)buckets[b].size();
            metrics.objectsDrawn += (int)buckets[b].size();
        }

        glBindVertexArray(0);
        shader.setInt("useInstanceModel", 0);   // back to the uniform-model path for the props
    }

    void clean() {
        if (cubeVAO) { glDeleteVertexArrays(1, &cubeVAO); cubeVAO = 0; }
        if (cubeVBO) { glDeleteBuffers(1, &cubeVBO); cubeVBO = 0; }
        if (cubeEBO) { glDeleteBuffers(1, &cubeEBO); cubeEBO = 0; }
        if (instanceVBO) { glDeleteBuffers(1, &instanceVBO); instanceVBO = 0; }
    }

private:
    GLuint cubeVAO = 0, cubeVBO = 0, cubeEBO = 0;
    GLuint instanceVBO = 0;
    int indexCount = 0;
    std::vector<glm::mat4> buckets[3];   // scratch, reused each frame to avoid reallocating
};
