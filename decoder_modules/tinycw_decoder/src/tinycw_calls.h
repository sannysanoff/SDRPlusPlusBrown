// SPDX-License-Identifier: MIT
// Copyright (C) 2026 DeepFist integration for SDR++ Brown
//
// Per-channel callsign vote, then a display pass that cuts the text at the
// voted callsigns. Runs after the timing spaces and the common-word fix
// (tinycw_spacing.h).
//
// Tuned on a 285 s 20m contest recording (11,464 decoded characters). A
// contest station repeats its call (CQ TEST DL6KVA DL6KVA), but every repeat is
// decoded a little differently (DL6KVA, L6KVA, DL6KVAD...). The vote:
//
// 1. Input: the raw frozen characters of the channel (not the rendered text),
//    the last kLexiconChars of them, cut at pauses > kVotePauseSec.
// 2. Candidates: every substring of 3..10 characters that passes isCall()
//    (DeepFist tools/rescore.py is_call, plus prefixes that start with a digit
//    such as 5B4APL). An occurrence does not count if its extra characters are
//    the start of a common word that follows in full ("IZ8GCBT" in
//    "IZ8GCBTEST") or the end of one that precedes it.
// 3. Select, most frequent first: skip a candidate that a one-character-longer
//    candidate covers in >= kCallExtendRatio of its count (L6KVA -> DL6KVA), a
//    call followed by the start of its own repeat (DL6KVAD, HG8WHG), and
//    anything that contains or is contained in a selected call. A count of at
//    least kCallMinCount is needed.
// 4. Variants: a selected call within one edit of a part of a more frequent
//    call is a misdecode of it (C0LM -> UN0LM, R9UP -> RV9UP) and is dropped.
//
// callPass() then removes the spaces inside a voted call ("UN 0LM" ->
// "UN0LM"), cuts glued repeats ("DL6KVADL6KVA" -> "DL6KVA DL6KVA") and
// replaces a one-edit variant of a call of 5+ characters with the call.
#pragma once

#include <algorithm>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "tinycw_spacing.h"

