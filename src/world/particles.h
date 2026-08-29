#pragma once

// Instanced spark / ember particles rising from the flames of the dungeon.
//
// Kept completely SEPARATE from Lorenzo's Renderer, exactly like DebugDraw (src/world/debug_draw.h):
// it owns its own little shader (shaders/particle.vert/.frag), its own VAO + buffers, and the CPU
// particle pool. main just calls update()/draw() after the opaque scene is drawn. Nothing here
// touches the Renderer or the shared shaders.
//
// Technique = GPU INSTANCING (the measurable point of this milestone): the particles are a single
// small quad drawn with per-instance data (center/color/size/alpha) via glVertexAttribDivisor, so
// N particles cost ONE glDrawArraysInstanced call. For the benchmark there is also a NAIVE path
// (`instanced = false`): one draw call per particle, which is what the same effect costs WITHOUT
// instancing -> flipping the two and watching the frame time shows exactly what instancing saves.
//
// The simulation is a simple CPU Euler step (embers float up with a bit of turbulence, fade in then
// out, respawn at a random flame when they die); the emitters are the scene's lights (every light
// in this dungeon is a fire), refreshed each frame so a dungeon regenerate is picked up on its own.

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <vector>
#include <random>
#include <cmath>     // std::sin, for the flame flicker (matches the light flicker in the renderer)

#include "engine/shader.h"
#include "core/scene.h"
#include "core/metrics.h"

class ParticleSystem {
public:
    bool enabled   = true;   // draw the particles at all
    bool instanced = true;   // instanced (1 draw call) vs naive (1 draw call per particle) - for A/B

    ParticleSystem()
        : shader("shaders/particle.vert", "shaders/particle.frag"),
          rng(1234)
    {
        setupGL();
    }

    // One-time setup: fill the emitter list from the scene lights and size the particle pool.
    void init(const Scene& scene, int count) {
        refreshEmitters(scene);
        setCount(count);
        // start each particle at a random point in its life, so they don't all pop in together
        for (Particle& p : particles) { respawn(p); p.life = rand01() * p.maxLife; }
    }

    // Resize the particle pool (called from init and from the HUD count slider / experiments).
    void setCount(int n) {
        if (n < 0) n = 0;
        int old = (int)particles.size();
        if (old == n) return;
        particles.resize(n);
        gpu.resize(n);
        for (int i = old; i < n; i++) { respawn(particles[i]); particles[i].life = rand01() * particles[i].maxLife; }
    }
    int count() const { return (int)particles.size(); }

    // Advance the simulation. Also refreshes the emitters from the current scene lights (cheap),
    // so regenerating the dungeon is picked up automatically with no explicit re-init.
    void update(const Scene& scene, float dt, float time) {
        refreshEmitters(scene);
        // Per-emitter flicker, using the SAME formula and time base as the torch-light flicker in
        // the renderer (renderer.cpp ~line 607), so the fire brightens/dims TOGETHER with its light.
        // Keyed by the fire's index here; the renderer keys by its own send-order, so the phase is
        // close but not locked (a merge-time refactor sharing one signal would lock it - see notes).
        for (size_t i = 0; i < emitters.size(); i++) {
            float seed = (float)i * 12.9898f;
            float f = 0.85f + 0.10f * std::sin(time * 6.0f + seed) + 0.05f * std::sin(time * 17.0f + seed * 2.3f);
            emitters[i].flicker = glm::clamp(f, 0.6f, 1.15f);
        }
        if (!enabled || emitters.empty()) return;
        for (Particle& p : particles) {
            p.life -= dt;
            if (p.life <= 0.0f) respawn(p);
            p.vel.y += 0.4f * dt;                                // mild buoyancy (keep the flame low)
            p.vel.x += (rand11() * 0.25f) * dt;                  // gentle flicker
            p.vel.z += (rand11() * 0.25f) * dt;
            p.pos   += p.vel * dt;
        }
    }

