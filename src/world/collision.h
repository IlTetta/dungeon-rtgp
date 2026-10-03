#pragma once

// Simple ad-hoc collisions between the player and the scene.
//
// The player is a vertical CYLINDER: seen from above it is a circle centered on the camera x/z.
// We solve the collision only in the horizontal X/Z plane and ignore the height, so every
// blocking object stops the player no matter how tall it is (a low barrel blocks like a column).
// The camera height (y) is never changed.
//
// Only walls and props block. Floors and ceilings cover the whole tile in X/Z, so testing them
// would trap the player everywhere; chains and wall decor (torches) are walked through.

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

#include "core/scene.h"

// Given the position the player is trying to move to, return a corrected position that is not
// inside any wall or prop.
inline glm::vec3 resolveWallCollisions(glm::vec3 center, float radius, const Scene& scene) {
    // Two passes: in a corner, pushing the player out of one box can move it inside the other
    // one, and the second pass fixes that.
    for (int pass = 0; pass < 2; pass++) {
        for (const RenderObject& obj : scene.objects) {
            if (obj.material == MAT_FLOOR || obj.material == MAT_CEILING ||
                obj.material == MAT_CHAIN || obj.material == MAT_DECOR)
                continue;
            const AABB& box = obj.worldBounds;

            // closest point of the box footprint (X/Z) to the circle center: clamp the
            // center inside the box on each axis
            float cx = std::max(box.min.x, std::min(center.x, box.max.x));
            float cz = std::max(box.min.z, std::min(center.z, box.max.z));
            float dx = center.x - cx;
            float dz = center.z - cz;
            float dist2 = dx * dx + dz * dz;

            // closer than the radius: the circle overlaps the box, so we push it out along the
            // direction from that closest point. Against a wall this push is perpendicular to
            // the wall, so the sideways part of the movement stays and the player slides along it.
            // (dist2 > 0 skips the case where the center is exactly inside the box.)
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
