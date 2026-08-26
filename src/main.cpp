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

// Dear ImGui (performance HUD)
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// window size
const unsigned int WIDTH = 1280;
const unsigned int HEIGHT = 720;

// radius of the player sphere used for wall collisions
const float PLAYER_RADIUS = 0.4f;

// frustum culling on/off (toggled with the C key), to compare performance ON vs OFF
bool cullingEnabled = true;

// UI mode (toggled with F1): the mouse cursor is released so we can interact with the ImGui HUD
// (drag it, collapse it), and the camera stops turning. F1 again goes back to first-person.
bool uiMode = false;

// Set when we switch from HUD back to first-person, so the render loop can drop the keyboard
// focus from any HUD widget (e.g. a text field) before movement keys are read. Cleared once
// handled. (See the F1 handler and the SetWindowFocus call in the loop.)
bool requestClearFocus = false;

// debug spectator camera (toggled with V): we DRAW the scene from far above the player, while the
// frustum culling keeps using the PLAYER frustum. This lets us watch, from outside, exactly what
// the culling draws/skips as we move and turn. It is an M3 tool, off by default.
bool debugCamEnabled = false;

// globals used by the input callbacks (same simple approach as the lab code)
Camera camera(glm::vec3(0.0f, 1.6f, 0.0f), true);   // start position is set later, after we build the level
bool keys[1024] = { false };
float deltaTime = 0.0f;   // time between the current frame and the previous one
float lastFrame = 0.0f;
float lastX = WIDTH / 2.0f;
float lastY = HEIGHT / 2.0f;
bool firstMouse = true;

// called by GLFW when a key is pressed or released
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mode) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
    // toggle frustum culling on/off
    if (key == GLFW_KEY_C && action == GLFW_PRESS)
        cullingEnabled = !cullingEnabled;
    // toggle the debug spectator camera (far, top-down view of the culling)
    if (key == GLFW_KEY_V && action == GLFW_PRESS)
        debugCamEnabled = !debugCamEnabled;
    // F1: toggle the mouse cursor free (to use the HUD) / captured (first-person look).
    // We use F1 rather than TAB on purpose: TAB is ImGui's own "focus next field" key, so
    // toggling with TAB would also move the keyboard focus into a HUD text box (and then WASD
    // would be typed into it instead of moving the camera). F1 never touches the HUD widgets.
    if (key == GLFW_KEY_F1 && action == GLFW_PRESS) {
        uiMode = !uiMode;
        glfwSetInputMode(window, GLFW_CURSOR, uiMode ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
        firstMouse = true;   // avoid a camera jump when going back to first-person
        // going back to first-person: ask the loop to drop the focus from any HUD widget that
        // still holds it, so movement keys reach the game and not a text field.
        if (!uiMode) requestClearFocus = true;
    }
    if (key >= 0 && key < 1024) {
        if (action == GLFW_PRESS)   keys[key] = true;
        if (action == GLFW_RELEASE) keys[key] = false;
    }
}

// called by GLFW when the mouse moves
void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (uiMode) return;          // in UI mode the mouse controls the HUD, not the camera
    if (firstMouse) {            // avoid a big jump on the very first mouse event
        lastX = (float)xpos;
        lastY = (float)ypos;
        firstMouse = false;
    }
    float xoffset = (float)xpos - lastX;
    float yoffset = lastY - (float)ypos;   // reversed: screen y grows downward
    lastX = (float)xpos;
    lastY = (float)ypos;
    camera.processMouse(xoffset, yoffset);
}

