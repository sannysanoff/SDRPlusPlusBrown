// SPDX-License-Identifier: MIT
// Copyright (C) 2026 DeepFist integration for SDR++ Brown
//
// Word spaces from character timing, then a fix-up of common words.
//
// The model has a space token, but after voting it practically never survives
// (0 of 11,464 characters on a 285 s 20m contest recording). So the spaces come
// from the time between characters:
//
// 1. Each character time is the CTC output frame. Its place inside the
//    character depends on the symbol, so a per-symbol offset (in dits) was
//    fitted on that recording (robust least squares, intra-word pairs, E = 0):
//        dt = dit * (dur(prev) + gap + off(cur) - off(prev))
//    dur() is the Morse length in dits including the gaps between elements.
// 2. Speed: the dit is the median of dt / (dur(prev) + 3 + off(cur) - off(prev))
//    over the last kSpaceHistory character pairs of the stream (gap = 3 is the
//    letter gap, the common case; the median ignores the word gaps).
// 3. gap = dt / dit - dur(prev) - off(cur) + off(prev). gap >= kWordGapDits is a
//    word space. Contest keyers shorten the word space: at known word
//    boundaries the median gap was 4.8 dits, not 7, and inside words the 90th
//    percentile was 3.8. 4.0 found 69% of the boundaries and split 7.7% of the
//    letter pairs.
// 4. fixWords(): rejoins common words split by step 3 ("T EST" -> "TEST") and
//    splits common words glued to a callsign or to each other
//    ("CQTESTDL6KVA" -> "CQ TEST DL6KVA", "TU5NN" -> "TU 5NN").
#pragma once

#include <algorithm>
#include <cstddef>
#include <deque>
#include <string>
#include <vector>

namespace tinycw {

constexpr double kWordGapDits = 4.0;
constexpr int    kSpaceHistory = 25;   // character pairs in the speed median
constexpr int    kSpaceMinHistory = 6; // no timing spaces before this many pairs

// Morse code of a model token, "" if it has none (blank, space).
inline const char* morseCode(const std::string& sym) {
    static const char* const letters[26] = {
        ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---", "-.-", ".-..", "--",
        "-.", "---", ".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--.."
    };
    static const char* const digits[10] = {
        "-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----."
    };
    if (sym.size() == 1) {
        const char c = sym[0];
        if (c >= 'A' && c <= 'Z') { return letters[c - 'A']; }
        if (c >= '0' && c <= '9') { return digits[c - '0']; }
        switch (c) {
        case '.': return ".-.-.-";
        case ',': return "--..--";
        case '?': return "..--..";
        case '/': return "-..-.";
        case '=': return "-...-";
        case '+': return ".-.-.";
        case '-': return "-....-";
        case '@': return ".--.-.";
        default: return "";
        }
    }
    if (sym == "<SK>") { return "...-.-"; }
    if (sym == "<KN>") { return "-.--."; }
    return "";
}

// Length in dits: dot 1, dash 3, plus one dit between elements. 0 = no code.
inline int morseDits(const std::string& sym) {
    const char* code = morseCode(sym);
    int n = 0, els = 0;
    for (const char* p = code; *p; ++p, ++els) { n += (*p == '.') ? 1 : 3; }
    return (els > 0) ? n + els - 1 : 0;
}

// Fitted emission offset in dits (see the header comment). Symbols that the
// fit did not see keep the prior (-0.75, the ridge value relative to E).
inline float emitOffsetDits(const std::string& sym) {
    struct Off { const char* sym; float off; };
    static const Off table[] = {
        {"A", -4.07f}, {"B", -5.97f}, {"C", 0.28f}, {"D", -4.67f}, {"E", 0.00f}, {"F", -1.69f},
        {"G", -1.33f}, {"H", -2.40f}, {"I", -4.42f}, {"J", -0.33f}, {"K", 0.38f}, {"L", -3.48f},
        {"M", -1.94f}, {"N", -5.02f}, {"O", 2.47f}, {"P", -0.09f}, {"Q", 2.67f}, {"R", -2.84f},
        {"S", -1.90f}, {"T", -3.92f}, {"U", -1.15f}, {"V", 0.05f}, {"W", 0.29f}, {"X", 1.32f},
        {"Y", 3.44f}, {"Z", -1.69f}, {"1", 3.27f}, {"2", 3.44f}, {"3", 2.25f}, {"4", 1.08f},
        {"5", -1.80f}, {"6", -5.36f}, {"7", -2.72f}, {"8", 1.50f}, {"9", 0.83f}, {"0", 0.61f},
        {"/", 1.89f}, {"=", 2.42f},
    };
    for (const Off& o : table) {
        if (sym == o.sym) { return o.off; }
    }
    return -0.75f;
}

// Per-stream speed tracker. feed() returns true if a word space belongs
// before `sym`, which comes `dt` seconds after `prev`.
class SpaceTimer {
public:
    bool feed(const std::string& prev, const std::string& sym, double dt) {
        const int dp = morseDits(prev);
        if (dp <= 0 || morseDits(sym) <= 0 || dt <= 0.0) { return false; }
        const double base = dp + emitOffsetDits(sym) - emitOffsetDits(prev);
        bool space = false;
        if ((int)dits_.size() >= kSpaceMinHistory) {
            const double gap = dt / ditSec() - base;
            space = (gap >= kWordGapDits);
        }
        if (base + 3.0 > 1.0) {
            dits_.push_back(dt / (base + 3.0));
            if ((int)dits_.size() > kSpaceHistory) { dits_.pop_front(); }
        }
        return space;
    }

