#pragma once

// ExperimentRunner: plays the loaded benchmark path once per configuration, unattended, writing
// one CSV per run. This automates the core experiment of the project (frustum culling ON vs OFF,
// optionally crossed with SSAO on/off): press one button and it runs every config back to back, so
// the numbers all come from the EXACT same camera path with only the tested knob changing between
// runs. That is what makes the runs comparable.
//
// It reuses the BenchmarkHarness for the actual replay + logging: for each config it calls
// beginReplay() (with that config's name in the CSV file), waits for the harness to finish the
// path, then moves on to the next config. The knobs themselves (culling, SSAO) live in main / the
// renderer, so main reads currentConfig() each frame and applies them - the runner stays decoupled
// from how the app is actually drawn.

#include <string>
#include <vector>
#include "bench/benchmark.h"

// One configuration to measure. Add more runtime knobs here as they become available (e.g. a
// shadow-light budget, once that is a runtime value instead of a compile-time constant).
struct ExperimentConfig {
    std::string name;   // goes into the CSV file name, and tells the configs apart
    bool culling;       // frustum culling ON/OFF for this run
    bool ssao;          // SSAO ON/OFF for this run
};

class ExperimentRunner {
public:
    std::vector<ExperimentConfig> configs;   // the batch to run, filled by main before start()

    bool running() const { return running_; }
    int  currentIndex() const { return index_; }
    int  count() const { return (int)configs.size(); }
    const ExperimentConfig& currentConfig() const { return configs[index_]; }

    // Begin the batch. seed / shadowBudget go into every CSV header; seed also names the files.
    // Does nothing (running() stays false) if there is no config or no path loaded.
    void start(const BenchmarkHarness& bench, unsigned int seed, int shadowBudget) {
        if (configs.empty() || bench.path().empty()) return;
        seed_ = seed;
        shadowBudget_ = shadowBudget;
        index_ = 0;
        started_ = false;
        running_ = true;
    }

    // Stop the batch early (aborts the current run's CSV, which stays as far as it got).
    void stop(BenchmarkHarness& bench) {
        if (running_) { bench.stopReplay(); running_ = false; }
    }

    // Call once per frame at the TOP of the loop, before bench.beginFrame(). Starts the current
    // config's replay, and when a replay finishes advances to the next config until none are left.
    void update(BenchmarkHarness& bench) {
        if (!running_) return;

        // while a run is playing there is nothing to do: the harness logs each frame, and
        // bench.beginFrame() ends the replay by itself when the path is over.
        if (bench.mode() == BenchmarkHarness::REPLAYING) return;

        // no run is playing: the previous config just finished (started_), or we are at the start
        if (started_) {
            index_++;          // move on to the next config
            started_ = false;
        }
        if (index_ >= (int)configs.size()) {   // all configs done -> batch finished
            running_ = false;
            return;
        }
        // launch the run for configs[index_]
        std::string csv = "benchmarks/benchmark_seed" + std::to_string(seed_)
                        + "_" + configs[index_].name + ".csv";
        bench.beginReplay(csv, seed_, configs[index_].culling, shadowBudget_);
        started_ = true;
    }

private:
    bool running_ = false;
    bool started_ = false;   // has the current index's run been launched yet?
    int  index_ = 0;
    unsigned int seed_ = 0;
    int  shadowBudget_ = 0;
};
