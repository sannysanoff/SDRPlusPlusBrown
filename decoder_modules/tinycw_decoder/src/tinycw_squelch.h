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

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#include "tinycw_fft.h"

namespace tinycw {

constexpr double kSquelchFrameMs   = 10.0;
constexpr double kSquelchLoBelowHz = 150.0;   // band = pitch - 150 .. pitch + 100 (550..800 at 700 Hz)
constexpr double kSquelchHiAboveHz = 100.0;
constexpr double kSquelchThreshold = 12.0;

// Keying ratio for one window. ~4 = no signal, tens = keyed CW.
// As squelch.py keying_ratio() at the 48 kHz it was made for: 20 ms frames
// with a 10 ms step (noverlap = nper - hop), magnitudes scaled by
// 1/sum(window) like scipy. squelch.py raises the frame to 512 samples, which
// at 3200 Hz is 160 ms and averages 20+ WPM keying flat; here the frame is
// 2 * hop rounded up to a power of two (64 samples, 50 Hz bins). The audio is
// first scaled to unit RMS, so the result does not depend on the SDR level.
// The division guard is 1e-3 of the median in-band noise (p10), not a fixed
// value: unit RMS is set by the whole channel, so a strong neighbour outside
// the band pushes the in-band noise far below any fixed floor.
inline double keyingRatio(const float* audio, int n, int sr, double pitchHz = 700.0) {
    if (audio == nullptr || n < 256) { return 0.0; }

    int hop = (int)(sr * kSquelchFrameMs / 1000.0);
    if (hop < 1) { hop = 1; }
    int nper = 32;
    while (nper < hop * 2) { nper *= 2; }
    if (nper > n) { return 0.0; }

    const int step = hop;

    const int loBin = std::max(0, (int)std::ceil((pitchHz - kSquelchLoBelowHz) * nper / (double)sr));
    const int hiBin = std::min(nper / 2, (int)std::floor((pitchHz + kSquelchHiAboveHz) * nper / (double)sr));
    if (hiBin <= loBin) { return 0.0; }
    const int nBins = hiBin - loBin + 1;

    double ss = 0.0;
    for (int i = 0; i < n; i++) { ss += (double)audio[i] * (double)audio[i]; }
    const double rms = std::sqrt(ss / (double)n);
    if (!(rms > 0.0)) { return 0.0; }

    Fft fft(nper);
    const std::vector<double> win = hannPeriodic(nper);
    double winSum = 0.0;
    for (double w : win) { winSum += w; }
    const double scale = 1.0 / (rms * winSum);
    std::vector<std::complex<double>> buf((size_t)nper);

    std::vector<std::vector<float>> mag((size_t)nBins);
    int frames = 0;
    for (int start = 0; start + nper <= n; start += step) {
        for (int i = 0; i < nper; i++) {
            buf[(size_t)i] = std::complex<double>((double)audio[start + i] * win[(size_t)i] * scale, 0.0);
        }
        fft.forward(buf.data());
        for (int b = 0; b < nBins; b++) {
            mag[(size_t)b].push_back((float)std::abs(buf[(size_t)(loBin + b)]));
        }
        frames++;
    }
    if (frames < 4) { return 0.0; }

    // Sum over 3 adjacent bins (a CW tone spans a couple of bins).
    std::vector<double> tmp((size_t)frames);
    std::vector<double> p10s((size_t)nBins), p90s((size_t)nBins);
    for (int b = 0; b < nBins; b++) {
        for (int t = 0; t < frames; t++) {
            double s = 0.0;
            for (int k = b - 1; k <= b + 1; k++) {
                if (k < 0 || k >= nBins) { continue; }
                s += mag[(size_t)k][(size_t)t];
            }
            tmp[(size_t)t] = s;
        }
        std::sort(tmp.begin(), tmp.end());
        p10s[(size_t)b] = percentile(tmp.data(), frames, 0.10);
        p90s[(size_t)b] = percentile(tmp.data(), frames, 0.90);
    }
    std::vector<double> floors = p10s;
    std::nth_element(floors.begin(), floors.begin() + (long)(floors.size() / 2), floors.end());
    const double eps = std::max(1e-12, 1e-3 * floors[floors.size() / 2]);
    double best = 0.0;
    for (int b = 0; b < nBins; b++) {
        const double ratio = p90s[(size_t)b] / (p10s[(size_t)b] + eps);
        if (ratio > best) { best = ratio; }
    }
    return best;
}

inline bool hasSignal(const float* audio, int n, int sr, double thresh = kSquelchThreshold,
                      double pitchHz = 700.0) {
    return keyingRatio(audio, n, sr, pitchHz) >= thresh;
}

} // namespace tinycw