#pragma once

// Loads the dungeon props (assets/props/) and places them room by room with simple rules:
// wall torches on every side, one or two braziers per room, colonnades in the big rooms,
// barrels / crates / urns scattered around, rubble in small piles, hanging chains, and one
// showpiece room (the largest) with the statue on the altar, facing the entrance.
// It also creates all the lights of the scene: every light is a torch or a brazier fire.
//
// To keep props from overlapping, every room keeps a list of occupied floor circles
// (footprints) and a new prop is placed only where it does not touch them.
//
// Every prop is an .obj + an albedo texture, modeled at real scale (1 unit = 1 world unit, Y up,
// base on the floor). addProps() is called by buildWorld() after buildScene().

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <random>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

#include "core/scene.h"
#include "engine/obj_loader.h"
#include "engine/texture.h"
#include "dungeon/dungeon_generator.h"
#include "world/dungeon_geometry.h"
#include "world/chain_physics.h"

// AABB of a mesh in its own (local) space, from all its vertices.
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

// A loaded prop: its mesh and material in the Scene, and its local AABB.
struct PropAsset {
    int meshIndex;
    int materialIndex;
    AABB local;
};

// Load one prop (mesh + albedo) into the Scene, once. Every instance then reuses the same mesh.
inline PropAsset loadPropAsset(Scene& scene, const std::string& name, float roughness) {
    std::string dir = "assets/props/" + name + "/";
    int meshIndex = (int)scene.meshes.size();
    scene.meshes.push_back(loadOBJ((dir + name + ".obj").c_str()));

    Material mat;
    mat.albedo = loadTexture((dir + name + "_albedo.png").c_str());
    mat.uvScale = 1.0f;
    mat.roughness = roughness;
    int materialIndex = (int)scene.materials.size();
    scene.materials.push_back(mat);

    return { 
        meshIndex, materialIndex, meshLocalBounds(scene.meshes[meshIndex])
    };
}

// Add one instance of a prop with the given model matrix. Its world AABB is the box around the
// 8 transformed corners of the local AABB: with a rotation it is a bit bigger than the prop, but
// it always contains it.
inline void addPropInstance(Scene& scene, const PropAsset& a, const glm::mat4& model, MaterialId kind = MAT_PROP) {
    RenderObject obj;
    obj.meshIndex = a.meshIndex;
    obj.materialIndex = a.materialIndex;
    obj.material = kind;   // MAT_PROP blocks the player; MAT_DECOR (torches, braziers) does not
    obj.modelMatrix = model;

    glm::vec3 wmin(1e9f), wmax(-1e9f);
    for (int c = 0; c < 8; c++) {
        glm::vec3 corner(
            (c & 1) ? a.local.max.x : a.local.min.x,
            (c & 2) ? a.local.max.y : a.local.min.y,
            (c & 4) ? a.local.max.z : a.local.min.z);
        glm::vec3 w = glm::vec3(model * glm::vec4(corner, 1.0f));
        wmin = glm::min(wmin, w);
        wmax = glm::max(wmax, w);
    }
    obj.worldBounds = { wmin, wmax };
    scene.objects.push_back(obj);
}

// World (x, z) of a corridor opening on the room border: the first FLOOR tile in the ring just
// outside the room. If there is none, the room center.
inline glm::vec2 findRoomEntrance(const Dungeon& d, const Rect& room, float t) {
    for (int x = room.x - 1; x <= room.x + room.w; x++) {
        for (int y = room.y - 1; y <= room.y + room.h; y++) {
            bool outside = (x < room.x || x >= room.x + room.w || y < room.y || y >= room.y + room.h);
            if (!outside)
                continue;
            if (x < 0 || y < 0 || x >= d.width || y >= d.height)
                continue;
            if (d.get(x, y) == FLOOR)
                return glm::vec2((x + 0.5f) * t, (y + 0.5f) * t);
        }
    }
    return glm::vec2((room.x + room.w * 0.5f) * t, (room.y + room.h * 0.5f) * t);
}

// one occupied circle on the floor
struct Footprint { 
    glm::vec2 pos; 
    float radius; 
};

// Torch and light settings, editable from the HUD "Lighting" window. Color, intensity and radius
// are applied live; spacing, height, tilt and corridor stride only change
// the placement, so they need a Regenerate.
struct LightingParams {
    float torchSpacing = 6.0f;  // about one wall torch every N world units
    int corridorEvery = 4;  // corridor torch stride (higher = fewer torches)
    float torchHeight = 2.0f;   // height on the wall
    float coneTilt = 0.35f; // how much the torch direction points down into the room
    glm::vec3 torchColor = glm::vec3(1.0f, 0.75f, 0.45f);
    float torchIntensity = 1.6f;
    float torchRadius = 5.0f;   // in tiles (world radius = torchRadius * tileSize)
};

