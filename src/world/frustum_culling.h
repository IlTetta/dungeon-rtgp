#pragma once

// Frustum culling: skip drawing the objects that are completely outside the camera view.
//
// The camera sees a truncated pyramid (the "view frustum") bounded by 6 planes: left, right,
// bottom, top, near, far. An object whose bounding box (AABB) is completely on the "outside" of
// any of these planes cannot be seen, so we do not draw it. This reduces draw calls and triangles.
//
// This is the measurable optimization at the heart of the project.
// It uses only the AABB already stored in each RenderObject, so it does
// not depend on how the objects are actually drawn.

#include <glm/glm.hpp>
#include "core/scene.h"

// A frustum is just its 6 planes. Each plane is stored as (a, b, c, d) meaning a*x+b*y+c*z+d = 0,
// with the normal (a,b,c) pointing INWARD (so a point is inside the plane when a*x+b*y+c*z+d >= 0).
struct Frustum {
    glm::vec4 planes[6];
};

// Extract the 6 world-space frustum planes from a view-projection matrix (proj * view).
// This is the standard Gribb-Hartmann method: the planes are sums/differences of the rows of the
// matrix. (glm stores matrices column-major, so row i is (m[0][i], m[1][i], m[2][i], m[3][i]).)
inline Frustum extractFrustum(const glm::mat4& m) {
    // helper to read a row of the matrix
    auto row = [&](int i) { return glm::vec4(m[0][i], m[1][i], m[2][i], m[3][i]); };

    Frustum f;
    f.planes[0] = row(3) + row(0);   // left
    f.planes[1] = row(3) - row(0);   // right
    f.planes[2] = row(3) + row(1);   // bottom
    f.planes[3] = row(3) - row(1);   // top
    f.planes[4] = row(3) + row(2);   // near
    f.planes[5] = row(3) - row(2);   // far

    // normalize each plane (divide by the length of its normal) so distances are real distances
    for (int i = 0; i < 6; i++) {
        float length = glm::length(glm::vec3(f.planes[i]));
        f.planes[i] /= length;
    }
    return f;
}

// Is this AABB at least partially inside the frustum? (conservative: it may keep a few boxes that
// are just outside near the corners, but it never wrongly removes a visible one).
inline bool isAABBVisible(const Frustum& frustum, const AABB& box) {
    for (int i = 0; i < 6; i++) {
        glm::vec3 n(frustum.planes[i]);   // plane normal
        float d = frustum.planes[i].w;    // plane offset

        // "positive vertex": the corner of the box that is farthest along the plane normal.
        // For each axis we pick max if the normal points that way, min otherwise.
        glm::vec3 p;
        p.x = (n.x >= 0.0f) ? box.max.x : box.min.x;
        p.y = (n.y >= 0.0f) ? box.max.y : box.min.y;
        p.z = (n.z >= 0.0f) ? box.max.z : box.min.z;

        // if even this farthest corner is on the outside of the plane, the whole box is outside
        if (glm::dot(n, p) + d < 0.0f)
            return false;
    }
    return true;
}
