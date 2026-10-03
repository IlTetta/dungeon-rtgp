#pragma once

// Instanced fire particles (flames and sparks) rising from the torches and braziers.
//
// It is separate from the Renderer, like DebugDraw: it has its own shader
// (shaders/particle.vert/.frag), its own VAO and buffers, and the CPU particle pool. main calls
// update() every frame and draw() after the opaque scene.
//
// The simulation runs on the CPU: a simple Euler step (the particles go up with a bit of random
// side motion, fade out, and respawn at a random flame when they die). The emitters are the fire
// lights of the scene, read again every frame, so a regenerated dungeon is picked up by itself.

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <vector>
#include <random>
#include <cmath>

#include "engine/shader.h"
#include "core/scene.h"
#include "core/metrics.h"

class ParticleSystem {
public:
    bool enabled = true;
    bool instanced = true;   // true: 1 draw call for all; false: 1 draw call per particle (A/B)

    ParticleSystem()
        : shader("shaders/particle.vert", "shaders/particle.frag"),
          rng(1234)
    {
        setupGL();
    }

    // Read the emitters from the scene and create the particle pool.
    void init(const Scene& scene, int count) {
        refreshEmitters(scene);
        setCount(count);
        // start every particle at a random point of its life, so they do not all appear together
        for (Particle& p : particles) {
            respawn(p);
            p.life = rand01() * p.maxLife;
        }
    }

    // Resize the pool (from init, the HUD slider and the experiments).
    void setCount(int n) {
        if (n < 0)
            n = 0;
        int old = (int)particles.size();
        if (old == n)
            return;
        particles.resize(n);
        gpu.resize(n);
        for (int i = old; i < n; i++) {
            respawn(particles[i]);
            particles[i].life = rand01() * particles[i].maxLife;
        }
    }

    int count() const {
        return (int)particles.size();
    }

    // Advance the simulation by dt. `time` is glfwGetTime(), used for the flame flicker.
    void update(const Scene& scene, float dt, float time) {
        refreshEmitters(scene);

        // Flicker of each flame: two sines with a phase per light.
        for (size_t i = 0; i < emitters.size(); i++) {
            float seed = (float)i * 12.9898f;
            float f = 0.85f + 0.10f * std::sin(time * 6.0f + seed) + 0.05f * std::sin(time * 17.0f + seed * 2.3f);
            emitters[i].flicker = glm::clamp(f, 0.6f, 1.15f);
        }
        if (!enabled || emitters.empty())
            return;

        // Euler step: first the velocity, then the position with the new velocity
        for (Particle& p : particles) {
            p.life -= dt;
            if (p.life <= 0.0f)
                respawn(p);
            p.vel.y += 0.4f * dt;   // a little push upward
            p.vel.x += (rand11() * 0.25f) * dt;   // small random side motion
            p.vel.z += (rand11() * 0.25f) * dt;
            p.pos += p.vel * dt;
        }
    }

