#pragma once

// SHARED FILE: da cambiare insieme all'inizio di ogni milestone

// All the numbers we measure each frame: we show them in the ImGui HUD and we also
// write them to a CSV file for the performance experiments (the core goal of the project).
//
// The comment on the right of each field says WHO fills it. We keep one single writer per
// field on purpose: if two different parts of the code wrote the same field we could get
// confusing results (and merge conflicts).
//
// We give every field a default value (= 0) because these counters are reset / recomputed
// at the beginning of every frame, so starting from 0 is the natural state.
struct FrameMetrics {
    float fps           = 0.0f;   // filled in main.cpp
    float frameTimeMs   = 0.0f;   // filled in main.cpp

    // These four are filled by the Renderer: since the frustum culling now happens inside
    // render() (it iterates the objects anyway), the Renderer is the natural single writer
    // for all the object/draw counters.
    int objectsTotal    = 0;      // how many objects the scene has
    int objectsDrawn    = 0;      // how many objects were actually drawn this frame (visible ones)
    int objectsCulled   = 0;      // how many were skipped by frustum culling (M2)
    int activeLights    = 0;      // how many lights are actually used

    // How many real GL draw calls the color pass issued. On the plain per-object path this equals
    // objectsDrawn (one call per object); with structural instancing ON the floor/wall/ceiling
    // slabs collapse into 3 instanced calls, so drawCalls drops well below objectsDrawn - that gap
    // is exactly what the instancing A/B measures (same triangles, far fewer calls).
    int drawCalls       = 0;      // filled by Renderer
    int trianglesDrawn  = 0;      // filled by Renderer (how many triangles were sent)
    int fogSteps        = 0;      // filled by Renderer (ray-march steps of the fog, M3)

    // how many lights actually cast a real shadow this frame, and how many depth passes that
    // took (a POINT light costs 6, one per cubemap face; a SPOT light costs 1)
    int shadowLights    = 0;      // filled by Renderer
    int shadowPasses    = 0;      // filled by Renderer

    // Particles (M3, Andrea): filled by ParticleSystem. particleDrawCalls is 1 when the particles
    // are drawn INSTANCED (all in one draw call) and = particlesDrawn on the naive
    // one-call-per-particle path, so the CSV shows directly what instancing saves.
    int particlesDrawn    = 0;    // filled by ParticleSystem
    int particleDrawCalls = 0;    // filled by ParticleSystem
};
