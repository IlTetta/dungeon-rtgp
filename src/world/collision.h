#pragma once

// Simple ad-hoc collision handling.
//
// We treat the player as a SPHERE (its center is the camera position, at eye height) and we push
// it out of any WALL box it is penetrating. This is enough to stop the player from walking
// through walls, and it naturally "slides" along a wall when you move diagonally into it.
//
// We block against WALL and PROP objects (walls, columns, statues), but NOT floors: the floor
// slabs are ~1.6 units below the camera, well outside the sphere, so they never block the
// movement (and we don't want them to). Torches are MAT_PROP too, but they float near the
// ceiling, above the sphere, so they naturally never collide either.

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

#include "core/scene.h"

// Given the player's attempted position and radius, return a corrected position that is not
// inside any wall.
inline glm::vec3 resolveWallCollisions(glm::vec3 center, float radius, const Scene& scene) {
    // We repeat the resolution a couple of times: after being pushed out of one wall the sphere
    // might end up slightly inside another (for example in a corner), so a second pass fixes it.
    for (int pass = 0; pass < 2; pass++) {
        for (const RenderObject& obj : scene.objects) {
            if (obj.material == MAT_FLOOR) continue;   // floors don't block; walls and props do
            const AABB& box = obj.worldBounds;

            // closest point on the box to the sphere center (clamp the center inside the box on
            // each axis)
            glm::vec3 closest;
            closest.x = std::max(box.min.x, std::min(center.x, box.max.x));
            closest.y = std::max(box.min.y, std::min(center.y, box.max.y));
            closest.z = std::max(box.min.z, std::min(center.z, box.max.z));

            // vector from that closest point to the sphere center, and its squared length
            glm::vec3 diff = center - closest;
            float dist2 = glm::dot(diff, diff);

            // if the closest point is nearer than the radius, the sphere is overlapping the wall
            if (dist2 < radius * radius && dist2 > 1e-8f) {
                float dist = std::sqrt(dist2);
                // push the sphere out, along the direction away from the wall surface, just enough
                // so that it only touches the wall
                center += (diff / dist) * (radius - dist);
            }
        }
    }
    return center;
}
