#pragma once

// ExperimentRunner: replays the loaded path once per configuration, one run after the other
// without any click, and writes one CSV per run. All the numbers then come from exactly the same
// camera path, and only the tested setting changes between two runs.
//
// The HUD buttons fill `configs` for each experiment: culling ON/OFF (optionally crossed with
// SSAO), particle instancing, structural instancing, number of shadow lights, fog steps.
//
// The runner does the replay through the BenchmarkHarness: for each config it calls
// beginReplay() with a CSV named after the config, waits for the path to end, then goes on.
// It does not apply the settings itself: main reads currentConfig() every frame and sets culling,
// SSAO, ... on the app, so the runner does not need to know how the scene is drawn.

#include <string>
#include <vector>
#include "bench/benchmark.h"

// One configuration to measure.
struct ExperimentConfig {
    std::string name;            // used in the CSV file name
    bool culling;                // frustum culling on/off
    bool ssao;                   // SSAO on/off
    bool structuralInstanced;    // floor/wall/ceiling drawn instanced (3 calls) or one call per slab
    bool instanced;              // particles instanced (1 call) or one call per particle
    int particleCount;
    // Shadow caster budget for this run (the "number of lights" sweep). 99 means "full budget":
    // the renderer clamps it to the compile-time maximum, so the other sweeps run with all
    // shadows on.
    int maxSpotShadows = 99;
    int maxPointShadows = 99;
    // Fog ray-march steps for this run (the "fog quality vs FPS" sweep). -1 means "leave the
    // user's current value", so only the fog sweep changes it.
    int fogSteps = -1;
};

class ExperimentRunner {
public:
    std::vector<ExperimentConfig> configs;   // the batch, filled by the HUD before start()

    bool running() const {
        return running_;
    }

    int currentIndex() const {
        return index_;
    }

    int count() const {
        return (int)configs.size();
    }

    const ExperimentConfig& currentConfig() const {
        return configs[index_];
    }

    // Start the batch. seed and shadowBudget go into every CSV header, and the seed also into the
    // file names. Does nothing if there are no configs or no path is loaded.
    void start(const BenchmarkHarness& bench, unsigned int seed, int shadowBudget) {
        if (configs.empty() || bench.path().empty())
            return;
        seed_ = seed;
        shadowBudget_ = shadowBudget;
        index_ = 0;
        started_ = false;
        running_ = true;
    }

    // Stop the batch early. The CSV of the current run keeps the rows written so far.
    void stop(BenchmarkHarness& bench) {
        if (running_) {
            bench.stopReplay();
            running_ = false;
        }
    }

    // Called once per frame at the top of the loop, before bench.beginFrame(). It starts the
    // replay of the current config and, when that replay is over, moves to the next config.
    void update(BenchmarkHarness& bench) {
        if (!running_)
            return;

        // a run is playing: nothing to do, the harness logs every frame and stops by itself at
        // the end of the path
        if (bench.mode() == BenchmarkHarness::REPLAYING)
            return;

        // no run playing: either the previous config just finished (started_) or we just began
        if (started_) {
            index_++;
            started_ = false;
        }
        if (index_ >= (int)configs.size()) {
            running_ = false;   // all configs done
            return;
        }
        // start the run of configs[index_]. nextFreeCsvPath adds _run2, _run3, ... if the file
        // already exists, so running the same sweep again does not overwrite the old CSVs.
        std::string csv = "benchmarks/benchmark_seed" + std::to_string(seed_)
                        + "_" + configs[index_].name + ".csv";
        csv = nextFreeCsvPath(csv);
        bench.beginReplay(csv, seed_, configs[index_].culling, shadowBudget_);
        started_ = true;
    }

private:
    bool running_ = false;
    bool started_ = false;   // has the run of the current index been started?
    int index_ = 0;
    unsigned int seed_ = 0;
    int shadowBudget_ = 0;
};
