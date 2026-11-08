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

    // Drops expired entries and copies the live ones into `out`.
    void collect(double now, std::vector<Marker>& out) {
        out.clear();
        for (size_t i = 0; i < entries_.size();) {
            if (now >= entries_[i].carrierUntil && now >= entries_[i].decodeUntil) {
                entries_.erase(entries_.begin() + (long)i);
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
    };

    void note(double freqHz, double now, bool decoded) {
        for (Entry& e : entries_) {
            if (std::fabs(e.freqHz - freqHz) < 60.0) {
                e.freqHz = freqHz;
                e.carrierUntil = now + holdSec_;
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