    // Median dit of the history, seconds (0 if empty).
    double ditSec() const {
        if (dits_.empty()) { return 0.0; }
        std::vector<double> v(dits_.begin(), dits_.end());
        std::nth_element(v.begin(), v.begin() + (long)(v.size() / 2), v.end());
        return v[v.size() / 2];
    }
    double wpm() const {
        const double d = ditSec();
        return (d > 0.0) ? 1.2 / d : 0.0;
    }

private:
    std::deque<double> dits_;
};

// ----------------------------------------------------------------- words

inline bool isCommonWord(const std::string& w) {
    static const char* const words[] = { "CQ", "TEST", "TU", "5NN", "DE", "QRZ", "QSL", "TNX",
                                         "73", "599", "QRL", "GM" };
    for (const char* x : words) {
        if (w == x) { return true; }
    }
    return false;
}

// 3+ characters with a letter and a digit: a callsign or an exchange.
inline bool looksLikeCall(const std::string& w) {
    if (w.size() < 3) { return false; }
    bool digit = false, alpha = false;
    for (char c : w) {
        if (c >= '0' && c <= '9') { digit = true; }
        if (c >= 'A' && c <= 'Z') { alpha = true; }
    }
    return digit && alpha;
}

namespace detail {

struct Word {
    std::string text;
    std::vector<float> conf;
    std::string sep;            // spaces after the word
    std::vector<float> sepConf;
};

inline Word slice(const Word& w, size_t from, size_t n) {
    Word o;
    o.text = w.text.substr(from, n);
    o.conf.assign(w.conf.begin() + (long)from, w.conf.begin() + (long)(from + o.text.size()));
    return o;
}

// "CQTESTDL6KVA" -> CQ | TEST | DL6KVA. A common word splits off the front or
// the back only if the rest is a common word or looks like a callsign.
inline void splitWord(const Word& w, std::vector<Word>& out) {
    static const char* const pre[] = { "TEST", "CQ", "TU", "DE", "QRZ", "TNX", "GM" };
    static const char* const suf[] = { "TEST", "5NN", "TU", "GM" };
    const std::string& t = w.text;
    auto ok = [](const std::string& r) { return isCommonWord(r) || looksLikeCall(r); };
    if (!isCommonWord(t)) {
        for (const char* p : pre) {
            const std::string ps(p);
            // GM followed by a digit is a Scottish call prefix (GM3ABC).
            if (ps == "GM" && t.size() > 2 && t[2] >= '0' && t[2] <= '9') { continue; }
            if (t.size() > ps.size() && t.compare(0, ps.size(), ps) == 0 && ok(t.substr(ps.size()))) {
                Word a = slice(w, 0, ps.size());
                a.sep = " ";
                a.sepConf = { 1.0f };
                out.push_back(a);
                Word b = slice(w, ps.size(), t.size() - ps.size());
                b.sep = w.sep;
                b.sepConf = w.sepConf;
                splitWord(b, out);
                return;
            }
        }
        for (const char* s : suf) {
            const std::string ss(s);
            if (t.size() > ss.size() && t.compare(t.size() - ss.size(), ss.size(), ss) == 0 &&
                ok(t.substr(0, t.size() - ss.size()))) {
                Word a = slice(w, 0, t.size() - ss.size());
                a.sep = " ";
                a.sepConf = { 1.0f };
                splitWord(a, out);
                Word b = slice(w, t.size() - ss.size(), ss.size());
                b.sep = w.sep;
                b.sepConf = w.sepConf;
                out.push_back(b);
                return;
            }
        }
    }
    out.push_back(w);
}

} // namespace detail

// Rejoins split common words, then splits glued ones. Only single spaces are
// joined: a double space is a pause between transmissions. `conf` (one value
// per byte, may be empty) is kept in step with the text.
inline void fixWords(std::string& text, std::vector<float>& conf) {
    const bool hasConf = (conf.size() == text.size());
    std::vector<detail::Word> words;
    std::string lead;
    std::vector<float> leadConf;
    size_t i = 0;
    while (i < text.size() && text[i] == ' ') {
        lead += ' ';
        leadConf.push_back(hasConf ? conf[i] : 1.0f);
        i++;
    }
    while (i < text.size()) {
        detail::Word w;
        while (i < text.size() && text[i] != ' ') {
            w.text += text[i];
            w.conf.push_back(hasConf ? conf[i] : 1.0f);
            i++;
        }
        while (i < text.size() && text[i] == ' ') {
            w.sep += ' ';
            w.sepConf.push_back(hasConf ? conf[i] : 1.0f);
            i++;
        }
        words.push_back(std::move(w));
    }

    // Join: up to 3 pieces separated by single spaces whose concatenation is a
    // common word, if none of the pieces is a common word itself.
    std::vector<detail::Word> joined;
    for (size_t k = 0; k < words.size();) {
        bool done = false;
        for (size_t n = 3; n >= 2 && !done; n--) {
            if (k + n > words.size()) { continue; }
            std::string cat;
            bool single = true, anyCommon = false;
            for (size_t j = 0; j < n; j++) {
                cat += words[k + j].text;
                if (isCommonWord(words[k + j].text)) { anyCommon = true; }
                if (j + 1 < n && words[k + j].sep != " ") { single = false; }
            }
            if (!single || anyCommon || !isCommonWord(cat)) { continue; }
            detail::Word w;
            for (size_t j = 0; j < n; j++) {
                w.text += words[k + j].text;
                w.conf.insert(w.conf.end(), words[k + j].conf.begin(), words[k + j].conf.end());
            }
            w.sep = words[k + n - 1].sep;
            w.sepConf = words[k + n - 1].sepConf;
            joined.push_back(std::move(w));
            k += n;
            done = true;
        }
        // "C QTEST": the split fell inside a common word that is glued to the
        // next one. Rejoin when the pair starts with a common word longer
        // than the first piece; splitWord() below cuts it at the right place.
        if (!done && k + 1 < words.size() && words[k].sep == " " && words[k].text.size() < 4 &&
            !isCommonWord(words[k].text) && !isCommonWord(words[k + 1].text)) {
            const std::string cat = words[k].text + words[k + 1].text;
            std::vector<detail::Word> parts;
            detail::Word w;
            w.text = cat;
            w.conf = words[k].conf;
            w.conf.insert(w.conf.end(), words[k + 1].conf.begin(), words[k + 1].conf.end());
            w.sep = words[k + 1].sep;
            w.sepConf = words[k + 1].sepConf;
            detail::splitWord(w, parts);
            if (parts.size() > 1 && isCommonWord(parts[0].text) &&
                parts[0].text.size() > words[k].text.size()) {
                joined.push_back(std::move(w));
                k += 2;
                done = true;
            }
        }
        if (!done) { joined.push_back(words[k]); k++; }
    }

    std::vector<detail::Word> split;
    for (const detail::Word& w : joined) { detail::splitWord(w, split); }

    std::string t = lead;
    std::vector<float> c = leadConf;
    for (const detail::Word& w : split) {
        t += w.text;
        c.insert(c.end(), w.conf.begin(), w.conf.end());
        t += w.sep;
        c.insert(c.end(), w.sepConf.begin(), w.sepConf.end());
    }
    text = std::move(t);
    if (hasConf) { conf = std::move(c); }
}

inline std::string fixWords(const std::string& text) {
    std::string t = text;
    std::vector<float> c;
    fixWords(t, c);
    return t;
}

} // namespace tinycw
