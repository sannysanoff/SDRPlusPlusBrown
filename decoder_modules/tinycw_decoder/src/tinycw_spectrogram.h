// SPDX-License-Identifier: MIT
// Copyright (C) 2026 DeepFist integration for SDR++ Brown
//
// Port of DeepFist `deepfist/features/spectrogram.py`.
//
// The model was trained on this exact front-end, so every constant and the
// padding/window geometry must match torch:
//   torch.stft(n_fft=256, hop=48, window=hann(periodic), center=True,
//              pad_mode="reflect", return_complex=True)  (no 1/N scaling)
//   mag = |stft|, slice bins 400..1200 Hz (65 bins at 12.5 Hz/bin)
//   spec = log1p(mag), then global standardize (mean over all bins AND frames)

#pragma once

#include <cmath>
#include <complex>
#include <vector>

#include "tinycw_fft.h"

namespace tinycw {

constexpr int   kSampleRate    = 3200;
constexpr int   kFftSize       = 256;
constexpr int   kHop           = 48;
constexpr double kBandLoHz     = 400.0;
constexpr double kBandHiHz     = 1200.0;
constexpr int   kFreqBins      = 65;    // bins 32..96 inclusive
constexpr int   kFreqBinLo     = 32;
constexpr int   kSamplesPerSec = kSampleRate;

// Frame count of torch.stft(center=True) for `n` samples.
inline int spectrogramFrames(int n) { return n / kHop + 1; }

class Spectrogram {
public:
    Spectrogram() : fft_(kFftSize), window_(hannPeriodic(kFftSize)) {}

    // audio: n samples at 3200 Hz.
    // out:   kFreqBins * frames floats, layout [freq][time] with time contiguous,
    //        matching the model's [1, 1, 65, T] input layout.
    bool compute(const float* audio, int n, std::vector<float>& out, int& frames) {
        if (audio == nullptr || n <= 0) { return false; }
        const int pad = kFftSize / 2;
        if (n <= pad) { return false; }

        frames = spectrogramFrames(n);
        out.resize((size_t)kFreqBins * (size_t)frames);

        std::complex<double> buf[kFftSize];
        double mags[kFreqBins];

        for (int t = 0; t < frames; t++) {
            const int start = t * kHop;
            for (int i = 0; i < kFftSize; i++) {
                const double s = (double)reflectSample(audio, n, start + i - pad, pad);
                buf[i] = std::complex<double>(s * window_[i], 0.0);
            }
            fft_.forward(buf);
            for (int b = 0; b < kFreqBins; b++) {
                const std::complex<double>& z = buf[kFreqBinLo + b];
                mags[b] = std::log1p(std::sqrt(z.real() * z.real() + z.imag() * z.imag()));
            }
            for (int b = 0; b < kFreqBins; b++) {
                out[(size_t)b * (size_t)frames + (size_t)t] = (float)mags[b];
            }
        }

        // Global standardize over the whole tile (torch .std() is sample std).
        const size_t total = out.size();
        double sum = 0.0;
        for (size_t i = 0; i < total; i++) { sum += out[i]; }
        const double mean = sum / (double)total;
        double acc = 0.0;
        for (size_t i = 0; i < total; i++) {
            const double d = (double)out[i] - mean;
            acc += d * d;
        }
        const double denom = (total > 1) ? (double)(total - 1) : 1.0;
        const double stddev = std::sqrt(acc / denom) + 1e-6;

        const float inv = (float)(1.0 / stddev);
        const float mu = (float)mean;
        for (size_t i = 0; i < total; i++) { out[i] = (out[i] - mu) * inv; }
        return true;
    }

private:
    // torch.nn.functional.pad(mode="reflect") index mapping (edge not repeated).
    static inline int reflectIndex(int i, int n) {
        if (n == 1) { return 0; }
        const int period = 2 * (n - 1);
        int k = i % period;
        if (k < 0) { k += period; }
        return (k < n) ? k : (period - k);
    }

    static inline float reflectSample(const float* audio, int n, int idx, int pad) {
        if (idx < 0 || idx >= n) { idx = reflectIndex(idx, n); }
        (void)pad;
        if (idx < 0) { idx = 0; }
        if (idx >= n) { idx = n - 1; }
        return audio[idx];
    }

    Fft fft_;
    std::vector<double> window_;
};

} // namespace tinycw