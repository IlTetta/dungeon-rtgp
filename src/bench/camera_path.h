#pragma once

// CameraPath: a recorded walkthrough of the dungeon that we can replay as many times as we want.
//
// The benchmark measures the SAME journey under different settings (culling on/off, number of
// shadow lights, fog steps, ...), so we record the path once and replay exactly the same
// trajectory for every configuration.
//
// A camera pose is fully described by 5 numbers: the position (x, y, z) and the two angles Yaw
// and Pitch (see engine/camera.h). A path is a list of these poses, each with the time in ms
// since the start of the recording.
//
// The path is indexed by TIME, not by frame: on replay we advance a clock and sample the path at
// that time. So the same trajectory always takes the same real time, whatever the fps: a slower
// configuration just renders fewer frames of the same journey. With one sample per frame the
// replay speed would depend on the frame rate, and the runs would not be comparable.

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <glm/glm.hpp>

// One recorded camera pose.
struct CameraKeyframe {
    float tMs;       // ms since the recording started
    glm::vec3 pos;   // camera position
    float yaw;       // degrees, same convention as Camera
    float pitch;     // degrees
};

class CameraPath {
public:
    // Seed of the dungeon this path was recorded on. It is saved in the file, so the replay can
    // regenerate the same dungeon first: a path only makes sense on its own map.
    unsigned int seed = 0;

    void clear() {
        keyframes.clear();
    }

    bool empty() const {
        return keyframes.empty();
    }

    int size() const {
        return (int)keyframes.size();
    }

    // length of the path in ms (0 if empty)
    float durationMs() const {
        return keyframes.empty() ? 0.0f : keyframes.back().tMs;
    }

    void addKeyframe(float tMs, const glm::vec3& pos, float yaw, float pitch) {
        keyframes.push_back({ tMs, pos, yaw, pitch });
    }

    // Sample the path at time tMs and write the interpolated pose in the out parameters.
    // Returns false when the path is empty or tMs is past its end (the replay is over).
    bool sample(float tMs, glm::vec3& outPos, float& outYaw, float& outPitch) const {
        if (keyframes.empty())
            return false;
        if (tMs >= keyframes.back().tMs)
            return false;

        // before the first keyframe: stay on the start pose
        if (tMs <= keyframes.front().tMs) {
            outPos = keyframes.front().pos;
            outYaw = keyframes.front().yaw;
            outPitch = keyframes.front().pitch;
            return true;
        }

        // Find the segment [a, b] that contains tMs and interpolate linearly inside it.
        // A linear scan is enough: a path has a couple of thousand keyframes and we sample it
        // once per frame.
        // Lerping the yaw is safe because the camera never wraps it to [0, 360): it just keeps
        // growing, so two consecutive keyframes are always close.
        for (size_t i = 0; i + 1 < keyframes.size(); ++i) {
            const CameraKeyframe& a = keyframes[i];
            const CameraKeyframe& b = keyframes[i + 1];
            if (tMs <= b.tMs) {
                float span = b.tMs - a.tMs;
                float alpha = (span > 0.0f) ? (tMs - a.tMs) / span : 0.0f;
                outPos = glm::mix(a.pos, b.pos, alpha);
                outYaw = a.yaw + (b.yaw - a.yaw) * alpha;
                outPitch = a.pitch + (b.pitch - a.pitch) * alpha;
                return true;
            }
        }
        return false;   // never reached, the past-the-end case is handled above
    }

    // Save the path as a text file:
    //   # comment lines start with '#'
    //   seed <n>
    //   <tMs> <px> <py> <pz> <yaw> <pitch>     (one line per keyframe)
    // Plain text so it is easy to read and to commit as a reference path.
    // Returns false if the file cannot be opened.
    bool save(const std::string& filename) const {
        std::ofstream out(filename);
        if (!out)
            return false;
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

    // Load a file written by save(), replacing the current keyframes.
    // Returns false if the file cannot be opened.
    bool load(const std::string& filename) {
        std::ifstream in(filename);
        if (!in)
            return false;
        keyframes.clear();
        seed = 0;
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#')
                continue;
            std::istringstream ss(line);
            std::string first;
            ss >> first;
            if (first == "seed") {
                ss >> seed;
                continue;
            }
            // a keyframe line: parse it again from the start as 6 numbers
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
