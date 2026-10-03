#pragma once


// The numbers we measure every frame: the HUD shows them and the benchmark writes them to CSV.
struct FrameMetrics {
    float fps = 0.0f;   // main (ImGui's averaged fps, only for display)
    float frameTimeMs = 0.0f;   // main (deltaTime, the value used in the analysis)

    // The Renderer writes all the object and draw counters, since the culling happens inside it.
    int objectsTotal = 0;   // objects in the scene
    int objectsDrawn = 0;   // objects drawn this frame
    int objectsCulled = 0;  // objectsTotal - objectsDrawn
    int activeLights = 0;   // lights that shade the scene this frame

    // Real GL draw calls of the color pass.
    int drawCalls = 0;
    int trianglesDrawn = 0;
    int fogSteps = 0;

    // lights with a real shadow this frame, and the depth passes they took (SPOT 1, POINT 6)
    int shadowLights = 0;
    int shadowPasses = 0;

    // particleDrawCalls is 1 when the particles are instanced and equal to particlesDrawn on the
    // naive path (one call per particle)
    int particlesDrawn = 0;
    int particleDrawCalls = 0;
};
