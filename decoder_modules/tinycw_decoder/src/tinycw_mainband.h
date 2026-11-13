// SPDX-License-Identifier: MIT
// Copyright (C) 2026 DeepFist integration for SDR++ Brown
//
// Main tone of one channel window: the rules the FastCW training clips were
// made with (DeepFist tools/screen_clips.py screen() and
// tools/decode_sets.py exact_tone()).
//
// 1. Band power: 160-point frames (50 ms, 20 Hz bands), 80-sample hop (25 ms),
//    symmetric Hann, at 3200 Hz.
// 2. Noise floor: median over time per band, then median over 100-1200 Hz.
// 3. On frame: >= 10 dB over the floor, a spectral peak, and within 30 dB of
//    the loudest band within 200 Hz. A band also owns the on frames of its
//    two adjacent bands.
// 4. Top 5: the most energetic local maxima of the mean spectrum, 100-1200 Hz
//    (narrowed by the caller to its own channel, see findMainTone).
// 5. Main band: the top band with the most present seconds (>= 10 % on
//    frames). Presence wins over energy; a tie goes to the more energetic band.
// 6. Tone: the peak of the Hann-windowed FFT of the whole window within
//    +-20 Hz of the main band centre.
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

#include "tinycw_fft.h"

namespace tinycw {

struct MainTone {
    bool found = false;      // false: no in-band spectral peak (no signal)
    double bandHz = 0.0;     // main band centre (20 Hz grid)
    double toneHz = 0.0;     // refined tone
    int presentSec = 0;      // present seconds of the main band
};

namespace mainband {
constexpr int    kMbSr = 3200;
constexpr int    kMbFrame = 160;
constexpr int    kMbHop = 80;
constexpr int    kMbBins = kMbFrame / 2 + 1;          // 0..1600 Hz
constexpr double kMbBandHz = (double)kMbSr / kMbFrame;  // 20 Hz
constexpr double kMbLoHz = 100.0, kMbHiHz = 1200.0;
constexpr double kMbOnDb = 10.0, kMbLeakDb = 30.0, kMbNearHz = 200.0, kMbPresentFrac = 0.10;
constexpr int    kMbTop = 5;
constexpr double kMbToneSearchHz = 20.0;
constexpr int    kMbToneFft = 1 << 16;              // >= 20 s at 3200 Hz

inline double median(std::vector<double> v) {
    if (v.empty()) { return 0.0; }
    const size_t h = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + (long)h, v.end());
    const double hi = v[h];
    if (v.size() % 2) { return hi; }
    return 0.5 * (hi + *std::max_element(v.begin(), v.begin() + (long)h));
}

// Present flag per whole second from an on mask over frames.
inline std::vector<uint8_t> secondsPresent(const std::vector<uint8_t>& on) {
    const int n = (int)on.size();
    if (n == 0) { return {}; }
    const int nsec = ((n - 1) * kMbHop + kMbFrame / 2) / kMbSr + 1;
    std::vector<int> cnt(nsec, 0), tot(nsec, 0);
    for (int t = 0; t < n; t++) {
        const int s = (t * kMbHop + kMbFrame / 2) / kMbSr;
        tot[s]++;
        cnt[s] += on[t];
    }
    std::vector<uint8_t> out(nsec, 0);
    for (int s = 0; s < nsec; s++) { out[s] = tot[s] && (double)cnt[s] / tot[s] >= kMbPresentFrac; }
    return out;
}
} // namespace mainband

// Power in dB [bins][frames] of 160-sample Hann frames (screen_clips.band_power).
inline std::vector<std::vector<double>> bandPowerDb(const float* x, int n) {
    using namespace mainband;
    // DFT tables [m][k] in float: the inner loop over the bins is a plain
    // multiply-add over contiguous memory, which the compiler vectorizes.
    // Float sums of 160 terms change the dB values by < 1e-3 dB.
    static const std::vector<float> win = [] {
        const std::vector<double> w = hannSymmetric(kMbFrame);
        return std::vector<float>(w.begin(), w.end());
    }();
    static const std::vector<float> tabCos = [] {
        std::vector<float> t((size_t)kMbFrame * kMbBins);
        for (int m = 0; m < kMbFrame; m++)
            for (int k = 0; k < kMbBins; k++)
                t[(size_t)m * kMbBins + k] = (float)std::cos(2.0 * kPi * k * m / kMbFrame);
        return t;
    }();
    static const std::vector<float> tabSin = [] {
        std::vector<float> t((size_t)kMbFrame * kMbBins);
        for (int m = 0; m < kMbFrame; m++)
            for (int k = 0; k < kMbBins; k++)
                t[(size_t)m * kMbBins + k] = (float)std::sin(2.0 * kPi * k * m / kMbFrame);
        return t;
    }();
    const int frames = 1 + std::max(0, n - kMbFrame) / kMbHop;
    std::vector<std::vector<double>> db(kMbBins, std::vector<double>((size_t)frames));
    float re[kMbBins], im[kMbBins];
    for (int t = 0; t < frames; t++) {
        std::fill(re, re + kMbBins, 0.0f);
        std::fill(im, im + kMbBins, 0.0f);
        for (int m = 0; m < kMbFrame; m++) {
            const int i = t * kMbHop + m;
            const float v = (i < n ? x[i] : 0.0f) * win[(size_t)m];
            if (v == 0.0f) { continue; }
            const float* c = &tabCos[(size_t)m * kMbBins];
            const float* s = &tabSin[(size_t)m * kMbBins];
            for (int k = 0; k < kMbBins; k++) {
                re[k] += v * c[k];
                im[k] -= v * s[k];
            }
        }
        for (int k = 0; k < kMbBins; k++) {
            const double p = (double)re[k] * re[k] + (double)im[k] * im[k];
            db[(size_t)k][(size_t)t] = 10.0 * std::log10(p + 1e-12);
        }
    }
    return db;
}

