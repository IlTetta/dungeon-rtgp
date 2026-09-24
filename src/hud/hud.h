#pragma once

// The ImGui performance HUD, pulled out of main.cpp.
//
// main.cpp had grown big, and most of that size was the four ImGui panels (Performance,
// Lighting, Benchmark, Particles). They are pure "draw the UI and poke some state" code, so
// they are a clean thing to move into their own class and keep main focused on the loop.
//
// The panels touch a LOT of the app's state (the scene, the renderer, the benchmark harness,
// a bunch of flags...). Instead of passing ~two dozen separate parameters we group them into
// one HudState struct of REFERENCES: main fills it once (the referenced objects live for the
// whole program), and passes it to Hud::draw() every frame. The Hud only OWNS the little bits
// of state that are UI-only (the path-file text box, the last status line, the SSAO-sweep
// checkbox); everything else stays owned by main and is only referenced here.
//
// This is header-only like the rest of world/, bench/, engine/, so it needs no CMake change.

#include <string>
#include <cstdio>    // snprintf, for the frame-time plot overlay label
#include <cfloat>    // FLT_MAX, to auto-scale the frame-time plot

#include <glad/glad.h>
#include <glfw/glfw3.h>      // GLFWwindow, glfwSwapInterval, glfwGetTime (VSync toggle / random seed)

#include "imgui.h"

#include "core/scene.h"
#include "core/metrics.h"
#include "engine/camera.h"
#include "world/chain_physics.h"
#include "world/world_builder.h"   // buildWorld(); also pulls in DungeonParams and LightingParams
#include "render/renderer.h"
#include "bench/benchmark.h"
#include "bench/experiment.h"
#include "world/particles.h"

// All the state the HUD panels read or write, as references into what main owns. Built once in
// main (see the comment on top) and handed to Hud::draw() each frame.
struct HudState {
    // subsystems
    Scene&             scene;
    ChainSystem&       chainSystem;
    Camera&            camera;
    Renderer&          renderer;
    BenchmarkHarness&  bench;
    ExperimentRunner&  experiments;
    ParticleSystem&    particles;

    // generation parameters + the current seed
    DungeonParams&     params;
    LightingParams&    lightingParams;
    unsigned int&      dungeonSeed;

    // flags shared with the render loop / input callbacks
    bool&  cullingEnabled;
    bool&  vsyncEnabled;
    bool&  debugCamEnabled;
    bool&  showFrustumWire;
    bool&  debugCamFollowYaw;
    float& debugCamHeight;
    bool&  firstMouse;        // set to true after a teleport, so the mouse-look doesn't jump

    // particle count + the settings we snapshot at the start of an experiment batch and
    // restore when it ends (the batch-restore itself lives in the loop, these are its storage)
    int&   particleCount;
    bool&  savedCulling;
    bool&  savedSsao;
    bool&  savedInstanced;
    int&   savedParticleCount;
    bool&  savedStructuralInstancing;
    int&   savedMaxSpotShadows;
    int&   savedMaxPointShadows;
    int&   savedFogSteps;

    // this frame's metrics (the panels only display them)
    const FrameMetrics& metrics;

    // needed to flip VSync (glfwSwapInterval) from the Benchmark panel
    GLFWwindow* window;
};

class Hud {
public:
    // Draw all four HUD windows for this frame. Must be called inside an ImGui frame (i.e. after
    // ImGui::NewFrame()), same place the panels used to sit in the loop, so the behaviour is
    // identical.
    void draw(HudState& s) {
        drawConfigBanner(s);
        drawPerformancePanel(s);
        drawLightingPanel(s);
        drawBenchmarkPanel(s);
        drawParticlesPanel(s);
    }

private:
    // UI-only state that used to be locals in main
    char        benchPathFile_[128] = "benchmarks/bench_path.txt";  // Save/Load target
    std::string benchStatus_;        // last Save/Load/Replay result, shown in the panel
    bool        sweepSSAO_ = false;  // if set, "Run experiments" also toggles SSAO (2x2 matrix)

    // Live frame-time plot (demo aid): a small ring buffer of the last frames, drawn as a graph in
    // the Performance panel so the effect of a toggle is visible as a moving curve, not just a number.
    static const int kFrameHist = 120;
    float frameMs_[kFrameHist] = {};
    int   frameMsHead_ = 0;