    // Draw the particles after the opaque scene (the depth buffer is already filled) and write
    // the particle counters in `metrics`.
    void draw(const glm::mat4& view, const glm::mat4& proj, FrameMetrics& metrics) {
        int n = (enabled && !emitters.empty()) ? (int)particles.size() : 0;
        metrics.particlesDrawn = n;
        metrics.particleDrawCalls = 0;
        if (n == 0)
            return;

        // pack the simulation state into the per-instance GPU layout
        for (int i = 0; i < n; i++) {
            const Particle& p = particles[i];
            float t = p.life / p.maxLife;   // 1 when born, 0 when it dies
            // The flame takes the HUE of its light (changing the light color changes the flame),
            // but keeps a fire look: almost white when young (at the base), the light hue and
            // darker when old (at the top). We divide the color by its brightest channel, so only
            // the hue is kept and the brightness comes from the fire.
            bool hasEm = (p.emitterIdx >= 0 && p.emitterIdx < (int)emitters.size());
            float fl = hasEm ? emitters[p.emitterIdx].flicker : 1.0f;
            glm::vec3 tint = hasEm ? emitters[p.emitterIdx].color : glm::vec3(1.0f, 0.8f, 0.5f);
            float m = tint.r;
            if (tint.g > m)
                m = tint.g;
            if (tint.b > m)
                m = tint.b;
            tint /= (m > 0.001f ? m : 0.001f);
            glm::vec3 hotCore = glm::mix(glm::vec3(1.0f), tint, 0.35f);
            glm::vec3 coolTip = tint * 0.55f;
            glm::vec3 col = glm::mix(coolTip, hotCore, t) * (p.bright * fl);
            float a = t;                              // fades out while it rises
            float sz = p.size * (0.5f + 0.5f * t);    // and gets smaller
            gpu[i].center = p.pos;
            gpu[i].color = col;
            gpu[i].sizeAlpha = glm::vec2(sz, glm::clamp(a, 0.0f, 1.0f));
        }
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
        // new data every frame: glBufferData reallocates the buffer (GL_STREAM_DRAW), so we do
        // not have to wait for the GPU to finish with last frame's data
        glBufferData(GL_ARRAY_BUFFER, n * (GLsizeiptr)sizeof(ParticleGPU), gpu.data(), GL_STREAM_DRAW);

        // additive blending for the glow. Depth TEST on, so walls hide the particles, but depth
        // WRITE off, so the transparent particles do not hide each other.
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);

        shader.use();
        shader.setMat4("view", view);
        shader.setMat4("projection", proj);

        if (instanced) {
            // all n particles in one draw call: 4 vertices, n instances
            shader.setInt("uInstanced", 1);
            glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, n);
            metrics.particleDrawCalls = 1;
        }
        else {
            // naive path: one draw call per particle, with its data as uniforms. OpenGL 4.1 has
            // no glDrawArraysInstancedBaseInstance to start from instance i, so this is how a
            // version without instancing would really do it.
            shader.setInt("uInstanced", 0);
            for (int i = 0; i < n; i++) {
                shader.setVec3("uCenter", gpu[i].center);
                shader.setVec3("uColor", gpu[i].color);
                shader.setVec2("uSizeAlpha", gpu[i].sizeAlpha);
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            }
            metrics.particleDrawCalls = n;
        }

        // back to the default state for the HUD and the next frame
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glBindVertexArray(0);
    }

    void clean() {
        glDeleteVertexArrays(1, &vao);
        glDeleteBuffers(1, &quadVBO);
        glDeleteBuffers(1, &instanceVBO);
        shader.clean();
    }