    // Draw after the opaque scene (depth buffer already filled). Writes the particle counters into
    // `metrics`. `view`/`proj` are whatever camera we draw from.
    void draw(const glm::mat4& view, const glm::mat4& proj, FrameMetrics& metrics) {
        int n = (enabled && !emitters.empty()) ? (int)particles.size() : 0;
        metrics.particlesDrawn    = n;
        metrics.particleDrawCalls = 0;
        if (n == 0) return;

        // pack the current sim state into the GPU instance layout (position/color/size+alpha)
        for (int i = 0; i < n; i++) {
            const Particle& p = particles[i];
            float t = p.life / p.maxLife;                              // 1 at birth -> 0 at death
            // fire gradient: hot yellow-white when young (at the base), deep orange/red when old
            glm::vec3 hot  = glm::vec3(1.0f, 0.9f, 0.55f);
            glm::vec3 cold = glm::vec3(0.85f, 0.20f, 0.05f);
            // multiply by the flame's flicker so the whole fire pulses in step with its light
            float fl = (p.emitterIdx >= 0 && p.emitterIdx < (int)emitters.size())
                     ? emitters[p.emitterIdx].flicker : 1.0f;
            glm::vec3 col  = glm::mix(cold, hot, t) * (p.bright * fl);
            float a  = t;                                             // brightest young, fading as it rises
            float sz = p.size * (0.5f + 0.5f * t);                    // taper: narrower as it ages
            gpu[i].center    = p.pos;
            gpu[i].color     = col;
            gpu[i].sizeAlpha = glm::vec2(sz, glm::clamp(a, 0.0f, 1.0f));
        }
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
        // orphan + re-upload this frame's instances (streaming data, changes every frame)
        glBufferData(GL_ARRAY_BUFFER, n * (GLsizeiptr)sizeof(ParticleGPU), gpu.data(), GL_STREAM_DRAW);

        // additive glow; test against the scene depth but do NOT write depth (particles are
        // transparent and must not occlude each other or the scene)
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);

        shader.use();
        shader.setMat4("view", view);
        shader.setMat4("projection", proj);

