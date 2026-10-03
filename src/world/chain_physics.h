#pragma once

// Hanging chains simulated with Verlet integration: the only DYNAMIC objects of the scene.
//
// A chain is a small rope of point masses (nodes): node 0 is fixed to the ceiling, the others
// hang below. Every frame we:
//   1. move each free node with Verlet (inertia + gravity),
//   2. push the low nodes out of the player cylinder, so walking through a chain makes it swing,
//   3. fix the distance between consecutive nodes (a few iterations), so the links keep their
//      length and the top stays fixed,
//   4. rebuild the model matrix and the AABB of every link from the node positions.

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vector>
#include <cmath>

#include "core/scene.h"

struct ChainNode {
    glm::vec3 pos;
    glm::vec3 prev;   // position at the previous step (pos - prev is the velocity)
};

struct Chain {
    std::vector<ChainNode> nodes;   // nodes[0] = anchor on the ceiling
    std::vector<int> segObj;    // index in scene.objects of every link (nodes - 1 links)
    float restLen;  // distance between two consecutive nodes
    float linkMinY; // min y of the link mesh (to build its matrix)
    float linkLen;  // height of the link mesh
    glm::vec3 anchor;   // fixed top point
};

// Model matrix that places the link mesh between `from` and `to`: stretched to that length and
// rotated so that its +Y axis points along the link.
inline glm::mat4 chainLinkModel(glm::vec3 from, glm::vec3 to, float linkMinY, float linkLen) {
    glm::vec3 dir = to - from;
    float len = glm::length(dir);
    if (len < 1e-5f)
        len = 1e-5f;
    glm::vec3 d = dir / len;

    // rotation from +Y to d: around the axis up x d, by the angle acos(up . d)
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    glm::vec3 axis = glm::cross(up, d);
    float dotv = glm::clamp(glm::dot(up, d), -1.0f, 1.0f);
    glm::mat4 R(1.0f);
    if (glm::length(axis) > 1e-5f)
        R = glm::rotate(glm::mat4(1.0f), std::acos(dotv), glm::normalize(axis));
    else if (dotv < 0.0f)
        R = glm::rotate(glm::mat4(1.0f), 3.14159265f, glm::vec3(1.0f, 0.0f, 0.0f));   // d = -Y: the cross product is 0, turn by 180 degrees

    // read right to left: move the mesh base to y = 0, stretch it to the link length, rotate it
    // to the link direction, move it to `from`
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

    // Create a chain hanging from (x, ceilingY, z) with `segments` links: the nodes for the
    // physics and one RenderObject per link in the scene.
    void addChain(Scene& scene, int meshIndex, int materialIndex, const AABB& chainLocal,
                  float x, float z, float ceilingY, int segments) {
        Chain ch;
        ch.linkMinY = chainLocal.min.y;
        ch.linkLen = chainLocal.max.y - chainLocal.min.y;
        ch.restLen = ch.linkLen;
        ch.anchor = glm::vec3(x, ceilingY, z);

        // the nodes start straight down and at rest (prev == pos)
        ch.nodes.resize(segments + 1);
        for (int k = 0; k <= segments; k++) {
            glm::vec3 p(x, ceilingY - k * ch.restLen, z);
            ch.nodes[k].pos = p;
            ch.nodes[k].prev = p;
        }

        for (int i = 0; i < segments; i++) {
            RenderObject obj;
            obj.meshIndex = meshIndex;
            obj.materialIndex = materialIndex;
            obj.material = MAT_CHAIN;   // the collisions skip it, so the player can walk into it
            obj.modelMatrix = chainLinkModel(ch.nodes[i].pos, ch.nodes[i + 1].pos, ch.linkMinY, ch.linkLen);
            obj.worldBounds = linkBounds(ch.nodes[i].pos, ch.nodes[i + 1].pos);
            ch.segObj.push_back((int)scene.objects.size());
            scene.objects.push_back(obj);
        }
        chains.push_back(ch);
    }

    // Advance the simulation by one frame and write the result into the scene objects.
    void update(float dt, glm::vec3 playerPos, float playerRadius, Scene& scene) {
        if (dt > 0.02f)
            dt = 0.02f;   // a very long frame would make the step too big and the chain explode
        glm::vec3 gravity(0.0f, -9.8f, 0.0f);
        // close to 1: little energy lost per step, so the chain keeps swinging for a while
        float damping = 0.995f;

        for (Chain& ch : chains) {
            int N = (int)ch.nodes.size();

            // 1. Verlet: new = pos + (pos - prev) * damping + gravity * dt^2 (node 0 is fixed)
            for (int k = 1; k < N; k++) {
                glm::vec3 temp = ch.nodes[k].pos;
                ch.nodes[k].pos += (ch.nodes[k].pos - ch.nodes[k].prev) * damping + gravity * (dt * dt);
                ch.nodes[k].prev = temp;

                // limit the speed (the movement in one step), so running into a chain cannot
                // throw a node through the ceiling
                glm::vec3 vel = ch.nodes[k].pos - ch.nodes[k].prev;
                float speed = glm::length(vel);
                const float maxSpeed = 0.35f;
                if (speed > maxSpeed)
                    ch.nodes[k].prev = ch.nodes[k].pos - vel * (maxSpeed / speed);
            }

            // 2. the player: push the nodes below 2 m out of the player circle (X/Z), like the
            // wall collisions. Moving only pos changes (pos - prev), so the push becomes a velocity
            // and the chain swings.
            for (int k = 1; k < N; k++) {
                if (ch.nodes[k].pos.y > 2.0f)
                    continue;
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

            // 3. distance constraints: move each pair of nodes so they are restLen apart again.
            // The fixed node does not move, so on the first link the free node takes the whole
            // correction; on the others the two nodes take half each. Repeating it 8 times makes
            // the whole chain converge.
            for (int iter = 0; iter < 8; iter++) {
                ch.nodes[0].pos = ch.anchor;
                for (int k = 0; k < N - 1; k++) {
                    glm::vec3 delta = ch.nodes[k + 1].pos - ch.nodes[k].pos;
                    float d = glm::length(delta);
                    if (d < 1e-5f)
                        continue;
                    float diff = (d - ch.restLen) / d;
                    if (k == 0)
                        ch.nodes[k + 1].pos -= delta * diff;
                    else {
                        ch.nodes[k].pos += delta * (0.5f * diff);
                        ch.nodes[k + 1].pos -= delta * (0.5f * diff);
                    }
                }
            }
            ch.nodes[0].pos = ch.anchor;

            // keep every node between the floor and the ceiling; at the limit we also stop the
            // vertical velocity (prev.y = pos.y), otherwise it would keep pushing against it
            for (int k = 1; k < N; k++) {
                if (ch.nodes[k].pos.y > ch.anchor.y) {
                    ch.nodes[k].pos.y = ch.anchor.y;
                    ch.nodes[k].prev.y = ch.nodes[k].pos.y;
                }
                if (ch.nodes[k].pos.y < 0.0f) {
                    ch.nodes[k].pos.y = 0.0f;
                    ch.nodes[k].prev.y = ch.nodes[k].pos.y;
                }
            }

            // 4. update the matrix and the AABB of every link (so culling still works on them)
            for (int i = 0; i < (int)ch.segObj.size(); i++) {
                RenderObject& o = scene.objects[ch.segObj[i]];
                o.modelMatrix = chainLinkModel(ch.nodes[i].pos, ch.nodes[i + 1].pos, ch.linkMinY, ch.linkLen);
                o.worldBounds = linkBounds(ch.nodes[i].pos, ch.nodes[i + 1].pos);
            }
        }
    }

private:
    // box around the two nodes of a link, with some margin for the thickness of the mesh
    static AABB linkBounds(glm::vec3 a, glm::vec3 b) {
        AABB box;
        box.min = glm::min(a, b) - glm::vec3(0.2f);
        box.max = glm::max(a, b) + glm::vec3(0.2f);
        return box;
    }
};
