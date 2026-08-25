#pragma once

// Ties the whole "make a playable dungeon" pipeline behind ONE call:
//   BSP generation -> 3D geometry -> Scene -> props/chains -> camera spawn.
//
// Why: main used to run this sequence once at startup. By wrapping it in a function we can
// rebuild the world at RUNTIME from a new seed (the ImGui "Generate" button), without closing
// the window, editing the seed in code, recompiling and reopening. This is a tool for M3/M4:
// it lets us try many dungeons quickly and always regenerate the SAME one from a given seed.

#include <glm/glm.hpp>

#include "engine/camera.h"
#include "core/scene.h"
#include "dungeon/dungeon_generator.h"
#include "world/dungeon_geometry.h"      // DungeonParams, buildDungeonLayout, buildScene
#include "world/props.h"                 // addProps
#include "world/chain_physics.h"         // ChainSystem

// Map size in tiles. Kept constant: only the SEED changes at runtime (so different seeds are
// comparable, same map size). These were the 60/30 hardcoded in main before.
const int DUNGEON_TILES_X = 60;
const int DUNGEON_TILES_Y = 30;

// Build (or REBUILD) the whole world for a given seed.
//   - fills `scene` from scratch (geometry + materials + lights + props),
//   - resets `chains` and re-creates the hanging chains for the new scene,
//   - moves `camera` to the spawn point (center of the first room).
// Called once at startup and again on every "Generate" click.
//
// Note on GPU memory: `scene = buildScene(...)` is a MOVE-assign. Scene owns its meshes, and
// Mesh has a destructor that frees its VAO/VBO/EBO (see engine/mesh.h). So replacing the scene
// this way destroys the old meshes and frees them on the GPU automatically, no manual cleanup.
// (The per-object textures are reloaded each time though: see world_notes, that is a known
// small leak, fine for an occasional debug regeneration.)
inline void buildWorld(unsigned int seed, const DungeonParams& params, const LightingParams& lp,
                       Scene& scene, ChainSystem& chains, Camera& camera) {
    // 1. generate the 2D dungeon (WALL / FLOOR grid) for this seed
    DungeonGenerator generator(DUNGEON_TILES_X, DUNGEON_TILES_Y, seed);
    Dungeon dungeon = generator.generate();

    // 2. turn it into the 3D layout (pure math) and then into the GPU Scene
    DungeonLayout layout = buildDungeonLayout(dungeon, params);
    scene = buildScene(layout);   // move-assign: frees the previous scene's meshes (see above)

    // 3. props + dynamic chains. The chain system stores indices into scene.objects, so it must
    // start empty for the new scene; addProps then re-creates the chains.
    chains.chains.clear();
    addProps(scene, dungeon, params, chains, lp);

    // 4. spawn the camera at eye height near the center of the first room (same rule as before),
    // offset a bit so we do not spawn right inside the central brazier.
    if (!dungeon.rooms.empty()) {
        Rect r = dungeon.rooms[0];
        float cx = (r.x + r.w * 0.5f) * params.tileSize;
        float cz = (r.y + r.h * 0.5f) * params.tileSize;
        camera.Position = glm::vec3(cx + params.tileSize, 1.6f, cz);
    }
}
