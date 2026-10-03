#pragma once

// The ImGui HUD: five windows (Config, Performance, Lighting / Torches, Benchmark, Particles).
//
// The panels read and change a lot of the app state (scene, renderer, benchmark, many flags).
// Instead of passing two dozen parameters we put references to all of them in one struct,
// HudState: main builds it once (the objects live for the whole program) and passes it to
// Hud::draw() every frame. The Hud only owns the state that exists just for the UI (the path
// file text box, the last status line, the SSAO sweep checkbox, the frame time history).

#include <string>
#include <cstdio>
#include <cfloat>

#include <glad/glad.h>
#include <glfw/glfw3.h>

#include "imgui.h"

#include "core/scene.h"
#include "core/metrics.h"
#include "engine/camera.h"
#include "world/chain_physics.h"
#include "world/world_builder.h"
#include "render/renderer.h"
#include "bench/benchmark.h"
#include "bench/experiment.h"
#include "world/particles.h"

// References to everything the panels read or write, all owned by main.
struct HudState {
    Scene& scene;
    ChainSystem& chainSystem;
    Camera& camera;
    Renderer& renderer;
    BenchmarkHarness& bench;
    ExperimentRunner& experiments;
    ParticleSystem& particles;

    // generation parameters and the current seed
    DungeonParams& params;
    LightingParams& lightingParams;
    unsigned int& dungeonSeed;

    // flags shared with the loop and the input callbacks
    bool& cullingEnabled;
    bool& vsyncEnabled;
    bool& debugCamEnabled;
    bool& showFrustumWire;
    bool& debugCamFollowYaw;
    float& debugCamHeight;
    bool& firstMouse;   // set after a teleport, so the view does not jump

    // particle count, and the user settings saved when an experiment batch starts (main restores
    // them when the batch ends)
    int& particleCount;
    bool& savedCulling;
    bool& savedSsao;
    bool& savedInstanced;
    int& savedParticleCount;
    bool& savedStructuralInstancing;
    int& savedMaxSpotShadows;
    int& savedMaxPointShadows;
    int& savedFogSteps;

    const FrameMetrics& metrics;   // this frame's numbers, only displayed

    GLFWwindow* window;
};

class Hud {
public:
    // Draw all the windows. Must be called inside an ImGui frame (after ImGui::NewFrame()).
    void draw(HudState& s) {
        drawConfigBanner(s);
        drawPerformancePanel(s);
        drawLightingPanel(s);
        drawBenchmarkPanel(s);
        drawParticlesPanel(s);
    }

private:
    char benchPathFile_[128] = "benchmarks/bench_path.txt";   // file for Save / Load
    std::string benchStatus_;   // result of the last Save / Load / Replay, shown in the panel
    bool sweepSSAO_ = false;    // "Run experiments" also switches SSAO on/off (2x2)

    // the last frame times, in a ring buffer, for the graph in the Performance panel
    static const int kFrameHist = 120;
    float frameMs_[kFrameHist] = {};
    int frameMsHead_ = 0;

    // Small window that always lists the main toggles, so in the demo video the current
    // configuration is visible even with the other panels closed.
    void drawConfigBanner(HudState& s) {
        ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.55f);
        ImGui::Begin("Config", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
        auto flag = [](const char* name, bool on) {
            ImGui::TextColored(on ? ImVec4(0.45f, 1.0f, 0.45f, 1.0f) : ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
                               "%s: %s", name, on ? "ON" : "OFF");
        };
        flag("Culling", s.cullingEnabled);
        flag("Shadows", s.renderer.tuning.shadowsEnabled);
        flag("SSAO", s.renderer.tuning.ssaoEnabled);
        flag("Fog", s.renderer.tuning.fogEnabled);
        flag("Instancing", s.renderer.structuralInstancing);
        flag("Direct lights", s.renderer.directLightsEnabled);
        ImGui::End();
    }