        if (instanced) {
            // THE instanced path: all N particles in one draw call
            shader.setInt("uInstanced", 1);
            glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, n);
            metrics.particleDrawCalls = 1;
        }
        else {
            // NAIVE path for the benchmark A/B: one draw call per particle, feeding the per-particle
            // data as uniforms. (OpenGL 4.1 has no glDrawArraysInstancedBaseInstance to index the
            // instance buffer per call, so the honest "no instancing" baseline is plain glDrawArrays
            // with uniforms - which is exactly what a first version without instancing would do.)
            shader.setInt("uInstanced", 0);
            for (int i = 0; i < n; i++) {
                shader.setVec3("uCenter", gpu[i].center);
                shader.setVec3("uColor", gpu[i].color);
                shader.setVec2("uSizeAlpha", gpu[i].sizeAlpha);
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            }
            metrics.particleDrawCalls = n;
        }

        // restore the default state for the rest of the frame (HUD, next frame's scene)
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
    // CPU simulation state of one particle.
    struct Particle {
        glm::vec3 pos;
        glm::vec3 vel;
        float size;
        float life;      // seconds left to live
        float maxLife;
        float bright;    // per-particle brightness jitter (flicker)
        bool  spark;     // true = small fast ember flying up; false = big slow flame-body particle
        int   emitterIdx;// which flame this particle belongs to (index into `emitters`)
    };

    // GPU per-instance layout (must match the attribute pointers in setupGL and the locations in
    // particle.vert): center (vec3) + color (vec3) + sizeAlpha (vec2) = 8 floats, 32 bytes.
    struct ParticleGPU {
        glm::vec3 center;
        glm::vec3 color;
        glm::vec2 sizeAlpha;
    };

    // A flame source: where to spawn from, plus a size scale (torches = small, braziers = a bigger
    // fire). Everything about a particle scales with this, so both kinds come out of one pool.
    struct Emitter {
        glm::vec3 pos;
        float scale;
        float flicker = 1.0f;   // current brightness multiplier (synced with the light, see update)
    };

    void setupGL() {
        // base quad: 4 corners in [-0.5, 0.5], drawn as a triangle strip (per-vertex, location 0)
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

        // per-instance buffer (filled every frame in draw()). Attribute divisor = 1 means "advance
        // once per instance", not per vertex -> that is what makes the draw instanced.
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

    // copy the scene light positions into the emitter list (every light is a flame here). The
    // lights sit a bit ABOVE the visible fire (they are placed for lighting, at y = 1.2), so we
    // drop the spawn point down into the actual cup/bowl.
    void refreshEmitters(const Scene& scene) {
        emitters.clear();
        for (const Light& L : scene.lights) {
            if (!L.isFire) continue;   // only real fires (torches/braziers) spawn particles
            Emitter em;
            em.pos = L.position;
            if (L.isTorch) {
                em.pos.y += 0.10f;   // raise the flame to the torch head
                em.scale = 1.0f;
                // push it out from the wall toward the head (into the room), along the torch's aim:
                // Light::direction's horizontal part is the inward wall normal.
                glm::vec3 horiz(L.direction.x, 0.0f, L.direction.z);
                float len = glm::length(horiz);
                if (len > 0.001f) em.pos += (horiz / len) * 0.12f;
            }
            else {
                em.pos.y -= 0.45f;   // brazier: lower into the bowl
                em.scale = 1.8f;     // and a bigger fire
            }
            emitters.push_back(em);
        }
    }

    // send a particle back to a random flame. Most particles are the short, wide "flame body" that
    // hugs the fire; a small fraction are fast little "sparks" that fly up (the embers).
    void respawn(Particle& p) {
        if (emitters.empty()) { p.life = 0.0f; p.maxLife = 1.0f; return; }
        int e = (int)(rand01() * emitters.size());
        if (e >= (int)emitters.size()) e = (int)emitters.size() - 1;
        const Emitter& em = emitters[e];
        float s = em.scale;   // 1 = torch, > 1 = bigger brazier fire
        p.emitterIdx = e;     // remember the flame, to read its flicker in draw()

        p.spark  = (rand01() < 0.15f);   // ~15% sparks, the rest is the flame body
        p.bright = 0.8f + rand01() * 0.5f;
        if (p.spark) {
            p.pos     = em.pos + glm::vec3(rand11() * 0.10f * s, 0.0f, rand11() * 0.10f * s);
            p.vel     = glm::vec3(rand11() * 0.25f, 1.1f + rand01() * 1.0f, rand11() * 0.25f);  // shoots up
            p.maxLife = 0.5f + rand01() * 0.6f;
            p.size    = (0.02f + rand01() * 0.02f) * (0.7f + 0.3f * s);
        }
        else {
            // Tight and short-lived, but big: many overlapping additive quads read as one flame.
            // Everything scales with the emitter, so a brazier gets a wider, taller, fatter fire
            // than a torch out of the same shared particle pool.
            p.pos     = em.pos + glm::vec3(rand11() * 0.08f * s, rand01() * 0.05f * s, rand11() * 0.08f * s);
            p.vel     = glm::vec3(rand11() * 0.15f * s, (0.35f + rand01() * 0.5f) * (0.8f + 0.3f * s), rand11() * 0.15f * s);
            p.maxLife = (0.28f + rand01() * 0.35f) * (0.85f + 0.25f * s);
            p.size    = (0.09f + rand01() * 0.10f) * s;
        }
        p.life = p.maxLife;
    }

    // small random helpers
    float rand01() { std::uniform_real_distribution<float> d(0.0f, 1.0f); return d(rng); }
    float rand11() { std::uniform_real_distribution<float> d(-1.0f, 1.0f); return d(rng); }

    Shader shader;
    std::mt19937 rng;

    std::vector<Particle> particles;    // CPU simulation
    std::vector<ParticleGPU> gpu;       // packed per-instance data, uploaded each frame
    std::vector<Emitter> emitters;      // flame sources (position + scale), from the scene lights

    GLuint vao = 0, quadVBO = 0, instanceVBO = 0;
};
