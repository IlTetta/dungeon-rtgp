#pragma once

// Verlet-integrated hanging chains (the only DYNAMIC objects in the scene).
//
// Each chain is a little rope of point masses ("nodes"): node 0 is pinned to the ceiling, the
// others hang below. Every frame we:
//   1. integrate each free node with Verlet (position + inertia + gravity),
//   2. push the low nodes out of the player cylinder (so walking through makes the chain swing),
//   3. satisfy the distance constraints between consecutive nodes (a few iterations), keeping the
//      link length constant and the top pinned,
//   4. rebuild the model matrix (and AABB) of each chain-link RenderObject from the node positions.
//
// Verlet integration is the classic, very stable way to simulate ropes/cloth: instead of storing a
// velocity, each node remembers its PREVIOUS position, and (pos - prevPos) IS the velocity. This is
// the "position based dynamics" idea seen in the physics lectures, done by hand (no Bullet).

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vector>
#include <cmath>

#include "core/scene.h"

struct ChainNode {
    glm::vec3 pos;
    glm::vec3 prev;   // position on the previous frame (Verlet uses this as the velocity)
};

struct Chain {
    std::vector<ChainNode> nodes;   // node[0] = anchor (pinned to the ceiling)
    std::vector<int> segObj;        // scene.objects index of each link (segments = nodes - 1)
    float restLen;                  // rest distance between two consecutive nodes
    float linkMinY;                 // chain mesh local min y (to build the link matrix)
    float linkLen;                  // chain mesh local height
    glm::vec3 anchor;               // fixed top position
};

// Model matrix that stretches/orients the chain-link mesh so it goes from `from` to `to`.
inline glm::mat4 chainLinkModel(glm::vec3 from, glm::vec3 to, float linkMinY, float linkLen) {
    glm::vec3 dir = to - from;
    float len = glm::length(dir);
    if (len < 1e-5f) len = 1e-5f;
    glm::vec3 d = dir / len;

    // rotation that takes the mesh's +Y axis onto the link direction d
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    glm::vec3 axis = glm::cross(up, d);
    float dotv = glm::clamp(glm::dot(up, d), -1.0f, 1.0f);
    glm::mat4 R(1.0f);
    if (glm::length(axis) > 1e-5f)
        R = glm::rotate(glm::mat4(1.0f), std::acos(dotv), glm::normalize(axis));
    else if (dotv < 0.0f)
        R = glm::rotate(glm::mat4(1.0f), 3.14159265f, glm::vec3(1.0f, 0.0f, 0.0f));   // straight down

    // shift base to 0, scale to the link length, rotate to the direction, move to `from`
    glm::mat4 m(1.0f);
    m = glm::translate(m, from);
    m = m * R;
    m = glm::scale(m, glm::vec3(1.0f, len / linkLen, 1.0f));
    m = glm::translate(m, glm::vec3(0.0f, -linkMinY, 0.0f));
    return m;
}

class ChainSystem {
public:
    std::vector<Chain> chains;

    // Create a chain hanging from (x, ceilingY, z) with `segments` links, and create its link
    // RenderObjects in the scene (using the chain mesh / material). Records it for the physics.
    void addChain(Scene& scene, int meshIndex, int materialIndex, const AABB& chainLocal,
                  float x, float z, float ceilingY, int segments) {
        Chain ch;
        ch.linkMinY = chainLocal.min.y;
        ch.linkLen = chainLocal.max.y - chainLocal.min.y;
        ch.restLen = ch.linkLen;
        ch.anchor = glm::vec3(x, ceilingY, z);

        // nodes hang straight down initially (at rest: prev == pos)
        ch.nodes.resize(segments + 1);
        for (int k = 0; k <= segments; k++) {
            glm::vec3 p(x, ceilingY - k * ch.restLen, z);
            ch.nodes[k].pos = p;
            ch.nodes[k].prev = p;
        }

        // one RenderObject per link
        for (int i = 0; i < segments; i++) {
            RenderObject obj;
            obj.meshIndex = meshIndex;
            obj.materialIndex = materialIndex;
            obj.material = MAT_CHAIN;   // non-blocking: the collision skips it, so we can swing it
            obj.modelMatrix = chainLinkModel(ch.nodes[i].pos, ch.nodes[i + 1].pos, ch.linkMinY, ch.linkLen);
            obj.worldBounds = linkBounds(ch.nodes[i].pos, ch.nodes[i + 1].pos);
            ch.segObj.push_back((int)scene.objects.size());
            scene.objects.push_back(obj);
        }
        chains.push_back(ch);
    }