    // Performance window: metrics, shadow and fog settings, dungeon seed, debug camera.
    void drawPerformancePanel(HudState& s) {
        ImGui::Begin("Performance");
        ImGui::Text("FPS: %.0f  (%.2f ms)", s.metrics.fps, s.metrics.frameTimeMs);
        // graph of the last frame times: with VSync off you see the curve move when you change
        // fog steps, shadow budget or instancing, which is clearer than a single number
        frameMs_[frameMsHead_] = s.metrics.frameTimeMs;
        frameMsHead_ = (frameMsHead_ + 1) % kFrameHist;
        char ftOverlay[32];
        snprintf(ftOverlay, sizeof(ftOverlay), "%.2f ms", s.metrics.frameTimeMs);
        ImGui::PlotLines("##frametime", frameMs_, kFrameHist, frameMsHead_, ftOverlay,
                         0.0f, FLT_MAX, ImVec2(0, 48));
        ImGui::Separator();
        ImGui::Text("Objects drawn : %d / %d", s.metrics.objectsDrawn, s.metrics.objectsTotal);
        ImGui::Text("Objects culled: %d", s.metrics.objectsCulled);
        ImGui::Text("Draw calls    : %d", s.metrics.drawCalls);   // less than objects drawn with instancing
        ImGui::Text("Triangles     : %d", s.metrics.trianglesDrawn);
        ImGui::Text("Lights        : %d", s.metrics.activeLights);
        ImGui::Text("Shadow lights : %d  (%d passes)", s.metrics.shadowLights, s.metrics.shadowPasses);
        ImGui::Text("Fog steps     : %d", s.metrics.fogSteps);
        ImGui::Separator();
        ImGui::Text("Frustum culling: %s", s.cullingEnabled ? "ON" : "OFF");
        // ticking this, "Draw calls" drops a lot while "Objects drawn" stays the same
        ImGui::Checkbox("Structural instancing", &s.renderer.structuralInstancing);

        // shading and shadow settings, live
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Shadow tuning")) {
            ImGui::Checkbox("Shadows enabled", &s.renderer.tuning.shadowsEnabled);
            // Direct lights off leaves only ambient * albedo * AO: with a high Ambient, toggling
            // SSAO shows the occlusion alone (used in the demo video).
            ImGui::Checkbox("Direct lights", &s.renderer.directLightsEnabled);
            ImGui::SliderFloat("Ambient", &s.renderer.tuning.ambient, 0.0f, 1.0f);
            // how many lights cast a shadow: the knob of the "number of lights" experiment
            ImGui::TextDisabled("Shadow-light budget (benchmark)");
            ImGui::SliderInt("Max SPOT casters", &s.renderer.maxSpotShadows, 0, Renderer::MAX_SPOT_SHADOWS);
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

        // volumetric fog (shaders/fog.frag). Steps is the knob of the "fog quality vs FPS"
        // experiment and goes into the fog_steps column of the CSV.
        if (ImGui::CollapsingHeader("Volumetric fog")) {
            ImGui::Checkbox("Fog enabled", &s.renderer.tuning.fogEnabled);
            ImGui::ColorEdit3("Fog color", &s.renderer.tuning.fogColor.x);
            ImGui::SliderFloat("Density", &s.renderer.tuning.fogDensity, 0.0f, 0.2f);
            ImGui::SliderFloat("Scatter strength", &s.renderer.tuning.fogScatter, 0.0f, 2.0f);
            ImGui::SliderFloat("Max distance", &s.renderer.tuning.fogMaxDistance, 5.0f, 100.0f);
            ImGui::SliderInt("Steps", &s.renderer.tuning.fogSteps, 1, 64);
        }

        // Regenerate the dungeon from a seed. ImGui has no unsigned input, so we edit an int,
        // clamp it to >= 0 and cast it back.
        ImGui::Separator();
        ImGui::Text("Dungeon");
        int seedField = (int)s.dungeonSeed;
        if (ImGui::InputInt("Seed", &seedField)) {
            if (seedField < 0)
                seedField = 0;
            s.dungeonSeed = (unsigned int)seedField;
        }
        if (ImGui::Button("Generate")) {
            buildWorld(s.dungeonSeed, s.params, s.lightingParams, s.scene, s.chainSystem, s.camera);
            s.renderer.resetLightFades();   // new lights: the old fade state does not apply
            s.firstMouse = true;    // the camera teleported
        }
        ImGui::SameLine();
        if (ImGui::Button("Random seed")) {
            s.dungeonSeed = (unsigned int)(glfwGetTime() * 100000.0);
            buildWorld(s.dungeonSeed, s.params, s.lightingParams, s.scene, s.chainSystem, s.camera);
            s.renderer.resetLightFades();
            s.firstMouse = true;
        }

        // spectator camera
        ImGui::Separator();
        ImGui::Checkbox("Debug camera (V)", &s.debugCamEnabled);
        if (s.debugCamEnabled) {
            ImGui::Checkbox("Show frustum wireframe", &s.showFrustumWire);
            ImGui::Checkbox("Follow player rotation", &s.debugCamFollowYaw);
            ImGui::SliderFloat("Cam height", &s.debugCamHeight, 10.0f, 90.0f);
        }

        ImGui::Separator();
        ImGui::TextDisabled("F1 cursor | C culling | V debug cam | WASD move | Shift sprint | ESC quit");
        ImGui::End();
    }

