// SPDX-License-Identifier: MIT
// Copyright (C) 2026 DeepFist integration for SDR++ Brown
//
// Port of DeepFist `deepfist/features/conditioner.py`
// (itself the vectorized twin of the Rust live conditioner):
//   AGC (unit RMS) -> tone AFC (FFT peak in 400..1200 Hz) ->
//   complex downconvert -> two cascaded 1-pole LPFs (45 Hz) ->
//   re-center to 600 Hz -> peak normalize.
//
// The model saw conditioned audio in training, so this stage is mandatory.

#pragma once

#include <cmath>
#include <complex>
#include <vector>

#include "tinycw_fft.h"

namespace tinycw {

constexpr int    kCondSampleRate = 3200;
constexpr int    kToneFftSize    = 4096;
constexpr double kOutPitchHz     = 600.0;
constexpr double kCondBandwidth  = 90.0;
constexpr double kCondBandLoHz   = 400.0;
constexpr double kCondBandHiHz   = 1200.0;

inline double detectTone(const float* audio, int n, int sr = kCondSampleRate) {
    const int m = (n < kToneFftSize) ? n : kToneFftSize;
    if (m < 8) { return kOutPitchHz; }

    static Fft fft(kToneFftSize);
    const std::vector<double> win = hannSymmetric(m);

    std::vector<std::complex<double>> buf(kToneFftSize, std::complex<double>(0.0, 0.0));
    for (int i = 0; i < m; i++) { buf[i] = std::complex<double>((double)audio[i] * win[i], 0.0); }
    fft.forward(buf.data());

    int best = -1;
    double bestMag = -1.0;
    for (int k = 0; k <= kToneFftSize / 2; k++) {
        const double hz = (double)k * (double)sr / (double)kToneFftSize;
        if (hz < kCondBandLoHz || hz > kCondBandHiHz) { continue; }
        const double mag = std::sqrt(buf[k].real() * buf[k].real() + buf[k].imag() * buf[k].imag());
        if (mag > bestMag) { bestMag = mag; best = k; }
    }
    if (best < 0) { return kOutPitchHz; }
    return (double)best * (double)sr / (double)kToneFftSize;
}

class Conditioner {
public:
    // Returns audio at the same rate (3200 Hz), peak ~1.0.
    // Mirrors the Python behaviour: windows shorter than 4096 samples are
    // passed through untouched.
    // toneHz > 0 forces the tone (the channel's main tone, tinycw_mainband.h)
    // instead of the AFC peak, like conditioner.py condition(tone_hz=...).
    void process(const float* audio, int n, std::vector<float>& out, int sr = kCondSampleRate,
                 double toneHz = 0.0) {
        out.resize((size_t)n);
        if (n < kToneFftSize) {
            for (int i = 0; i < n; i++) { out[i] = audio[i]; }
            return;
        }

        // 1. AGC: unit RMS.
        double acc = 0.0;
        for (int i = 0; i < n; i++) { acc += (double)audio[i] * (double)audio[i]; }
        const double rms = std::sqrt(acc / (double)n) + 1e-9;
        std::vector<double> x(n);
        for (int i = 0; i < n; i++) { x[i] = (double)audio[i] / rms; }

        // 2. Tone AFC on the head of the window.
        const double tone = (toneHz > 0.0) ? toneHz : detectTone(audio, n, sr);

        // 3. Complex downconvert + two cascaded 1-pole low-pass filters.
        const double alpha = 1.0 - std::exp(-2.0 * kPi * (kCondBandwidth * 0.5) / (double)sr);
        const double dOmega = -2.0 * kPi * tone / (double)sr;
        const double rOmega = 2.0 * kPi * kOutPitchHz / (double)sr;

        std::complex<double> lp1(0.0, 0.0), lp2(0.0, 0.0);
        std::vector<double> re(n);
        double peak = 0.0;
        for (int k = 0; k < n; k++) {
            const double phaseD = dOmega * (double)k;
            const std::complex<double> rot(std::cos(phaseD), std::sin(phaseD));
            const std::complex<double> bb = x[k] * rot;
            lp1 = alpha * bb + (1.0 - alpha) * lp1;
            lp2 = alpha * lp1 + (1.0 - alpha) * lp2;

            const double phaseU = rOmega * (double)k;
            const std::complex<double> up = lp2 * std::complex<double>(std::cos(phaseU), std::sin(phaseU));
            re[k] = up.real();
            const double a = std::fabs(re[k]);
            if (a > peak) { peak = a; }
        }

        // 4. Peak normalize.
        const double norm = 1.0 / (peak + 1e-9);
        for (int i = 0; i < n; i++) { out[i] = (float)(re[i] * norm); }
    }
};

} // namespace tinycw