    // --- Config banner: a small always-on overlay listing the active toggles, so a viewer of the
    // demo video always knows the current configuration even when the panels are collapsed. Drag it
    // wherever it reads best. (Demo aid.) ---
    void drawConfigBanner(HudState& s) {
        ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.55f);
        ImGui::Begin("Config", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
        auto flag = [](const char* name, bool on) {
            ImGui::TextColored(on ? ImVec4(0.45f, 1.0f, 0.45f, 1.0f) : ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
                               "%s: %s", name, on ? "ON" : "OFF");
        };
        flag("Culling",       s.cullingEnabled);
        flag("Shadows",       s.renderer.tuning.shadowsEnabled);
        flag("SSAO",          s.renderer.tuning.ssaoEnabled);
        flag("Fog",           s.renderer.tuning.fogEnabled);
        flag("Instancing",    s.renderer.structuralInstancing);
        flag("Direct lights", s.renderer.directLightsEnabled);
        ImGui::End();
    }

    // --- Performance window: metrics + shadow/fog tuning + dungeon regen + debug camera ---
    void drawPerformancePanel(HudState& s) {
        ImGui::Begin("Performance");
        ImGui::Text("FPS: %.0f  (%.2f ms)", s.metrics.fps, s.metrics.frameTimeMs);
        // live frame-time graph (demo aid): push this frame, plot the last kFrameHist. The curve
        // reacts as you toggle fog steps / shadow budget / instancing (with VSync OFF) - much more
        // readable on camera than a single number. Auto-scaled (0..max in the window).
        frameMs_[frameMsHead_] = s.metrics.frameTimeMs;
        frameMsHead_ = (frameMsHead_ + 1) % kFrameHist;
        char ftOverlay[32];
        snprintf(ftOverlay, sizeof(ftOverlay), "%.2f ms", s.metrics.frameTimeMs);
        ImGui::PlotLines("##frametime", frameMs_, kFrameHist, frameMsHead_, ftOverlay,
                         0.0f, FLT_MAX, ImVec2(0, 48));
        ImGui::Separator();
        ImGui::Text("Objects drawn : %d / %d", s.metrics.objectsDrawn, s.metrics.objectsTotal);
        ImGui::Text("Objects culled: %d", s.metrics.objectsCulled);
        ImGui::Text("Draw calls    : %d", s.metrics.drawCalls);   // < objectsDrawn when instancing
        ImGui::Text("Triangles     : %d", s.metrics.trianglesDrawn);
        ImGui::Text("Lights        : %d", s.metrics.activeLights);
        ImGui::Text("Shadow lights : %d  (%d passes)", s.metrics.shadowLights, s.metrics.shadowPasses);
        ImGui::Text("Fog steps     : %d", s.metrics.fogSteps);
        ImGui::Separator();
        ImGui::Text("Frustum culling: %s", s.cullingEnabled ? "ON" : "OFF");
        // structural instancing A/B (floor/wall/ceiling as 3 instanced calls vs one per slab).
        // Watch "Draw calls" collapse when you tick this while "Objects drawn" stays the same.
        ImGui::Checkbox("Structural instancing", &s.renderer.structuralInstancing);

        // shading/shadow constants, live - move them around until it looks right, then bake
        // the values in as the new defaults.
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Shadow tuning")) {
            ImGui::Checkbox("Shadows enabled", &s.renderer.tuning.shadowsEnabled);
            // Demo aid: turn the direct lights OFF so only ambient*albedo*AO remains -> the SSAO
            // "showcase" (Direct lights OFF, raise Ambient, then toggle "SSAO enabled" below and the
            // occlusion is the only thing darkening). Also gives the "dark dungeon" shot - for that,
            // turn Particles off too, otherwise sparks glow with no light around.
            ImGui::Checkbox("Direct lights", &s.renderer.directLightsEnabled);
            ImGui::SliderFloat("Ambient", &s.renderer.tuning.ambient, 0.0f, 1.0f);   // 1.0 for a flat SSAO showcase
            // Runtime cap on how many lights cast a shadow (the "scaling #lights" experiment knob).
            // Watch "Shadow lights"/"passes" in the metrics and the frame time react as you drag these.
            ImGui::TextDisabled("Shadow-light budget (benchmark)");
            ImGui::SliderInt("Max SPOT casters",  &s.renderer.maxSpotShadows,  0, Renderer::MAX_SPOT_SHADOWS);
            ImGui::SliderInt("Max POINT casters", &s.renderer.maxPointShadows, 0, Renderer::MAX_POINT_SHADOWS);
            ImGui::TextDisabled("SPOT (wall torches)");
            ImGui::SliderFloat("Spot bias max", &s.renderer.tuning.spotBiasMax, 0.0f, 0.2f);
            ImGui::SliderFloat("Spot bias min", &s.renderer.tuning.spotBiasMin, 0.0f, 0.05f);
            ImGui::SliderFloat("Spot normal offset", &s.renderer.tuning.spotNormalOffset, 0.0f, 0.2f);
            ImGui::TextDisabled("POINT (braziers, cubemap)");
            ImGui::SliderFloat("Point bias scale", &s.renderer.tuning.pointBiasScale, 0.0f, 0.2f);
            ImGui::SliderFloat("Point bias min scale", &s.renderer.tuning.pointBiasMinScale, 0.0f, 0.1f);
            ImGui::SliderFloat("Point normal offset", &s.renderer.tuning.pointNormalOffset, 0.0f, 0.3f);
            ImGui::SliderFloat("Point softness", &s.renderer.tuning.pointPCFRadius, 0.0f, 0.15f);
            ImGui::TextDisabled("Pop-in fix (shadows)");
            ImGui::SliderFloat("Fade seconds", &s.renderer.tuning.shadowFadeSeconds, 0.0f, 1.5f);
            ImGui::TextDisabled("Pop-in fix (lights, >32 nearby)");
            ImGui::SliderFloat("Light hysteresis margin", &s.renderer.tuning.lightHysteresisMargin, 1.0f, 2.0f);
            ImGui::SliderFloat("Light fade seconds", &s.renderer.tuning.lightFadeSeconds, 0.0f, 1.5f);
            ImGui::TextDisabled("Contact shadows (SSAO)");
            ImGui::Checkbox("SSAO enabled", &s.renderer.tuning.ssaoEnabled);
            ImGui::SliderFloat("SSAO radius", &s.renderer.tuning.ssaoRadius, 0.05f, 2.0f);
            ImGui::SliderFloat("SSAO bias", &s.renderer.tuning.ssaoBias, 0.0f, 0.1f);
            ImGui::SliderFloat("SSAO strength", &s.renderer.tuning.ssaoStrength, 0.5f, 4.0f);
            if (ImGui::Button("Reset to defaults"))
                s.renderer.tuning = Renderer::ShadingTuning();
        }

