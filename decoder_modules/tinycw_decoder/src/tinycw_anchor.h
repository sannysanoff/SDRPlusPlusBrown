// SPDX-License-Identifier: MIT
// Copyright (C) 2026 DeepFist integration for SDR++ Brown
//
// Keeps the focused row of the decodes window at the same screen position
// when rows appear or disappear above it. The list has a blank pad of one
// window height above and below the rows, so the scroll can always move.
#pragma once

#include <algorithm>
#include <cmath>

namespace tinycw {

struct RowAnchor {
    bool init = false;
    float pad = 0.0f;       // blank pad above the rows, pixels
    int idx = -1;           // index of the focused row in the last frame
    double freqHz = 0.0;    // its frequency

    // Returns the scroll for this frame. idx = index of the focused row now
    // (-1 = none), freqHz = its frequency, newPad = the pad for this frame.
    // The same row is the one within kSameRowHz of the last frequency.
    float update(float scroll, int newIdx, double newFreqHz, float newPad, float rowH) {
        constexpr double kSameRowHz = 60.0;
        if (!init) {
            init = true;
            scroll = newPad;   // start with the pad out of view
        }
        else {
            scroll += newPad - pad;   // a window resize changes the pad
            if (newIdx >= 0 && idx >= 0 && newIdx != idx && std::fabs(newFreqHz - freqHz) < kSameRowHz) {
                scroll += (float)(newIdx - idx) * rowH;
            }
        }
        pad = newPad;
        idx = newIdx;
        freqHz = newFreqHz;
        return std::max(0.0f, scroll);
    }
};

} // namespace tinycw
