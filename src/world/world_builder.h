#pragma once

// buildWorld(): the whole "seed to playable dungeon" pipeline in one call:
//   BSP generation -> 3D layout -> Scene -> props and chains -> camera spawn.
//
// Having it in one function lets us rebuild the world at runtime from a new seed (the HUD
// "Generate" button), and since the same code runs at startup and on regenerate, a given seed
// always gives exactly the same dungeon.

#include <glm/glm.hpp>

#include "engine/camera.h"
#include "core/scene.h"
#include "dungeon/dungeon_generator.h"
#include "world/dungeon_geometry.h"
#include "world/props.h"
#include "world/chain_physics.h"

// Map size in tiles. Fixed on purpose: only the seed changes, so different dungeons stay
// comparable.
const int DUNGEON_TILES_X = 60;
const int DUNGEON_TILES_Y = 30;

// Build (or rebuild) the world for a given seed:
//   - fills `scene` from scratch (geometry, materials, props, lights),
//   - clears `chains` and creates the hanging chains of the new scene,
//   - moves `camera` to the spawn point in the first room.
//
// GPU memory: `scene = buildScene(...)` is a move assignment. The old Scene is destroyed, and
// since Mesh frees its VAO/VBO/EBO in its destructor (engine/mesh.h), the old meshes are freed
// automatically. The textures are not: they are loaded again at every rebuild and the old ones
// are never deleted. It is a small known leak, acceptable for a debug button.
inline void buildWorld(unsigned int seed, const DungeonParams& params, const LightingParams& lp,
                       Scene& scene, ChainSystem& chains, Camera& camera) {
    // 1. the 2D grid of WALL / FLOOR tiles for this seed
    DungeonGenerator generator(DUNGEON_TILES_X, DUNGEON_TILES_Y, seed);
    Dungeon dungeon = generator.generate();

    // 2. the 3D layout, then the GPU Scene
    DungeonLayout layout = buildDungeonLayout(dungeon, params);
    scene = buildScene(layout);

    // 3. props and chains. Each chain keeps indices into scene.objects, so the old chains would
    // point to the wrong objects: we clear them and addProps creates the new ones.
    chains.chains.clear();
    addProps(scene, dungeon, params, chains, lp);

    // 4. spawn at eye height (1.6) in the first room, one tile off the center so we do not
    // start inside the central brazier
    if (!dungeon.rooms.empty()) {
        Rect r = dungeon.rooms[0];
        float cx = (r.x + r.w * 0.5f) * params.tileSize;
        float cz = (r.y + r.h * 0.5f) * params.tileSize;
        camera.Position = glm::vec3(cx + params.tileSize, 1.6f, cz);
    }
}
