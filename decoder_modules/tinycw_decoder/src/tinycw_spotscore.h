// SPDX-License-Identifier: MIT
// Copyright (C) 2026 DeepFist integration for SDR++ Brown
//
// Channel detection gate. The scanner's criterion for a spot is its keying
// ratio p90/p10 of the bin magnitude over the last 3 s, in dB. Each spot keeps
// the maximum of that value over a sliding window (the channel idle time), so
// a station that pauses between overs keeps its channel. A channel opens and
// stays open only while that score is at or above the gate.
//
// The UI shows a sensitivity: sensitivity = kSpotGateMaxDb - gate. Moving it
// down raises the gate and closes the weaker channels at once.

#pragma once

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>

namespace tinycw {

constexpr double kSpotFloorDb = 15.0;       // the scanner tracks spots from here (noise is ~13 dB)
constexpr double kSpotGateMaxDb = 40.0;     // the least sensitive gate
constexpr double kSpotGateDefaultDb = 20.0; // ratio 10: the former fixed gate

inline double ratioDb(double ratio) {
    return ratio > 0.0 ? 20.0 * std::log10(ratio) : -std::numeric_limits<double>::infinity();
}

inline double clampGateDb(double gateDb) { return std::clamp(gateDb, kSpotFloorDb, kSpotGateMaxDb); }
inline double gateDbFromSensitivity(double sens) { return clampGateDb(kSpotGateMaxDb - sens); }
inline double sensitivityFromGateDb(double gateDb) { return kSpotGateMaxDb - clampGateDb(gateDb); }

// Maximum of (time, dB) samples within the last windowSec (monotonic deque).
class ScoreWindow {
public:
    void add(double t, double db, double windowSec) {
        while (!q_.empty() && q_.back().db <= db) { q_.pop_back(); }
        q_.push_back({ t, db });
        while (!q_.empty() && q_.front().t < t - windowSec) { q_.pop_front(); }
    }
    double max() const { return q_.empty() ? -std::numeric_limits<double>::infinity() : q_.front().db; }
    bool empty() const { return q_.empty(); }

private:
    struct Sample { double t; double db; };
    std::deque<Sample> q_;
};

} // namespace tinycw