        // M3: volumetric fog, ray marched (see shaders/fog.frag). fogSteps also drives the
        // FrameMetrics::fogSteps counter above, for the "fog steps vs fps" experiment.
        if (ImGui::CollapsingHeader("Volumetric fog")) {
            ImGui::Checkbox("Fog enabled", &s.renderer.tuning.fogEnabled);
            ImGui::ColorEdit3("Fog color", &s.renderer.tuning.fogColor.x);
            ImGui::SliderFloat("Density", &s.renderer.tuning.fogDensity, 0.0f, 0.2f);
            ImGui::SliderFloat("Scatter strength", &s.renderer.tuning.fogScatter, 0.0f, 2.0f);
            ImGui::SliderFloat("Max distance", &s.renderer.tuning.fogMaxDistance, 5.0f, 100.0f);
            ImGui::SliderInt("Steps", &s.renderer.tuning.fogSteps, 1, 64);
        }

        // --- M3 tools ---
        // (1) regenerate the dungeon from a seed, live. ImGui has no unsigned field, so we edit an
        // int and clamp it to >= 0 before casting back to the unsigned seed.
        ImGui::Separator();
        ImGui::Text("Dungeon");
        int seedField = (int)s.dungeonSeed;
        if (ImGui::InputInt("Seed", &seedField)) {
            if (seedField < 0) seedField = 0;
            s.dungeonSeed = (unsigned int)seedField;
        }
        if (ImGui::Button("Generate")) {
            buildWorld(s.dungeonSeed, s.params, s.lightingParams, s.scene, s.chainSystem, s.camera);
            s.renderer.resetLightFades();   // new scene, new lights - old fade state does not apply
            s.firstMouse = true;   // the camera teleported: avoid a mouse-look jump next frame
        }
        ImGui::SameLine();
        if (ImGui::Button("Random seed")) {
            s.dungeonSeed = (unsigned int)(glfwGetTime() * 100000.0);
            buildWorld(s.dungeonSeed, s.params, s.lightingParams, s.scene, s.chainSystem, s.camera);
            s.renderer.resetLightFades();
            s.firstMouse = true;
        }

