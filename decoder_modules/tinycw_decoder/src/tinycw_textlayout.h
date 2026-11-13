// SPDX-License-Identifier: MIT
// Copyright (C) 2026 DeepFist integration for SDR++ Brown
//
// Layout of one decoded line for the UI, without ImGui. The UI font has no
// kerning or ligatures, so a text width is the sum of the glyph advances.
// The UI builds a layout only when the text, the confidence, the room or the
// font changes, and draws the cached runs on every frame.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace tinycw {

constexpr int kConfLevels = 16;   // confidence steps (brightness levels)

// Characters with the same confidence step and colour: one draw call.
struct TextRun {
    float x = 0.0f;       // offset from the left edge of the line
    uint32_t a = 0, b = 0;   // [a, b) in TextLayout::chars
    uint8_t level = kConfLevels - 1;   // 0 = darkest, kConfLevels-1 = full
    bool fresh = false;   // the newest characters (another colour)
};

struct TextLayout {
    std::string chars;    // the characters that fit
    std::vector<TextRun> runs;
    float width = 0.0f;
};

// adv[256] = glyph advances at scale 1; scale = font size / font base size.
// text = the whole line, fresh = its newest characters (a suffix of text, or
// stale: then only fresh is shown), conf = one value per byte of text (other
// sizes: all 1). Characters are cut from the left until the width <= room.
inline TextLayout layoutText(const std::string& text, const std::string& fresh, const std::vector<float>& conf,
                             const float* adv, float scale, float room) {
    auto w = [&](char c) { return adv[(unsigned char)c] * scale; };
    std::string body = text, tail = fresh;
    bool hasConf = conf.size() == text.size();
    if (!tail.empty()) {
        if (body.size() >= tail.size() && body.compare(body.size() - tail.size(), tail.size(), tail) == 0) {
            body.resize(body.size() - tail.size());
        }
        else {
            body.clear();   // the text no longer ends with the new characters
            hasConf = false;
        }
    }
    float bodyW = 0.0f, tailW = 0.0f;
    for (char c : body) { bodyW += w(c); }
    for (char c : tail) { tailW += w(c); }
    size_t cutB = 0, cutT = 0;
    while (bodyW + tailW > room && (cutB < body.size() || cutT < tail.size())) {
        if (cutB < body.size()) { bodyW -= w(body[cutB++]); }
        else { tailW -= w(tail[cutT++]); }
    }

    TextLayout out;
    out.chars = body.substr(cutB) + tail.substr(cutT);
    const size_t nb = body.size() - cutB;
    float x = 0.0f;
    size_t a = 0;
    auto levelAt = [&](size_t k) {
        if (!hasConf) { return kConfLevels - 1; }
        // body char k comes from text[cutB + k]; tail char from text[body.size() + cutT + k - nb]
        const size_t src = (k < nb) ? cutB + k : body.size() + cutT + (k - nb);
        const float c = std::clamp(conf[src], 0.0f, 1.0f);
        return (int)std::lround(c * (float)(kConfLevels - 1));
    };
    while (a < out.chars.size()) {
        const int lv = levelAt(a);
        const bool fr = a >= nb;
        size_t b = a + 1;
        while (b < out.chars.size() && (b >= nb) == fr && levelAt(b) == lv) { b++; }
        TextRun r;
        r.x = x;
        r.a = (uint32_t)a;
        r.b = (uint32_t)b;
        r.level = (uint8_t)lv;
        r.fresh = fr;
        out.runs.push_back(r);
        for (size_t k = a; k < b; k++) { x += w(out.chars[k]); }
        a = b;
    }
    out.width = x;
    return out;
}

} // namespace tinycw
