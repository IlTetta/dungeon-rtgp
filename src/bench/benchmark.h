#pragma once

// BenchmarkHarness: records / replays a CameraPath and, during replay, logs the per-frame
// performance metrics to a CSV file. This is the core of the M3 benchmark: it lets us drive the
// exact same journey through the dungeon under different settings and write down what the
// renderer did each frame, so we can compare the configurations offline (in a spreadsheet).
//
// It keeps all the benchmark logic OUT of main.cpp: main only creates one of these, calls
// beginFrame() at the top of the loop and endFrame() after rendering, and drives it from a small
// ImGui panel. It never touches the renderer (Lorenzo's code) nor the shared metrics struct.

#include <string>
#include <fstream>
#include <filesystem>   // create the output folder (e.g. "benchmarks/") before writing into it

#include "engine/camera.h"
#include "core/metrics.h"
#include "bench/camera_path.h"

class BenchmarkHarness {
public:
    enum Mode { IDLE, RECORDING, REPLAYING };

    // --- queries used by main / the HUD ---
    Mode mode() const { return mode_; }
    const char* modeName() const {
        return mode_ == RECORDING ? "RECORDING" : (mode_ == REPLAYING ? "REPLAYING" : "idle");
    }
    // true while WE are driving the camera (replay): main must then SKIP its normal input +
    // wall-collision step, because the recorded path is the authoritative pose this frame.
    bool drivingCamera() const { return mode_ == REPLAYING; }

    const CameraPath& path() const { return path_; }
    float replayTimeMs() const { return replayTimeMs_; }

    // How often (in ms) we capture a keyframe while recording. We do NOT capture one per frame:
    // sampling at a fixed rate keeps the path file the same size no matter the recording
    // machine's fps, and replay interpolates between samples anyway.
    float sampleIntervalMs = 20.0f;   // ~50 samples per second

    // --- recording ---
    // Start recording a new path on the given dungeon seed (stored in the path so the replay
    // knows which dungeon to regenerate).
    void beginRecord(unsigned int seed) {
        path_.clear();
        path_.seed = seed;
        recordTimeMs_ = 0.0f;
        lastSampleMs_ = 0.0f;
        mode_ = RECORDING;
    }
    void stopRecord() { if (mode_ == RECORDING) mode_ = IDLE; }

    // --- replay + logging ---
    // Start replaying the current path and open a CSV log. The extra args describe the
    // configuration being measured; they go into the CSV header so each file is self-describing.
    // Returns false (and changes nothing) if the path is empty or the CSV cannot be opened.
    bool beginReplay(const std::string& csvFilename,
                     unsigned int seed, bool cullingOn, int shadowBudget) {
        if (path_.empty()) return false;
        ensureParentDir(csvFilename);   // make "benchmarks/" (or whatever folder) if it is missing
        csv_.open(csvFilename);
        if (!csv_) return false;

        // NDEBUG is defined only in Release builds. The measures that count must be taken in
        // Release (Debug MSVC is much slower and not representative), so we record which one
        // produced this file.
    #ifdef NDEBUG
        const char* build = "Release";
    #else
        const char* build = "Debug";
    #endif
        // metadata header: a '#' comment line, so tools like pandas can skip it with comment='#'
        csv_ << "# seed=" << seed
             << " culling=" << (cullingOn ? "ON" : "OFF")
             << " shadow_budget=" << shadowBudget
             << " build=" << build << "\n";
        // column header
        csv_ << "frame,tMs,frame_ms,fps_smoothed,draw_calls,objects_total,objects_culled,"
                "triangles,lights_active,shadow_lights,shadow_passes,fog_steps,"
                "particles,particle_draw_calls,"
                "cam_x,cam_y,cam_z,yaw,pitch\n";

        replayTimeMs_ = 0.0f;
        replayFrame_  = 0;
        mode_ = REPLAYING;
        return true;
    }
    void stopReplay() { if (mode_ == REPLAYING) { csv_.close(); mode_ = IDLE; } }

    // Called at the TOP of the render loop, before the normal input step. In replay it advances
    // the replay clock, samples the path and writes the pose into `camera`; when the path is over
    // it stops the replay (and main regains manual control the same frame).
    void beginFrame(Camera& camera, float dt) {
        if (mode_ != REPLAYING) return;

        // Advance the replay clock, but NOT on the very first replay frame: that frame's dt still
        // includes the load/regenerate hitch, and using it would jump us far into the path (and
        // would be a bogus frame time). So frame 0 sits at t=0 and is not logged (see endFrame).
        if (replayFrame_ > 0)
            replayTimeMs_ += dt * 1000.0f;

        glm::vec3 pos; float yaw, pitch;
        if (!path_.sample(replayTimeMs_, pos, yaw, pitch)) {
            stopReplay();   // reached the end of the path
            return;
        }

        // Drive the camera to the sampled pose. Yaw/Pitch are public; processMouse(0,0) is the
        // existing public way to rebuild Front/Right/Up from them (updateVectors() is private, and
        // adding a pose setter would mean touching the shared engine/camera.h). A zero mouse delta
        // leaves Yaw/Pitch as we just set them and only refreshes the direction vectors.
        camera.Position = pos;
        camera.Yaw      = yaw;
        camera.Pitch    = pitch;
        camera.processMouse(0.0f, 0.0f);
    }