namespace tinycw {

constexpr size_t kLexiconChars = 3000;
constexpr int    kCallMinCount = 3;
constexpr double kCallExtendRatio = 0.5;
constexpr double kLexiconUpdateSec = 3.0;

// Callsign shape, DeepFist tools/rescore.py CALL_RE with a digit-first prefix:
//   ^([A-Z0-9]{1,3}/)?(\d[A-Z]{1,2}|[A-Z]{1,2})\d{1,3}[A-Z]{1,4}(/[A-Z0-9]{1,3})?$
// and not a contest/QSO marker.
inline bool isCall(const std::string& s) {
    static const char* const markers[] = { "CQ", "DE", "TEST", "QRZ", "TU", "73", "88", "599", "5NN",
                                           "5N", "K", "KN", "SK", "AGN", "PSE", "NR", "R" };
    if (s.size() < 3) { return false; }
    for (const char* m : markers) {
        if (s == m) { return false; }
    }
    auto alpha = [](char c) { return c >= 'A' && c <= 'Z'; };
    auto digit = [](char c) { return c >= '0' && c <= '9'; };
    auto alnum = [&](char c) { return alpha(c) || digit(c); };

    // Core: (\d[A-Z]{1,2}|[A-Z]{1,2})\d{1,3}[A-Z]{1,4} on s[b, e).
    auto core = [&](size_t b, size_t e) {
        // Test every split of the prefix and the digit run.
        for (int lead = 0; lead <= 1; lead++) {
            size_t i = b;
            if (lead == 1) {
                if (i >= e || !digit(s[i])) { continue; }
                i++;
            }
            for (int na = 1; na <= 2; na++) {
                size_t j = i;
                bool ok = true;
                for (int k = 0; k < na; k++, j++) {
                    if (j >= e || !alpha(s[j])) { ok = false; break; }
                }
                if (!ok) { continue; }
                for (int nd = 1; nd <= 3; nd++) {
                    size_t m = j;
                    bool okd = true;
                    for (int k = 0; k < nd; k++, m++) {
                        if (m >= e || !digit(s[m])) { okd = false; break; }
                    }
                    if (!okd) { break; }
                    const size_t rest = e - m;
                    if (rest < 1 || rest > 4) { continue; }
                    bool okr = true;
                    for (size_t k = m; k < e; k++) {
                        if (!alpha(s[k])) { okr = false; break; }
                    }
                    if (okr) { return true; }
                }
            }
        }
        return false;
    };

    // Optional "XXX/" in front and "/XXX" behind.
    auto affix = [&](size_t b, size_t e) {
        if (e - b < 1 || e - b > 3) { return false; }
        for (size_t k = b; k < e; k++) {
            if (!alnum(s[k])) { return false; }
        }
        return true;
    };
    const size_t s1 = s.find('/');
    if (s1 == std::string::npos) { return core(0, s.size()); }
    const size_t s2 = s.find('/', s1 + 1);
    if (s2 == std::string::npos) {
        return (affix(0, s1) && core(s1 + 1, s.size())) || (core(0, s1) && affix(s1 + 1, s.size()));
    }
    if (s.find('/', s2 + 1) != std::string::npos) { return false; }
    return affix(0, s1) && core(s1 + 1, s2) && affix(s2 + 1, s.size());
}

inline int editDistance(const std::string& a, const std::string& b) {
    std::vector<int> p(b.size() + 1), c(b.size() + 1);
    for (size_t j = 0; j <= b.size(); j++) { p[j] = (int)j; }
    for (size_t i = 1; i <= a.size(); i++) {
        c[0] = (int)i;
        for (size_t j = 1; j <= b.size(); j++) {
            c[j] = std::min({ p[j] + 1, c[j - 1] + 1, p[j - 1] + (a[i - 1] != b[j - 1] ? 1 : 0) });
        }
        std::swap(p, c);
    }
    return p[b.size()];
}

namespace detail {

inline const std::vector<std::string>& voteWords() {
    static const std::vector<std::string> w = { "TEST", "CQ", "TU", "DE", "5NN", "QRZ", "TNX", "QSL", "AGN", "GM" };
    return w;
}

// s[i, j) ends with the start of a common word that continues in full after j.
inline bool tailIsWord(const std::string& g, size_t i, size_t j) {
    for (size_t k = i + 1; k < j; k++) {
        for (const std::string& w : voteWords()) {
            if (k + w.size() >= j && g.compare(k, w.size(), w) == 0) { return true; }
        }
    }
    return false;
}

// s[i, j) starts with the end of a common word that began before i.
inline bool headIsWord(const std::string& g, size_t i, size_t j) {
    for (size_t k = i + 1; k < j; k++) {
        for (const std::string& w : voteWords()) {
            if (k >= w.size() && k - w.size() < i && g.compare(k - w.size(), w.size(), w) == 0) { return true; }
        }
    }
    return false;
}

inline bool selfGlue(const std::string& y) {
    for (size_t k = 3; k < y.size(); k++) {
        const std::string a = y.substr(0, k), b = y.substr(k);
        if (isCall(a) && a.compare(0, b.size(), b) == 0) { return true; }
    }
    return false;
}

// x is within one edit of a part (3+ characters) of call.
inline bool nearPart(const std::string& x, const std::string& call) {
    for (size_t i = 0; i < call.size(); i++) {
        for (size_t j = i + 3; j <= call.size(); j++) {
            const int len = (int)(j - i);
            if (std::abs(len - (int)x.size()) <= 1 && editDistance(x, call.substr(i, (size_t)len)) <= 1) {
                return true;
            }
        }
    }
    return false;
}

} // namespace detail

// Voted callsigns from raw text segments (step 2-4 of the header).
inline std::vector<std::string> voteCalls(const std::vector<std::string>& segments) {
    std::map<std::string, int> cnt;
    for (const std::string& g : segments) {
        for (size_t i = 0; i < g.size(); i++) {
            for (size_t len = 3; len <= 10 && i + len <= g.size(); len++) {
                const std::string x = g.substr(i, len);
                if (!isCall(x)) { continue; }
                if (detail::tailIsWord(g, i, i + len) || detail::headIsWord(g, i, i + len)) { continue; }
                cnt[x]++;
            }
        }
    }
    std::vector<std::pair<std::string, int>> order(cnt.begin(), cnt.end());
    std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) { return a.second > b.second; }
        if (a.first.size() != b.first.size()) { return a.first.size() > b.first.size(); }
        return a.first < b.first;
    });
    std::vector<std::string> acc;
    for (const auto& [x, c] : order) {
        if (c < kCallMinCount) { break; }
        if (detail::selfGlue(x)) { continue; }
        bool covered = false;
        static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/";
        for (char ch : chars) {
            for (const std::string& y : { ch + x, x + ch }) {
                const auto it = cnt.find(y);
                if (it != cnt.end() && it->second >= kCallExtendRatio * c && !detail::selfGlue(y)) {
                    covered = true;
                }
            }
            if (covered) { break; }
        }
        if (covered) { continue; }
        bool overlap = false;
        for (const std::string& a : acc) {
            if (a.find(x) != std::string::npos || x.find(a) != std::string::npos || detail::nearPart(x, a)) {
                overlap = true;
                break;
            }
        }
        if (!overlap) { acc.push_back(x); }
    }
    return acc;
}