        // (2) debug spectator camera controls
        ImGui::Separator();
        ImGui::Checkbox("Debug camera (V)", &s.debugCamEnabled);
        if (s.debugCamEnabled) {
            ImGui::Checkbox("Show frustum wireframe", &s.showFrustumWire);
            ImGui::Checkbox("Follow player rotation", &s.debugCamFollowYaw);
            ImGui::SliderFloat("Cam height", &s.debugCamHeight, 10.0f, 90.0f);
        }

        ImGui::Separator();
        ImGui::TextDisabled("F1 cursor - C culling - V debug cam - WASD move - Shift sprint - ESC quit");
        ImGui::End();
    }

    // --- Lighting / torches tuning (separate window) ---
    // Live sliders (intensity/radius/color) are pushed into the scene lights every frame by
    // applyTorchLightTuning(); the placement sliders only take effect on "Regenerate".
    void drawLightingPanel(HudState& s) {
        ImGui::Begin("Lighting / Torches");
        ImGui::TextDisabled("Live (applied immediately):");
        ImGui::SliderFloat("Intensity", &s.lightingParams.torchIntensity, 0.0f, 5.0f);
        ImGui::SliderFloat("Radius (tiles)", &s.lightingParams.torchRadius, 1.0f, 15.0f);
        ImGui::ColorEdit3("Color", &s.lightingParams.torchColor.x);
        ImGui::Separator();
        ImGui::TextDisabled("Placement (press Regenerate to apply):");
        ImGui::SliderFloat("Wall spacing (u)", &s.lightingParams.torchSpacing, 2.0f, 15.0f);
        ImGui::SliderInt("Corridor every", &s.lightingParams.corridorEvery, 1, 8);
        ImGui::SliderFloat("Mount height (u)", &s.lightingParams.torchHeight, 1.0f, 4.0f);
        ImGui::SliderFloat("Cone tilt down", &s.lightingParams.coneTilt, 0.0f, 1.0f);
        if (ImGui::Button("Regenerate with these")) {
            buildWorld(s.dungeonSeed, s.params, s.lightingParams, s.scene, s.chainSystem, s.camera);
            s.renderer.resetLightFades();
            s.firstMouse = true;   // camera teleported: avoid a mouse-look jump next frame
        }
        ImGui::End();
    }

    // --- Benchmark harness (record/replay a fixed camera path + CSV logging) ---
    // Record a walkthrough, Save/Load it to a file, then Replay it: the replay drives the
    // camera along the exact same path while logging the per-frame metrics to a CSV. Replaying
    // the same path under different settings (culling ON/OFF, ...) is how we get comparable
    // measurements. NB: for real numbers use a Release build and keep VSync off.
    void drawBenchmarkPanel(HudState& s) {
        ImGui::Begin("Benchmark");
        ImGui::Text("Mode: %s", s.bench.modeName());
        ImGui::Text("Path: %d keyframes  (%.1f s)  seed %u",
                    s.bench.path().size(), s.bench.path().durationMs() / 1000.0f, s.bench.path().seed);
        if (s.bench.mode() == BenchmarkHarness::REPLAYING)
            ImGui::Text("Replay: %.1f / %.1f s",
                        s.bench.replayTimeMs() / 1000.0f, s.bench.path().durationMs() / 1000.0f);

        // VSync toggle. Checkbox returns true only on the frame the value changes, so we call
        // glfwSwapInterval only then (0 = uncapped, for benchmarking; 1 = capped to refresh).
        if (ImGui::Checkbox("VSync", &s.vsyncEnabled))
            glfwSwapInterval(s.vsyncEnabled ? 1 : 0);
        ImGui::SameLine();
        ImGui::TextDisabled("(turn OFF to benchmark)");
        // loud reminder if we are actually replaying+logging with the frame rate still capped
        if (s.bench.mode() == BenchmarkHarness::REPLAYING && s.vsyncEnabled)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "VSync ON: frame rate is capped!");

        if (s.experiments.running()) {
            // A batch is running: show progress and hide the manual controls (the harness mode
            // flickers REPLAYING/idle between runs, so we don't want the manual buttons here).
            ImGui::Separator();
            ImGui::Text("Experiment %d/%d: %s", s.experiments.currentIndex() + 1,
                        s.experiments.count(), s.experiments.currentConfig().name.c_str());
            if (ImGui::Button("Stop experiments")) s.experiments.stop(s.bench);
        }
        else {
            ImGui::InputText("Path file", benchPathFile_, sizeof(benchPathFile_));

            if (s.bench.mode() == BenchmarkHarness::IDLE) {
                // record on the CURRENT seed (stored inside the path)
                if (ImGui::Button("Record")) s.bench.beginRecord(s.dungeonSeed);
                ImGui::SameLine();
                // Save / Load report their result in benchStatus_, so it is obvious the click did
                // something. The file is written relative to the working directory (with the VS
                // "Open Folder" flow that is the build output folder, e.g. out/build/x64-Debug/).
                if (ImGui::Button("Save")) {
                    bool ok = s.bench.savePath(benchPathFile_);
                    benchStatus_ = ok ? ("Saved " + std::to_string(s.bench.path().size())
                                        + " keyframes -> " + std::string(benchPathFile_))
                                     : ("SAVE FAILED -> " + std::string(benchPathFile_));
                }
                ImGui::SameLine();
                if (ImGui::Button("Load")) {
                    bool ok = s.bench.loadPath(benchPathFile_);
                    benchStatus_ = ok ? ("Loaded " + std::to_string(s.bench.path().size())
                                        + " keyframes (seed " + std::to_string(s.bench.path().seed)
                                        + ") <- " + std::string(benchPathFile_))
                                     : ("LOAD FAILED (file not found?) <- " + std::string(benchPathFile_));
                }

                if (ImGui::Button("Replay + log CSV")) {
                    // A path only makes sense on the dungeon it was recorded on: if the (loaded)
                    // path has a different seed, regenerate that exact dungeon first.
                    if (s.bench.path().seed != s.dungeonSeed) {
                        s.dungeonSeed = s.bench.path().seed;
                        buildWorld(s.dungeonSeed, s.params, s.lightingParams, s.scene, s.chainSystem, s.camera);
                        s.renderer.resetLightFades();   // new scene, new lights - drop stale fade state
                        s.firstMouse = true;
                    }
                    // The CSV name encodes the configuration under test, so files from different
                    // experiments do not overwrite each other ("benchmark_*.csv" is git-ignored).
                    // It goes under benchmarks/ (the harness creates the folder if missing).
                    // nextFreeCsvPath appends _run2/_run3/... if this exact name already exists, so
                    // replaying the same config again keeps the earlier run instead of clobbering it.
                    std::string csv = "benchmarks/benchmark_seed" + std::to_string(s.dungeonSeed)
                                    + (s.cullingEnabled ? "_cullON" : "_cullOFF") + ".csv";
                    csv = nextFreeCsvPath(csv);
                    bool ok = s.bench.beginReplay(csv, s.dungeonSeed, s.cullingEnabled, Renderer::MAX_SHADOW_LIGHTS);
                    benchStatus_ = ok ? ("Replaying, logging -> " + csv)
                                     : "REPLAY FAILED (empty path, or CSV could not be opened)";
                }

                // --- automated experiment sweep ---
                // Replay the SAME path once per configuration and log a CSV each, unattended. The
                // core experiment is culling ON vs OFF (everything else equal); optionally we also
                // cross it with SSAO on/off (a 2x2 matrix). The path is the only thing kept fixed.
                ImGui::Separator();
                ImGui::Checkbox("Also sweep SSAO on/off (2x2)", &sweepSSAO_);
                if (ImGui::Button("Run experiments")) {
                    // same dungeon as the path, exactly (regenerate if the seed differs)
                    if (s.bench.path().seed != s.dungeonSeed) {
                        s.dungeonSeed = s.bench.path().seed;
                        buildWorld(s.dungeonSeed, s.params, s.lightingParams, s.scene, s.chainSystem, s.camera);
                        s.renderer.resetLightFades();   // new scene, new lights - drop stale fade state
                        s.firstMouse = true;
                    }
                    // remember the user's settings so we can restore them after the batch
                    s.savedCulling = s.cullingEnabled;
                    s.savedSsao = s.renderer.tuning.ssaoEnabled;
                    s.savedInstanced = s.particles.instanced;
                    s.savedParticleCount = s.particleCount;
                    s.savedStructuralInstancing = s.renderer.structuralInstancing;
                    s.savedMaxSpotShadows = s.renderer.maxSpotShadows;
                    s.savedMaxPointShadows = s.renderer.maxPointShadows;
                    s.savedFogSteps = s.renderer.tuning.fogSteps;
                    // build the config list: culling ON/OFF, optionally crossed with SSAO on/off.
                    // Every other knob (SSAO when not swept, structural instancing, particles) is kept
                    // at the user's current value in every run, so the culling comparison is fair (only
                    // the tested knob changes; everything else equal).
                    s.experiments.configs.clear();
                    if (sweepSSAO_) {
                        s.experiments.configs.push_back({ "cullON_ssaoON",   true,  true,  s.savedStructuralInstancing, s.savedInstanced, s.savedParticleCount });
                        s.experiments.configs.push_back({ "cullOFF_ssaoON",  false, true,  s.savedStructuralInstancing, s.savedInstanced, s.savedParticleCount });
                        s.experiments.configs.push_back({ "cullON_ssaoOFF",  true,  false, s.savedStructuralInstancing, s.savedInstanced, s.savedParticleCount });
                        s.experiments.configs.push_back({ "cullOFF_ssaoOFF", false, false, s.savedStructuralInstancing, s.savedInstanced, s.savedParticleCount });
                    } else {
                        s.experiments.configs.push_back({ "cullON",  true,  s.savedSsao, s.savedStructuralInstancing, s.savedInstanced, s.savedParticleCount });
                        s.experiments.configs.push_back({ "cullOFF", false, s.savedSsao, s.savedStructuralInstancing, s.savedInstanced, s.savedParticleCount });
                    }
                    s.experiments.start(s.bench, s.dungeonSeed, Renderer::MAX_SHADOW_LIGHTS);
                    benchStatus_ = s.experiments.running()
                        ? ("Running " + std::to_string(s.experiments.count()) + " experiments (path = "
                           + std::to_string(s.bench.path().size()) + " keyframes)...")
                        : "Cannot run experiments: record or load a path first";
                }

                // --- particle instancing sweep: instanced vs naive, everything else equal ---
                // Measures directly what instancing saves: same path, same culling/SSAO, same
                // particle count, only the draw strategy changes (1 call vs one call per particle).
                if (ImGui::Button("Run particle sweep")) {
                    if (s.bench.path().seed != s.dungeonSeed) {
                        s.dungeonSeed = s.bench.path().seed;
                        buildWorld(s.dungeonSeed, s.params, s.lightingParams, s.scene, s.chainSystem, s.camera);
                        s.renderer.resetLightFades();   // new scene, new lights - drop stale fade state
                        s.firstMouse = true;
                    }
                    s.savedCulling = s.cullingEnabled;
                    s.savedSsao = s.renderer.tuning.ssaoEnabled;
                    s.savedInstanced = s.particles.instanced;
                    s.savedParticleCount = s.particleCount;
                    s.savedStructuralInstancing = s.renderer.structuralInstancing;
                    s.savedMaxSpotShadows = s.renderer.maxSpotShadows;
                    s.savedMaxPointShadows = s.renderer.maxPointShadows;
                    s.savedFogSteps = s.renderer.tuning.fogSteps;
                    s.experiments.configs.clear();
                    s.experiments.configs.push_back({ "instancedON",  s.savedCulling, s.savedSsao, s.savedStructuralInstancing, true,  s.particleCount });
                    s.experiments.configs.push_back({ "instancedOFF", s.savedCulling, s.savedSsao, s.savedStructuralInstancing, false, s.particleCount });
                    s.experiments.start(s.bench, s.dungeonSeed, Renderer::MAX_SHADOW_LIGHTS);
                    benchStatus_ = s.experiments.running()
                        ? ("Running particle sweep (" + std::to_string(s.particleCount) + " particles)...")
                        : "Cannot run: record or load a path first";
                }

                // --- structural instancing sweep: instanced vs per-object, everything else equal ---
                // Measures what instancing the floor/wall/ceiling slabs saves: same path, same
                // culling/SSAO/particles, only the structural draw strategy changes (3 instanced
                // calls vs one call per slab). Watch draw_calls collapse in the CSV while triangles
                // stay the same. NB: this holds culling at the user's current value; for the cleanest
                // reading run it with culling ON (structural instancing is drawn on the visible slabs).
                if (ImGui::Button("Run structural instancing sweep")) {
                    if (s.bench.path().seed != s.dungeonSeed) {
                        s.dungeonSeed = s.bench.path().seed;
                        buildWorld(s.dungeonSeed, s.params, s.lightingParams, s.scene, s.chainSystem, s.camera);
                        s.renderer.resetLightFades();   // new scene, new lights - drop stale fade state
                        s.firstMouse = true;
                    }
                    s.savedCulling = s.cullingEnabled;
                    s.savedSsao = s.renderer.tuning.ssaoEnabled;
                    s.savedInstanced = s.particles.instanced;
                    s.savedParticleCount = s.particleCount;
                    s.savedStructuralInstancing = s.renderer.structuralInstancing;
                    s.savedMaxSpotShadows = s.renderer.maxSpotShadows;
                    s.savedMaxPointShadows = s.renderer.maxPointShadows;
                    s.savedFogSteps = s.renderer.tuning.fogSteps;
                    s.experiments.configs.clear();
                    s.experiments.configs.push_back({ "structInstON",  s.savedCulling, s.savedSsao, true,  s.savedInstanced, s.savedParticleCount });
                    s.experiments.configs.push_back({ "structInstOFF", s.savedCulling, s.savedSsao, false, s.savedInstanced, s.savedParticleCount });
                    s.experiments.start(s.bench, s.dungeonSeed, Renderer::MAX_SHADOW_LIGHTS);
                    benchStatus_ = s.experiments.running()
                        ? "Running structural instancing sweep (ON vs OFF)..."
                        : "Cannot run: record or load a path first";
                }

                // --- shadow-light sweep: scale the number of shadow-casting lights ---
                // The proposal's "scaling the number of dynamic lights" experiment: replay the same
                // path with a growing SPOT shadow budget (the dominant per-light cost - each caster is
                // a depth pass) and see frame time rise. POINT budget + everything else stay at the
                // user's current values. Read frame_ms vs shadow_lights / shadow_passes across the CSVs.
                if (ImGui::Button("Run shadow-light sweep")) {
                    if (s.bench.path().seed != s.dungeonSeed) {
                        s.dungeonSeed = s.bench.path().seed;
                        buildWorld(s.dungeonSeed, s.params, s.lightingParams, s.scene, s.chainSystem, s.camera);
                        s.renderer.resetLightFades();   // new scene, new lights - drop stale fade state
                        s.firstMouse = true;
                    }
                    s.savedCulling = s.cullingEnabled;
                    s.savedSsao = s.renderer.tuning.ssaoEnabled;
                    s.savedInstanced = s.particles.instanced;
                    s.savedParticleCount = s.particleCount;
                    s.savedStructuralInstancing = s.renderer.structuralInstancing;
                    s.savedMaxSpotShadows = s.renderer.maxSpotShadows;
                    s.savedMaxPointShadows = s.renderer.maxPointShadows;
                    s.savedFogSteps = s.renderer.tuning.fogSteps;
                    s.experiments.configs.clear();
                    // increasing SPOT-caster counts (capped at the compile-time budget)
                    const int spotSteps[] = { 1, 2, 4, 6, 8 };
                    for (int n : spotSteps) {
                        if (n > Renderer::MAX_SPOT_SHADOWS) continue;
                        s.experiments.configs.push_back({ "shadowSpot" + std::to_string(n),
                            s.savedCulling, s.savedSsao, s.savedStructuralInstancing, s.savedInstanced,
                            s.savedParticleCount, n, s.savedMaxPointShadows });
                    }
                    s.experiments.start(s.bench, s.dungeonSeed, Renderer::MAX_SHADOW_LIGHTS);
                    benchStatus_ = s.experiments.running()
                        ? "Running shadow-light sweep (SPOT casters 1..8)..."
                        : "Cannot run: record or load a path first";
                }

                // --- fog sweep: scale the ray-march step count (fog quality vs FPS) ---
                // The proposal's "fog quality vs FPS" experiment: replay the same path with a growing
                // number of ray-march steps (the fog cost knob) and see the frame time rise. Everything
                // else stays at the user's current values. Read frame_ms vs the fog_steps column across
                // the CSVs. NB: fog must be enabled (it is by default) or the step count has no effect.
                if (ImGui::Button("Run fog sweep")) {
                    if (s.bench.path().seed != s.dungeonSeed) {
                        s.dungeonSeed = s.bench.path().seed;
                        buildWorld(s.dungeonSeed, s.params, s.lightingParams, s.scene, s.chainSystem, s.camera);
                        s.renderer.resetLightFades();   // new scene, new lights - drop stale fade state
                        s.firstMouse = true;
                    }
                    s.savedCulling = s.cullingEnabled;
                    s.savedSsao = s.renderer.tuning.ssaoEnabled;
                    s.savedInstanced = s.particles.instanced;
                    s.savedParticleCount = s.particleCount;
                    s.savedStructuralInstancing = s.renderer.structuralInstancing;
                    s.savedMaxSpotShadows = s.renderer.maxSpotShadows;
                    s.savedMaxPointShadows = s.renderer.maxPointShadows;
                    s.savedFogSteps = s.renderer.tuning.fogSteps;
                    s.experiments.configs.clear();
                    // increasing ray-march step counts (the HUD "Steps" slider goes up to 64)
                    const int fogStepValues[] = { 4, 8, 12, 16, 24, 32, 48, 64 };
                    for (int n : fogStepValues) {
                        s.experiments.configs.push_back({ "fogSteps" + std::to_string(n),
                            s.savedCulling, s.savedSsao, s.savedStructuralInstancing, s.savedInstanced,
                            s.savedParticleCount, s.savedMaxSpotShadows, s.savedMaxPointShadows, n });
                    }
                    s.experiments.start(s.bench, s.dungeonSeed, Renderer::MAX_SHADOW_LIGHTS);
                    benchStatus_ = s.experiments.running()
                        ? "Running fog sweep (ray-march steps 4..64)..."
                        : "Cannot run: record or load a path first";
                }
                if (s.bench.path().empty())
                    ImGui::TextDisabled("(record or load a path first)");
            }
            else if (s.bench.mode() == BenchmarkHarness::RECORDING) {
                if (ImGui::Button("Stop recording")) s.bench.stopRecord();
            }
            else {   // REPLAYING (manual)
                if (ImGui::Button("Stop replay")) s.bench.stopReplay();
            }
        }

        // last Save/Load/Replay result (with the absolute path), so it is clear what happened
        if (!benchStatus_.empty()) {
            ImGui::Separator();
            ImGui::TextWrapped("%s", benchStatus_.c_str());
        }
        ImGui::End();
    }

    // --- Particles (M3): instanced sparks/embers, with an instanced-vs-naive A/B ---
    void drawParticlesPanel(HudState& s) {
        ImGui::Begin("Particles");
        ImGui::Checkbox("Enabled", &s.particles.enabled);
        ImGui::Checkbox("Instanced (1 draw call)", &s.particles.instanced);
        if (ImGui::SliderInt("Count", &s.particleCount, 0, 8000))
            s.particles.setCount(s.particleCount);
        ImGui::Text("Particle draw calls this frame: %d", s.metrics.particleDrawCalls);
        if (!s.particles.instanced)
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Naive: one draw call per particle");
        ImGui::End();
    }

public:
    // Read the batch-end status line so main can set it when an experiment batch finishes (the
    // batch-restore logic lives in the loop, but the message belongs to the Benchmark panel).
    void setStatus(const std::string& msg) { benchStatus_ = msg; }
};
