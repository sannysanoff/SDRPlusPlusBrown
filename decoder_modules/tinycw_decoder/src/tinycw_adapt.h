// SPDX-License-Identifier: MIT
// Copyright (C) 2026 DeepFist integration for SDR++ Brown
//
// Adaptive decoded-bandwidth policy shared by the GUI module and the CLI so the
// behaviour that runs in the app is the behaviour under test.

#pragma once

#include <algorithm>
#include <cmath>

namespace tinycw {

struct AdaptiveWidth {
    double minHz = 2000.0;
    double ceilingHz = 20000.0;
    double currentHz = 20000.0;
    bool autoMode = true;

    // In auto mode start narrow and let update() widen: on a first run the CPU
    // budget is unknown, and over-committing would drop audio until it narrows
    // again. In manual mode start at the configured ceiling.
    void reset(double ceiling, bool autoMode_ = true) {
        ceilingHz = ceiling;
        currentHz = autoMode_ ? minHz : ceiling;
    }

    // Narrows when the decode thread saturates, widens when there is headroom.
    // Returns true when the value moved by more than 1 Hz.
    bool update(double busyPercent) {
        if (!autoMode) { return false; }
        const double before = currentHz;
        if (busyPercent > 75.0) {
            currentHz = std::max(minHz, currentHz * 0.85);
        }
        else if (busyPercent < 45.0) {
            currentHz = std::min(ceilingHz, currentHz * 1.08 + 200.0);
        }
        currentHz = std::clamp(currentHz, minHz, ceilingHz);
        return std::fabs(currentHz - before) > 1.0;
    }
};

} // namespace tinycw