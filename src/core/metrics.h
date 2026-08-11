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
    int objectsCulled   = 0;      // how many were skipped by frustum culling (M2)
    int activeLights    = 0;      // how many lights are actually used

    int drawCalls       = 0;      // how many draw calls this frame (= objects actually drawn)
    int trianglesDrawn  = 0;      // filled by Renderer (how many triangles were sent)
    int fogSteps        = 0;      // filled by Renderer (ray-march steps of the fog, M3)
};
