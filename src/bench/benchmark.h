#pragma once

// BenchmarkHarness: records and replays a CameraPath, and during the replay writes the metrics
// of every frame to a CSV file. Replaying the same path with different settings and comparing
// the CSVs (in MATLAB) is how we measure the optimizations.
//
// It is a small state machine: IDLE, RECORDING or REPLAYING. main only calls beginFrame() at the
// top of the loop and endFrame() after rendering; the HUD Benchmark panel drives the rest.
// It only reads the metrics, it never changes the renderer.

#include <string>
#include <fstream>
#include <filesystem>

#include "engine/camera.h"
#include "core/metrics.h"
#include "bench/camera_path.h"

class BenchmarkHarness {
public:
    enum Mode { IDLE, RECORDING, REPLAYING };

    Mode mode() const {
        return mode_;
    }

    const char* modeName() const {
        return mode_ == RECORDING ? "RECORDING" : (mode_ == REPLAYING ? "REPLAYING" : "idle");
    }

    // true during a replay: the path decides the camera pose, so main must skip the normal
    // input and the collisions for this frame
    bool drivingCamera() const {
        return mode_ == REPLAYING;
    }

    const CameraPath& path() const {
        return path_;
    }

    float replayTimeMs() const {
        return replayTimeMs_;
    }

    // While recording we store a keyframe every sampleIntervalMs, not one per frame: the path
    // file has the same size whatever the fps, and the replay interpolates between samples.
    float sampleIntervalMs = 20.0f;   // about 50 samples per second

    // Start recording a new path. The seed is stored in the path, so the replay knows which
    // dungeon to regenerate.
    void beginRecord(unsigned int seed) {
        path_.clear();
        path_.seed = seed;
        recordTimeMs_ = 0.0f;
        lastSampleMs_ = 0.0f;
        mode_ = RECORDING;
    }

    void stopRecord() {
        if (mode_ == RECORDING)
            mode_ = IDLE;
    }

    // Start replaying the current path and open the CSV log. seed / culling / shadowBudget only go
    // in the CSV header, so every file says which configuration produced it.
    // Returns false (and changes nothing) if the path is empty or the CSV cannot be opened.
    bool beginReplay(const std::string& csvFilename, unsigned int seed, bool cullingOn, int shadowBudget) {
        if (path_.empty())
            return false;
        ensureParentDir(csvFilename);
        csv_.open(csvFilename);
        if (!csv_)
            return false;

        // NDEBUG is defined only in Release builds. Real measures must come from Release (the
        // MSVC Debug build is much slower), so we write down which build made this file.
    #ifdef NDEBUG
        const char* build = "Release";
    #else
        const char* build = "Debug";
    #endif
        // metadata line starting with '#', so MATLAB / pandas can skip it as a comment
        csv_ << "# seed=" << seed
             << " culling=" << (cullingOn ? "ON" : "OFF")
             << " shadow_budget=" << shadowBudget
             << " build=" << build << "\n";
        csv_ << "frame,tMs,frame_ms,fps_smoothed,draw_calls,objects_total,objects_culled,"
                "triangles,lights_active,shadow_lights,shadow_passes,fog_steps,"
                "particles,particle_draw_calls,"
                "cam_x,cam_y,cam_z,yaw,pitch\n";

        replayTimeMs_ = 0.0f;
        replayFrame_ = 0;
        mode_ = REPLAYING;
        return true;
    }

    void stopReplay() {
        if (mode_ == REPLAYING) {
            csv_.close();
            mode_ = IDLE;
        }
    }

    // Called at the top of the loop. During a replay it advances the replay clock, samples the
    // path and puts that pose on the camera; at the end of the path it stops the replay.
    void beginFrame(Camera& camera, float dt) {
        if (mode_ != REPLAYING)
            return;

        // We do NOT advance the clock on the first replay frame: its dt still contains the
        // hitch of loading / regenerating the dungeon, and it would make us jump far into the
        // path. So frame 0 stays at t = 0 and is not logged (see endFrame).
        if (replayFrame_ > 0)
            replayTimeMs_ += dt * 1000.0f;

        glm::vec3 pos;
        float yaw, pitch;
        if (!path_.sample(replayTimeMs_, pos, yaw, pitch)) {
            stopReplay();
            return;
        }

        // Put the camera on the sampled pose. updateVectors() is private, so we set Yaw / Pitch
        // and call processMouse(0, 0): with a zero offset the angles stay as we set them and only
        // Front / Right / Up are rebuilt. This way we did not have to change camera.h.
        camera.Position = pos;
        camera.Yaw = yaw;
        camera.Pitch = pitch;
        camera.processMouse(0.0f, 0.0f);
    }

    // Called after rendering, when the metrics of this frame are ready. During a replay it
    // writes one CSV row; while recording it stores a keyframe every sampleIntervalMs.
    void endFrame(const Camera& camera, const FrameMetrics& metrics, float dt) {
        if (mode_ == REPLAYING) {
            if (replayFrame_ > 0)   // frame 0 is the hitch frame, see beginFrame
                logRow(camera, metrics, dt);
            replayFrame_++;
        }
        else if (mode_ == RECORDING) {
            // the first keyframe is always stored, then one every sampleIntervalMs
            if (path_.empty() || recordTimeMs_ - lastSampleMs_ >= sampleIntervalMs) {
                path_.addKeyframe(recordTimeMs_, camera.Position, camera.Yaw, camera.Pitch);
                lastSampleMs_ = recordTimeMs_;
            }
            recordTimeMs_ += dt * 1000.0f;
        }
    }

    bool savePath(const std::string& f) const {
        ensureParentDir(f);
        return path_.save(f);
    }

    bool loadPath(const std::string& f) {
        return path_.load(f);
    }

private:
    // Create the folder of the file (e.g. "benchmarks/") if it is missing, otherwise the ofstream
    // cannot open the file. With the error_code version a failure does not throw: the open()
    // after it simply fails and we return false.
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
             << (dt * 1000.0f) << ","   // frame_ms: the real time of this frame, used in the analysis
             << m.fps << ","            // fps_smoothed: ImGui's averaged fps, only for reference
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
    int replayFrame_ = 0;
};

// If `basePath` (e.g. "benchmarks/benchmark_seed12345_cullON.csv") does not exist, return it as
// it is. If it exists, return the first free "..._run2.csv", "..._run3.csv", ... instead.
// So running the same configuration again keeps the old CSV, and we can repeat each config a few
// times to estimate the measurement noise (the path is fixed, the noise comes from the system).
inline std::string nextFreeCsvPath(const std::string& basePath) {
    std::error_code ec;
    if (!std::filesystem::exists(basePath, ec))
        return basePath;

    // add "_runN" before the ".csv" extension
    std::filesystem::path p(basePath);
    std::filesystem::path dir = p.parent_path();
    std::string stem = p.stem().string();       // file name without extension
    std::string ext = p.extension().string();   // ".csv"
    for (int run = 2; run < 1000; ++run) {
        std::filesystem::path candidate = dir / (stem + "_run" + std::to_string(run) + ext);
        if (!std::filesystem::exists(candidate, ec))
            return candidate.string();
    }
    return basePath;   // 999 runs already: give up and overwrite
}
