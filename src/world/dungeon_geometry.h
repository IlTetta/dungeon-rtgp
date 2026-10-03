#pragma once

// Turns the 2D dungeon grid (WALL / FLOOR tiles) into 3D geometry for the Scene.
//
// Two separate steps:
//   1. buildDungeonLayout(): it decides where every box goes,
//      so we can test it in the console (bsp_test) without opening a window.
//   2. makeCubeMesh() + buildScene(): the OpenGL part. One cube mesh on the GPU, and one
//      RenderObject per box that reuses it. Needs an OpenGL context.
//
// Coordinates: the grid lies on the X/Z plane and Y is up.
//   tile (gx, gy) -> world center x = gx*tileSize + tileSize/2, z = gy*tileSize + tileSize/2
//   the floor top is at y = 0, walls go from y = 0 to wallHeight, the ceiling sits on top.

#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "core/scene.h"
#include "engine/texture.h"
#include "dungeon/dungeon_generator.h"

// Sizes of the 3D dungeon, in world units.
struct DungeonParams {
    float tileSize = 2.0f;  // size of one grid cell
    float wallHeight = 4.5f;    // tall enough for the statue on the altar
    float floorThickness = 0.2f;    // thickness of the floor and ceiling slabs
};

// One axis-aligned box to place in the world: a floor slab, a wall block or a ceiling slab.
struct BoxPlacement {
    glm::vec3 center;   // center of the box in world space
    glm::vec3 size;   // full size along x, y, z
    MaterialId material;    // MAT_FLOOR, MAT_WALL or MAT_CEILING
    AABB bounds;    // center +- size/2, computed once (used by culling and collisions)
};

// Result of the layout step. `lights` stays empty: every light comes from a fire prop
// (torch or brazier) and is added in props.h, so no light is left without a visible source.
struct DungeonLayout {
    std::vector<BoxPlacement> boxes;
    std::vector<Light> lights;
};

// Build a BoxPlacement from a center and a size, computing its AABB.
inline BoxPlacement makeBox(glm::vec3 center, glm::vec3 size, MaterialId material) {
    BoxPlacement box;
    box.center = center;
    box.size = size;
    box.material = material;
    glm::vec3 half = size * 0.5f;
    box.bounds.min = center - half;
    box.bounds.max = center + half;
    return box;
}

// A wall tile is worth drawing only if at least one of its 8 neighbors is FLOOR. Walls fully
// surrounded by other walls can never be seen, so skipping them saves a lot of objects (and
// draw calls) before the renderer even starts.
inline bool isWallVisible(const Dungeon& d, int x, int y) {
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0)
                continue;
            int nx = x + dx;
            int ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= d.width || ny >= d.height)
                continue;   // outside the map
            if (d.get(nx, ny) == FLOOR)
                return true;
        }
    }
    return false;
}

// Compute all the boxes: a floor slab and a ceiling slab for every FLOOR tile, and a wall block
// for every visible WALL tile.
inline DungeonLayout buildDungeonLayout(const Dungeon& d, const DungeonParams& p = DungeonParams()) {
    DungeonLayout layout;
    float t = p.tileSize;

    for (int gy = 0; gy < d.height; gy++) {
        for (int gx = 0; gx < d.width; gx++) {
            // world center of this tile on the X/Z plane
            float cx = gx * t + t * 0.5f;
            float cz = gy * t + t * 0.5f;

            if (d.get(gx, gy) == FLOOR) {
                // thin slab with its TOP at y = 0, so its center is a bit below 0
                glm::vec3 size(t, p.floorThickness, t);
                glm::vec3 center(cx, -p.floorThickness * 0.5f, cz);
                layout.boxes.push_back(makeBox(center, size, MAT_FLOOR));

                // same slab on top of the walls (bottom at y = wallHeight): the dungeon is closed
                glm::vec3 ceilCenter(cx, p.wallHeight + p.floorThickness * 0.5f, cz);
                layout.boxes.push_back(makeBox(ceilCenter, size, MAT_CEILING));
            }
            else {
                if (isWallVisible(d, gx, gy)) {
                    glm::vec3 size(t, p.wallHeight, t);
                    glm::vec3 center(cx, p.wallHeight * 0.5f, cz);
                    layout.boxes.push_back(makeBox(center, size, MAT_WALL));
                }
            }
        }
    }

    return layout;
}

// Unit cube centered on the origin (from -0.5 to +0.5 on each axis). Every face has its own 4
// vertices so it can have its own flat normal: 24 vertices instead of 8.
inline Mesh makeCubeMesh() {
    std::vector<Vertex> vertices;
    std::vector<GLuint> indices;

    // add one face given its 4 corners (counter-clockwise seen from outside) and its normal.
    // Tangent and Bitangent stay at 0.
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

        // two triangles: (0,1,2) and (0,2,3)
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

    // the Mesh constructor moves the two vectors and uploads them to the GPU
    return Mesh(vertices, indices);
}

// Build the Scene from the layout: one shared cube mesh, the three surface materials and one
// RenderObject per box.
inline Scene buildScene(const DungeonLayout& layout) {
    Scene scene;

    // mesh 0 = the unit cube, reused by every box with its own model matrix
    scene.meshes.push_back(makeCubeMesh());

    // surface materials in a fixed order: 0 = floor, 1 = wall, 2 = ceiling.
    // props.h appends the prop materials after these.
    Material floorMat;
    floorMat.albedo = loadTexture("assets/textures/floor_albedo.png");
    floorMat.uvScale = 2.0f;
    floorMat.roughness = 0.9f;

    Material wallMat;
    wallMat.albedo = loadTexture("assets/textures/wall_albedo.png");
    wallMat.uvScale = 2.0f;
    wallMat.roughness = 0.85f;

    Material ceilingMat;
    ceilingMat.albedo = loadTexture("assets/textures/ceiling_albedo.png");
    ceilingMat.uvScale = 2.0f;
    ceilingMat.roughness = 0.9f;

    scene.materials.push_back(floorMat);
    scene.materials.push_back(wallMat);
    scene.materials.push_back(ceilingMat);

    scene.objects.reserve(layout.boxes.size());
    for (const BoxPlacement& box : layout.boxes) {
        RenderObject obj;
        obj.meshIndex = 0;
        // translate * scale: glm applies them right to left, so the unit cube is first scaled
        // to the box size and then moved to its center
        glm::mat4 m(1.0f);
        m = glm::translate(m, box.center);
        m = glm::scale(m, box.size);
        obj.modelMatrix = m;
        obj.worldBounds = box.bounds;
        obj.material = box.material;
        obj.materialIndex = (box.material == MAT_FLOOR) ? 0 : (box.material == MAT_WALL) ? 1 : 2;
        scene.objects.push_back(obj);
    }

    scene.lights = layout.lights;
    return scene;
}
