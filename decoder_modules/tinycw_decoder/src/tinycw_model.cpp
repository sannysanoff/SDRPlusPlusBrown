// SPDX-License-Identifier: MIT
// Copyright (C) 2026 DeepFist integration for SDR++ Brown
//
// tinycw::Model backed by FastCW (tinycw_fastcw.c): a plain C forward pass
// over weights compiled into the binary, no runtime library to load.
// Same contract: spectrogram [batch, 65, T] in, log probs [T', batch, 48] out.

// Windows.h defines min/max as macros; the DSP code below uses std::min/std::max.
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "tinycw_model.h"
#include "tinycw_fastcw.h"

#include <mutex>

namespace tinycw {

namespace {
// FastCW keeps its scratch in static buffers: one pass at a time.
std::mutex& fastcwMutex() {
    static std::mutex m;
    return m;
}

bool endsWith(const std::string& s, const std::string& tail) {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}
} // namespace

struct Model::Impl {};

Model::Model() {}

Model::~Model() { unload(); }

void Model::unload() {
    ready_ = false;
    path_.clear();
}

// weightsPath: a ".weights" file overrides the embedded weights; an empty path
// (or anything else) uses the embedded ones.
bool Model::load(const std::string& weightsPath, std::string& err) {
    std::lock_guard<std::mutex> lck(fastcwMutex());
    unload();
    if (endsWith(weightsPath, ".weights")) {
        if (fastcw_load_file(weightsPath.c_str()) != 0) {
            err = "cannot read FastCW weights: " + weightsPath;
            return false;
        }
        path_ = weightsPath;
    }
    else {
        fastcw_init();
        path_ = "embedded FastCW single-band weights";
    }
    ready_ = true;
    return true;
}

bool Model::infer(const float* spectrogram, int freqBins, int frames,
                  std::vector<float>& logProbs, int& outFrames, std::string& err) {
    return inferBatch(spectrogram, 1, freqBins, frames, logProbs, outFrames, err);
}

bool Model::inferBatch(const float* spectrogram, int batch, int freqBins, int frames,
                       std::vector<float>& logProbs, int& outFrames, std::string& err) {
    if (!ready_) {
        err = "model not loaded";
        return false;
    }
    if (freqBins != 65 || frames < 1 || frames > fastcw_max_frames() || batch < 1) {
        err = "unsupported input shape";
        return false;
    }
    outFrames = fastcw_frame_count(frames);
    logProbs.resize((size_t)outFrames * (size_t)batch * 48);
    std::lock_guard<std::mutex> lck(fastcwMutex());
    fastcw_infer(spectrogram, logProbs.data(), batch, frames);
    return true;
}

} // namespace tinycw
