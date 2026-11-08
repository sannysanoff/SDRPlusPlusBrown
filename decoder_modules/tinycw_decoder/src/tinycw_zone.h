// SPDX-License-Identifier: MIT
// Copyright (C) 2026 DeepFist integration for SDR++ Brown
//
// Commit zone: how the characters of overlapping windows become one text.
//
// FastCW places each character as a sharp CTC spike. Its frame moves by one or
// two 30 ms frames from one window to the next, because every window does its
// own tempo search and resampling. Averaging the frame probabilities of many
// windows then splits one character into two peaks, and CTC prints it twice.
//
// So every window commits only the characters inside its own zone:
//   zone end   = window end - lag   (the character has `lag` s of right context)
//   zone start = the previous zone end of this decoder
// Zones tile the time axis, so each character comes from exactly one window.
// A character that moves across a zone border can still appear twice: the same
// symbol within kZoneDupSec of the last committed one is dropped.
#pragma once

#include <vector>

namespace tinycw {

constexpr double kZoneDupSec = 0.15;   // < the shortest character period at 40 WPM
constexpr double kZoneLagSec = 2.0;    // default right context of a committed character
constexpr double kZoneEdgeSec = 0.4;   // never commit from the first 0.4 s of a window

struct ZoneState {
    double zoneEndSec = -1.0;   // end of the last committed zone, -1 = none yet
    double lastCharSec = -1e9;  // time of the last committed character
    int    lastId = -1;
};

struct ZoneChar {
    int    id;
    double sec;    // absolute time
    int    frame;  // output frame in the window
};

// ids/frames: greedy CTC of one window (blank-free ids, frame of each).
// t0: window start (s). firstSec: the earliest time the zone may start
// (used when nothing was committed before, or after a gap).
inline std::vector<ZoneChar> commitZone(ZoneState& st, const std::vector<int>& ids,
                                        const std::vector<int>& frames, double t0,
                                        double framePeriod, double windowSec, double lagSec,
                                        double firstSec) {
    const double hi = t0 + windowSec - lagSec;
    double lo = st.zoneEndSec;
    if (lo < firstSec) { lo = firstSec; }       // first window, or windows were skipped
    std::vector<ZoneChar> out;
    if (hi <= lo) { return out; }
    for (size_t i = 0; i < ids.size() && i < frames.size(); i++) {
        const double sec = t0 + (double)frames[i] * framePeriod;
        if (sec < lo || sec >= hi) { continue; }
        if (ids[i] == st.lastId && sec - st.lastCharSec < kZoneDupSec) { continue; }
        out.push_back({ ids[i], sec, frames[i] });
        st.lastId = ids[i];
        st.lastCharSec = sec;
    }
    st.zoneEndSec = hi;
    return out;
}

} // namespace tinycw
