// main.cpp
//
// Creates the window and the OpenGL context, builds the world and all the subsystems, and
// runs the frame loop: experiments, benchmark, input and collisions, chains and particles,
// rendering, metrics, HUD.
//
// Controls: WASD move, Shift sprint, mouse look, C culling on/off, V spectator camera,
// F1 free the cursor for the HUD, ESC quit.

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
#include "world/world_builder.h"
#include "world/debug_draw.h"
#include "world/frustum_culling.h"
#include "render/renderer.h"
#include "bench/benchmark.h"
#include "bench/experiment.h"
#include "world/particles.h"
#include "hud/hud.h"
#include "input/input.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// window size
const unsigned int WIDTH = 1280;
const unsigned int HEIGHT = 720;

// radius of the player cylinder (a circle in X/Z) used by the collisions and to push the chains
const float PLAYER_RADIUS = 0.4f;

int main() {
    // window + OpenGL 4.1 core context
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

    // The camera and the toggles changed by the keys are here; initInput() installs the GLFW
    // callbacks and gives them pointers to these. It must run before ImGui is initialized, so
    // ImGui can chain its callbacks onto ours.
    Camera camera(glm::vec3(0.0f, 1.6f, 0.0f), true);   // the spawn point is set by buildWorld
    bool cullingEnabled = true; // C
    bool debugCamEnabled = false;   // V
    bool firstMouse = true; // true after a teleport, so the view does not jump
    bool requestClearFocus = false; // set when going back to first person (F1)
    initInput(window, camera, cullingEnabled, debugCamEnabled, firstMouse, requestClearFocus);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    // the world: BSP dungeon, geometry, props, chains, spawn (needs the GL context for the meshes)
    DungeonParams params;
    unsigned int dungeonSeed = 12345;   // seed of the current dungeon, editable from the HUD
    LightingParams lightingParams;
    Scene scene;
    ChainSystem chainSystem;
    buildWorld(dungeonSeed, params, lightingParams, scene, chainSystem, camera);

    Renderer renderer("shaders/ggx.vert", "shaders/ggx.frag");
    renderer.setViewport(WIDTH, HEIGHT);

    // spectator camera (V)
    DebugDraw debugDraw;
    float debugCamHeight = 35.0f;   // how high above the player
    bool showFrustumWire = true;    // draw the yellow frustum cage
    bool debugCamFollowYaw = true;  // behind the player (true) or fixed, north up (false)

    BenchmarkHarness bench;

    // VSync ON by default, so normal play does not run the GPU at 100%. It must be turned OFF
    // (Benchmark window) before measuring: with VSync the frame rate is capped to the monitor
    // refresh and the real cost of a frame is hidden.
    bool vsyncEnabled = true;
    glfwSwapInterval(1);

    // Experiment batches. The saved values keep the user settings during a batch, so we can
    // restore them at the end (the HUD fills them when a batch starts).
    ExperimentRunner experiments;
    bool experimentsWereRunning = false;   // to notice the frame where a batch ends
    bool savedCulling = cullingEnabled;
    bool savedSsao = false;
    bool savedStructuralInstancing = false;
    int savedMaxSpotShadows = Renderer::MAX_SPOT_SHADOWS;
    int savedMaxPointShadows = Renderer::MAX_POINT_SHADOWS;
    int savedFogSteps = 12;   // the ShadingTuning default

    // fire particles (the emitters are the fire lights of the scene)
    ParticleSystem particles;
    int particleCount = 4000;
    bool savedInstanced = true;
    int savedParticleCount = 4000;
    particles.init(scene, particleCount);

    // Dear ImGui. "true" makes ImGui chain to the callbacks installed by initInput().
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 410");

    FrameMetrics metrics;   // filled by the renderer and the particles; fps and frame time here

    // HudState is built once with references to the objects above, which live until the end of
    // main. The fields must be in the same order as in the struct.
    Hud hud;
    HudState hudState{
        scene, chainSystem, camera, renderer, bench, experiments, particles,
        params, lightingParams, dungeonSeed,
        cullingEnabled, vsyncEnabled, debugCamEnabled, showFrustumWire, debugCamFollowYaw,
        debugCamHeight, firstMouse,
        particleCount, savedCulling, savedSsao, savedInstanced, savedParticleCount,
        savedStructuralInstancing, savedMaxSpotShadows, savedMaxPointShadows, savedFogSteps,
        metrics, window
    };

    // deltaTime = seconds since the last frame: used by movement, physics and the benchmark
    float deltaTime = 0.0f;
    float lastFrame = 0.0f;

    while (!glfwWindowShouldClose(window)) {
        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        glfwPollEvents();

        // Experiment batch: update() starts the run of each config and moves to the next one.
        // During a batch we apply the current config every frame; on the frame it ends we put
        // back the user settings.
        experiments.update(bench);
        if (experiments.running()) {
            const ExperimentConfig& c = experiments.currentConfig();
            cullingEnabled = c.culling;
            renderer.tuning.ssaoEnabled = c.ssao;
            renderer.structuralInstancing = c.structuralInstanced;
            renderer.maxSpotShadows = c.maxSpotShadows;   // the renderer clamps it to its maximum
            renderer.maxPointShadows = c.maxPointShadows;
            if (c.fogSteps > 0)
                renderer.tuning.fogSteps = c.fogSteps;    // -1 = leave the fog as it is
            particles.instanced = c.instanced;
            particleCount = c.particleCount;
            particles.setCount(particleCount);
        }
        else if (experimentsWereRunning) {
            cullingEnabled = savedCulling;
            renderer.tuning.ssaoEnabled = savedSsao;
            renderer.structuralInstancing = savedStructuralInstancing;
            renderer.maxSpotShadows = savedMaxSpotShadows;
            renderer.maxPointShadows = savedMaxPointShadows;
            renderer.tuning.fogSteps = savedFogSteps;
            particles.instanced = savedInstanced;
            particleCount = savedParticleCount;
            particles.setCount(particleCount);
            hud.setStatus("Experiments finished: see benchmarks/benchmark_*.csv");
        }
        experimentsWereRunning = experiments.running();

        // During a replay the recorded path sets the camera, so we skip input and collisions.
        bench.beginFrame(camera, deltaTime);
        if (!bench.drivingCamera()) {
            applyMovements(deltaTime);
            camera.Position = resolveWallCollisions(camera.Position, PLAYER_RADIUS, scene);
        }

        chainSystem.update(deltaTime, camera.Position, PLAYER_RADIUS, scene);
        particles.update(scene, deltaTime, currentFrame);   // currentFrame drives the flicker

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Back in first person (F1): take the keyboard focus away from any HUD widget, so WASD
        // move the camera and are not typed in a text box. SetWindowFocus(NULL) also clears the
        // active text field; it has to run after NewFrame().
        if (requestClearFocus) {
            ImGui::SetWindowFocus(NULL);
            requestClearFocus = false;
        }

        // live torch settings from the Lighting window into the scene lights
        applyTorchLightTuning(scene, lightingParams, params.tileSize);

        renderer.cullingEnabled = cullingEnabled;
        if (debugCamEnabled) {
            // We cull with the PLAYER frustum but draw from a camera high above, to see what the
            // culling keeps.
            glm::mat4 playerVP = renderer.getProjection() * camera.getViewMatrix();
            Frustum playerFrustum = extractFrustum(playerVP);

            // The spectator looks down at a steep angle, so we see both the map and the wall heights.
            glm::vec3 specEye;
            if (debugCamFollowYaw)
                specEye = camera.Position + glm::vec3(0.0f, debugCamHeight, 0.0f) - camera.WorldFront * (debugCamHeight * 0.5f);
            else
                specEye = camera.Position + glm::vec3(0.0f, debugCamHeight, debugCamHeight * 0.35f);
            glm::mat4 specView = glm::lookAt(specEye, camera.Position, glm::vec3(0.0f, 1.0f, 0.0f));

            renderer.renderSpectator(scene, specView, specEye, playerFrustum, /*hideCeiling*/ true, metrics);
            // no particles in the spectator view
            metrics.particlesDrawn = 0;
            metrics.particleDrawCalls = 0;

            if (showFrustumWire)
                debugDraw.drawFrustum(playerVP, specView, renderer.getProjection(), glm::vec3(1.0f, 0.9f, 0.2f));
        }
        else {
            renderer.render(scene, camera, metrics);
            // after the scene, so they are depth-tested against it
            particles.draw(camera.getViewMatrix(), renderer.getProjection(), metrics);
        }

        // the two metrics written by main
        metrics.fps = ImGui::GetIO().Framerate;
        metrics.frameTimeMs = deltaTime * 1000.0f;

        // after rendering, when all the metrics of this frame are ready: a CSV row (replay) or a
        // keyframe (recording)
        bench.endFrame(camera, metrics, deltaTime);

        // The HUD. A "Generate" click rebuilds the scene here, after the frame has been drawn,
        // so the next frame already uses the new one.
        hud.draw(hudState);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    debugDraw.clean();
    renderer.clean();
    particles.clean();
    glfwTerminate();
    return 0;
}
