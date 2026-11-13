// SPDX-License-Identifier: MIT
// Copyright (C) 2026 DeepFist integration for SDR++ Brown
//
// Typical CW segments per amateur band (IARU Region 1 band plan, 2021).
// Hard-coded defaults, overridable per band in tinycw_config.json.
//
// 160m 1810-1838, 80m 3500-3570, 60m 5354-5366, 40m 7000-7040 (widened to
// 7060 in this module to cover contest activity), 30m 10100-10130,
// 20m 14000-14070, 17m 18068-18095, 15m 21000-21070, 12m 24890-24915,
// 10m 28000-28070, 6m 50000-50100, 2m 144000-144150.

#pragma once

#include <string>
#include <vector>

namespace tinycw {

struct CwBandDef {
    const char* name;
    double loHz;
    double hiHz;
};

inline const std::vector<CwBandDef>& cwBandTable() {
    static const std::vector<CwBandDef> t = {
        { "160m", 1810000.0,  1838000.0  },
        { "80m",  3500000.0,  3570000.0  },
        { "60m",  5354000.0,  5366000.0  },
        { "40m",  7000000.0,  7060000.0  },   // contest-wide on purpose
        { "30m",  10100000.0, 10130000.0 },
        { "20m",  14000000.0, 14070000.0 },
        { "17m",  18068000.0, 18095000.0 },
        { "15m",  21000000.0, 21070000.0 },
        { "12m",  24890000.0, 24915000.0 },
        { "10m",  28000000.0, 28070000.0 },
        { "6m",   50000000.0, 50100000.0 },
        { "2m",   144000000.0, 144150000.0 },
    };
    return t;
}

inline const CwBandDef* findBand(const std::string& name) {
    for (const CwBandDef& b : cwBandTable()) {
        if (name == b.name) { return &b; }
    }
    return nullptr;
}

struct RangeResolution {
    bool valid = false;
    std::string bandName;

    double segLoHz = 0.0;       // full CW segment of the band
    double segHiHz = 0.0;
    double spanLoHz = 0.0;      // what the current source can actually see
    double spanHiHz = 0.0;
    double settingLoHz = 0.0;   // user setting for this band (clamped to segment)
    double settingHiHz = 0.0;
    double loHz = 0.0;          // effective decode range
    double hiHz = 0.0;

    bool segmentPartial = false;  // only part of the CW segment is in view
    bool settingClipped = false;  // setting narrowed by span / max width
    bool limitedByMaxWidth = false;
};

inline double clampd(double v, double lo, double hi) { return (v < lo) ? lo : ((v > hi) ? hi : v); }

// Which band the operator is actually listening to.
//
// Only bands that overlap the visible span are candidates, but the lowest of
// them is the wrong pick: on a wide source (10 MHz span or a zoomed-out view)
// the view covers 60m/40m/30m at once, and "lowest" then decodes 60m no matter
// where the VFO is. Tuning from 7 MHz to 14 MHz moved the pick from 60m to 30m
// and never touched 20m, which looked exactly like "nothing new appears".
// The band that contains the centre wins; if the centre sits between bands the
// nearest edge does.
inline const CwBandDef* pickBand(double centerHz, double spanLoHz, double spanHiHz) {
    const CwBandDef* best = nullptr;
    double bestDist = 1e30;
    for (const CwBandDef& b : cwBandTable()) {
        if (b.loHz >= spanHiHz || b.hiHz <= spanLoHz) { continue; }   // not in view
        double d = 0.0;
        if (centerHz < b.loHz) { d = b.loHz - centerHz; }
        else if (centerHz > b.hiHz) { d = centerHz - b.hiHz; }
        if (d < bestDist) { bestDist = d; best = &b; }
    }
    return best;
}

// Returns the effective CW decode range for the current view.
//
// `userLoHz`/`userHiHz` are the per-band slider values; pass 0/0 for defaults.
// `maxWidthHz` is the global CPU guard rail (0 = unlimited).
// `pickHz` selects the band (the tuned VFO); 0 = the view centre.
inline RangeResolution resolveRange(double centerHz, double spanHz,
                                    double maxWidthHz,
                                    double userLoHz = 0.0, double userHiHz = 0.0,
                                    double pickHz = 0.0) {
    RangeResolution r;
    r.spanLoHz = centerHz - spanHz * 0.5;
    r.spanHiHz = centerHz + spanHz * 0.5;

    // The band the VFO (or the centre) is in (see pickBand: not the lowest one in view).
    const CwBandDef* band = pickBand(pickHz > 0.0 ? pickHz : centerHz, r.spanLoHz, r.spanHiHz);
    if (band == nullptr) { return r; }

    r.valid = true;
    r.bandName = band->name;
    r.segLoHz = band->loHz;
    r.segHiHz = band->hiHz;

    const double availLo = (band->loHz > r.spanLoHz) ? band->loHz : r.spanLoHz;
    const double availHi = (band->hiHz < r.spanHiHz) ? band->hiHz : r.spanHiHz;
    if (availHi <= availLo) { r.valid = false; return r; }
    r.segmentPartial = (availLo > band->loHz + 0.5) || (availHi < band->hiHz - 0.5);

    double setLo = (userLoHz > 0.0) ? userLoHz : band->loHz;
    double setHi = (userHiHz > 0.0) ? userHiHz : band->hiHz;
    if (setHi < setLo) { std::swap(setLo, setHi); }
    setLo = clampd(setLo, band->loHz, band->hiHz);
    setHi = clampd(setHi, band->loHz, band->hiHz);
    if (setHi - setLo < 1000.0) {   // keep at least 1 kHz
        setHi = clampd(setLo + 1000.0, band->loHz, band->hiHz);
        setLo = clampd(setHi - 1000.0, band->loHz, band->hiHz);
    }
    r.settingLoHz = setLo;
    r.settingHiHz = setHi;

    double lo = (setLo > availLo) ? setLo : availLo;
    double hi = (setHi < availHi) ? setHi : availHi;
    if (hi <= lo) { r.valid = false; return r; }

    if (maxWidthHz > 0.0 && (hi - lo) > maxWidthHz) {
        const double mid = (lo + hi) * 0.5;
        lo = mid - maxWidthHz * 0.5;
        hi = mid + maxWidthHz * 0.5;
        lo = clampd(lo, availLo, availHi);
        hi = clampd(hi, availLo, availHi);
        if (hi - lo > maxWidthHz) {
            // Hit an edge of the available window; keep the width.
            if (lo <= availLo + 0.5) { hi = lo + maxWidthHz; }
            else { lo = hi - maxWidthHz; }
        }
        r.limitedByMaxWidth = true;
    }

    r.settingClipped = r.limitedByMaxWidth || (lo > setLo + 0.5) || (hi < setHi - 0.5);
    r.loHz = lo;
    r.hiHz = hi;
    return r;
}

} // namespace tinycw