// Push the HUD values into the fire lights, every frame. The color goes to every fire (torches
// and braziers, and so to their flame particles too); intensity and radius only to the torches,
// so the braziers keep their stronger, wider light.
inline void applyTorchLightTuning(Scene& scene, const LightingParams& lp, float tileSize) {
    for (Light& L : scene.lights) {
        if (!L.isFire)
            continue;
        L.color = lp.torchColor;
        if (L.isTorch) {
            L.intensity = lp.torchIntensity;
            L.radius = lp.torchRadius * tileSize;
        }
    }
}

inline void addProps(Scene& scene, const Dungeon& dungeon, const DungeonParams& params, ChainSystem& chains, const LightingParams& lp) {
    float t = params.tileSize;
    float ceilingY = params.wallHeight;

    // fixed seed: for a given dungeon the props are always placed the same way
    std::mt19937 rng(777);
    auto rf = [&](float lo, float hi) {
        std::uniform_real_distribution<float> d(lo, hi);
        return d(rng);
    };
    auto ri = [&](int lo, int hi) {
        std::uniform_int_distribution<int> d(lo, hi);
        return d(rng);
    };

    PropAsset brazier = loadPropAsset(scene, "brazier", 0.5f);
    PropAsset torch = loadPropAsset(scene, "torch", 0.55f);
    PropAsset column = loadPropAsset(scene, "column", 0.8f);
    PropAsset statue = loadPropAsset(scene, "statue", 0.6f);
    PropAsset altar = loadPropAsset(scene, "altar", 0.7f);
    PropAsset barrel = loadPropAsset(scene, "barrel", 0.7f);
    PropAsset crate = loadPropAsset(scene, "crate", 0.7f);
    PropAsset urn = loadPropAsset(scene, "urn", 0.7f);
    PropAsset rubble = loadPropAsset(scene, "rubble", 0.9f);
    PropAsset chain = loadPropAsset(scene, "chain", 0.4f);

    const PropAsset* containers[3] = { &barrel, &crate, &urn };

    // radius of a prop's footprint: half of its larger X/Z size
    auto footprintRadius = [&](const PropAsset& a) {
        return 0.5f * std::max(a.local.max.x - a.local.min.x, a.local.max.z - a.local.min.z);
    };

    // model matrix of a prop on the floor: lifted by -min.y so its base sits at y = 0, turned by
    // yaw around Y, uniformly scaled
    auto floorModel = [&](const PropAsset& a, float x, float z, float yaw, float scale) {
        glm::mat4 m(1.0f);
        m = glm::translate(m, glm::vec3(x, -a.local.min.y * scale, z));
        m = glm::rotate(m, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        m = glm::scale(m, glm::vec3(scale));
        return m;
    };

    // a column stretched only in Y, so it goes exactly from the floor to the ceiling
    auto columnModel = [&](float x, float z, float yaw) {
        float colH = column.local.max.y - column.local.min.y;
        float sy = ceilingY / colH;
        glm::mat4 m(1.0f);
        m = glm::translate(m, glm::vec3(x, -column.local.min.y * sy, z));
        m = glm::rotate(m, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        m = glm::scale(m, glm::vec3(1.0f, sy, 1.0f));
        return m;
    };

    // the fire of a brazier: a warm POINT light (the default type) with a cubemap shadow
    auto addLight = [&](float x, float y, float z) {
        Light L;
        L.position = glm::vec3(x, y, z);
        L.color = glm::vec3(1.0f, 0.8f, 0.5f);
        L.intensity = 3.5f;
        L.radius = 10.0f * t;
        L.castsShadow = true;
        L.isFire = true;   // particles spawn here
        scene.lights.push_back(L);
    };

    // register a footprint without checking, for props at fixed positions
    auto reserve = [&](std::vector<Footprint>& occ, float x, float z, float radius) {
        occ.push_back({ glm::vec2(x, z), radius });
    };

    // Look for a free spot for a prop of the given radius: up to 25 random tries inside the room
    // (keeping 1 tile from the walls). A spot is free if it is far enough from every footprint.
    // If found, the spot is registered and we return true; if the room is too full, false and
    // the prop is just skipped.
    auto findSpot = [&](std::vector<Footprint>& occ, const Rect& room, float radius, glm::vec2& out) {
        for (int attempt = 0; attempt < 25; attempt++) {
            glm::vec2 p((room.x + rf(1.0f, room.w - 1.0f)) * t, (room.y + rf(1.0f, room.h - 1.0f)) * t);
            bool ok = true;
            for (const Footprint& f : occ) {
                float minDist = f.radius + radius + 0.15f;   // small margin
                glm::vec2 d = p - f.pos;
                if (glm::dot(d, d) < minDist * minDist) {
                    ok = false;
                    break;
                }
            }
            if (ok) {
                occ.push_back({ p, radius });
                out = p;
                return true;
            }
        }
        return false;
    };

    // One wall torch at (px, pz) on a wall face, at height y, facing n (into the room).
    // The torch mesh is MAT_DECOR: the collisions ignore the height, so a solid torch would be
    // an invisible wall in front of the real one.
    auto addTorch = [&](float px, float pz, float y, glm::vec3 n) {
        float yaw = std::atan2(-n.x, -n.z);   // the mesh faces -Z, turn it to face n
        glm::mat4 m(1.0f);
        m = glm::translate(m, glm::vec3(px, y, pz));
        m = glm::rotate(m, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        addPropInstance(scene, torch, m, MAT_DECOR);

        Light L;
        L.position = glm::vec3(px, y + 0.6f, pz) + n * 0.3f;   // at the flame, a bit into the room
        L.color = lp.torchColor;
        L.intensity = lp.torchIntensity;
        L.radius = lp.torchRadius * t;
        L.isTorch = true;
        L.isFire = true;
        L.type = LIGHT_SPOT;
        L.castsShadow = true;
        L.direction = glm::normalize(n + glm::vec3(0.0f, -lp.coneTilt, 0.0f));
        scene.lights.push_back(L);
    };

    // Wall torches of a room: evenly spaced along every side, skipping the spots that fall on a
    // corridor opening (a torch must not float in a doorway).
    auto placeWallTorches = [&](const Rect& room) {
        const float torchY = lp.torchHeight;

        // placeSide parameters:
        //   horizontal: the side runs along X (north / south walls) if true, along Z if false
        //   ringGrid: grid row (or column) of the wall ring just outside the room
        //   alongStart, lenTiles: where the side starts and how long it is, in tiles
        //   innerFace: world Z (or X) of the inner face of that wall
        //   n: normal pointing into the room
        auto placeSide = [&](bool horizontal, int ringGrid, int alongStart, int lenTiles,
                             float innerFace, glm::vec3 n) {
            float sideLen = lenTiles * t;
            int count = std::max(1, (int)std::lround(sideLen / lp.torchSpacing));
            float start = alongStart * t;
            for (int k = 0; k < count; k++) {
                float along = start + sideLen * (k + 0.5f) / count;   // centers of count equal parts
                // the wall tile behind this spot: if it is FLOOR it is a corridor opening
                int cell = (int)(along / t);
                int gx = horizontal ? cell : ringGrid;
                int gy = horizontal ? ringGrid : cell;
                if (gx < 0 || gy < 0 || gx >= dungeon.width || gy >= dungeon.height)
                    continue;
                if (dungeon.get(gx, gy) == FLOOR)
                    continue;
                float px = horizontal ? along : innerFace;
                float pz = horizontal ? innerFace : along;
                addTorch(px, pz, torchY, n);
            }
        };

        int x0 = room.x, y0 = room.y, w = room.w, h = room.h;
        placeSide(true, y0 - 1, x0, w, y0 * t, glm::vec3(0.0f, 0.0f, 1.0f));    // north
        placeSide(true, y0 + h, x0, w, (y0 + h) * t, glm::vec3(0.0f, 0.0f, -1.0f)); // south
        placeSide(false, x0 - 1, y0, h, x0 * t, glm::vec3(1.0f, 0.0f, 0.0f));   // west
        placeSide(false, x0 + w, y0, h, (x0 + w) * t, glm::vec3(-1.0f, 0.0f, 0.0f));    // east
    };

    // Torches in the corridors (FLOOR tiles outside every room), so they are not completely
    // dark: on some tiles, on the first wall next to it, facing the corridor.
    auto placeCorridorTorches = [&]() {
        auto inAnyRoom = [&](int gx, int gy) {
            for (const Rect& r : dungeon.rooms)
                if (gx >= r.x && gx < r.x + r.w && gy >= r.y && gy < r.y + r.h)
                    return true;
            return false;
        };
        const int dxs[4] = { 0, 0, -1, 1 };
        const int dys[4] = { -1, 1, 0, 0 };
        for (int gy = 0; gy < dungeon.height; gy++) {
            for (int gx = 0; gx < dungeon.width; gx++) {
                if (dungeon.get(gx, gy) != FLOOR)
                    continue;
                if (inAnyRoom(gx, gy))
                    continue;
                // a simple hash of the tile to keep about one tile out of corridorEvery
                int every = std::max(1, lp.corridorEvery);
                if (((gx * 3 + gy * 5) % every) != 0)
                    continue;
                for (int d = 0; d < 4; d++) {
                    int wx = gx + dxs[d], wy = gy + dys[d];
                    if (wx < 0 || wy < 0 || wx >= dungeon.width || wy >= dungeon.height)
                        continue;
                    if (dungeon.get(wx, wy) != WALL)
                        continue;
                    glm::vec3 n((float)-dxs[d], 0.0f, (float)-dys[d]);   // from the wall into the corridor
                    float px = (gx + 0.5f) * t + dxs[d] * (t * 0.5f);    // on the shared wall face
                    float pz = (gy + 0.5f) * t + dys[d] * (t * 0.5f);
                    addTorch(px, pz, lp.torchHeight, n);
                    break;   // one torch per tile
                }
            }
        }
    };

    // the largest room is the showpiece
    int largestIdx = -1, largestArea = 0;
    for (int i = 0; i < (int)dungeon.rooms.size(); i++) {
        int area = dungeon.rooms[i].w * dungeon.rooms[i].h;
        if (area > largestArea) {
            largestArea = area;
            largestIdx = i;
        }
    }

    for (int i = 0; i < (int)dungeon.rooms.size(); i++) {
        const Rect& room = dungeon.rooms[i];
        int area = room.w * room.h;
        float cx = (room.x + room.w * 0.5f) * t;
        float cz = (room.y + room.h * 0.5f) * t;

        std::vector<Footprint> occ;   // occupied spots in this room

        placeWallTorches(room);

        // ---------- showpiece room ----------
        if (i == largestIdx) {
            addPropInstance(scene, altar, floorModel(altar, cx, cz, 0.0f, 1.0f));
            reserve(occ, cx, cz, 1.0f);

            // the statue stands on the altar and faces the entrance (its front is +Z)
            glm::vec2 entrance = findRoomEntrance(dungeon, room, t);
            float yaw = std::atan2(entrance.x - cx, entrance.y - cz);
            float altarTop = altar.local.max.y;
            glm::mat4 sm = glm::translate(glm::mat4(1.0f), glm::vec3(cx, altarTop - statue.local.min.y, cz));
            sm = glm::rotate(sm, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
            addPropInstance(scene, statue, sm);

            // two braziers beside the statue, each with its POINT light. Like the torches they are
            // MAT_DECOR: they do not block the player and do not cast shadows (a brazier would
            // shadow its own light).
            for (int s = -1; s <= 1; s += 2) {
                float bx = cx + s * 3.0f;
                addPropInstance(scene, brazier, floorModel(brazier, bx, cz, 0.0f, 1.0f), MAT_DECOR);
                addLight(bx, 1.2f, cz);
                reserve(occ, bx, cz, 0.7f);
            }

            // two columns beside the altar, floor to ceiling
            for (int s = -1; s <= 1; s += 2) {
                float colx = cx + s * 5.0f;
                addPropInstance(scene, column, columnModel(colx, cz, 0.0f));
                reserve(occ, colx, cz, footprintRadius(column));
            }
            // two smaller statues on the floor, facing the center
            for (int s = 0; s < 2; s++) {
                glm::vec2 spot;
                if (findSpot(occ, room, 0.6f, spot)) {
                    float sy = std::atan2(cx - spot.x, cz - spot.y);
                    addPropInstance(scene, statue, floorModel(statue, spot.x, spot.y, sy, 0.85f));
                }
            }
            // some urns and one rubble pile
            for (int c = 0; c < 4; c++) {
                glm::vec2 spot;
                if (findSpot(occ, room, footprintRadius(urn), spot))
                    addPropInstance(scene, urn, floorModel(urn, spot.x, spot.y, rf(0.0f, 6.28f), 1.0f));
            }
            {
                glm::vec2 spot;
                if (findSpot(occ, room, 1.2f, spot)) {
                    int n = ri(3, 5);
                    for (int k = 0; k < n; k++)
                        addPropInstance(scene, rubble, floorModel(rubble, spot.x + rf(-0.6f, 0.6f), spot.y + rf(-0.6f, 0.6f), rf(0.0f, 6.28f), rf(0.7f, 1.3f)));
                }
            }

            // hanging chains
            for (int c = 0; c < 4; c++) {
                glm::vec2 spot;
                if (findSpot(occ, room, 0.4f, spot))
                    chains.addChain(scene, chain.meshIndex, chain.materialIndex, chain.local, spot.x, spot.y, ceilingY, ri(3, 4));
            }
            continue;
        }

        // ---------- normal room ----------
        // The braziers are the only POINT lights, and the renderer gives a shadow only to the 2
        // nearest ones.
        if (area >= 60) {
            float roomW = room.w * t, roomH = room.h * t;
            glm::vec2 b0, b1;
            if (roomW >= roomH) {
                b0 = glm::vec2(cx - roomW * 0.22f, cz);
                b1 = glm::vec2(cx + roomW * 0.22f, cz);
            }
            else {
                b0 = glm::vec2(cx, cz - roomH * 0.22f);
                b1 = glm::vec2(cx, cz + roomH * 0.22f);
            }
            addPropInstance(scene, brazier, floorModel(brazier, b0.x, b0.y, rf(0.0f, 6.28f), 1.0f), MAT_DECOR);
            reserve(occ, b0.x, b0.y, 0.7f);
            addLight(b0.x, 1.2f, b0.y);
            addPropInstance(scene, brazier, floorModel(brazier, b1.x, b1.y, rf(0.0f, 6.28f), 1.0f), MAT_DECOR);
            reserve(occ, b1.x, b1.y, 0.7f);
            addLight(b1.x, 1.2f, b1.y);
        }
        else {
            addPropInstance(scene, brazier, floorModel(brazier, cx, cz, rf(0.0f, 6.28f), 1.0f), MAT_DECOR);
            reserve(occ, cx, cz, 0.7f);
            addLight(cx, 1.2f, cz);
        }

        // colonnade along two walls of the bigger rooms (fixed positions, so reserved)
        if (room.w >= 6 && room.h >= 6) {
            int count = std::max(2, std::min(room.w, room.h) / 3);
            for (int c = 0; c < count; c++) {
                float f = (c + 1.0f) / (count + 1.0f);
                float zline = (room.y + room.h * f) * t;
                float lx = (room.x + 1.2f) * t, rx = (room.x + room.w - 1.2f) * t;
                addPropInstance(scene, column, columnModel(lx, zline, 0.0f));
                reserve(occ, lx, zline, footprintRadius(column));
                addPropInstance(scene, column, columnModel(rx, zline, 0.0f));
                reserve(occ, rx, zline, footprintRadius(column));
            }
        }

        // barrels / crates / urns: more in bigger rooms (2 to 6)
        int nContainers = std::min(6, std::max(2, area / 12));
        for (int c = 0; c < nContainers; c++) {
            const PropAsset* a = containers[ri(0, 2)];
            glm::vec2 spot;
            if (findSpot(occ, room, footprintRadius(*a), spot))
                addPropInstance(scene, *a, floorModel(*a, spot.x, spot.y, rf(0.0f, 6.28f), 1.0f));
        }

        // rubble piles (2 in big rooms): the whole pile is one footprint
        int nPiles = (area >= 60) ? 2 : 1;
        for (int pile = 0; pile < nPiles; pile++) {
            glm::vec2 spot;
            if (findSpot(occ, room, 1.2f, spot)) {
                int n = ri(3, 5);
                for (int k = 0; k < n; k++)
                    addPropInstance(scene, rubble, floorModel(rubble, spot.x + rf(-0.6f, 0.6f), spot.y + rf(-0.6f, 0.6f), rf(0.0f, 6.28f), rf(0.7f, 1.3f)));
            }
        }

        // one or two hanging chains in big rooms
        if (area >= 60) {
            int nChains = ri(1, 2);
            for (int c = 0; c < nChains; c++) {
                glm::vec2 spot;
                if (findSpot(occ, room, 0.4f, spot))
                    chains.addChain(scene, chain.meshIndex, chain.materialIndex, chain.local, spot.x, spot.y, ceilingY, ri(3, 4));
            }
        }
    }

    placeCorridorTorches();
}
