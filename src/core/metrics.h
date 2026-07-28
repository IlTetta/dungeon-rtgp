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

    int objectsTotal    = 0;      // filled by World  (how many objects the scene has)
    int objectsCulled   = 0;      // filled by World  (how many were skipped by culling, M2)
    int activeLights    = 0;      // filled by World  (how many lights are used)

    int drawCalls       = 0;      // filled by Renderer (how many draw calls this frame)
    int trianglesDrawn  = 0;      // filled by Renderer (how many triangles were sent)
    int fogSteps        = 0;      // filled by Renderer (ray-march steps of the fog, M3)
};
