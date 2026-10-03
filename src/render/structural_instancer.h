#pragma once

// Draws the STRUCTURAL geometry of the dungeon (floor, wall and ceiling slabs) with instancing.
//
// Every slab is the same unit cube with its own model matrix, and it uses one of only three
// materials. The normal color pass makes one draw call per slab (hundreds). Here we group the
// visible slabs by material and make ONE glDrawElementsInstanced per material (3 calls at most),
// passing each slab's model matrix as a per-instance vertex attribute.
// Same cube, same shader, same pixels: only the number of draw calls changes, while the
// triangles stay the same. This removes draw call / CPU submission overhead, not GPU work.
//
// It is OFF by default (Renderer::structuralInstancing): the per-object path is the baseline,
// and with instancing on the structural draw calls would stay at 3 whatever the culling does,
// hiding the effect of culling in that experiment.
//
// It keeps its own copy of the unit cube, built once, so it does not depend on the Scene, which
// is rebuilt at every "Generate". Only the instance matrices are uploaded every frame.

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <vector>

#include "core/scene.h"
#include "core/metrics.h"
#include "engine/mesh.h"             // Vertex layout (same locations 0..4 as ggx.vert)
#include "engine/shader.h"
#include "world/frustum_culling.h"

// Floor, wall and ceiling. Used by both the instancer and the renderer's per-object loop, so
// they agree on which objects the instancer draws.
inline bool isStructural(MaterialId m) {
    return m == MAT_FLOOR || m == MAT_WALL || m == MAT_CEILING;
}

class StructuralInstancer {
public:
    // Build the cube VAO/VBO/EBO and the instance VBO. Needs the OpenGL context, so the Renderer
    // calls it once from its constructor.
    void init() {
        // The same unit cube as makeCubeMesh() in dungeon_geometry.h (24 vertices, flat normals).
        // We build our own instead of using scene.meshes[0] because we need a VAO where we can add
        // the instance attributes, and it must outlive the Scene.
        std::vector<Vertex> vertices;
        std::vector<GLuint> indices;
        auto addFace = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 n) {
            GLuint start = (GLuint)vertices.size();

            Vertex v0;
            v0.Position = a;
            v0.Normal = n;
            v0.TexCoords = glm::vec2(0.0f, 0.0f);
            v0.Tangent = glm::vec3(0.0f);
            v0.Bitangent = glm::vec3(0.0f);

            Vertex v1;
            v1.Position = b;
            v1.Normal = n;
            v1.TexCoords = glm::vec2(1.0f, 0.0f);
            v1.Tangent = glm::vec3(0.0f);
            v1.Bitangent = glm::vec3(0.0f);

            Vertex v2;
            v2.Position = c;
            v2.Normal = n;
            v2.TexCoords = glm::vec2(1.0f, 1.0f);
            v2.Tangent = glm::vec3(0.0f);
            v2.Bitangent = glm::vec3(0.0f);

            Vertex v3;
            v3.Position = d;
            v3.Normal = n;
            v3.TexCoords = glm::vec2(0.0f, 1.0f);
            v3.Tangent = glm::vec3(0.0f);
            v3.Bitangent = glm::vec3(0.0f);

            vertices.push_back(v0);
            vertices.push_back(v1);
            vertices.push_back(v2);
            vertices.push_back(v3);

            indices.push_back(start + 0);
            indices.push_back(start + 1);
            indices.push_back(start + 2);
            indices.push_back(start + 0);
            indices.push_back(start + 2);
            indices.push_back(start + 3);
        };

