// SPDX-License-Identifier: MIT
// Copyright (C) 2026 DeepFist integration for SDR++ Brown
//
// Port of DeepFist `deepfist/morse/alphabet.py` (token order) and
// `deepfist/model/decode.py` (greedy CTC decode).

#pragma once

#include <algorithm>
#include <cmath>
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


// Prefix beam search for the CTC head. Same frame grid as greedyCtc but keeps
// the `beam` best hypotheses, so a locally wrong frame does not cost a
// character. With 48 classes and ~400 frames this costs well under a
// millisecond per window.
inline float logAdd(float a, float b) {
    if (a == -INFINITY) { return b; }
    if (b == -INFINITY) { return a; }
    const float m = (a > b) ? a : b;
    return m + std::log(std::exp(a - m) + std::exp(b - m));
}

inline void beamCtc(const float* logProbs, int T, int C, int beamWidth,
                    std::vector<int>& ids, std::vector<int>& frames) {
    ids.clear();
    frames.clear();
    if (T <= 0 || C <= 0) { return; }

    struct Beam {
        std::vector<int> prefix;
        std::vector<int> times;
        float pBlank = -INFINITY;
        float pNonBlank = -INFINITY;
    };
    std::vector<Beam> beams(1);

    for (int t = 0; t < T; t++) {
        const float* row = logProbs + (size_t)t * (size_t)C;
        std::vector<Beam> next;
        auto findOrAdd = [&next](const std::vector<int>& p) -> Beam& {
            for (auto& b : next) {
                if (b.prefix == p) { return b; }
            }
            next.emplace_back();
            next.back().prefix = p;
            next.back().pBlank = -INFINITY;
            next.back().pNonBlank = -INFINITY;
            return next.back();
        };

        for (const Beam& b : beams) {
            const float total = logAdd(b.pBlank, b.pNonBlank);
            // stay on blank
            {
                Beam& nb = findOrAdd(b.prefix);
                nb.pBlank = logAdd(nb.pBlank, total + row[blankId()]);
                if (nb.times.size() < b.times.size()) { nb.times = b.times; }
            }
            // repeat the last character without a blank in between
            if (!b.prefix.empty()) {
                const int last = b.prefix.back();
                Beam& nb = findOrAdd(b.prefix);
                nb.pNonBlank = logAdd(nb.pNonBlank, b.pNonBlank + row[last]);
                if (nb.times.size() < b.times.size()) { nb.times = b.times; }
            }
            // emit a new character
            for (int c = 0; c < C; c++) {
                if (c == blankId()) { continue; }
                if (!b.prefix.empty() && b.prefix.back() == c) { continue; }
                std::vector<int> p = b.prefix;
                p.push_back(c);
                Beam& nb = findOrAdd(p);
                nb.pNonBlank = logAdd(nb.pNonBlank, total + row[c]);
                nb.times = b.times;
                nb.times.push_back(t);
            }
        }

        std::sort(next.begin(), next.end(), [](const Beam& a, const Beam& b) {
            return logAdd(a.pBlank, a.pNonBlank) > logAdd(b.pBlank, b.pNonBlank);
        });
        if ((int)next.size() > beamWidth) { next.resize((size_t)beamWidth); }
        beams.swap(next);
    }

    const Beam* best = &beams.front();
    for (const Beam& b : beams) {
        if (logAdd(b.pBlank, b.pNonBlank) > logAdd(best->pBlank, best->pNonBlank)) { best = &b; }
    }
    ids = best->prefix;
    frames = best->times;
}

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