// Per-channel store of raw decoded characters and the voted calls.
class CallLexicon {
public:
    // One frozen character (model token) at time sec.
    void add(const std::string& sym, double sec) {
        if (sym == " ") { return; }
        if (lastSec_ > 0.0 && sec - lastSec_ > kVotePauseSecCalls) { raw_ += '|'; }
        raw_ += (sym.size() == 1) ? sym : std::string("#");
        lastSec_ = sec;
        if (raw_.size() > kLexiconChars) { raw_.erase(0, raw_.size() - kLexiconChars); }
        dirty_ = true;
    }

    // Votes again if new text came in and kLexiconUpdateSec passed.
    void update(double now) {
        if (!dirty_ || (now - votedAt_) < kLexiconUpdateSec) { return; }
        dirty_ = false;
        votedAt_ = now;
        std::vector<std::string> segs;
        size_t a = 0;
        while (a <= raw_.size()) {
            const size_t b = raw_.find('|', a);
            const size_t e = (b == std::string::npos) ? raw_.size() : b;
            if (e > a) { segs.push_back(raw_.substr(a, e - a)); }
            if (b == std::string::npos) { break; }
            a = b + 1;
        }
        calls_ = voteCalls(segs);
    }

    const std::vector<std::string>& calls() const { return calls_; }

private:
    static constexpr double kVotePauseSecCalls = 2.0;
    std::string raw_;
    double lastSec_ = 0.0;
    double votedAt_ = -1e18;
    bool dirty_ = false;
    std::vector<std::string> calls_;
};

// Cuts the text at the voted calls (see the header). Double spaces (pauses)
// stay; other spaces stay where no call matched. conf follows the text.
inline void callPass(std::string& text, std::vector<float>& conf, const std::vector<std::string>& calls) {
    if (calls.empty()) { return; }
    const bool hasConf = (conf.size() == text.size());
    std::vector<std::string> byLen = calls;
    std::sort(byLen.begin(), byLen.end(),
              [](const std::string& a, const std::string& b) { return a.size() > b.size(); });

    std::string out;
    std::vector<float> outConf;
    size_t p = 0;
    while (p <= text.size()) {
        // One segment: up to a double space.
        size_t q = text.find("  ", p);
        if (q == std::string::npos) { q = text.size(); }
        // Characters without spaces, the confidence of each, and where the
        // spaces were (before index k of z).
        std::string z;
        std::vector<float> zc;
        std::vector<bool> spaceBefore;
        bool pending = false;
        for (size_t i = p; i < q; i++) {
            if (text[i] == ' ') { pending = true; continue; }
            z += text[i];
            zc.push_back(hasConf ? conf[i] : 1.0f);
            spaceBefore.push_back(pending && !z.empty() && z.size() > 1);
            pending = false;
        }

        std::string seg;
        std::vector<float> segConf;
        bool runOpen = false;   // last thing written is free text (not a call)
        auto sep = [&]() {
            if (!seg.empty() && seg.back() != ' ') { seg += ' '; segConf.push_back(1.0f); }
        };
        size_t i = 0;
        while (i < z.size()) {
            std::string match;
            size_t used = 0;
            for (const std::string& c : byLen) {
                if (z.compare(i, c.size(), c) == 0) { match = c; used = c.size(); break; }
            }
            if (match.empty()) {
                for (const std::string& c : calls) {
                    if (c.size() < 5) { continue; }
                    for (size_t len : { c.size(), c.size() - 1, c.size() + 1 }) {
                        if (i + len > z.size()) { continue; }
                        const std::string s = z.substr(i, len);
                        if (isCommonWord(s)) { continue; }
                        if ((s[0] == c[0] && editDistance(s, c) <= 1) ||
                            (len == c.size() - 1 && s == c.substr(1))) {
                            match = c;
                            used = len;
                            break;
                        }
                    }
                    if (!match.empty()) { break; }
                }
            }
            if (!match.empty()) {
                float m = 1.0f;
                for (size_t k = i; k < i + used; k++) { m = std::min(m, zc[k]); }
                sep();
                seg += match;
                segConf.insert(segConf.end(), match.size(), m);
                i += used;
                runOpen = false;
                continue;
            }
            if (runOpen) {
                if (spaceBefore[i]) { seg += ' '; segConf.push_back(1.0f); }
            }
            else {
                sep();
            }
            seg += z[i];
            segConf.push_back(zc[i]);
            runOpen = true;
            i++;
        }
        out += seg;
        outConf.insert(outConf.end(), segConf.begin(), segConf.end());
        if (q >= text.size()) { break; }
        out += "  ";
        outConf.push_back(1.0f);
        outConf.push_back(1.0f);
        p = q + 2;
        while (p < text.size() && text[p] == ' ') { p++; }
    }
    text = std::move(out);
    if (hasConf) { conf = std::move(outConf); }
}

inline std::string callPass(const std::string& text, const std::vector<std::string>& calls) {
    std::string t = text;
    std::vector<float> c;
    callPass(t, c, calls);
    return t;
}

} // namespace tinycw
