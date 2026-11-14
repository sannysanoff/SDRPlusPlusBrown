// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Brent Crier (N9BC): DeepFist, the Python original
// Copyright (C) 2026 TinyCW decoder for SDR++ Brown: this C++ port
//
// C++ port of `deepfist/morse/alphabet.py` (token order, commit 5945dc8) and
// `deepfist/model/decode.py` (greedy CTC decode, commit 8e72665) from
// DeepFist (https://github.com/n9bc/DeepFist). TinyCW was trained with the
// same token order.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tinycw {

// Token table in exactly the order used to train the classifier head:
//   [<blank>, " ", A..Z, 1..9,0, . , ? / = + - @ , <SK>, <KN>]
inline const std::vector<std::string>& tokens() {
    static const std::vector<std::string> t = [] {
        std::vector<std::string> v;
        v.emplace_back("");            // 0: CTC blank
        v.emplace_back(" ");           // 1: word space
        const char* letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        for (const char* p = letters; *p; ++p) { v.emplace_back(1, *p); }
        const char* digits = "1234567890";
        for (const char* p = digits; *p; ++p) { v.emplace_back(1, *p); }
        const char* punct = ".,?/=+-@";
        for (const char* p = punct; *p; ++p) { v.emplace_back(1, *p); }
        v.emplace_back("<SK>");
        v.emplace_back("<KN>");
        return v;
    }();
    return t;
}

inline int blankId() { return 0; }
inline int classCount() { return (int)tokens().size(); }


// Greedy CTC decode of one time-step-major frame grid.
// logProbs: [T][C], row-major.
// Returns the collapsed token ids (blank removed) and the frame index of each.
inline void greedyCtc(const float* logProbs, int T, int C,
                      std::vector<int>& ids, std::vector<int>& frames) {
    ids.clear();
    frames.clear();
    int prev = -1;
    for (int t = 0; t < T; t++) {
        const float* row = logProbs + (size_t)t * (size_t)C;
        int best = 0;
        float bestVal = row[0];
        for (int c = 1; c < C; c++) {
            if (row[c] > bestVal) { bestVal = row[c]; best = c; }
        }
        if (best != prev) {
            if (best != blankId()) {
                ids.push_back(best);
                frames.push_back(t);
            }
            prev = best;
        }
    }
}

inline std::string idsToText(const std::vector<int>& ids) {
    const std::vector<std::string>& tok = tokens();
    std::string s;
    for (int id : ids) {
        if (id <= 0 || id >= (int)tok.size()) { continue; }
        s += tok[(size_t)id];
    }
    return s;
}

} // namespace tinycw