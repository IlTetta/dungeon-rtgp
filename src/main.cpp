// main.cpp
//
// Integrated application (M1 + M2):
//   - Andrea's side: procedural BSP dungeon -> 3D geometry (the Scene), FPS camera with
//     ad-hoc wall collisions, and the ImGui performance HUD.
//   - Lorenzo's side: the Renderer (GGX forward shading) that actually draws the Scene.
//   - The frustum culling now lives inside the Renderer (it iterates the objects anyway).
//
// Controls: WASD move, Shift sprint, mouse look, C toggle culling, ESC quit.

#ifdef _WIN32
    #define APIENTRY __stdcall
#endif

#include <glad/glad.h>
#include <glfw/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <string>
#include <iostream>

#include "engine/camera.h"
#include "core/scene.h"
#include "core/metrics.h"
#include "dungeon/dungeon_generator.h"
#include "world/dungeon_geometry.h"
#include "world/props.h"
#include "world/chain_physics.h"
#include "world/collision.h"
#include "world/world_builder.h"     // buildWorld(): (re)generate the whole dungeon from a seed
#include "world/debug_draw.h"        // DebugDraw: frustum wireframe for the spectator view
#include "world/frustum_culling.h"   // extractFrustum / Frustum, to freeze the player frustum
#include "render/renderer.h"
#include "bench/benchmark.h"         // M3 benchmark harness: record/replay camera path + CSV log
#include "bench/experiment.h"        // M3 experiment automation: replay one path once per config
#include "world/particles.h"         // M3 instanced spark/ember particles rising from the flames
#include "hud/hud.h"                 // the ImGui performance HUD, pulled out of this file
#include "input/input.h"             // FPS keyboard/mouse callbacks + applyMovements, out of this file

// Dear ImGui (performance HUD)
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// window size
const unsigned int WIDTH = 1280;
const unsigned int HEIGHT = 720;

// radius of the player sphere used for wall collisions
const float PLAYER_RADIUS = 0.4f;

// The FPS input state (keyboard/mouse callbacks + applyMovements) now lives in input/input.h; the
// state those callbacks drive (camera, culling / debug-cam toggles, ...) is declared as locals in
// main() and wired to the input with initInput().

