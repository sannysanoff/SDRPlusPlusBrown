// SPDX-License-Identifier: MIT
// Copyright (C) 2026 DeepFist integration for SDR++ Brown
//
// Small dependency-free radix-2 FFT + window helpers.
// Kept header-only and double-precision so the DSP core stays portable
// (macOS / Linux / Windows / Android) and testable without the GUI.

#pragma once

#include <cmath>
#include <complex>
#include <stdexcept>
#include <vector>
#include <algorithm>

namespace tinycw {

inline bool isPow2(int n) { return n > 0 && (n & (n - 1)) == 0; }

static constexpr double kPi = 3.14159265358979323846;

// In-place iterative radix-2 FFT (double precision). n must be a power of two.
class Fft {
public:
    Fft() {}
    explicit Fft(int n) { init(n); }

    void init(int n) {
        if (!isPow2(n)) { throw std::runtime_error("[tinycw] FFT size must be a power of two"); }
        _n = n;
        int bits = 0;
        while ((1 << bits) < n) { bits++; }

        _rev.resize(n);
        for (int i = 0; i < n; i++) {
            int r = 0;
            for (int b = 0; b < bits; b++) {
                if (i & (1 << b)) { r |= 1 << (bits - 1 - b); }
            }
            _rev[i] = r;
        }

        _tw.resize(n / 2);
        for (int i = 0; i < n / 2; i++) {
            double a = -2.0 * kPi * i / n;
            _tw[i] = std::complex<double>(std::cos(a), std::sin(a));
        }
    }

    int size() const { return _n; }

    void forward(std::complex<double>* a) const {
        const int n = _n;
        for (int i = 0; i < n; i++) {
            int j = _rev[i];
            if (j > i) { std::swap(a[i], a[j]); }
        }
        for (int len = 2; len <= n; len <<= 1) {
            const int half = len >> 1;
            const int step = n / len;
            for (int i = 0; i < n; i += len) {
                for (int k = 0; k < half; k++) {
                    const std::complex<double> w = _tw[k * step];
                    const std::complex<double> u = a[i + k];
                    const std::complex<double> v = a[i + k + half] * w;
                    a[i + k] = u + v;
                    a[i + k + half] = u - v;
                }
            }
        }
    }

private:
    int _n = 0;
    std::vector<int> _rev;
    std::vector<std::complex<double>> _tw;
};

// Periodic Hann (torch.hann_window default, scipy fftbins=True).
inline std::vector<double> hannPeriodic(int n) {
    std::vector<double> w(n);
    for (int i = 0; i < n; i++) { w[i] = 0.5 * (1.0 - std::cos(2.0 * kPi * i / n)); }
    return w;
}

// Symmetric Hann (numpy.hanning).
inline std::vector<double> hannSymmetric(int n) {
    std::vector<double> w(n);
    if (n <= 1) {
        if (n == 1) { w[0] = 1.0; }
        return w;
    }
    for (int i = 0; i < n; i++) { w[i] = 0.5 * (1.0 - std::cos(2.0 * kPi * i / (n - 1))); }
    return w;
}

// Linear-interpolated percentile, matching numpy.percentile defaults.
inline double percentile(const double* v, int n, double q) {
    if (n <= 0) { return 0.0; }
    if (n == 1) { return v[0]; }
    const double pos = q * (n - 1);
    int lo = (int)std::floor(pos);
    if (lo < 0) { lo = 0; }
    if (lo > n - 1) { lo = n - 1; }
    int hi = lo + 1;
    if (hi > n - 1) { hi = n - 1; }
    const double frac = pos - lo;
    return v[lo] + (v[hi] - v[lo]) * frac;
}

} // namespace tinycw