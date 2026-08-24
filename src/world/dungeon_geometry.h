#pragma once

// Turns a 2D dungeon grid (WALL / FLOOR tiles) into 3D geometry for the Scene.
//
// The work is split in two parts on purpose:
//   1. buildDungeonLayout(): PURE math (glm only, no OpenGL). It decides WHERE every box and
//      light goes. We can test it in the console (print counts / positions) without a window.
//   2. makeCubeMesh() + buildScene(): the OpenGL part. It creates one cube mesh on the GPU and
//      wraps every box into a RenderObject. It needs an active OpenGL context, so it runs only
//      after the window is created.
//
// Coordinate mapping: the grid is on the X/Z plane, Y is up.
//   grid tile (gx, gy)  ->  world center x = gx*tileSize + tileSize/2 , z = gy*tileSize + ...
//   floors sit with their top surface at y = 0; walls stand on top of them.

#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>   // glm::translate, glm::scale

#include "core/scene.h"                    // Scene, RenderObject, AABB, Light, MaterialId
#include "engine/texture.h"                // loadTexture()
#include "dungeon/dungeon_generator.h"     // Dungeon, Tile, Rect

// Tunable sizes for the 3D dungeon (in world units).
struct DungeonParams {
    float tileSize = 2.0f;         // size of one grid cell in world units
    float wallHeight = 4.5f;       // how tall the walls are (taller = roomier, fits statue-on-altar)
    float floorThickness = 0.2f;   // how thick the floor slabs are
};

// One axis-aligned box to place in the world (a floor slab or a wall block).
struct BoxPlacement {
    glm::vec3 center;      // center of the box in world space
    glm::vec3 size;        // full size along x, y, z
    MaterialId material;   // FLOOR or WALL
    AABB bounds;           // = center +/- size/2 (precomputed, used for culling later)
};

// The result of the pure layout step: all the boxes and all the lights.
struct DungeonLayout {
    std::vector<BoxPlacement> boxes;
    std::vector<Light> lights;
};

// ---------------------------------------------------------------------------------------------
// PART 1 - pure layout (no OpenGL)
// ---------------------------------------------------------------------------------------------

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

// A wall tile is "visible" (worth drawing) only if at least one of its 8 neighbors is FLOOR.
// Interior walls completely surrounded by other walls are never seen, so we skip them: this
// removes a huge number of useless objects (and useless draw calls later).
inline bool isWallVisible(const Dungeon& d, int x, int y) {
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx;
            int ny = y + dy;
            // skip neighbors outside the map
            if (nx < 0 || ny < 0 || nx >= d.width || ny >= d.height) continue;
            if (d.get(nx, ny) == FLOOR) return true;
        }
    }
    return false;
}

// Compute all the boxes (floors + visible walls) and the lights (one per room).
inline DungeonLayout buildDungeonLayout(const Dungeon& d, const DungeonParams& p = DungeonParams()) {
    DungeonLayout layout;
    float t = p.tileSize;

    // --- floors and walls ---
    for (int gy = 0; gy < d.height; gy++) {
        for (int gx = 0; gx < d.width; gx++) {
            // world center of this tile on the X/Z plane
            float cx = gx * t + t * 0.5f;
            float cz = gy * t + t * 0.5f;

            if (d.get(gx, gy) == FLOOR) {
                // a thin slab, with its TOP surface at y = 0 (so its center is a bit below 0)
                glm::vec3 size(t, p.floorThickness, t);
                glm::vec3 center(cx, -p.floorThickness * 0.5f, cz);
                layout.boxes.push_back(makeBox(center, size, MAT_FLOOR));

                // a matching ceiling slab on TOP of the walls (its bottom at y = wallHeight),
                // so the dungeon is closed overhead
                glm::vec3 ceilCenter(cx, p.wallHeight + p.floorThickness * 0.5f, cz);
                layout.boxes.push_back(makeBox(ceilCenter, size, MAT_CEILING));
            }
            else {
                // a wall, but only if it borders some walkable space
                if (isWallVisible(d, gx, gy)) {
                    glm::vec3 size(t, p.wallHeight, t);
                    glm::vec3 center(cx, p.wallHeight * 0.5f, cz);   // standing on the floor (y = 0..wallHeight)
                    layout.boxes.push_back(makeBox(center, size, MAT_WALL));
                }
            }
        }
    }

    // --- lights: one warm "torch" near the ceiling at the center of each room ---
    for (const Rect& room : d.rooms) {
        float cx = (room.x + room.w * 0.5f) * t;
        float cz = (room.y + room.h * 0.5f) * t;

        Light light;
        light.position = glm::vec3(cx, 1.2f, cz);    // low, near the brazier fire we place here
        light.color = glm::vec3(1.0f, 0.8f, 0.5f);   // warm, orange-ish
        light.intensity = 3.5f;                      // brighter so props are clearly visible
        light.radius = 10.0f * t;                    // reaches farther, into the corridors a bit
        // M2 (M2_shadows_plan.md, S1): a brazier is a floor prop with no natural "aim", so it
        // stays LIGHT_POINT (the default) and gets a cubemap shadow, not the SPOT cone. Which
        // of these ever become the MAX_SHADOW_LIGHTS *real* shadow-casters this frame is
        // decided dynamically by nearest-to-camera in Renderer::renderInternal, not here.
        light.castsShadow = true;
        layout.lights.push_back(light);
    }

    return layout;
}

