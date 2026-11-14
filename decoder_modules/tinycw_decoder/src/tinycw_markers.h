// SPDX-License-Identifier: MIT
// Copyright (C) 2026 DeepFist integration for SDR++ Brown
//
// Waterfall marker bookkeeping: keeps a carrier (keyed CW) flag and a decode
// flag per frequency for `holdSec` after the last event. Kept separate from the
// decoder channels so a marker stays visible after its channel closes.

#pragma once

#include <cmath>
#include <vector>

namespace tinycw {

struct Marker {
    double freqHz = 0.0;
    bool carrier = false;   // yellow: keyed CW present recently
    bool decode = false;    // cyan: text decoded recently
};

class MarkerTracker {
public:
    explicit MarkerTracker(double holdSec = 5.0) : holdSec_(holdSec) {}

    void setHold(double holdSec) { holdSec_ = holdSec; }
    double hold() const { return holdSec_; }

    void noteCarrier(double freqHz, double now) { note(freqHz, now, false); }
    void noteDecode(double freqHz, double now) { note(freqHz, now, true); }

    // A channel closed by the detection gate: its yellow dot goes at once, and
    // a decode still in flight for it does not bring the dot back for blockSec.
    // A new channel at the same frequency (noteCarrier) lifts the block.
    void dropCarrier(double freqHz, double now, double blockSec) {
        for (Entry& e : entries_) {
            if (std::fabs(e.freqHz - freqHz) < 60.0) {
                e.carrierUntil = -1.0;
                e.carrierBlockUntil = now + blockSec;
            }
        }
    }

    // Drops expired entries and copies the live ones into `out`.
    void collect(double now, std::vector<Marker>& out) {
        out.clear();
        for (size_t i = 0; i < entries_.size();) {
            if (now >= entries_[i].carrierUntil && now >= entries_[i].decodeUntil) {
                // A blocked entry stays (not drawn) until its block ends.
                if (now >= entries_[i].carrierBlockUntil) {
                    entries_.erase(entries_.begin() + (long)i);
                }
                else {
                    i++;
                }
                continue;
            }
            Marker m;
            m.freqHz = entries_[i].freqHz;
            m.carrier = (now < entries_[i].carrierUntil);
            m.decode = (now < entries_[i].decodeUntil);
            out.push_back(m);
            i++;
        }
    }

    void clear() { entries_.clear(); }
    size_t size() const { return entries_.size(); }

private:
    struct Entry {
        double freqHz = 0.0;
        double carrierUntil = -1.0;
        double decodeUntil = -1.0;
        double carrierBlockUntil = -1.0;
    };

    void note(double freqHz, double now, bool decoded) {
        for (Entry& e : entries_) {
            if (std::fabs(e.freqHz - freqHz) < 60.0) {
                e.freqHz = freqHz;
                if (!decoded) { e.carrierBlockUntil = -1.0; }
                if (now >= e.carrierBlockUntil) { e.carrierUntil = now + holdSec_; }
                if (decoded) { e.decodeUntil = now + holdSec_; }
                return;
            }
        }
        Entry e;
        e.freqHz = freqHz;
        e.carrierUntil = now + holdSec_;
        e.decodeUntil = decoded ? (now + holdSec_) : -1.0;
        entries_.push_back(e);
    }

    double holdSec_;
    std::vector<Entry> entries_;
};

} // namespace tinycw