// check which movement keys are held and move the camera accordingly
void applyMovements() {
    // hold SHIFT to sprint: normal speed feels right in the rooms, sprint helps in the long corridors
    camera.MovementSpeed = (keys[GLFW_KEY_LEFT_SHIFT] || keys[GLFW_KEY_RIGHT_SHIFT]) ? 15.0f : 6.0f;

    if (keys[GLFW_KEY_W]) camera.processKeyboard(FORWARD, deltaTime);
    if (keys[GLFW_KEY_S]) camera.processKeyboard(BACKWARD, deltaTime);
    if (keys[GLFW_KEY_A]) camera.processKeyboard(LEFT, deltaTime);
    if (keys[GLFW_KEY_D]) camera.processKeyboard(RIGHT, deltaTime);
}

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
    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);   // capture and hide the mouse

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
    BenchmarkHarness bench;
    char benchPathFile[128] = "benchmarks/bench_path.txt";   // file used by the Save/Load buttons
    std::string benchStatus;   // last Save/Load/Replay result, shown in the panel (empty = nothing yet)

    // VSync: ON by default, so normal walking around does not spin the GPU at max. Turn it OFF
    // (checkbox in the Benchmark panel) BEFORE measuring: with VSync the frame rate is capped to
    // the monitor refresh, which hides the true per-frame cost the benchmark is meant to reveal.
    // The context is already current here (created above), so glfwSwapInterval is valid.
    bool vsyncEnabled = true;
    glfwSwapInterval(1);

    // --- init Dear ImGui (for the performance HUD) ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    // "true" lets ImGui chain to the keyboard/mouse callbacks we already installed above.
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 410");

    FrameMetrics metrics;   // most fields are filled by the renderer; fps/frameTime here in main

    // --- render loop ---
    while (!glfwWindowShouldClose(window)) {
        // time management
        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        glfwPollEvents();

        // Benchmark: in replay the camera pose comes from the recorded path, so we skip the
        // normal input + wall-collision step for this frame; otherwise play normally. beginFrame
        // sets the camera pose (in replay) and stops the replay when the path is over.
        bench.beginFrame(camera, deltaTime);
        if (!bench.drivingCamera()) {
            applyMovements();
            // push the player out of any wall it tried to walk into
            camera.Position = resolveWallCollisions(camera.Position, PLAYER_RADIUS, scene);
        }

        // advance the swinging chains (Verlet) and write their transforms back into the scene
        chainSystem.update(deltaTime, camera.Position, PLAYER_RADIUS, scene);

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

            // overlay the player frustum as a yellow wireframe cage, so the culling volume is visible
            if (showFrustumWire)
                debugDraw.drawFrustum(playerVP, specView, renderer.getProjection(), glm::vec3(1.0f, 0.9f, 0.2f));
        }
        else {
            renderer.render(scene, camera, metrics);
        }

        // the two metrics that main is responsible for
        metrics.fps = ImGui::GetIO().Framerate;
        metrics.frameTimeMs = deltaTime * 1000.0f;

        // Benchmark: record a keyframe (if recording) or log this frame's metrics (if replaying).
        // Called here, after metrics.fps is filled, so the CSV row is complete.
        bench.endFrame(camera, metrics, deltaTime);

        // --- HUD window ---
        ImGui::Begin("Performance");
        ImGui::Text("FPS: %.0f  (%.2f ms)", metrics.fps, metrics.frameTimeMs);
        ImGui::Separator();
        ImGui::Text("Objects drawn : %d / %d", metrics.drawCalls, metrics.objectsTotal);
        ImGui::Text("Objects culled: %d", metrics.objectsCulled);
        ImGui::Text("Triangles     : %d", metrics.trianglesDrawn);
        ImGui::Text("Lights        : %d", metrics.activeLights);
        ImGui::Text("Shadow lights : %d  (%d passes)", metrics.shadowLights, metrics.shadowPasses);
        ImGui::Separator();
        ImGui::Text("Frustum culling: %s", cullingEnabled ? "ON" : "OFF");

        // shading/shadow constants, live - move them around until it looks right, then bake
        // the values in as the new defaults.
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Shadow tuning")) {
            ImGui::SliderFloat("Ambient", &renderer.tuning.ambient, 0.0f, 0.5f);
            ImGui::TextDisabled("SPOT (wall torches)");
            ImGui::SliderFloat("Spot bias max", &renderer.tuning.spotBiasMax, 0.0f, 0.2f);
            ImGui::SliderFloat("Spot bias min", &renderer.tuning.spotBiasMin, 0.0f, 0.05f);
            ImGui::SliderFloat("Spot normal offset", &renderer.tuning.spotNormalOffset, 0.0f, 0.2f);
            ImGui::TextDisabled("POINT (braziers, cubemap)");
            ImGui::SliderFloat("Point bias scale", &renderer.tuning.pointBiasScale, 0.0f, 0.2f);
            ImGui::SliderFloat("Point bias min scale", &renderer.tuning.pointBiasMinScale, 0.0f, 0.1f);
            ImGui::SliderFloat("Point normal offset", &renderer.tuning.pointNormalOffset, 0.0f, 0.3f);
            ImGui::SliderFloat("Point softness", &renderer.tuning.pointPCFRadius, 0.0f, 0.15f);
            ImGui::TextDisabled("Pop-in fix");
            ImGui::SliderFloat("Hysteresis margin", &renderer.tuning.shadowHysteresisMargin, 1.0f, 2.0f);
            ImGui::SliderFloat("Fade seconds", &renderer.tuning.shadowFadeSeconds, 0.0f, 1.5f);
            ImGui::TextDisabled("Contact shadows (SSAO)");
            ImGui::Checkbox("SSAO enabled", &renderer.tuning.ssaoEnabled);
            ImGui::SliderFloat("SSAO radius", &renderer.tuning.ssaoRadius, 0.05f, 2.0f);
            ImGui::SliderFloat("SSAO bias", &renderer.tuning.ssaoBias, 0.0f, 0.1f);
            ImGui::SliderFloat("SSAO strength", &renderer.tuning.ssaoStrength, 0.5f, 4.0f);
            if (ImGui::Button("Reset to defaults"))
                renderer.tuning = Renderer::ShadingTuning();
        }

        // --- M3 tools ---
        // (1) regenerate the dungeon from a seed, live. ImGui has no unsigned field, so we edit an
        // int and clamp it to >= 0 before casting back to the unsigned seed.
        ImGui::Separator();
        ImGui::Text("Dungeon");
        int seedField = (int)dungeonSeed;
        if (ImGui::InputInt("Seed", &seedField)) {
            if (seedField < 0) seedField = 0;
            dungeonSeed = (unsigned int)seedField;
        }
        if (ImGui::Button("Generate")) {
            buildWorld(dungeonSeed, params, lightingParams, scene, chainSystem, camera);
            firstMouse = true;   // the camera teleported: avoid a mouse-look jump next frame
        }
        ImGui::SameLine();
        if (ImGui::Button("Random seed")) {
            dungeonSeed = (unsigned int)(glfwGetTime() * 100000.0);
            buildWorld(dungeonSeed, params, lightingParams, scene, chainSystem, camera);
            firstMouse = true;
        }

        // (2) debug spectator camera controls
        ImGui::Separator();
        ImGui::Checkbox("Debug camera (V)", &debugCamEnabled);
        if (debugCamEnabled) {
            ImGui::Checkbox("Show frustum wireframe", &showFrustumWire);
            ImGui::Checkbox("Follow player rotation", &debugCamFollowYaw);
            ImGui::SliderFloat("Cam height", &debugCamHeight, 10.0f, 90.0f);
        }

        ImGui::Separator();
        ImGui::TextDisabled("F1 cursor - C culling - V debug cam - WASD move - Shift sprint - ESC quit");
        ImGui::End();

        // --- Lighting / torches tuning (separate window) ---
        // Live sliders (intensity/radius/color) are pushed into the scene lights every frame by
        // applyTorchLightTuning(); the placement sliders only take effect on "Regenerate".
        ImGui::Begin("Lighting / Torches");
        ImGui::TextDisabled("Live (applied immediately):");
        ImGui::SliderFloat("Intensity", &lightingParams.torchIntensity, 0.0f, 5.0f);
        ImGui::SliderFloat("Radius (tiles)", &lightingParams.torchRadius, 1.0f, 15.0f);
        ImGui::ColorEdit3("Color", &lightingParams.torchColor.x);
        ImGui::Separator();
        ImGui::TextDisabled("Placement (press Regenerate to apply):");
        ImGui::SliderFloat("Wall spacing (u)", &lightingParams.torchSpacing, 2.0f, 15.0f);
        ImGui::SliderInt("Corridor every", &lightingParams.corridorEvery, 1, 8);
        ImGui::SliderFloat("Mount height (u)", &lightingParams.torchHeight, 1.0f, 4.0f);
        ImGui::SliderFloat("Cone tilt down", &lightingParams.coneTilt, 0.0f, 1.0f);
        if (ImGui::Button("Regenerate with these")) {
            buildWorld(dungeonSeed, params, lightingParams, scene, chainSystem, camera);
            firstMouse = true;   // camera teleported: avoid a mouse-look jump next frame
        }
        ImGui::End();

        // --- Benchmark harness (record/replay a fixed camera path + CSV logging) ---
        // Record a walkthrough, Save/Load it to a file, then Replay it: the replay drives the
        // camera along the exact same path while logging the per-frame metrics to a CSV. Replaying
        // the same path under different settings (culling ON/OFF, ...) is how we get comparable
        // measurements. NB: for real numbers use a Release build and keep VSync off.
        ImGui::Begin("Benchmark");
        ImGui::Text("Mode: %s", bench.modeName());
        ImGui::Text("Path: %d keyframes  (%.1f s)  seed %u",
                    bench.path().size(), bench.path().durationMs() / 1000.0f, bench.path().seed);
        if (bench.mode() == BenchmarkHarness::REPLAYING)
            ImGui::Text("Replay: %.1f / %.1f s",
                        bench.replayTimeMs() / 1000.0f, bench.path().durationMs() / 1000.0f);

        // VSync toggle. Checkbox returns true only on the frame the value changes, so we call
        // glfwSwapInterval only then (0 = uncapped, for benchmarking; 1 = capped to refresh).
        if (ImGui::Checkbox("VSync", &vsyncEnabled))
            glfwSwapInterval(vsyncEnabled ? 1 : 0);
        ImGui::SameLine();
        ImGui::TextDisabled("(turn OFF to benchmark)");
        // loud reminder if we are actually replaying+logging with the frame rate still capped
        if (bench.mode() == BenchmarkHarness::REPLAYING && vsyncEnabled)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "VSync ON: frame rate is capped!");

        ImGui::InputText("Path file", benchPathFile, sizeof(benchPathFile));

        if (bench.mode() == BenchmarkHarness::IDLE) {
            // record on the CURRENT seed (stored inside the path)
            if (ImGui::Button("Record")) bench.beginRecord(dungeonSeed);
            ImGui::SameLine();
            // Save / Load report their result in benchStatus, so it is obvious the click did
            // something. The file is written relative to the working directory (with the VS
            // "Open Folder" flow that is the build output folder, e.g. out/build/x64-Debug/).
            if (ImGui::Button("Save")) {
                bool ok = bench.savePath(benchPathFile);
                benchStatus = ok ? ("Saved " + std::to_string(bench.path().size())
                                    + " keyframes -> " + std::string(benchPathFile))
                                 : ("SAVE FAILED -> " + std::string(benchPathFile));
            }
            ImGui::SameLine();
            if (ImGui::Button("Load")) {
                bool ok = bench.loadPath(benchPathFile);
                benchStatus = ok ? ("Loaded " + std::to_string(bench.path().size())
                                    + " keyframes (seed " + std::to_string(bench.path().seed)
                                    + ") <- " + std::string(benchPathFile))
                                 : ("LOAD FAILED (file not found?) <- " + std::string(benchPathFile));
            }

            if (ImGui::Button("Replay + log CSV")) {
                // A path only makes sense on the dungeon it was recorded on: if the (loaded) path
                // has a different seed, regenerate that exact dungeon first.
                if (bench.path().seed != dungeonSeed) {
                    dungeonSeed = bench.path().seed;
                    buildWorld(dungeonSeed, params, lightingParams, scene, chainSystem, camera);
                    firstMouse = true;
                }
                // The CSV name encodes the configuration under test, so files from different
                // experiments do not overwrite each other (and "benchmark_*.csv" is git-ignored).
                // It goes under benchmarks/ (the harness creates the folder if missing).
                std::string csv = "benchmarks/benchmark_seed" + std::to_string(dungeonSeed)
                                + (cullingEnabled ? "_cullON" : "_cullOFF") + ".csv";
                bool ok = bench.beginReplay(csv, dungeonSeed, cullingEnabled, Renderer::MAX_SHADOW_LIGHTS);
                benchStatus = ok ? ("Replaying, logging -> " + csv)
                                 : "REPLAY FAILED (empty path, or CSV could not be opened)";
            }
        }
        else if (bench.mode() == BenchmarkHarness::RECORDING) {
            if (ImGui::Button("Stop recording")) bench.stopRecord();
        }
        else {   // REPLAYING
            if (ImGui::Button("Stop replay")) bench.stopReplay();
        }

        // last Save/Load/Replay result (with the absolute path), so it is clear what happened
        if (!benchStatus.empty()) {
            ImGui::Separator();
            ImGui::TextWrapped("%s", benchStatus.c_str());
        }
        ImGui::End();

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
    glfwTerminate();
    return 0;
}
