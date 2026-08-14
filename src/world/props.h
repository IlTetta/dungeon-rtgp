#pragma once

// Decorative props for the dungeon: torches on the lights, statues in the bigger rooms, and
// columns. They make the dungeon feel alive, add a lot of triangles (so frustum culling and
// shadows have something meaningful to work on), and are all MAT_PROP, so Lorenzo's renderer
// already knows how to shade them without any change.
//
// Called from main AFTER buildScene() (it needs the OpenGL context to upload the loaded meshes,
// and it reads scene.lights / dungeon.rooms that are already filled by then).

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "core/scene.h"
#include "engine/obj_loader.h"
#include "dungeon/dungeon_generator.h"
#include "world/dungeon_geometry.h"   // DungeonParams

// Bounding box of a mesh in its own local space (min/max over its vertices).
inline AABB meshLocalBounds(const Mesh& mesh) {
    AABB b;
    b.min = glm::vec3(1e9f);
    b.max = glm::vec3(-1e9f);
    for (const Vertex& v : mesh.vertices) {
        b.min = glm::min(b.min, v.Position);
        b.max = glm::max(b.max, v.Position);
    }
    return b;
}

inline void addProps(Scene& scene, const Dungeon& dungeon, const DungeonParams& params) {
    float t = params.tileSize;

    // --- load the prop meshes and remember their indices in scene.meshes ---
    // (each mesh is uploaded ONCE and then reused, with a different model matrix, by every
    // prop that uses it: that is the whole point of RenderObject::meshIndex.)
    int torchMesh = (int)scene.meshes.size();
    scene.meshes.push_back(loadOBJ("assets/models/sphere.obj"));
    int statueMesh = (int)scene.meshes.size();
    scene.meshes.push_back(loadOBJ("assets/models/bunny_lp.obj"));
    const int cubeMesh = 0;   // the unit cube built by buildScene(), reused for columns

    // helper: add one prop, computing its WORLD AABB from the mesh local bounds + its transform
    // (we transform the 8 corners of the local box and take the min/max). The AABB is what the
    // frustum culling and collisions use.
    auto addProp = [&](int meshIndex, glm::vec3 localMin, glm::vec3 localMax, const glm::mat4& model) {
        RenderObject obj;
        obj.meshIndex = meshIndex;
        obj.modelMatrix = model;
        obj.material = MAT_PROP;

        glm::vec3 wmin(1e9f), wmax(-1e9f);
        for (int c = 0; c < 8; c++) {
            glm::vec3 corner(
                (c & 1) ? localMax.x : localMin.x,
                (c & 2) ? localMax.y : localMin.y,
                (c & 4) ? localMax.z : localMin.z);
            glm::vec3 w = glm::vec3(model * glm::vec4(corner, 1.0f));
            wmin = glm::min(wmin, w);
            wmax = glm::max(wmax, w);
        }
        obj.worldBounds = { wmin, wmax };
        scene.objects.push_back(obj);
    };

    // --- torches: a small sphere floating on every light position ---
    AABB torchLocal = meshLocalBounds(scene.meshes[torchMesh]);
    for (const Light& light : scene.lights) {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), light.position);
        m = glm::scale(m, glm::vec3(0.25f));   // small
        addProp(torchMesh, torchLocal.min, torchLocal.max, m);
    }

    // --- statues: a bunny standing in the center of every room big enough to hold it ---
    // the model can have any size, so we scale it to a fixed target height and lift it so its
    // feet (local min y) rest on the floor (y = 0).
    AABB statueLocal = meshLocalBounds(scene.meshes[statueMesh]);
    float statueHeight = statueLocal.max.y - statueLocal.min.y;
    float targetHeight = 1.6f;
    float statueScale = (statueHeight > 0.0001f) ? (targetHeight / statueHeight) : 1.0f;
    for (const Rect& room : dungeon.rooms) {
        if (room.w < 5 || room.h < 5) continue;   // skip small rooms
        float cx = (room.x + room.w * 0.5f) * t;
        float cz = (room.y + room.h * 0.5f) * t;
        glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(cx, -statueLocal.min.y * statueScale, cz));
        m = glm::scale(m, glm::vec3(statueScale));
        addProp(statueMesh, statueLocal.min, statueLocal.max, m);
    }

    // --- columns: tall thin boxes near two inset corners of the bigger rooms ---
    glm::vec3 cubeMin(-0.5f), cubeMax(0.5f);   // the unit cube from buildScene()
    for (const Rect& room : dungeon.rooms) {
        if (room.w < 6 || room.h < 6) continue;
        glm::vec2 spots[2] = {
            { (room.x + 1.5f) * t,               (room.y + 1.5f) * t },
            { (room.x + room.w - 1.5f) * t,      (room.y + room.h - 1.5f) * t },
        };
        for (const glm::vec2& s : spots) {
            glm::vec3 size(0.5f, params.wallHeight, 0.5f);
            glm::vec3 center(s.x, params.wallHeight * 0.5f, s.y);
            glm::mat4 m = glm::translate(glm::mat4(1.0f), center);
            m = glm::scale(m, size);
            addProp(cubeMesh, cubeMin, cubeMax, m);
        }
    }
}