    // Torch settings. Intensity, radius and color are applied every frame by
    // applyTorchLightTuning(); the placement values need a regenerate.
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
            s.firstMouse = true;
        }
        ImGui::End();
    }

    // Benchmark window: record a walk, save / load it, replay it while logging a CSV, and the
    // automatic sweeps. Real numbers only in a Release build with VSync off.
    void drawBenchmarkPanel(HudState& s) {
        ImGui::Begin("Benchmark");
        ImGui::Text("Mode: %s", s.bench.modeName());
        ImGui::Text("Path: %d keyframes  (%.1f s)  seed %u",
                    s.bench.path().size(), s.bench.path().durationMs() / 1000.0f, s.bench.path().seed);
        if (s.bench.mode() == BenchmarkHarness::REPLAYING)
            ImGui::Text("Replay: %.1f / %.1f s",
                        s.bench.replayTimeMs() / 1000.0f, s.bench.path().durationMs() / 1000.0f);

        // Checkbox returns true only on the frame the value changes, so we call
        // glfwSwapInterval only then (0 = no cap, 1 = capped to the monitor refresh)
        if (ImGui::Checkbox("VSync", &s.vsyncEnabled))
            glfwSwapInterval(s.vsyncEnabled ? 1 : 0);
        ImGui::SameLine();
        ImGui::TextDisabled("(turn OFF to benchmark)");
        if (s.bench.mode() == BenchmarkHarness::REPLAYING && s.vsyncEnabled)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "VSync ON: frame rate is capped!");

        if (s.experiments.running()) {
            // during a batch we only show the progress: between two runs the harness goes
            // through IDLE for a frame, and the manual buttons must not appear then
            ImGui::Separator();
            ImGui::Text("Experiment %d/%d: %s", s.experiments.currentIndex() + 1,
                        s.experiments.count(), s.experiments.currentConfig().name.c_str());
            if (ImGui::Button("Stop experiments"))
                s.experiments.stop(s.bench);
        }
        else {
            ImGui::InputText("Path file", benchPathFile_, sizeof(benchPathFile_));

            if (s.bench.mode() == BenchmarkHarness::IDLE) {
                if (ImGui::Button("Record"))
                    s.bench.beginRecord(s.dungeonSeed);   // the current seed goes into the path
                ImGui::SameLine();
                // Save and Load write their result in benchStatus_. The path is relative to the
                // working directory, which with Visual Studio is the build folder
                // (e.g. out/build/x64-Release/).
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
                    // a path only makes sense on its own dungeon: regenerate it if the seed differs
                    if (s.bench.path().seed != s.dungeonSeed) {
                        s.dungeonSeed = s.bench.path().seed;
                        buildWorld(s.dungeonSeed, s.params, s.lightingParams, s.scene, s.chainSystem, s.camera);
                        s.renderer.resetLightFades();
                        s.firstMouse = true;
                    }
                    // the file name says the configuration; nextFreeCsvPath adds _run2, _run3, ...
                    // if it already exists, so an older run is never overwritten
                    std::string csv = "benchmarks/benchmark_seed" + std::to_string(s.dungeonSeed)
                                    + (s.cullingEnabled ? "_cullON" : "_cullOFF") + ".csv";
                    csv = nextFreeCsvPath(csv);
                    bool ok = s.bench.beginReplay(csv, s.dungeonSeed, s.cullingEnabled, Renderer::MAX_SHADOW_LIGHTS);
                    benchStatus_ = ok ? ("Replaying, logging -> " + csv)
                                     : "REPLAY FAILED (empty path, or CSV could not be opened)";
                }

                // Automatic sweeps: replay the same path once per configuration, one CSV each.
                // Every button does the same steps: regenerate the path's dungeon if needed, save
                // the user settings (main restores them at the end), fill the config list, start.
                // In every sweep only the tested setting changes, the others keep the user value.

                // culling ON / OFF, optionally crossed with SSAO ON / OFF
                ImGui::Separator();
                ImGui::Checkbox("Also sweep SSAO on/off (2x2)", &sweepSSAO_);
                if (ImGui::Button("Run experiments")) {
                    if (s.bench.path().seed != s.dungeonSeed) {
                        s.dungeonSeed = s.bench.path().seed;
                        buildWorld(s.dungeonSeed, s.params, s.lightingParams, s.scene, s.chainSystem, s.camera);
                        s.renderer.resetLightFades();
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
                    if (sweepSSAO_) {
                        s.experiments.configs.push_back({ "cullON_ssaoON", true, true, s.savedStructuralInstancing, s.savedInstanced, s.savedParticleCount });
                        s.experiments.configs.push_back({ "cullOFF_ssaoON", false, true, s.savedStructuralInstancing, s.savedInstanced, s.savedParticleCount });
                        s.experiments.configs.push_back({ "cullON_ssaoOFF", true, false, s.savedStructuralInstancing, s.savedInstanced, s.savedParticleCount });
                        s.experiments.configs.push_back({ "cullOFF_ssaoOFF", false, false, s.savedStructuralInstancing, s.savedInstanced, s.savedParticleCount });
                    }
                    else {
                        s.experiments.configs.push_back({ "cullON", true, s.savedSsao, s.savedStructuralInstancing, s.savedInstanced, s.savedParticleCount });
                        s.experiments.configs.push_back({ "cullOFF", false, s.savedSsao, s.savedStructuralInstancing, s.savedInstanced, s.savedParticleCount });
                    }
                    s.experiments.start(s.bench, s.dungeonSeed, Renderer::MAX_SHADOW_LIGHTS);
                    benchStatus_ = s.experiments.running()
                        ? ("Running " + std::to_string(s.experiments.count()) + " experiments (path = "
                           + std::to_string(s.bench.path().size()) + " keyframes)...")
                        : "Cannot run experiments: record or load a path first";
                }

                // particles: instanced (1 call) vs naive (1 call per particle)
                if (ImGui::Button("Run particle sweep")) {
                    if (s.bench.path().seed != s.dungeonSeed) {
                        s.dungeonSeed = s.bench.path().seed;
                        buildWorld(s.dungeonSeed, s.params, s.lightingParams, s.scene, s.chainSystem, s.camera);
                        s.renderer.resetLightFades();
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
                    s.experiments.configs.push_back({ "instancedON", s.savedCulling, s.savedSsao, s.savedStructuralInstancing, true, s.particleCount });
                    s.experiments.configs.push_back({ "instancedOFF", s.savedCulling, s.savedSsao, s.savedStructuralInstancing, false, s.particleCount });
                    s.experiments.start(s.bench, s.dungeonSeed, Renderer::MAX_SHADOW_LIGHTS);
                    benchStatus_ = s.experiments.running()
                        ? ("Running particle sweep (" + std::to_string(s.particleCount) + " particles)...")
                        : "Cannot run: record or load a path first";
                }

                // structural slabs: 3 instanced calls vs one call per slab. draw_calls drops, the
                // triangles stay the same. It keeps the user's culling: run it with culling ON.
                if (ImGui::Button("Run structural instancing sweep")) {
                    if (s.bench.path().seed != s.dungeonSeed) {
                        s.dungeonSeed = s.bench.path().seed;
                        buildWorld(s.dungeonSeed, s.params, s.lightingParams, s.scene, s.chainSystem, s.camera);
                        s.renderer.resetLightFades();
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
                    s.experiments.configs.push_back({ "structInstON", s.savedCulling, s.savedSsao, true, s.savedInstanced, s.savedParticleCount });
                    s.experiments.configs.push_back({ "structInstOFF", s.savedCulling, s.savedSsao, false, s.savedInstanced, s.savedParticleCount });
                    s.experiments.start(s.bench, s.dungeonSeed, Renderer::MAX_SHADOW_LIGHTS);
                    benchStatus_ = s.experiments.running()
                        ? "Running structural instancing sweep (ON vs OFF)..."
                        : "Cannot run: record or load a path first";
                }

                // number of shadow lights: SPOT caster budget 1, 2, 4, 6, 8 (each caster is one
                // more depth pass). Compare frame_ms with shadow_lights / shadow_passes.
                if (ImGui::Button("Run shadow-light sweep")) {
                    if (s.bench.path().seed != s.dungeonSeed) {
                        s.dungeonSeed = s.bench.path().seed;
                        buildWorld(s.dungeonSeed, s.params, s.lightingParams, s.scene, s.chainSystem, s.camera);
                        s.renderer.resetLightFades();
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
                    const int spotSteps[] = { 1, 2, 4, 6, 8 };
                    for (int n : spotSteps) {
                        if (n > Renderer::MAX_SPOT_SHADOWS)
                            continue;
                        s.experiments.configs.push_back({ "shadowSpot" + std::to_string(n),
                            s.savedCulling, s.savedSsao, s.savedStructuralInstancing, s.savedInstanced,
                            s.savedParticleCount, n, s.savedMaxPointShadows });
                    }
                    s.experiments.start(s.bench, s.dungeonSeed, Renderer::MAX_SHADOW_LIGHTS);
                    benchStatus_ = s.experiments.running()
                        ? "Running shadow-light sweep (SPOT casters 1..8)..."
                        : "Cannot run: record or load a path first";
                }

                // fog quality: ray-march steps 4 to 64 (the HUD slider max). The fog must be
                // enabled, otherwise the step count does nothing.
                if (ImGui::Button("Run fog sweep")) {
                    if (s.bench.path().seed != s.dungeonSeed) {
                        s.dungeonSeed = s.bench.path().seed;
                        buildWorld(s.dungeonSeed, s.params, s.lightingParams, s.scene, s.chainSystem, s.camera);
                        s.renderer.resetLightFades();
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
                if (ImGui::Button("Stop recording"))
                    s.bench.stopRecord();
            }
            else {   // manual replay
                if (ImGui::Button("Stop replay"))
                    s.bench.stopReplay();
            }
        }

        // result of the last action, with the file it used
        if (!benchStatus_.empty()) {
            ImGui::Separator();
            ImGui::TextWrapped("%s", benchStatus_.c_str());
        }
        ImGui::End();
    }

    // Particles window, with the instanced / naive switch.
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
    // main uses it to show the end-of-batch message in the Benchmark window
    void setStatus(const std::string& msg) {
        benchStatus_ = msg;
    }
};
