#pragma once

// test_scene.h — TEMPORARY, Lorenzo-side only.
//
// Andrea's BSP dungeon generator (src/world/) is what will normally fill the Scene for
// real. Until that exists, the renderer has nothing to draw and cannot be tested on its
// own. This file builds a small, fully hand-made Scene (a floor + a couple of "prop"
// cubes + two torch-like point lights) so that M1 (basic forward rendering + mesh
// loading/management) can be built and verified end to end, independently of M1's other
// half (the dungeon geometry).
//
// This is NOT meant to survive into main.cpp forever: once src/world/ can build a real
// Scene, main.cpp should call that instead, and this file can be deleted (or kept around
// only as a quick smoke test / fallback while the dungeon generator is being rewritten).
//
// It only touches Scene (via addMesh/addObject helpers you already own) and does not
// depend on anything Andrea's side owns, so it is safe to add without stepping on his code.

#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/mesh.h"
#include "core/scene.h"
#include "core/material.h"

namespace TestScene {

// Builds a flat quad on the XZ plane (y = 0), "size" units wide, centered at the origin,
// facing up (+Y). Used as a stand-in for one floor tile.
inline Mesh makeFloorQuad(float size) {
    float h = size * 0.5f;

    std::vector<Vertex> vertices = {
        // Position                Normal              TexCoords     Tangent          Bitangent
        { {-h, 0.0f, -h}, {0, 1, 0}, {0.0f, 0.0f}, {1, 0, 0}, {0, 0, 1} },
        { { h, 0.0f, -h}, {0, 1, 0}, {1.0f, 0.0f}, {1, 0, 0}, {0, 0, 1} },
        { { h, 0.0f,  h}, {0, 1, 0}, {1.0f, 1.0f}, {1, 0, 0}, {0, 0, 1} },
        { {-h, 0.0f,  h}, {0, 1, 0}, {0.0f, 1.0f}, {1, 0, 0}, {0, 0, 1} },
    };
    std::vector<GLuint> indices = {
        0, 1, 2,
        0, 2, 3,
    };
    return Mesh(vertices, indices);
}

// Builds a unit cube (side = 1, centered at the origin). Each face gets its own 4
// vertices (24 in total, not 8) so every face can have its own flat normal: sharing
// vertices between faces would force us to average normals, which would make the cube
// look smooth/rounded instead of having sharp edges.
inline Mesh makeUnitCube() {
    std::vector<Vertex> vertices;
    std::vector<GLuint> indices;
    vertices.reserve(24);
    indices.reserve(36);

    // one entry per face: the 4 corners (already positioned) and the face normal
    struct Face { glm::vec3 corners[4]; glm::vec3 normal; };
    const float h = 0.5f;
    // We do not enable face culling in M1 (glEnable(GL_CULL_FACE) is not called anywhere
    // yet), so the winding order below does not affect correctness; we still keep it
    // consistently counter-clockwise (viewed from outside the cube, i.e. from the
    // direction the face's normal points to) so that culling can be turned on later
    // (e.g. for an optimization experiment) without having to revisit this geometry.
    Face faces[6] = {
        // +X (right)
        { { {h,-h,-h}, {h,h,-h}, {h,h,h}, {h,-h,h} }, {1, 0, 0} },
        // -X (left)
        { { {-h,-h,h}, {-h,h,h}, {-h,h,-h}, {-h,-h,-h} }, {-1, 0, 0} },
        // +Y (top)
        { { {-h,h,-h}, {h,h,-h}, {h,h,h}, {-h,h,h} }, {0, 1, 0} },
        // -Y (bottom)
        { { {-h,-h,h}, {h,-h,h}, {h,-h,-h}, {-h,-h,-h} }, {0, -1, 0} },
        // +Z (front)
        { { {-h,-h,h}, {h,-h,h}, {h,h,h}, {-h,h,h} }, {0, 0, 1} },
        // -Z (back)
        { { {h,-h,-h}, {-h,-h,-h}, {-h,h,-h}, {h,h,-h} }, {0, 0, -1} },
    };

    for (const Face& f : faces) {
        GLuint base = (GLuint)vertices.size();
        // simple 0,0 / 1,0 / 1,1 / 0,1 uv layout per face; good enough before we have
        // real texturing (M2+)
        glm::vec2 uvs[4] = { {0,0}, {1,0}, {1,1}, {0,1} };
        for (int i = 0; i < 4; ++i) {
            Vertex v;
            v.Position = f.corners[i];
            v.Normal = f.normal;
            v.TexCoords = uvs[i];
            v.Tangent = glm::vec3(0.0f);     // not needed until normal mapping (M2)
            v.Bitangent = glm::vec3(0.0f);
            vertices.push_back(v);
        }
        // two triangles per face, following the same winding as the floor quad above
        indices.push_back(base + 0); indices.push_back(base + 1); indices.push_back(base + 2);
        indices.push_back(base + 0); indices.push_back(base + 2); indices.push_back(base + 3);
    }

    return Mesh(vertices, indices);
}

// Fills "scene" with a small hand-built room: one floor, two prop cubes, two lights.
// Any previous content of "scene" is discarded.
inline void build(Scene& scene) {
    scene.meshes.clear();
    scene.objects.clear();
    scene.lights.clear();

    // --- meshes ---
    // index 0: the floor tile mesh, reused (with different model matrices) for every
    // floor RenderObject; index 1: the cube mesh, reused for every prop RenderObject.
    // (This is exactly the point of RenderObject::meshIndex: many objects, few meshes.)
    scene.meshes.push_back(makeFloorQuad(10.0f));
    scene.meshes.push_back(makeUnitCube());
    const int FLOOR_MESH = 0;
    const int CUBE_MESH = 1;

    // --- objects ---
    // the floor: no rotation/scale needed, identity model matrix places it exactly where
    // makeFloorQuad() already built it (10x10, centered at the origin, y = 0)
    RenderObject floor;
    floor.meshIndex = FLOOR_MESH;
    floor.modelMatrix = glm::mat4(1.0f);
    floor.worldBounds = { {-5.0f, -0.01f, -5.0f}, {5.0f, 0.01f, 5.0f} };
    floor.material = MAT_FLOOR;
    scene.objects.push_back(floor);

    // two "prop" cubes standing on the floor, at different spots
    glm::vec3 propPositions[2] = { {-2.0f, 0.5f, -1.5f}, {2.0f, 0.5f, 1.0f} };
    for (const glm::vec3& pos : propPositions) {
        RenderObject prop;
        prop.meshIndex = CUBE_MESH;
        prop.modelMatrix = glm::translate(glm::mat4(1.0f), pos);
        prop.worldBounds = { pos - glm::vec3(0.5f), pos + glm::vec3(0.5f) };
        prop.material = MAT_PROP;
        scene.objects.push_back(prop);
    }

    // --- lights ---
    // two warm "torches" floating above the floor, on opposite sides, so that both prop
    // cubes clearly receive light from a direction we can visually check
    Light torchA;
    torchA.position = { -2.0f, 2.0f, -1.5f };
    torchA.color = { 1.0f, 0.75f, 0.45f };
    torchA.intensity = 1.5f;
    torchA.radius = 8.0f;
    scene.lights.push_back(torchA);

    Light torchB;
    torchB.position = { 2.0f, 2.0f, 1.0f };
    torchB.color = { 1.0f, 0.75f, 0.45f };
    torchB.intensity = 1.5f;
    torchB.radius = 8.0f;
    scene.lights.push_back(torchB);
}

} // namespace TestScene
