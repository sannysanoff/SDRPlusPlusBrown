// SPDX-License-Identifier: MIT
// Copyright (C) 2026 DeepFist integration for SDR++ Brown
//
// Port of DeepFist `tools/squelch.py`: a keying-based signal gate.
//
// A steady tone (receiver AGC artefact) or noise has a flat envelope, so the
// per-bin p90/p10 amplitude ratio stays near 4. Real CW is keyed, so its tone
// bin swings from loud to the noise floor and the ratio reaches tens.
// Threshold ~12 separates the two.

#pragma once

#include <cmath>
#include <complex>
#include <vector>

#include "tinycw_fft.h"

namespace tinycw {

constexpr double kSquelchFrameMs   = 10.0;
constexpr double kSquelchLoHz      = 550.0;
constexpr double kSquelchHiHz      = 800.0;
constexpr double kSquelchThreshold = 12.0;

// Keying ratio for one window. ~4 = no signal, tens = keyed CW.
inline double keyingRatio(const float* audio, int n, int sr) {
    if (audio == nullptr || n < 256) { return 0.0; }

    int hop = (int)(sr * kSquelchFrameMs / 1000.0);
    if (hop < 1) { hop = 1; }
    int nper = hop * 2;
    if (nper < 512) { nper = 512; }
    if (nper > n) { nper = n; }
    if (nper < 32) { return 0.0; }

    const int step = nper - hop;
    if (step < 1) { return 0.0; }

    const int loBin = (int)std::ceil(kSquelchLoHz * nper / (double)sr);
    const int hiBin = (int)std::floor(kSquelchHiHz * nper / (double)sr);
    if (hiBin <= loBin) { return 0.0; }
    const int nBins = hiBin - loBin + 1;

    Fft fft(nper);
    const std::vector<double> win = hannPeriodic(nper);
    std::vector<std::complex<double>> buf((size_t)nper);

    std::vector<std::vector<float>> mag((size_t)nBins);
    int frames = 0;
    for (int start = 0; start + nper <= n || frames == 0; start += step) {
        for (int i = 0; i < nper; i++) {
            const int idx = start + i;
            const double s = (idx < n) ? (double)audio[idx] : 0.0;
            buf[(size_t)i] = std::complex<double>(s * win[i], 0.0);
        }
        fft.forward(buf.data());
        for (int b = 0; b < nBins; b++) {
            const std::complex<double>& z = buf[(size_t)(loBin + b)];
            mag[(size_t)b].push_back((float)std::sqrt(z.real() * z.real() + z.imag() * z.imag()));
        }
        frames++;
        if (frames > 20000) { break; }
    }
    if (frames < 4) { return 0.0; }

    // Sum over 3 adjacent bins (a CW tone spans a couple of bins).
    std::vector<float> smoothed((size_t)frames);
    std::vector<double> tmp((size_t)frames);
    double best = 0.0;
    for (int b = 0; b < nBins; b++) {
        for (int t = 0; t < frames; t++) {
            float s = 0.0f;
            for (int k = b - 1; k <= b + 1; k++) {
                if (k < 0 || k >= nBins) { continue; }
                s += mag[(size_t)k][(size_t)t];
            }
            smoothed[(size_t)t] = s;
        }
        for (int t = 0; t < frames; t++) { tmp[(size_t)t] = (double)smoothed[(size_t)t]; }
        std::sort(tmp.begin(), tmp.end());
        const double p10 = percentile(tmp.data(), frames, 0.10);
        const double p90 = percentile(tmp.data(), frames, 0.90);
        const double ratio = p90 / (p10 + 1e-3);
        if (ratio > best) { best = ratio; }
    }
    return best;
}

inline bool hasSignal(const float* audio, int n, int sr, double thresh = kSquelchThreshold) {
    return keyingRatio(audio, n, sr) >= thresh;
}

} // namespace tinycw