int main() {
    // --- window + OpenGL 4.1 core context ---
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);

    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "Dungeon RTGP", nullptr, nullptr);
    if (window == nullptr) {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);

    // --- FPS input state (owned here, driven by the callbacks in input.h) ---
    // The camera and the flags the callbacks toggle live in main; initInput() installs the GLFW
    // keyboard/mouse callbacks and points the input at these. Done before ImGui init, so ImGui
    // chains onto our callbacks (see initInput's note).
    Camera camera(glm::vec3(0.0f, 1.6f, 0.0f), true);   // spawn position is set later by buildWorld
    bool cullingEnabled  = true;     // C: frustum culling on/off, to compare performance ON vs OFF
    bool debugCamEnabled = false;    // V: top-down spectator view of what the culling draws/skips
    bool firstMouse      = true;     // reset after a camera teleport, to avoid a mouse-look jump
    bool requestClearFocus = false;  // set on return to first-person; the loop then clears HUD focus
    initInput(window, camera, cullingEnabled, debugCamEnabled, firstMouse, requestClearFocus);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    // --- build the dungeon and its 3D geometry (needs the OpenGL context, so we do it now) ---
    // The whole pipeline (BSP generation -> geometry -> Scene -> props/chains -> camera spawn) is
    // wrapped in buildWorld() so we can rebuild it from a new seed at runtime (see the HUD below).
    DungeonParams params;                 // default tile size / wall height
    unsigned int dungeonSeed = 12345;     // seed of the CURRENT dungeon (editable from the HUD)
    LightingParams lightingParams;        // torch/light tuning (editable from the "Lighting" window)
    Scene scene;
    ChainSystem chainSystem;
    buildWorld(dungeonSeed, params, lightingParams, scene, chainSystem, camera);   // static props + hanging chains

    // --- renderer (GGX forward shading) ---
    Renderer renderer("shaders/ggx.vert", "shaders/ggx.frag");
    renderer.setViewport(WIDTH, HEIGHT);

    // --- debug tooling (M3) ---
    DebugDraw debugDraw;              // draws the player frustum wireframe in the spectator view
    float debugCamHeight = 35.0f;     // how high above the player the spectator camera sits
    bool  showFrustumWire = true;     // draw the yellow frustum cage in the spectator view
    bool  debugCamFollowYaw = true;   // chase cam (follows where the player looks) vs fixed north-up

    // --- benchmark harness (M3): record/replay a fixed camera path + log metrics to CSV ---
    // (the Save/Load path-file text and the last status line now live inside the Hud object)
    BenchmarkHarness bench;

    // VSync: ON by default, so normal walking around does not spin the GPU at max. Turn it OFF
    // (checkbox in the Benchmark panel) BEFORE measuring: with VSync the frame rate is capped to
    // the monitor refresh, which hides the true per-frame cost the benchmark is meant to reveal.
    // The context is already current here (created above), so glfwSwapInterval is valid.
    bool vsyncEnabled = true;
    glfwSwapInterval(1);

    // --- experiment automation (M3): replay the loaded path once per configuration, unattended ---
    // (the "also sweep SSAO" checkbox now lives inside the Hud object)
    ExperimentRunner experiments;
    bool experimentsWereRunning = false;  // edge-detect the end of a batch, to restore the settings
    bool savedCulling = cullingEnabled;   // user settings captured at batch start, restored after
    bool savedSsao = false;               // (set from renderer.tuning when the batch starts)
    bool savedStructuralInstancing = false;   // (set from renderer.structuralInstancing at batch start)
    int  savedMaxSpotShadows  = Renderer::MAX_SPOT_SHADOWS;    // shadow-caster budget, restored after a batch
    int  savedMaxPointShadows = Renderer::MAX_POINT_SHADOWS;

    // --- particles (M3): instanced sparks/embers rising from the flames ---
    ParticleSystem particles;
    int  particleCount      = 4000;       // pool size, driven by the HUD slider / experiments
    bool savedInstanced     = true;       // particle settings captured at batch start, restored after
    int  savedParticleCount = 4000;
    particles.init(scene, particleCount);   // emitters come from the scene lights (each is a flame)

    // --- init Dear ImGui (for the performance HUD) ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    // "true" lets ImGui chain to the keyboard/mouse callbacks we already installed above.
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 410");

    FrameMetrics metrics;   // most fields are filled by the renderer; fps/frameTime here in main

    // --- HUD (the ImGui panels, moved into their own class) ---
    // The Hud owns only its UI-only state; everything it draws/edits is passed as references in
    // one HudState. We build that struct ONCE here: the referenced objects (scene, renderer, the
    // flags...) live for the whole program, so the references stay valid for every frame. The
    // fields must be listed in the same order as the struct declares them.
    Hud hud;
    HudState hudState{
        scene, chainSystem, camera, renderer, bench, experiments, particles,
        params, lightingParams, dungeonSeed,
        cullingEnabled, vsyncEnabled, debugCamEnabled, showFrustumWire, debugCamFollowYaw,
        debugCamHeight, firstMouse,
        particleCount, savedCulling, savedSsao, savedInstanced, savedParticleCount,
        savedStructuralInstancing, savedMaxSpotShadows, savedMaxPointShadows,
        metrics, window
    };

    // frame timing: deltaTime (seconds since last frame) drives movement, physics and the CSV log;
    // lastFrame is the previous timestamp. Plain locals now (used to be globals for the callbacks).
    float deltaTime = 0.0f;
    float lastFrame = 0.0f;

    // --- render loop ---
    while (!glfwWindowShouldClose(window)) {
        // time management
        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        glfwPollEvents();

        // Experiment automation: advance the batch (this starts each config's replay and moves to
        // the next one when a replay finishes). While a batch runs we force this config's knobs
        // (culling / SSAO) into the app every frame; when the batch ends we restore the settings
        // the user had before it started.
        experiments.update(bench);
        if (experiments.running()) {
            const ExperimentConfig& c = experiments.currentConfig();
            cullingEnabled = c.culling;
            renderer.tuning.ssaoEnabled = c.ssao;
            renderer.structuralInstancing = c.structuralInstanced;
            renderer.maxSpotShadows = c.maxSpotShadows;     // clamped to the compile-time max in renderInternal
            renderer.maxPointShadows = c.maxPointShadows;
            particles.instanced = c.instanced;
            particleCount = c.particleCount;
            particles.setCount(particleCount);
        }
        else if (experimentsWereRunning) {   // the batch just ended this frame
            cullingEnabled = savedCulling;
            renderer.tuning.ssaoEnabled = savedSsao;
            renderer.structuralInstancing = savedStructuralInstancing;
            renderer.maxSpotShadows = savedMaxSpotShadows;
            renderer.maxPointShadows = savedMaxPointShadows;
            particles.instanced = savedInstanced;
            particleCount = savedParticleCount;
            particles.setCount(particleCount);
            hud.setStatus("Experiments finished - see benchmarks/benchmark_*.csv");
        }
        experimentsWereRunning = experiments.running();

        // Benchmark: in replay the camera pose comes from the recorded path, so we skip the
        // normal input + wall-collision step for this frame; otherwise play normally. beginFrame
        // sets the camera pose (in replay) and stops the replay when the path is over.
        bench.beginFrame(camera, deltaTime);
        if (!bench.drivingCamera()) {
            applyMovements(deltaTime);
            // push the player out of any wall it tried to walk into
            camera.Position = resolveWallCollisions(camera.Position, PLAYER_RADIUS, scene);
        }

        // advance the swinging chains (Verlet) and write their transforms back into the scene
        chainSystem.update(deltaTime, camera.Position, PLAYER_RADIUS, scene);

        // advance the particle simulation (emitters are refreshed from the scene lights inside).
        // currentFrame (= glfwGetTime) is the time base for the flame flicker, matching the light.
        particles.update(scene, deltaTime, currentFrame);

        // start a new ImGui frame (before drawing anything)
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // If we just returned to first-person (F1), remove the keyboard focus from any HUD widget.
        // SetWindowFocus(NULL) unfocuses every window AND clears the active text field (see
        // FocusWindow in imgui.cpp), so WASD go to the camera and are not typed into a HUD box.
        // Must run inside a frame, i.e. after NewFrame(), so we do it here.
        if (requestClearFocus) {
            ImGui::SetWindowFocus(NULL);
            requestClearFocus = false;
        }

        // push the live torch tuning (brightness / reach / color) into the scene lights each frame,
        // so the "Lighting" window sliders are visible immediately without a rebuild
        applyTorchLightTuning(scene, lightingParams, params.tileSize);

        // draw the whole scene (this clears the screen, does the culling, and fills the
        // object/draw/light counters in "metrics")
        renderer.cullingEnabled = cullingEnabled;
        if (debugCamEnabled) {
            // Freeze the PLAYER frustum: this is the volume we cull against, no matter where we
            // draw from. (view-projection = projection * view of the first-person camera.)
            glm::mat4 playerVP = renderer.getProjection() * camera.getViewMatrix();
            Frustum playerFrustum = extractFrustum(playerVP);

            // Place the spectator camera high above the player, looking down at a steep angle
            // (not straight down) so we read both the map and the wall heights. Two modes:
            //  - follow-yaw (chase cam): sit BEHIND the player along its look direction, so "up" on
            //    screen is always where the player faces -> WASD stays intuitive.
            //  - fixed: sit slightly to +Z, north-up. Nicer stable "sweep" view for screenshots,
            //    but the controls feel mirrored when the player turns toward the camera.
            glm::vec3 specEye;
            if (debugCamFollowYaw)
                specEye = camera.Position + glm::vec3(0.0f, debugCamHeight, 0.0f) - camera.WorldFront * (debugCamHeight * 0.5f);
            else
                specEye = camera.Position + glm::vec3(0.0f, debugCamHeight, debugCamHeight * 0.35f);
            glm::mat4 specView = glm::lookAt(specEye, camera.Position, glm::vec3(0.0f, 1.0f, 0.0f));

            // draw from the spectator, cull against the frozen player frustum, hide the ceiling
            renderer.renderSpectator(scene, specView, specEye, playerFrustum, /*hideCeiling*/ true, metrics);
            // particles are skipped in the debug overhead view -> report zero for this frame
            metrics.particlesDrawn = 0;
            metrics.particleDrawCalls = 0;

            // overlay the player frustum as a yellow wireframe cage, so the culling volume is visible
            if (showFrustumWire)
                debugDraw.drawFrustum(playerVP, specView, renderer.getProjection(), glm::vec3(1.0f, 0.9f, 0.2f));
        }
        else {
            renderer.render(scene, camera, metrics);
            // sparks/embers on top of the opaque scene (fills the particle counters in metrics)
            particles.draw(camera.getViewMatrix(), renderer.getProjection(), metrics);
        }

        // the two metrics that main is responsible for
        metrics.fps = ImGui::GetIO().Framerate;
        metrics.frameTimeMs = deltaTime * 1000.0f;

        // Benchmark: record a keyframe (if recording) or log this frame's metrics (if replaying).
        // Called here, after metrics.fps is filled, so the CSV row is complete.
        bench.endFrame(camera, metrics, deltaTime);

        // --- HUD: all four ImGui panels, drawn from their own class ---
        hud.draw(hudState);

        // draw the HUD on top of the scene, then present the frame
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // --- cleanup ---
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    debugDraw.clean();
    renderer.clean();
    particles.clean();
    glfwTerminate();
    return 0;
}