// ---------------------------------------------------------------------------------------------
// PART 2 - OpenGL geometry (needs an active context)
// ---------------------------------------------------------------------------------------------

// Build a unit cube mesh (centered on the origin, going from -0.5 to +0.5 on each axis).
// Every face has its own 4 vertices, so each face can have its own (flat) normal.
inline Mesh makeCubeMesh() {
    std::vector<Vertex> vertices;
    std::vector<GLuint> indices;

    // helper that adds one square face given its 4 corners (a,b,c,d, counter-clockwise seen
    // from outside) and the face normal.
    auto addFace = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 n) {
        GLuint start = (GLuint)vertices.size();

        // the 4 corners, with simple uv coordinates. Tangent/Bitangent are left at 0 for now:
        // we do not need them for the basic shading of M1 (they matter for normal mapping, M2).
        Vertex v0; v0.Position = a; v0.Normal = n; v0.TexCoords = glm::vec2(0.0f, 0.0f); v0.Tangent = glm::vec3(0.0f); v0.Bitangent = glm::vec3(0.0f);
        Vertex v1; v1.Position = b; v1.Normal = n; v1.TexCoords = glm::vec2(1.0f, 0.0f); v1.Tangent = glm::vec3(0.0f); v1.Bitangent = glm::vec3(0.0f);
        Vertex v2; v2.Position = c; v2.Normal = n; v2.TexCoords = glm::vec2(1.0f, 1.0f); v2.Tangent = glm::vec3(0.0f); v2.Bitangent = glm::vec3(0.0f);
        Vertex v3; v3.Position = d; v3.Normal = n; v3.TexCoords = glm::vec2(0.0f, 1.0f); v3.Tangent = glm::vec3(0.0f); v3.Bitangent = glm::vec3(0.0f);
        vertices.push_back(v0);
        vertices.push_back(v1);
        vertices.push_back(v2);
        vertices.push_back(v3);

        // two triangles: (0,1,2) and (0,2,3)
        indices.push_back(start + 0); indices.push_back(start + 1); indices.push_back(start + 2);
        indices.push_back(start + 0); indices.push_back(start + 2); indices.push_back(start + 3);
    };

    // the 6 faces of the cube
    addFace({-0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, { 0, 0, 1}); // front  +Z
    addFace({ 0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, { 0, 0,-1}); // back   -Z
    addFace({ 0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f, 0.5f}, { 1, 0, 0}); // right  +X
    addFace({-0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f,-0.5f}, {-1, 0, 0}); // left   -X
    addFace({-0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}, { 0, 1, 0}); // top    +Y
    addFace({-0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f, 0.5f}, {-0.5f,-0.5f, 0.5f}, { 0,-1, 0}); // bottom -Y

    // the Mesh constructor uploads everything to the GPU (it MOVES the two vectors)
    return Mesh(vertices, indices);
}

// Build the full Scene from a layout: one cube mesh + one RenderObject per box + the lights.
inline Scene buildScene(const DungeonLayout& layout) {
    Scene scene;

    // mesh index 0 = the unit cube, reused (scaled/translated) by every object
    scene.meshes.push_back(makeCubeMesh());

    // the three surface materials, in a fixed order: 0 = floor, 1 = wall, 2 = ceiling.
    // (props.h will append its own materials after these, starting at index 3.)
    Material floorMat;   floorMat.albedo   = loadTexture("assets/textures/floor_albedo.png");   floorMat.uvScale = 2.0f; floorMat.roughness = 0.9f;
    Material wallMat;    wallMat.albedo    = loadTexture("assets/textures/wall_albedo.png");    wallMat.uvScale  = 2.0f; wallMat.roughness  = 0.85f;
    Material ceilingMat; ceilingMat.albedo = loadTexture("assets/textures/ceiling_albedo.png"); ceilingMat.uvScale = 2.0f; ceilingMat.roughness = 0.9f;
    scene.materials.push_back(floorMat);     // index 0
    scene.materials.push_back(wallMat);      // index 1
    scene.materials.push_back(ceilingMat);   // index 2

    scene.objects.reserve(layout.boxes.size());
    for (const BoxPlacement& box : layout.boxes) {
        RenderObject obj;
        obj.meshIndex = 0;
        // model matrix = move to the center, then scale the unit cube to the box size.
        // (glm applies these right-to-left, so the cube is scaled first and then translated.)
        glm::mat4 m(1.0f);
        m = glm::translate(m, box.center);
        m = glm::scale(m, box.size);
        obj.modelMatrix = m;
        obj.worldBounds = box.bounds;
        obj.material = box.material;
        // pick the surface material index from the kind
        obj.materialIndex = (box.material == MAT_FLOOR) ? 0 : (box.material == MAT_WALL) ? 1 : 2;
        scene.objects.push_back(obj);
    }

    scene.lights = layout.lights;
    return scene;
}