        // front +Z, back -Z, right +X, left -X, top +Y, bottom -Y
        addFace({-0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, { 0, 0, 1});
        addFace({ 0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, { 0, 0,-1});
        addFace({ 0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f, 0.5f}, { 1, 0, 0});
        addFace({-0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f,-0.5f}, {-1, 0, 0});
        addFace({-0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}, { 0, 1, 0});
        addFace({-0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f, 0.5f}, {-0.5f,-0.5f, 0.5f}, { 0,-1, 0});

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

        // per-vertex attributes 0..4, the same as Mesh::setupMesh()
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

        // Per-INSTANCE model matrix. An attribute holds at most a vec4, so a mat4 takes 4 locations
        // (5..8), one per column. With divisor 1 every vertex of one cube gets the matrix of its
        // instance. In ggx.vert this is "layout(location = 5) in mat4 aInstanceModel".
        glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
        for (int col = 0; col < 4; ++col) {
            glEnableVertexAttribArray(5 + col);
            glVertexAttribPointer(5 + col, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4),
                                  (void*)(col * sizeof(glm::vec4)));
            glVertexAttribDivisor(5 + col, 1);
        }

        glBindVertexArray(0);
    }

    // Draw all the visible structural slabs, grouped by material. `shader` is already active with
    // all the per-frame uniforms (lights, shadows, SSAO) set by the renderer. We switch ggx.vert
    // to the instance matrix (useInstanceModel = 1) and back to the uniform at the end, for the
    // props. Same culling and same counters as the per-object path.
    void drawStructural(Shader& shader, const Scene& scene, const Frustum& cullFrustum,
                        bool cullingEnabled, bool hideCeiling, int albedoUnit, FrameMetrics& metrics) {
        // one list of matrices per material: 0 = floor, 1 = wall, 2 = ceiling. The structural
        // objects always have materialIndex 0, 1 or 2 (see buildScene), so we use it as index.
        for (int b = 0; b < 3; ++b)
            buckets[b].clear();
        for (const RenderObject& obj : scene.objects) {
            if (!isStructural(obj.material))
                continue;
            if (hideCeiling && obj.material == MAT_CEILING)
                continue;
            if (cullingEnabled && !isAABBVisible(cullFrustum, obj.worldBounds))
                continue;
            buckets[obj.materialIndex].push_back(obj.modelMatrix);
        }

        shader.setInt("useInstanceModel", 1);
        glBindVertexArray(cubeVAO);
        glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);

        for (int b = 0; b < 3; ++b) {
            if (buckets[b].empty())
                continue;

            // the material, bound like in the per-object loop
            const Material& mat = scene.materials[b];
            glActiveTexture(GL_TEXTURE0 + albedoUnit);
            glBindTexture(GL_TEXTURE_2D, mat.albedo);
            shader.setFloat("uvScale", mat.uvScale);
            shader.setFloat("roughness", mat.roughness);
            shader.setVec3("F0", mat.F0);

            // upload this frame's visible matrices and draw the whole group in one call
            glBufferData(GL_ARRAY_BUFFER, buckets[b].size() * sizeof(glm::mat4),
                         buckets[b].data(), GL_DYNAMIC_DRAW);
            glDrawElementsInstanced(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0,
                                    (GLsizei)buckets[b].size());

            metrics.drawCalls += 1;
            metrics.trianglesDrawn += (indexCount / 3) * (int)buckets[b].size();
            metrics.objectsDrawn += (int)buckets[b].size();
        }

        glBindVertexArray(0);
        shader.setInt("useInstanceModel", 0);
    }

    void clean() {
        if (cubeVAO) {
            glDeleteVertexArrays(1, &cubeVAO);
            cubeVAO = 0;
        }
        if (cubeVBO) {
            glDeleteBuffers(1, &cubeVBO);
            cubeVBO = 0;
        }
        if (cubeEBO) {
            glDeleteBuffers(1, &cubeEBO);
            cubeEBO = 0;
        }
        if (instanceVBO) {
            glDeleteBuffers(1, &instanceVBO);
            instanceVBO = 0;
        }
    }

private:
    GLuint cubeVAO = 0, cubeVBO = 0, cubeEBO = 0;
    GLuint instanceVBO = 0;
    int indexCount = 0;
    std::vector<glm::mat4> buckets[3];   // reused every frame, so no new allocations
};
