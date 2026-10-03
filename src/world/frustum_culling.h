#pragma once

// Frustum culling: we skip the objects that are completely outside the camera view.
//
// What the camera sees is a truncated pyramid (the view frustum) bounded by 6 planes: left,
// right, bottom, top, near and far. If an object's AABB is completely outside even one of these
// planes it cannot be seen, so we do not draw it: fewer draw calls and fewer triangles.
//
// It only needs the AABB stored in every RenderObject, so it does not care how the objects are
// drawn. The renderer uses it for the color pass, the SSAO G-buffer and the SPOT shadow passes
// (with the torch's own frustum).

#include <glm/glm.hpp>
#include "core/scene.h"

// The 6 planes of a frustum. A plane is (a, b, c, d), meaning a*x + b*y + c*z + d = 0, with the
// normal (a, b, c) pointing INSIDE: a point is on the inner side when a*x + b*y + c*z + d >= 0.
struct Frustum {
    glm::vec4 planes[6];
};

// Get the 6 world-space planes from a view-projection matrix (proj * view), with the
// Gribb-Hartmann method.
inline Frustum extractFrustum(const glm::mat4& m) {
    auto row = [&](int i) {
        return glm::vec4(m[0][i], m[1][i], m[2][i], m[3][i]);
    };

    Frustum f;
    f.planes[0] = row(3) + row(0);   // left
    f.planes[1] = row(3) - row(0);   // right
    f.planes[2] = row(3) + row(1);   // bottom
    f.planes[3] = row(3) - row(1);   // top
    f.planes[4] = row(3) + row(2);   // near
    f.planes[5] = row(3) - row(2);   // far

    // Normalize each plane (divide by the length of its normal), so a*x + b*y + c*z + d is a real
    // signed distance. The inside/outside test below would work without it, since only the sign
    // matters there.
    for (int i = 0; i < 6; i++) {
        float length = glm::length(glm::vec3(f.planes[i]));
        f.planes[i] /= length;
    }
    return f;
}

// The test is conservative: a box just outside a corner of the frustum (outside two planes
// together, but not fully outside any single one) is kept. So we may draw a few extra objects,
// but we never remove a visible one.
inline bool isAABBVisible(const Frustum& frustum, const AABB& box) {
    for (int i = 0; i < 6; i++) {
        glm::vec3 n(frustum.planes[i]);   // plane normal
        float d = frustum.planes[i].w;    // plane offset

        // "positive vertex": the box corner that goes farthest along the plane normal. On each
        // axis we take max if the normal points that way, min otherwise.
        glm::vec3 p;
        p.x = (n.x >= 0.0f) ? box.max.x : box.min.x;
        p.y = (n.y >= 0.0f) ? box.max.y : box.min.y;
        p.z = (n.z >= 0.0f) ? box.max.z : box.min.z;

        // if even this corner is outside the plane, the whole box is outside: one test per plane
        // instead of testing all 8 corners
        if (glm::dot(n, p) + d < 0.0f)
            return false;
    }
    return true;
}