// FFT peak within +-20 Hz of centreHz over the whole window (decode_sets.exact_tone).
inline double exactTone(const float* x, int n, double centreHz) {
    using namespace mainband;
    const int m = std::min(n, kMbToneFft);
    static const Fft fft(kMbToneFft);
    std::vector<std::complex<double>> buf((size_t)kMbToneFft, std::complex<double>(0.0, 0.0));
    const std::vector<double> win = hannSymmetric(m);
    for (int i = 0; i < m; i++) { buf[(size_t)i] = (double)x[n - m + i] * win[(size_t)i]; }
    fft.forward(buf.data());
    const double res = (double)kMbSr / kMbToneFft;
    const int lo = std::max(0, (int)std::ceil((centreHz - kMbToneSearchHz) / res));
    const int hi = std::min(kMbToneFft / 2, (int)std::floor((centreHz + kMbToneSearchHz) / res));
    int best = -1;
    double bestMag = -1.0;
    for (int k = lo; k <= hi; k++) {
        const double mag = std::abs(buf[(size_t)k]);
        if (mag > bestMag) { bestMag = mag; best = k; }
    }
    return best < 0 ? centreHz : best * res;
}

// Audio at 3200 Hz. Candidate bands lie in [searchLoHz, searchHiHz]; the
// noise floor always uses 100-1200 Hz. A channel passes searchLo/Hi =
// pitch +- half the channel spacing, so a neighbour channel's station is not picked.
inline MainTone findMainTone(const float* x, int n, double searchLoHz = mainband::kMbLoHz,
                             double searchHiHz = mainband::kMbHiHz) {
    using namespace mainband;
    MainTone r;
    if (n < kMbFrame) { return r; }
    const auto db = bandPowerDb(x, n);
    const int frames = (int)db[0].size();
    auto hz = [](int b) { return b * kMbBandHz; };

    std::vector<double> med;
    for (int b = 0; b < kMbBins; b++) {
        if (hz(b) >= kMbLoHz && hz(b) <= kMbHiHz) { med.push_back(median(db[(size_t)b])); }
    }
    const double floorDb = median(med);

    // top bands: local maxima of the mean power spectrum
    std::vector<double> e(kMbBins, 0.0);
    for (int b = 0; b < kMbBins; b++) {
        double s = 0.0;
        for (double v : db[(size_t)b]) { s += std::pow(10.0, v / 10.0); }
        e[(size_t)b] = s / frames;
    }
    std::vector<int> order(kMbBins);
    for (int b = 0; b < kMbBins; b++) { order[(size_t)b] = b; }
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return e[(size_t)a] > e[(size_t)b]; });
    std::vector<int> tops;
    for (int b : order) {
        if (hz(b) < std::max(kMbLoHz, searchLoHz) || hz(b) > std::min(kMbHiHz, searchHiHz)) { continue; }
        const bool left = b == 0 || e[(size_t)b] >= e[(size_t)b - 1];
        const bool right = b == kMbBins - 1 || e[(size_t)b] >= e[(size_t)b + 1];
        if (left && right) { tops.push_back(b); }
        if ((int)tops.size() == kMbTop) { break; }
    }
    if (tops.empty()) { return r; }

    // on mask for the bands next to the top bands only
    const int near = (int)std::lround(kMbNearHz / kMbBandHz);
    auto onAt = [&](int b, int t) {
        if (b < 0 || b >= kMbBins) { return false; }
        const double v = db[(size_t)b][(size_t)t];
        if (v < floorDb + kMbOnDb) { return false; }
        if (b > 0 && v < db[(size_t)b - 1][(size_t)t]) { return false; }
        if (b < kMbBins - 1 && v < db[(size_t)b + 1][(size_t)t]) { return false; }
        double lm = -1e300;
        for (int k = std::max(0, b - near); k <= std::min(kMbBins - 1, b + near); k++) {
            lm = std::max(lm, db[(size_t)k][(size_t)t]);
        }
        return v >= lm - kMbLeakDb;
    };
    int best = -1, bestSec = -1;
    for (int b : tops) {
        std::vector<uint8_t> owned((size_t)frames, 0);
        for (int t = 0; t < frames; t++) { owned[(size_t)t] = onAt(b, t) || onAt(b - 1, t) || onAt(b + 1, t); }
        int sec = 0;
        for (uint8_t v : secondsPresent(owned)) { sec += v; }
        if (sec > bestSec) { bestSec = sec; best = b; }   // tops are in energy order: ties keep the first
    }
    // No band is present for a single second: noise, not a station. The
    // caller then uses its default pitch instead of a noise peak.
    if (bestSec <= 0) { return r; }
    r.found = true;
    r.bandHz = hz(best);
    r.presentSec = bestSec;
    r.toneHz = exactTone(x, n, r.bandHz);
    return r;
}

} // namespace tinycw