    // Called AFTER rendering (the metrics are filled by then). In replay it logs one CSV row; in
    // recording it captures a keyframe from the current camera at the fixed sample rate.
    void endFrame(const Camera& camera, const FrameMetrics& metrics, float dt) {
        if (mode_ == REPLAYING) {
            // skip logging frame 0 (its frame time is the startup/hitch frame, see beginFrame)
            if (replayFrame_ > 0)
                logRow(camera, metrics, dt);
            replayFrame_++;
        }
        else if (mode_ == RECORDING) {
            // capture a keyframe every sampleIntervalMs of recorded time (always the first one)
            if (path_.empty() || recordTimeMs_ - lastSampleMs_ >= sampleIntervalMs) {
                path_.addKeyframe(recordTimeMs_, camera.Position, camera.Yaw, camera.Pitch);
                lastSampleMs_ = recordTimeMs_;
            }
            recordTimeMs_ += dt * 1000.0f;
        }
    }

    // save / load the path (thin wrappers; main passes the filename from an ImGui text field)
    bool savePath(const std::string& f) const { ensureParentDir(f); return path_.save(f); }
    bool loadPath(const std::string& f)       { return path_.load(f); }

private:
    // Create the folder a file lives in (e.g. "benchmarks/") if it does not exist yet, so the
    // ofstream that follows can actually open the file. Uses the error_code overload so a failure
    // is just ignored (the open() will then fail and report false) instead of throwing.
    static void ensureParentDir(const std::string& filename) {
        std::filesystem::path p(filename);
        if (p.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(p.parent_path(), ec);
        }
    }

    void logRow(const Camera& camera, const FrameMetrics& m, float dt) {
        csv_ << replayFrame_ << ","
             << replayTimeMs_ << ","
             << (dt * 1000.0f) << ","   // frame_ms: the honest per-frame time (from deltaTime)
             << m.fps << ","            // fps_smoothed: ImGui's smoothed value (for reference only)
             << m.drawCalls << ","
             << m.objectsTotal << ","
             << m.objectsCulled << ","
             << m.trianglesDrawn << ","
             << m.activeLights << ","
             << m.shadowLights << ","
             << m.shadowPasses << ","
             << m.fogSteps << ","
             << m.particlesDrawn << ","
             << m.particleDrawCalls << ","
             << camera.Position.x << "," << camera.Position.y << "," << camera.Position.z << ","
             << camera.Yaw << "," << camera.Pitch << "\n";
    }

    Mode mode_ = IDLE;
    CameraPath path_;
    std::ofstream csv_;

    // recording clock
    float recordTimeMs_ = 0.0f;
    float lastSampleMs_ = 0.0f;

    // replay clock
    float replayTimeMs_ = 0.0f;
    int   replayFrame_  = 0;
};

// If `basePath` (e.g. "benchmarks/benchmark_seed12345_cullON.csv") does not exist yet, return it
// unchanged. If it DOES, return the first free "..._run2.csv" / "..._run3.csv" / ... so replaying the
// SAME configuration again does not overwrite the previous CSV. This lets us repeat each config a few
// times to estimate the measurement NOISE (the path is deterministic; the variance comes from the
// system) - see analysis_notes. The base file is implicitly "run 1", extra runs start at 2.
inline std::string nextFreeCsvPath(const std::string& basePath) {
    std::error_code ec;
    if (!std::filesystem::exists(basePath, ec)) return basePath;   // first run: keep the plain name

    // insert "_runN" just before the ".csv" extension
    std::filesystem::path p(basePath);
    std::filesystem::path dir = p.parent_path();
    std::string stem = p.stem().string();        // filename without the extension
    std::string ext  = p.extension().string();   // ".csv"
    for (int run = 2; run < 1000; ++run) {
        std::filesystem::path candidate = dir / (stem + "_run" + std::to_string(run) + ext);
        if (!std::filesystem::exists(candidate, ec))
            return candidate.string();
    }
    return basePath;   // give up after 999 runs (should never happen) and overwrite
}
