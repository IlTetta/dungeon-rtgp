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

inline void addPropInstance(Scene& scene, const PropAsset& a, const glm::mat4& model) {
    RenderObject obj;
    obj.meshIndex = a.meshIndex;
    obj.materialIndex = a.materialIndex;
    obj.material = MAT_PROP;
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

inline void addProps(Scene& scene, const Dungeon& dungeon, const DungeonParams& params, ChainSystem& chains) {
    float t = params.tileSize;
    float ceilingY = params.wallHeight;

    std::mt19937 rng(777);
    auto rf = [&](float lo, float hi) { std::uniform_real_distribution<float> d(lo, hi); return d(rng); };
    auto ri = [&](int lo, int hi) { std::uniform_int_distribution<int> d(lo, hi); return d(rng); };

    PropAsset brazier = loadPropAsset(scene, "brazier", 0.5f);
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

    auto addLight = [&](float x, float y, float z) {
        Light L;
        L.position = glm::vec3(x, y, z);
        L.color = glm::vec3(1.0f, 0.8f, 0.5f);
        L.intensity = 3.5f;
        L.radius = 10.0f * t;
        // M2 (M2_shadows_plan.md, S1): same reasoning as dungeon_geometry.h's room light -
        // a brazier stays LIGHT_POINT (default) and gets a cubemap shadow.
        L.castsShadow = true;
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

            for (int s = -1; s <= 1; s += 2) {
                float bx = cx + s * 3.0f;
                addPropInstance(scene, brazier, floorModel(brazier, bx, cz, 0.0f, 1.0f));
                addLight(bx, 1.2f, cz);
                reserve(occ, bx, cz, 0.7f);
            }
            for (int c = 0; c < 4; c++) {
                glm::vec2 spot;
                if (findSpot(occ, room, 0.4f, spot))
                    chains.addChain(scene, chain.meshIndex, chain.materialIndex, chain.local, spot.x, spot.y, ceilingY, ri(3, 4));
            }
            continue;
        }

        // -------- normal room --------
        // central brazier = the room's main light (the room light from dungeon_geometry is here)
        addPropInstance(scene, brazier, floorModel(brazier, cx, cz, rf(0.0f, 6.28f), 1.0f));
        reserve(occ, cx, cz, 0.7f);

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

        // big rooms: two extra braziers (with lights), placed in free spots
        if (area >= 60) {
            for (int b = 0; b < 2; b++) {
                glm::vec2 spot;
                if (findSpot(occ, room, 0.7f, spot)) {
                    addPropInstance(scene, brazier, floorModel(brazier, spot.x, spot.y, rf(0.0f, 6.28f), 1.0f));
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
}