private:
    // CPU state of one particle
    struct Particle {
        glm::vec3 pos;
        glm::vec3 vel;
        float size;
        float life;       // seconds left
        float maxLife;
        float bright;     // random brightness of this particle
        bool spark;       // true: small fast spark; false: big slow piece of flame
        int emitterIdx;   // which flame it belongs to (index into `emitters`)
    };

    // per-instance data on the GPU: center (vec3) + color (vec3) + size and alpha (vec2) =
    // 8 floats, 32 bytes. Must match the attribute pointers in setupGL() and particle.vert.
    struct ParticleGPU {
        glm::vec3 center;
        glm::vec3 color;
        glm::vec2 sizeAlpha;
    };

    // A flame: where particles spawn and a size scale (torch = 1, brazier = bigger). Everything
    // about a particle scales with it, so both kinds of fire come from the same pool.
    struct Emitter {
        glm::vec3 pos;
        float scale;
        float flicker = 1.0f;                            // brightness multiplier, see update()
        glm::vec3 color = glm::vec3(1.0f, 0.8f, 0.5f);   // color of the light, tints the flame
    };

    void setupGL() {
        // base quad: 4 corners in [-0.5, 0.5], drawn as a triangle strip (per vertex, location 0)
        const float quad[8] = {
            -0.5f, -0.5f,
             0.5f, -0.5f,
            -0.5f,  0.5f,
             0.5f,  0.5f,
        };
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);

        glGenBuffers(1, &quadVBO);
        glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

        // per-instance buffer, filled in draw(). glVertexAttribDivisor(loc, 1) means "move to the
        // next value once per instance, not once per vertex": this is what makes it instancing.
        glGenBuffers(1, &instanceVBO);
        glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
        GLsizei stride = (GLsizei)sizeof(ParticleGPU);
        glEnableVertexAttribArray(1);   // aCenter
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(0));
        glVertexAttribDivisor(1, 1);
        glEnableVertexAttribArray(2);   // aColor
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
        glVertexAttribDivisor(2, 1);
        glEnableVertexAttribArray(3);   // aSizeAlpha
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
        glVertexAttribDivisor(3, 1);

        glBindVertexArray(0);
    }

    // Build the emitter list from the fire lights. The light is not exactly where the flame is
    // drawn, so we move the spawn point onto the torch head or down into the brazier bowl.
    void refreshEmitters(const Scene& scene) {
        emitters.clear();
        for (const Light& L : scene.lights) {
            if (!L.isFire)
                continue;
            Emitter em;
            em.pos = L.position;
            em.color = L.color;
            if (L.isTorch) {
                em.pos.y += 0.10f;
                em.scale = 1.0f;
                // move it a bit away from the wall: the horizontal part of the torch direction
                // is the wall normal pointing into the room
                glm::vec3 horiz(L.direction.x, 0.0f, L.direction.z);
                float len = glm::length(horiz);
                if (len > 0.001f)
                    em.pos += (horiz / len) * 0.12f;
            }
            else {
                em.pos.y -= 0.45f;   // brazier: down into the bowl
                em.scale = 1.8f;     // and a bigger fire
            }
            emitters.push_back(em);
        }
    }

    // Send a particle back to a random flame. Most particles are short and wide pieces of the
    // flame body; about 15% are small fast sparks that fly up.
    void respawn(Particle& p) {
        if (emitters.empty()) {
            p.life = 0.0f;
            p.maxLife = 1.0f;
            return;
        }
        int e = (int)(rand01() * emitters.size());
        if (e >= (int)emitters.size())
            e = (int)emitters.size() - 1;
        const Emitter& em = emitters[e];
        float s = em.scale;
        p.emitterIdx = e;

        p.spark = (rand01() < 0.15f);
        p.bright = 0.8f + rand01() * 0.5f;
        if (p.spark) {
            p.pos = em.pos + glm::vec3(rand11() * 0.10f * s, 0.0f, rand11() * 0.10f * s);
            p.vel = glm::vec3(rand11() * 0.25f, 1.1f + rand01() * 1.0f, rand11() * 0.25f);
            p.maxLife = 0.5f + rand01() * 0.6f;
            p.size = (0.02f + rand01() * 0.02f) * (0.7f + 0.3f * s);
        }
        else {
            // short life but big size: many overlapping additive quads look like one flame
            p.pos = em.pos + glm::vec3(rand11() * 0.08f * s, rand01() * 0.05f * s, rand11() * 0.08f * s);
            p.vel = glm::vec3(rand11() * 0.15f * s, (0.35f + rand01() * 0.5f) * (0.8f + 0.3f * s), rand11() * 0.15f * s);
            p.maxLife = (0.28f + rand01() * 0.35f) * (0.85f + 0.25f * s);
            p.size = (0.09f + rand01() * 0.10f) * s;
        }
        p.life = p.maxLife;
    }

    float rand01() {
        std::uniform_real_distribution<float> d(0.0f, 1.0f);
        return d(rng);
    }

    float rand11() {
        std::uniform_real_distribution<float> d(-1.0f, 1.0f);
        return d(rng);
    }

    Shader shader;
    std::mt19937 rng;

    std::vector<Particle> particles;   // CPU simulation
    std::vector<ParticleGPU> gpu;      // per-instance data, uploaded every frame
    std::vector<Emitter> emitters;     // the flames, from the scene lights

    GLuint vao = 0, quadVBO = 0, instanceVBO = 0;
};