    // Advance the simulation one frame and write the results back into the scene objects.
    void update(float dt, glm::vec3 playerPos, float playerRadius, Scene& scene) {
        if (dt > 0.02f) dt = 0.02f;             // clamp so a big frame time can't blow up the sim
        glm::vec3 gravity(0.0f, -9.8f, 0.0f);
        // close to 1 = little energy loss, so the chain keeps swinging back and forth for a while
        // (like a real chain) instead of snapping back after a single swing
        float damping = 0.995f;

        for (Chain& ch : chains) {
            int N = (int)ch.nodes.size();

            // 1. Verlet integration of the free nodes (node 0 is the pinned anchor)
            for (int k = 1; k < N; k++) {
                glm::vec3 temp = ch.nodes[k].pos;
                ch.nodes[k].pos += (ch.nodes[k].pos - ch.nodes[k].prev) * damping + gravity * (dt * dt);
                ch.nodes[k].prev = temp;

                // clamp the (implicit) velocity so a fast hit (sprinting into the chain) cannot
                // fling a node violently past the anchor / through the ceiling
                glm::vec3 vel = ch.nodes[k].pos - ch.nodes[k].prev;
                float speed = glm::length(vel);
                const float maxSpeed = 0.35f;
                if (speed > maxSpeed)
                    ch.nodes[k].prev = ch.nodes[k].pos - vel * (maxSpeed / speed);
            }

            // 2. player collision: push the low nodes out of the player cylinder (in X/Z), which
            // is what makes the chain swing when you walk through it
            for (int k = 1; k < N; k++) {
                if (ch.nodes[k].pos.y > 2.0f) continue;   // only nodes within the player's height
                float dx = ch.nodes[k].pos.x - playerPos.x;
                float dz = ch.nodes[k].pos.z - playerPos.z;
                float dist2 = dx * dx + dz * dz;
                float r = playerRadius + 0.1f;
                if (dist2 < r * r && dist2 > 1e-6f) {
                    float dist = std::sqrt(dist2);
                    float push = (r - dist) / dist;
                    ch.nodes[k].pos.x += dx * push;
                    ch.nodes[k].pos.z += dz * push;
                }
            }

            // 3. satisfy the distance constraints (keep the links rigid, top pinned)
            for (int iter = 0; iter < 8; iter++) {
                ch.nodes[0].pos = ch.anchor;
                for (int k = 0; k < N - 1; k++) {
                    glm::vec3 delta = ch.nodes[k + 1].pos - ch.nodes[k].pos;
                    float d = glm::length(delta);
                    if (d < 1e-5f) continue;
                    float diff = (d - ch.restLen) / d;
                    if (k == 0)
                        ch.nodes[k + 1].pos -= delta * diff;               // only the free node moves
                    else {
                        ch.nodes[k].pos     += delta * (0.5f * diff);
                        ch.nodes[k + 1].pos -= delta * (0.5f * diff);
                    }
                }
            }
            ch.nodes[0].pos = ch.anchor;

            // 3b. keep every node between the floor and the ceiling (kill the vertical velocity
            // when it hits a limit, so it does not keep pushing against it)
            for (int k = 1; k < N; k++) {
                if (ch.nodes[k].pos.y > ch.anchor.y) { ch.nodes[k].pos.y = ch.anchor.y; ch.nodes[k].prev.y = ch.nodes[k].pos.y; }
                if (ch.nodes[k].pos.y < 0.0f)        { ch.nodes[k].pos.y = 0.0f;        ch.nodes[k].prev.y = ch.nodes[k].pos.y; }
            }

            // 4. rebuild each link's transform + AABB from the node positions
            for (int i = 0; i < (int)ch.segObj.size(); i++) {
                RenderObject& o = scene.objects[ch.segObj[i]];
                o.modelMatrix = chainLinkModel(ch.nodes[i].pos, ch.nodes[i + 1].pos, ch.linkMinY, ch.linkLen);
                o.worldBounds = linkBounds(ch.nodes[i].pos, ch.nodes[i + 1].pos);
            }
        }
    }

private:
    static AABB linkBounds(glm::vec3 a, glm::vec3 b) {
        AABB box;
        box.min = glm::min(a, b) - glm::vec3(0.2f);
        box.max = glm::max(a, b) + glm::vec3(0.2f);
        return box;
    }
};
