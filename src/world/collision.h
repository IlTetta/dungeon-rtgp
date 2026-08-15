#pragma once

// Simple ad-hoc collision handling.
//
// We treat the player as a vertical CYLINDER: seen from above it is a circle (center = camera
// x/z, given radius). We resolve collisions in the HORIZONTAL (X/Z) plane only, ignoring the
// height: this way ANY object blocks the player regardless of how tall it is (a short barrel
// stops you exactly like a tall column). The player Y (eye height) is never changed.
//
// (An earlier version tested a sphere centered at eye height (y = 1.6): that only collided with
// objects tall enough to reach 1.6, so low props like barrels/braziers were walked through.)
//
// We block against WALL and PROP objects, but NOT floors or ceilings (those cover the whole tile
// in X/Z, so testing them would trap the player everywhere).

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

#include "core/scene.h"

// Given the player's attempted position and radius, return a corrected position that is not
// inside any wall or prop.
inline glm::vec3 resolveWallCollisions(glm::vec3 center, float radius, const Scene& scene) {
    // We repeat the resolution a couple of times: after being pushed out of one box the player
    // might end up slightly inside another (for example in a corner), so a second pass fixes it.
    for (int pass = 0; pass < 2; pass++) {
        for (const RenderObject& obj : scene.objects) {
            if (obj.material == MAT_FLOOR || obj.material == MAT_CEILING || obj.material == MAT_CHAIN)
                continue;   // floors, ceilings and (swingable) chains never block the player
            const AABB& box = obj.worldBounds;

            // closest point on the box's X/Z footprint to the player circle center
            float cx = std::max(box.min.x, std::min(center.x, box.max.x));
            float cz = std::max(box.min.z, std::min(center.z, box.max.z));
            float dx = center.x - cx;
            float dz = center.z - cz;
            float dist2 = dx * dx + dz * dz;

            // if that point is nearer than the radius, the player overlaps the box -> push out
            // horizontally (in X/Z), leaving the height untouched
            if (dist2 < radius * radius && dist2 > 1e-8f) {
                float dist = std::sqrt(dist2);
                float push = (radius - dist) / dist;
                center.x += dx * push;
                center.z += dz * push;
            }
        }
    }
    return center;
}
