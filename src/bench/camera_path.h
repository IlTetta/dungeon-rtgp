#pragma once

// CameraPath: a recorded, replayable first-person walkthrough of the dungeon.
//
// The whole point of the benchmark is to measure the SAME journey through the dungeon under
// different settings (culling on/off, number of shadow lights, ...). For that we need a camera
// path that is reproducible: we record it once and then replay the exact same trajectory as
// many times as we want.
//
// A camera pose is fully described by 5 numbers: the position (x,y,z) and the two look angles
// Yaw and Pitch (see engine/camera.h). So a path is just a list of such poses, each stamped
// with the time (in milliseconds, from the start of the recording) at which it was captured.
//
// IMPORTANT - why the path is parameterised by TIME and not by frame index: on replay we
// advance a clock and sample the path at that time, so the same spatial trajectory is covered
// in the same wall-clock duration no matter how many frames per second the machine manages.
// A slower configuration simply renders FEWER frames of the identical journey, which is exactly
// what keeps the runs comparable for the benchmark. (One-sample-per-frame would instead depend
// on the frame rate and would not be reproducible across configurations.)

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <glm/glm.hpp>

// One recorded camera pose at a given time.
struct CameraKeyframe {
    float tMs;       // milliseconds since the recording started
    glm::vec3 pos;   // camera position
    float yaw;       // horizontal look angle (degrees), same convention as Camera
    float pitch;     // vertical look angle (degrees)
};

class CameraPath {
public:
    // The seed of the dungeon this path was recorded on. Stored in the file so the replay can
    // regenerate the SAME dungeon: a path only makes sense on the map it was recorded on.
    unsigned int seed = 0;

    void clear() { keyframes.clear(); }               // start a fresh recording
    bool empty() const { return keyframes.empty(); }
    int  size()  const { return (int)keyframes.size(); }

    // total length of the path in milliseconds (0 if empty)
    float durationMs() const { return keyframes.empty() ? 0.0f : keyframes.back().tMs; }

    // append one pose to the path (used while recording)
    void addKeyframe(float tMs, const glm::vec3& pos, float yaw, float pitch) {
        keyframes.push_back({ tMs, pos, yaw, pitch });
    }

    // Sample the path at time tMs, writing the interpolated pose into the out-params.
    // Returns false when tMs is past the end of the path (the replay is finished) or the path
    // is empty; true otherwise.
    bool sample(float tMs, glm::vec3& outPos, float& outYaw, float& outPitch) const {
        if (keyframes.empty()) return false;
        if (tMs >= keyframes.back().tMs) return false;   // past the end -> replay done

        // before (or exactly at) the first keyframe: just sit on the start pose
        if (tMs <= keyframes.front().tMs) {
            outPos   = keyframes.front().pos;
            outYaw   = keyframes.front().yaw;
            outPitch = keyframes.front().pitch;
            return true;
        }

        // Find the segment [a, b] that contains tMs and linearly interpolate inside it. A plain
        // linear scan is fine here: a path is a few hundred/thousand keyframes and this runs once
        // per frame, so the cost is negligible (no need for a fancier search).
        for (size_t i = 0; i + 1 < keyframes.size(); ++i) {
            const CameraKeyframe& a = keyframes[i];
            const CameraKeyframe& b = keyframes[i + 1];
            if (tMs <= b.tMs) {
                float span  = b.tMs - a.tMs;
                float alpha = (span > 0.0f) ? (tMs - a.tMs) / span : 0.0f;
                outPos   = glm::mix(a.pos, b.pos, alpha);          // lerp the position
                outYaw   = a.yaw   + (b.yaw   - a.yaw)   * alpha;  // lerp the angles
                outPitch = a.pitch + (b.pitch - a.pitch) * alpha;
                return true;
            }
        }
        return false;   // should never get here (the past-the-end case is handled above)
    }

    // Save the path to a text file. Format (whitespace separated, easy to read and inspect):
    //   # comment lines start with '#'
    //   seed <n>
    //   <tMs> <px> <py> <pz> <yaw> <pitch>     (one line per keyframe)
    // Returns false if the file could not be opened.
    bool save(const std::string& filename) const {
        std::ofstream out(filename);
        if (!out) return false;
        out << std::setprecision(7);
        out << "# dungeon-rtgp camera path v1\n";
        out << "seed " << seed << "\n";
        out << "# tMs px py pz yaw pitch\n";
        for (const CameraKeyframe& k : keyframes) {
            out << k.tMs << " "
                << k.pos.x << " " << k.pos.y << " " << k.pos.z << " "
                << k.yaw << " " << k.pitch << "\n";
        }
        return true;
    }

    // Load a path written by save(), replacing the current keyframes. Returns false if the file
    // could not be opened.
    bool load(const std::string& filename) {
        std::ifstream in(filename);
        if (!in) return false;
        keyframes.clear();
        seed = 0;
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;   // skip comments and blank lines
            std::istringstream ss(line);
            std::string first;
            ss >> first;
            if (first == "seed") {          // the "seed <n>" header line
                ss >> seed;
                continue;
            }
            // otherwise it is a keyframe line: re-parse it from the start as 6 numbers
            CameraKeyframe k;
            std::istringstream ks(line);
            ks >> k.tMs >> k.pos.x >> k.pos.y >> k.pos.z >> k.yaw >> k.pitch;
            keyframes.push_back(k);
        }
        return true;
    }

private:
    std::vector<CameraKeyframe> keyframes;
};
