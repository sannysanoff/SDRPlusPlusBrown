// SPDX-License-Identifier: MIT
// Copyright (C) 2026 DeepFist integration for SDR++ Brown
//
// TinyCW CW model. Backend: FastCW single band (tinycw_fastcw.c), a plain
// C forward pass over weights compiled into the binary.
// Input : spectrogram [batch, 1, 65, T] float32 (conditioned to the main tone)
// Output: log_probs   [T', batch, 48]  float32

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tinycw {

class Model {
public:
    Model();
    ~Model();

    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;

    // Loads (or reloads) the model. Returns false and fills `err` on failure.
    // weightsPath: a ".weights" file overrides the embedded FastCW weights;
    // an empty path uses them.
    bool load(const std::string& weightsPath, std::string& err);
    void unload();

    bool ready() const { return ready_; }
    const std::string& path() const { return path_; }

    // logProbs: out, T_out * classCount() floats, time-major (batch 1).
    bool infer(const float* spectrogram, int freqBins, int frames,
               std::vector<float>& logProbs, int& outFrames, std::string& err);

    // Batched inference. Input layout [batch, 1, freqBins, frames], output
    // [outFrames, batch, classes] -> logProbs is time-major, batch index inside
    // each frame: logProbs[(t * batch + b) * classes + c]. One session run for
    // the whole batch instead of one run per window.
    bool inferBatch(const float* spectrogram, int batch, int freqBins, int frames,
                    std::vector<float>& logProbs, int& outFrames, std::string& err);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool ready_ = false;
    std::string path_;
};

} // namespace tinycw