#pragma once

// Loads the real dungeon props (from assets/props/) and places them with a bit of "art direction":
// columns as colonnades, scattered barrels/crates/urns, rubble in tight piles, extra braziers to
// fill/light large rooms, and a showpiece room with the statue on the altar (facing the entrance).
//
// To avoid props overlapping each other, we keep a list of occupied footprints (a circle per prop)
// per room and only place a new prop where it does not collide with the ones already there.
//
// Each prop is a .obj + an albedo texture, modeled at real world scale (1 unit = 1 world unit,
// Y up, base on the floor). Called from main AFTER buildScene().

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
#include "world/dungeon_geometry.h"   // DungeonParams
#include "world/chain_physics.h"      // ChainSystem (dynamic hanging chains)

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

struct PropAsset {
    int meshIndex;
    int materialIndex;
    AABB local;
};

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

    return { meshIndex, materialIndex, meshLocalBounds(scene.meshes[meshIndex]) };
}

inline void addPropInstance(Scene& scene, const PropAsset& a, const glm::mat4& model, MaterialId kind = MAT_PROP) {
    RenderObject obj;
    obj.meshIndex = a.meshIndex;
    obj.materialIndex = a.materialIndex;
    obj.material = kind;   // MAT_PROP (blocks the player) by default; MAT_DECOR for wall torches
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

// A corridor opening on the room border -> world (x, z). Falls back to the room center.
inline glm::vec2 findRoomEntrance(const Dungeon& d, const Rect& room, float t) {
    for (int x = room.x - 1; x <= room.x + room.w; x++) {
        for (int y = room.y - 1; y <= room.y + room.h; y++) {
            bool outside = (x < room.x || x >= room.x + room.w || y < room.y || y >= room.y + room.h);
            if (!outside) continue;
            if (x < 0 || y < 0 || x >= d.width || y >= d.height) continue;
            if (d.get(x, y) == FLOOR)
                return glm::vec2((x + 0.5f) * t, (y + 0.5f) * t);
        }
    }
    return glm::vec2((room.x + room.w * 0.5f) * t, (room.y + room.h * 0.5f) * t);
}

// one occupied circle on the floor (so props don't overlap)
struct Footprint { glm::vec2 pos; float radius; };

// Tunable torch/lighting parameters, editable at runtime from the ImGui "Lighting" window.
// Brightness/reach/color apply LIVE (applyTorchLightTuning below); the placement fields
// (spacing/height/tilt/corridor stride) only take effect on a Regenerate.
struct LightingParams {
    float torchSpacing   = 6.0f;   // ~one wall torch every N world units
    int   corridorEvery  = 4;      // corridor torch stride (higher = fewer torches)
    float torchHeight    = 2.0f;   // mount height on the wall
    float coneTilt       = 0.35f;  // how much the shadow cone tilts downward into the room
    glm::vec3 torchColor = glm::vec3(1.0f, 0.75f, 0.45f);
    float torchIntensity = 1.6f;
    float torchRadius    = 5.0f;   // in TILES (world radius = torchRadius * tileSize)
};

// Live-update the torch lights from the current params (no rebuild needed for brightness / reach /
// color). Torches are tagged with Light::isTorch, so room lights are left untouched.
inline void applyTorchLightTuning(Scene& scene, const LightingParams& lp, float tileSize) {
    for (Light& L : scene.lights) {
        if (!L.isTorch) continue;
        L.intensity = lp.torchIntensity;
        L.radius    = lp.torchRadius * tileSize;
        L.color     = lp.torchColor;
    }
}

inline void addProps(Scene& scene, const Dungeon& dungeon, const DungeonParams& params, ChainSystem& chains, const LightingParams& lp) {
    float t = params.tileSize;
    float ceilingY = params.wallHeight;

    std::mt19937 rng(777);
    auto rf = [&](float lo, float hi) { std::uniform_real_distribution<float> d(lo, hi); return d(rng); };
    auto ri = [&](int lo, int hi) { std::uniform_int_distribution<int> d(lo, hi); return d(rng); };

    PropAsset brazier = loadPropAsset(scene, "brazier", 0.5f);   // floor fire: warm POINT light + a body
    PropAsset torch   = loadPropAsset(scene, "torch",   0.55f);   // wall-mounted, iron; light source
    PropAsset column  = loadPropAsset(scene, "column",  0.8f);
    PropAsset statue  = loadPropAsset(scene, "statue",  0.6f);
    PropAsset altar   = loadPropAsset(scene, "altar",   0.7f);
    PropAsset barrel  = loadPropAsset(scene, "barrel",  0.7f);
    PropAsset crate   = loadPropAsset(scene, "crate",   0.7f);
    PropAsset urn     = loadPropAsset(scene, "urn",     0.7f);
    PropAsset rubble  = loadPropAsset(scene, "rubble",  0.9f);
    PropAsset chain   = loadPropAsset(scene, "chain",   0.4f);

    const PropAsset* containers[3] = { &barrel, &crate, &urn };

    // radius of a prop's floor footprint (half of its larger X/Z extent)
    auto footprintRadius = [&](const PropAsset& a) {
        return 0.5f * std::max(a.local.max.x - a.local.min.x, a.local.max.z - a.local.min.z);
    };

    auto floorModel = [&](const PropAsset& a, float x, float z, float yaw, float scale) {
        glm::mat4 m(1.0f);
        m = glm::translate(m, glm::vec3(x, -a.local.min.y * scale, z));
        m = glm::rotate(m, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        m = glm::scale(m, glm::vec3(scale));
        return m;
    };

    auto columnModel = [&](float x, float z, float yaw) {
        float colH = column.local.max.y - column.local.min.y;
        float sy = ceilingY / colH;
        glm::mat4 m(1.0f);
        m = glm::translate(m, glm::vec3(x, -column.local.min.y * sy, z));
        m = glm::rotate(m, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        m = glm::scale(m, glm::vec3(1.0f, sy, 1.0f));
        return m;
    };

    // A brazier's fire: a warm POINT light (default LightType) that casts a cubemap shadow.
    auto addLight = [&](float x, float y, float z) {
        Light L;
        L.position = glm::vec3(x, y, z);
        L.color = glm::vec3(1.0f, 0.8f, 0.5f);
        L.intensity = 3.5f;
        L.radius = 10.0f * t;
        L.castsShadow = true;   // LIGHT_POINT -> cubemap shadow (Lorenzo's POINT path)
        L.isFire = true;        // a brazier fire -> particles spawn here
        scene.lights.push_back(L);
    };

    // reserve a spot (add a footprint without checking) - for props placed at fixed positions
    auto reserve = [&](std::vector<Footprint>& occ, float x, float z, float radius) {
        occ.push_back({ glm::vec2(x, z), radius });
    };

    // try to find a FREE spot in the room for a prop of the given radius; if found, register it
    // and return true. Returns false if the room is too crowded (the prop is then just skipped).
    auto findSpot = [&](std::vector<Footprint>& occ, const Rect& room, float radius, glm::vec2& out) {
        for (int attempt = 0; attempt < 25; attempt++) {
            glm::vec2 p((room.x + rf(1.0f, room.w - 1.0f)) * t, (room.y + rf(1.0f, room.h - 1.0f)) * t);
            bool ok = true;
            for (const Footprint& f : occ) {
                float minDist = f.radius + radius + 0.15f;   // small margin
                glm::vec2 d = p - f.pos;
                if (glm::dot(d, d) < minDist * minDist) { ok = false; break; }
            }
            if (ok) { occ.push_back({ p, radius }); out = p; return true; }
        }
        return false;
    };

    // --- wall torches ---
    // A wall torch is a SPOT light (a cone aimed into the room), non-blocking (MAT_DECOR, because
    // the collision ignores height and a solid torch would act as an invisible wall). It emits a
    // warm accent light and casts a shadow; which torches actually cast a shadow map each frame is
    // decided by the renderer (nearest MAX_SHADOW_LIGHTS to the camera - see renderer.cpp).

    // place one torch: mesh + a warm light, at `(px,pz)` on a wall face at height `y`, facing `n`
    auto addTorch = [&](float px, float pz, float y, glm::vec3 n) {
        float yaw = std::atan2(-n.x, -n.z);   // torch mesh faces -Z by default -> aim it along n
        glm::mat4 m(1.0f);
        m = glm::translate(m, glm::vec3(px, y, pz));
        m = glm::rotate(m, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        addPropInstance(scene, torch, m, MAT_DECOR);

        Light L;
        L.position = glm::vec3(px, y + 0.6f, pz) + n * 0.3f;   // at the flame, a bit into the room
        L.color = lp.torchColor;
        L.intensity = lp.torchIntensity;                       // moderate: many torches can be lit at once
        L.radius = lp.torchRadius * t;
        L.isTorch = true;                                      // tunable from the Lighting window
        L.isFire = true;                                       // a torch flame -> particles spawn here
        // A torch flame radiates in ALL directions, so it's a POINT light with a cubemap shadow:
        // that shadows the floor AND the walls (a single SPOT cone could only cover one of them).
        // Costs 6 depth passes vs 1 for a SPOT, but the renderer only shadows the nearest few.
        L.type = LIGHT_POINT;
        L.castsShadow = true;
        L.direction = glm::normalize(n + glm::vec3(0.0f, -lp.coneTilt, 0.0f));
        scene.lights.push_back(L);
    };

    // wall torches for a room: evenly spaced along EVERY side, skipping the spots that fall on a
    // corridor opening (so a torch never floats in a doorway). Unlike before, sides WITH a corridor
    // are no longer skipped whole - only the individual opening spots are.
    auto placeWallTorches = [&](const Rect& room) {
        const float torchY = lp.torchHeight;   // mount height on the wall (walls are wallHeight = 4.5)

        //   horizontal : side runs along X (north/south) if true, along Z (west/east) if false
        //   ringGrid   : grid row/col of the wall ring just outside the room
        //   alongStart / lenTiles : the run of the side, in tiles
        //   innerFace  : world coord (Z if horizontal, X if vertical) of the wall's inner face
        //   n          : inward normal (into the room)
        auto placeSide = [&](bool horizontal, int ringGrid, int alongStart, int lenTiles,
                             float innerFace, glm::vec3 n) {
            float sideLen = lenTiles * t;
            int count = std::max(1, (int)std::lround(sideLen / lp.torchSpacing));
            float start = alongStart * t;
            for (int k = 0; k < count; k++) {
                float along = start + sideLen * (k + 0.5f) / count;   // evenly spaced, half-margins
                // skip if the wall cell behind this spot is an opening (corridor), not solid
                int cell = (int)(along / t);
                int gx = horizontal ? cell : ringGrid;
                int gy = horizontal ? ringGrid : cell;
                if (gx < 0 || gy < 0 || gx >= dungeon.width || gy >= dungeon.height) continue;
                if (dungeon.get(gx, gy) == FLOOR) continue;   // opening here -> no torch
                float px = horizontal ? along : innerFace;
                float pz = horizontal ? innerFace : along;
                addTorch(px, pz, torchY, n);
            }
        };

        int x0 = room.x, y0 = room.y, w = room.w, h = room.h;
        placeSide(true,  y0 - 1, x0, w, y0 * t,         glm::vec3(0.0f, 0.0f,  1.0f));   // north wall
        placeSide(true,  y0 + h, x0, w, (y0 + h) * t,   glm::vec3(0.0f, 0.0f, -1.0f));   // south wall
        placeSide(false, x0 - 1, y0, h, x0 * t,         glm::vec3( 1.0f, 0.0f, 0.0f));   // west wall
        placeSide(false, x0 + w, y0, h, (x0 + w) * t,   glm::vec3(-1.0f, 0.0f, 0.0f));   // east wall
    };

    // torches in the corridors (floor cells that are NOT inside any room), so corridors are not
    // pitch black. One every few cells, on the first adjacent wall we find, facing the corridor.
    auto placeCorridorTorches = [&]() {
        auto inAnyRoom = [&](int gx, int gy) {
            for (const Rect& r : dungeon.rooms)
                if (gx >= r.x && gx < r.x + r.w && gy >= r.y && gy < r.y + r.h) return true;
            return false;
        };
        const int dxs[4] = { 0, 0, -1, 1 };
        const int dys[4] = { -1, 1, 0, 0 };
        for (int gy = 0; gy < dungeon.height; gy++) {
            for (int gx = 0; gx < dungeon.width; gx++) {
                if (dungeon.get(gx, gy) != FLOOR) continue;
                if (inAnyRoom(gx, gy)) continue;                   // rooms are handled by wall torches
                int every = std::max(1, lp.corridorEvery);
                if (((gx * 3 + gy * 5) % every) != 0) continue;    // space them out along the corridor
                for (int d = 0; d < 4; d++) {
                    int wx = gx + dxs[d], wy = gy + dys[d];
                    if (wx < 0 || wy < 0 || wx >= dungeon.width || wy >= dungeon.height) continue;
                    if (dungeon.get(wx, wy) != WALL) continue;
                    glm::vec3 n((float)-dxs[d], 0.0f, (float)-dys[d]);   // from the wall into the corridor
                    float px = (gx + 0.5f) * t + dxs[d] * (t * 0.5f);    // on the shared wall face
                    float pz = (gy + 0.5f) * t + dys[d] * (t * 0.5f);
                    addTorch(px, pz, lp.torchHeight, n);
                    break;   // one torch per corridor cell
                }
            }
        }
    };

    // largest room = showpiece
    int largestIdx = -1, largestArea = 0;
    for (int i = 0; i < (int)dungeon.rooms.size(); i++) {
        int area = dungeon.rooms[i].w * dungeon.rooms[i].h;
        if (area > largestArea) { largestArea = area; largestIdx = i; }
    }

    for (int i = 0; i < (int)dungeon.rooms.size(); i++) {
        const Rect& room = dungeon.rooms[i];
        int area = room.w * room.h;
        float cx = (room.x + room.w * 0.5f) * t;
        float cz = (room.y + room.h * 0.5f) * t;

        std::vector<Footprint> occ;   // occupied floor spots in this room

        // wall torches on every solid side (all rooms, showpiece included)
        placeWallTorches(room);

        // -------- showpiece room --------
        if (i == largestIdx) {
            addPropInstance(scene, altar, floorModel(altar, cx, cz, 0.0f, 1.0f));
            reserve(occ, cx, cz, 1.0f);

            glm::vec2 entrance = findRoomEntrance(dungeon, room, t);
            // face the entrance (this statue model's front points +Z, so we aim +Z at the opening)
            float yaw = std::atan2(entrance.x - cx, entrance.y - cz);
            float altarTop = altar.local.max.y;
            glm::mat4 sm = glm::translate(glm::mat4(1.0f), glm::vec3(cx, altarTop - statue.local.min.y, cz));
            sm = glm::rotate(sm, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
            addPropInstance(scene, statue, sm);

            // flanking braziers (each a warm POINT light) beside the statue. MAT_DECOR: like the
            // wall torches, a light source doesn't cast its own shadow (would self-shadow its base).
            for (int s = -1; s <= 1; s += 2) {
                float bx = cx + s * 3.0f;
                addPropInstance(scene, brazier, floorModel(brazier, bx, cz, 0.0f, 1.0f), MAT_DECOR);
                addLight(bx, 1.2f, cz);
                reserve(occ, bx, cz, 0.7f);
            }

            // --- P2: fill the showpiece room so it doesn't feel empty ---
            // columns flanking the altar (floor-to-ceiling)
            for (int s = -1; s <= 1; s += 2) {
                float colx = cx + s * 5.0f;
                addPropInstance(scene, column, columnModel(colx, cz, 0.0f));
                reserve(occ, colx, cz, footprintRadius(column));
            }
            // a couple of extra floor statues (no pedestal), each facing the centre
            for (int s = 0; s < 2; s++) {
                glm::vec2 spot;
                if (findSpot(occ, room, 0.6f, spot)) {
                    float sy = std::atan2(cx - spot.x, cz - spot.y);   // face the centre
                    addPropInstance(scene, statue, floorModel(statue, spot.x, spot.y, sy, 0.85f));
                }
            }
            // scattered urns + one rubble pile for clutter
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

        // -------- normal room --------
        // central brazier: gives the room's base POINT light (from dungeon_geometry, at this spot) a
        // visible body. MAT_DECOR so it doesn't self-shadow / block the player.
        addPropInstance(scene, brazier, floorModel(brazier, cx, cz, rf(0.0f, 6.28f), 1.0f), MAT_DECOR);
        reserve(occ, cx, cz, 0.7f);
        // the central brazier is its own light source now (there are no per-room fill lights any
        // more): a warm POINT light + fire, at the bowl. Same values the room fill light used to have.
        addLight(cx, 1.2f, cz);

        // colonnade along the two side walls of big rooms (fixed positions -> reserved)
        if (room.w >= 6 && room.h >= 6) {
            int count = std::max(2, std::min(room.w, room.h) / 3);
            for (int c = 0; c < count; c++) {
                float f = (c + 1.0f) / (count + 1.0f);
                float zline = (room.y + room.h * f) * t;
                float lx = (room.x + 1.2f) * t, rx = (room.x + room.w - 1.2f) * t;
                addPropInstance(scene, column, columnModel(lx, zline, 0.0f));  reserve(occ, lx, zline, footprintRadius(column));
                addPropInstance(scene, column, columnModel(rx, zline, 0.0f));  reserve(occ, rx, zline, footprintRadius(column));
            }
        }

        // big rooms: two extra braziers (each a POINT light), in free spots
        if (area >= 60) {
            for (int b = 0; b < 2; b++) {
                glm::vec2 spot;
                if (findSpot(occ, room, 0.7f, spot)) {
                    addPropInstance(scene, brazier, floorModel(brazier, spot.x, spot.y, rf(0.0f, 6.28f), 1.0f), MAT_DECOR);
                    addLight(spot.x, 1.2f, spot.y);
                }
            }
        }

        // barrels / crates / urns scattered (count grows with room size), no overlaps
        int nContainers = std::min(6, std::max(2, area / 12));
        for (int c = 0; c < nContainers; c++) {
            const PropAsset* a = containers[ri(0, 2)];
            glm::vec2 spot;
            if (findSpot(occ, room, footprintRadius(*a), spot))
                addPropInstance(scene, *a, floorModel(*a, spot.x, spot.y, rf(0.0f, 6.28f), 1.0f));
        }

        // rubble piles (1, or 2 in big rooms): reserve the whole pile as one footprint
        int nPiles = (area >= 60) ? 2 : 1;
        for (int pile = 0; pile < nPiles; pile++) {
            glm::vec2 spot;
            if (findSpot(occ, room, 1.2f, spot)) {
                int n = ri(3, 5);
                for (int k = 0; k < n; k++)
                    addPropInstance(scene, rubble, floorModel(rubble, spot.x + rf(-0.6f, 0.6f), spot.y + rf(-0.6f, 0.6f), rf(0.0f, 6.28f), rf(0.7f, 1.3f)));
            }
        }

        // a couple of hanging chains in big rooms (dynamic: they swing)
        if (area >= 60) {
            int nChains = ri(1, 2);
            for (int c = 0; c < nChains; c++) {
                glm::vec2 spot;
                if (findSpot(occ, room, 0.4f, spot))
                    chains.addChain(scene, chain.meshIndex, chain.materialIndex, chain.local, spot.x, spot.y, ceilingY, ri(3, 4));
            }
        }
    }

    // corridors get their own torches so they are not pitch black (rooms did theirs above)
    placeCorridorTorches();
}
