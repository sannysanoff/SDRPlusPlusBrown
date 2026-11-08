// SPDX-License-Identifier: MIT
// Copyright (C) 2026 DeepFist integration for SDR++ Brown
//
// Debug probe for the single-channel ("focus") decoder.
//
// It takes one already-channelized 3200 Hz WAV, slides a 12 s window over it in
// 1 s steps, runs the model on every window (batch 1, exactly like the GUI
// focus decoder) and prints per-pass diagnostics. Two decode paths are
// available so they can be compared on identical input:
//
//   single        commit the newest slice of each window (production behaviour)
//   ens-prob      average the per-frame class probabilities of all overlapping
//                 windows on an absolute 30 ms grid, then decode once
//   ens-log       same, but average the log-probabilities (geometric mean)
//
// Diagnostics per committed character: absolute time, grid key, how many
// windows contributed, the averaged probability of the chosen class and of the
// runner-up, and the fraction of contributing windows that agreed on argmax.
// That is what shows whether the ensemble actually removes the noise or only
// blurs the peaks.

// Windows.h defines min/max as macros; the DSP code below uses std::min/std::max.
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <utils/wav.h>

#include "tinycw_conditioner.h"
#include "tinycw_ctc.h"
#include "tinycw_model.h"
#include "tinycw_spectrogram.h"

using namespace tinycw;

namespace {

struct Options {
    std::string wav;
    std::string txt;
    std::string model;
    double windowSec = 12.0;
    int tickSamples = 0;      // 0 = one second (3200 samples)
    double guardSec = 0.40;
    std::string mode = "single";
    int minCount = 1;         // ensemble: skip frames seen by fewer windows
    double minP = 0.0;        // drop characters below this averaged probability
    double minAgree = 0.0;    // drop characters below this argmax agreement
    double gateFrac = 0.45;   // energy gate on the character frame; 0 = off
    int gateDilate = 2;       // frames of slack around an active frame
    double spaceGap = 2.2;    // insert a space when a gap exceeds med * this
    double spaceMin = 0.30;   // ...and is at least this long, seconds
    double spaceLong = 4.0;   // double space when the gap exceeds med * this
    bool dumpFrames = false;
};

void usage() {
    std::printf(
        "tinycw_focus_probe --wav FILE [--txt REF] [--model WEIGHTS]\n"
        "  --mode single|ens-prob|ens-log   (default single)\n"
        "  --window SEC                     window length (default 12)\n"
        "  --tick-samples N                 step between windows (default 3200 = 1 s)\n"
        "  --guard SEC                      drop window edges (ensemble only)\n"
        "  --min-count N                    require N contributing windows\n"
        "  --min-p P                        drop chars below this averaged probability\n"
        "  --min-agree A                    drop chars below this argmax agreement\n"
        "  --gate-frac F                    energy gate threshold (0..1, 0 = off)\n"
        "  --gate-dilate N                  frames of slack around an active frame\n"
        "  --space-gap F                    space when gap > med*F (0 = off)\n"
        "  --space-min S                    minimum gap for a space, seconds\n"
        "  --space-long F                   double space when gap > med*F\n"
        "  --intra-threads N\n"
        "  --dump-frames                    print every decoded frame class\n");
}

std::string norm(const std::string& in) {
    std::string t;
    t.reserve(in.size());
    for (char c : in) { t += (char)std::toupper((unsigned char)c); }
    // <SK> / <KN> become spaces, everything outside A-Z0-9 becomes a space.
    std::string cleaned;
    for (size_t i = 0; i < t.size();) {
        if (t.compare(i, 4, "<SK>") == 0) { cleaned += ' '; i += 4; continue; }
        if (t.compare(i, 4, "<KN>") == 0) { cleaned += ' '; i += 4; continue; }
        const char c = t[i++];
        cleaned += (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' ? c : ' ';
    }
    std::string out;
    bool space = false;
    for (char c : cleaned) {
        if (c == ' ') { space = true; continue; }
        if (space && !out.empty()) { out += ' '; }
        space = false;
        out += c;
    }
    return out;
}

int levenshtein(const std::string& a, const std::string& b) {
    const std::string& s = a.size() >= b.size() ? a : b;
    const std::string& r = a.size() >= b.size() ? b : a;
    std::vector<int> prev(r.size() + 1), cur(r.size() + 1);
    for (size_t j = 0; j <= r.size(); j++) { prev[j] = (int)j; }
    for (size_t i = 1; i <= s.size(); i++) {
        cur[0] = (int)i;
        for (size_t j = 1; j <= r.size(); j++) {
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1,
                               prev[j - 1] + (s[i - 1] != r[j - 1] ? 1 : 0)});
        }
        prev = cur;
    }
    return prev[r.size()];
}

bool loadWav(const std::string& path, std::vector<float>& out, int& rate) {
    wav::Reader reader(path);
    if (!reader.isValid()) {
        std::fprintf(stderr, "cannot open %s: %s\n", path.c_str(), reader.error.c_str());
        return false;
    }
    rate = (int)reader.getSampleRate();
    const int channels = (int)reader.getChannelCount();
    const int bits = (int)reader.getBitDepth();
    if (bits != 16) {
        std::fprintf(stderr, "only 16-bit PCM is supported, got %d\n", bits);
        return false;
    }
    std::vector<int16_t> raw(1 << 20);
    std::vector<int16_t> block;
    out.clear();
    while (true) {
        const size_t n = reader.readSamples2(raw.data(), raw.size() * sizeof(int16_t));
        if (n == 0) { break; }
        const size_t samples = n / sizeof(int16_t);
        const size_t frames = samples / (size_t)std::max(1, channels);
        for (size_t i = 0; i < frames; i++) {
            double acc = 0.0;
            for (int c = 0; c < channels; c++) {
                acc += (double)raw[i * (size_t)channels + (size_t)c];
            }
            out.push_back((float)(acc / (32768.0 * (double)std::max(1, channels))));
        }
        if (samples < raw.size()) { break; }
    }
    reader.close();
    return true;
}

std::string readFile(const std::string& path) {
    std::string s;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) { return s; }
    char buf[4096];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) { s.append(buf, n); }
    std::fclose(f);
    return s;
}

// One absolute output frame (30 ms) with the contributions of every window.
struct FrameAcc {
    std::vector<double> acc;    // summed probability or log-prob per class
    std::vector<int> votes;     // argmax votes per class
    int count = 0;
    double energy = 0.0;        // summed mean-square of the audio frame
};

} // namespace

int main(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--wav") { opt.wav = next(); }
        else if (a == "--txt") { opt.txt = next(); }
        else if (a == "--model") { opt.model = next(); }
        else if (a == "--mode") { opt.mode = next(); }
        else if (a == "--window") { opt.windowSec = std::atof(next().c_str()); }
        else if (a == "--tick-samples") { opt.tickSamples = std::atoi(next().c_str()); }
        else if (a == "--guard") { opt.guardSec = std::atof(next().c_str()); }
        else if (a == "--min-count") { opt.minCount = std::max(1, std::atoi(next().c_str())); }
        else if (a == "--min-p") { opt.minP = std::atof(next().c_str()); }
        else if (a == "--min-agree") { opt.minAgree = std::atof(next().c_str()); }
        else if (a == "--gate-frac") { opt.gateFrac = std::atof(next().c_str()); }
        else if (a == "--gate-dilate") { opt.gateDilate = std::atoi(next().c_str()); }
        else if (a == "--no-gate") { opt.gateFrac = 0.0; }
        else if (a == "--space-gap") { opt.spaceGap = std::atof(next().c_str()); }
        else if (a == "--space-min") { opt.spaceMin = std::atof(next().c_str()); }
        else if (a == "--space-long") { opt.spaceLong = std::atof(next().c_str()); }
        else if (a == "--no-space-gap") { opt.spaceGap = 0.0; }
        else if (a == "--dump-frames") { opt.dumpFrames = true; }
        else if (a == "--help" || a == "-h") { usage(); return 0; }
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); usage(); return 2; }
    }
    if (opt.wav.empty()) { usage(); return 2; }
    if (opt.tickSamples <= 0) { opt.tickSamples = kSampleRate; }

    std::vector<float> audio;
    int rate = 0;
    if (!loadWav(opt.wav, audio, rate)) { return 1; }
    if (rate != kSampleRate) {
        std::fprintf(stderr, "expected %d Hz, got %d Hz (use tools/extract_channel.py)\n",
                     kSampleRate, rate);
        return 1;
    }

    Model model;
    std::string err;
    if (!model.load(opt.model, err)) {
        std::fprintf(stderr, "model load failed: %s\n", err.c_str());
        return 1;
    }

    const int classes = classCount();
    const std::vector<std::string>& tok = tokens();
    const int windowSamples = (int)(opt.windowSec * (double)kSampleRate);
    const int guardSamples = (int)(opt.guardSec * (double)kSampleRate);
    const int frameSamples = 2 * kHop;                       // 30 ms
    const double framePeriod = (double)frameSamples / (double)kSampleRate;
    const bool ensemble = opt.mode == "ens-prob" || opt.mode == "ens-log";
    const bool avgLog = opt.mode == "ens-log";

    std::printf("mode %s  window %.1f s  tick %d samples (%.3f s)  guard %.2f s  min-count %d\n",
                opt.mode.c_str(), opt.windowSec, opt.tickSamples,
                (double)opt.tickSamples / (double)kSampleRate, opt.guardSec, opt.minCount);
    std::printf("audio %.1f s (%zu samples), frames per window ~%d\n",
                (double)audio.size() / (double)kSampleRate, audio.size(),
                (int)(opt.windowSec * kSampleRate / frameSamples));

    Conditioner cond;
    Spectrogram spec;
    std::vector<float> condBuf, specBuf, logProbs;
    std::map<int64_t, FrameAcc> frames;
    int64_t lastCommittedFrame = -1;
    double committedSec = 0.0;      // single mode: last committed character time
    std::string text;

    int pass = 0;
    size_t totalChars = 0;
    double probSum = 0.0;
    int probCount = 0;
    double agreeSum = 0.0;
    double lastCharSec = 0.0;
    std::vector<double> gaps;

    for (int64_t start = 0; start + windowSamples <= (int64_t)audio.size();
         start += opt.tickSamples) {
        const float* win = audio.data() + start;
        cond.process(win, windowSamples, condBuf);
        int nFrames = 0;
        if (!spec.compute(condBuf.data(), (int)condBuf.size(), specBuf, nFrames) || nFrames <= 0) {
            continue;
        }
        int outFrames = 0;
        if (!model.inferBatch(specBuf.data(), 1, kFreqBins, nFrames, logProbs, outFrames, err)) {
            std::fprintf(stderr, "infer failed: %s\n", err.c_str());
            return 1;
        }

        pass++;
        const double startSec = (double)start / (double)kSampleRate;
        std::printf("\n--- pass %d  window [%.2f .. %.2f] s  frames %d -> %d\n",
                    pass, startSec, startSec + opt.windowSec, nFrames, outFrames);

        if (!ensemble) {
            std::vector<int> ids, idx;
            greedyCtc(logProbs.data(), outFrames, classes, ids, idx);
            const double keepFrom = startSec + opt.windowSec - (double)opt.tickSamples / kSampleRate - 0.20;
            int added = 0;
            for (size_t i = 0; i < ids.size(); i++) {
                const double t = startSec + (double)idx[i] * framePeriod;
                if (t <= keepFrom || t <= committedSec) { continue; }
                committedSec = t;
                if (ids[i] <= 0) { continue; }
                const float* row = logProbs.data() + (size_t)idx[i] * (size_t)classes;
                const std::string& sym = tok[(size_t)ids[i]];
                std::printf("  + '%s' t=%.3f  p=%.3f\n", sym.c_str(), t, std::exp(row[ids[i]]));
                text += sym;
                added++;
                totalChars++;
            }
            std::printf("  committed %d chars\n", added);
            continue;
        }

        // ---- ensemble ----
        for (int j = 0; j < outFrames; j++) {
            const int64_t absSample = start + (int64_t)j * frameSamples;
            if (absSample - start < guardSamples) { continue; }
            if (start + windowSamples - absSample < guardSamples) { continue; }
            const int64_t key = absSample / frameSamples;
            FrameAcc& a = frames[key];
            if (a.acc.empty()) {
                a.acc.assign((size_t)classes, 0.0);
                a.votes.assign((size_t)classes, 0);
            }
            const float* row = logProbs.data() + (size_t)j * (size_t)classes;
            int best = 0;
            float bestP = row[0];
            for (int c = 0; c < classes; c++) {
                a.acc[(size_t)c] += avgLog ? (double)row[c] : std::exp((double)row[c]);
                if (row[c] > bestP) { bestP = row[c]; best = c; }
            }
            a.votes[(size_t)best] += 1;
            a.count += 1;

            // Energy of the matching audio frame, for the keying gate.
            const int64_t s0 = absSample;
            if (s0 + frameSamples <= (int64_t)condBuf.size()) {
                double e = 0.0;
                for (int k = 0; k < frameSamples; k++) {
                    const double v = (double)condBuf[(size_t)s0 - (size_t)start + (size_t)k];
                    e += v * v;
                }
                a.energy += e / (double)frameSamples;
            }
        }

        // Frames that have left the leading guard are as covered as they will get
        // for this lag budget: decode them once, in order.
        const int64_t cutoffKey = (start - (int64_t)std::max(0, opt.minCount - 1) * opt.tickSamples)
                                / frameSamples;
        std::vector<int64_t> ready;
        for (auto it = frames.lower_bound(lastCommittedFrame + 1); it != frames.end(); ++it) {
            if (it->first > cutoffKey) { break; }
            if (it->second.count < opt.minCount) { continue; }
            ready.push_back(it->first);
        }
        if (ready.empty()) {
            std::printf("  no frame ready (cutoff key %lld, last committed %lld, map %d)\n",
                        (long long)cutoffKey, (long long)lastCommittedFrame, (int)frames.size());
            if (opt.dumpFrames) {
                for (auto& kv : frames) {
                    std::printf("    key %lld count %d\n", (long long)kv.first, kv.second.count);
                }
            }
            continue;
        }

        std::vector<float> grid(ready.size() * (size_t)classes);
        for (size_t i = 0; i < ready.size(); i++) {
            const FrameAcc& a = frames[ready[i]];
            const double w = (double)std::max(1, a.count);
            for (int c = 0; c < classes; c++) {
                const double v = a.acc[(size_t)c] / w;
                grid[i * (size_t)classes + (size_t)c] =
                    (float)(avgLog ? v : std::log(std::max(v, 1e-12)));
            }
        }
        // Keying gate: frames without carrier energy only produce hallucinations.
        // Threshold is relative to the frame-energy spread of this segment.
        std::vector<char> active(ready.size(), 1);
        if (opt.gateFrac > 0.0 && ready.size() > 8) {
            std::vector<double> e(ready.size());
            for (size_t i = 0; i < ready.size(); i++) {
                const FrameAcc& a = frames[ready[i]];
                e[i] = a.count > 0 ? a.energy / (double)a.count : 0.0;
            }
            std::vector<double> sorted = e;
            std::sort(sorted.begin(), sorted.end());
            const double p10 = sorted[sorted.size() / 10];
            const double p95 = sorted[sorted.size() * 95 / 100];
            const double thr = p10 + opt.gateFrac * (p95 - p10);
            for (size_t i = 0; i < e.size(); i++) { active[i] = e[i] >= thr ? 1 : 0; }
            if (opt.gateDilate > 0) {
                std::vector<char> dil = active;
                for (size_t i = 0; i < active.size(); i++) {
                    if (active[i] == 0) { continue; }
                    for (int d = 1; d <= opt.gateDilate; d++) {
                        if (i >= (size_t)d) { dil[i - (size_t)d] = 1; }
                        if (i + (size_t)d < dil.size()) { dil[i + (size_t)d] = 1; }
                    }
                }
                active.swap(dil);
            }
        }

        std::vector<int> ids, idx;
        greedyCtc(grid.data(), (int)ready.size(), classes, ids, idx);

        int added = 0;
        for (size_t i = 0; i < ids.size(); i++) {
            const int64_t key = ready[(size_t)idx[i]];
            const double tChar = (double)key * framePeriod;
            const FrameAcc& a = frames[key];
            const float* row = grid.data() + (size_t)idx[i] * (size_t)classes;
            int runner = 0;
            float runnerP = row[0];
            for (int c = 1; c < classes; c++) {
                if (c == ids[i]) { continue; }
                if (row[c] > runnerP) { runnerP = row[c]; runner = c; }
            }
            const double agree = a.count > 0 ? (double)a.votes[(size_t)ids[i]] / (double)a.count : 0.0;
            const double t = (double)key * framePeriod;
            const char* sym = ids[i] > 0 ? tok[(size_t)ids[i]].c_str() : "·";
            std::printf("  + '%s' t=%7.3f key %5lld n=%2d p=%.3f blank=%.3f alt '%s' %.3f agree %.2f%s\n",
                        sym, t, (long long)key, a.count,
                        std::exp((double)row[ids[i]]), std::exp((double)row[0]),
                        runner > 0 ? tok[(size_t)runner].c_str() : "·",
                        std::exp((double)runnerP), agree,
                        active[(size_t)idx[i]] ? "" : "  [gated]");
            if (ids[i] <= 0) { continue; }
            if (std::exp((double)row[ids[i]]) < opt.minP) { continue; }
            if (agree < opt.minAgree) { continue; }
            if (!active[(size_t)idx[i]]) { continue; }

            // Word breaks from timing: the averaged grid smears the space token
            // over several frames, so a gap in the character stream is the
            // reliable signal for a word break.
            if (opt.spaceGap > 0.0 && !text.empty() && text.back() != ' ' && lastCharSec > 0.0) {
                const double gap = tChar - lastCharSec;
                double med = 0.15;
                if (gaps.size() >= 8) {
                    std::vector<double> v = gaps;
                    std::sort(v.begin(), v.end());
                    med = v[v.size() / 2];
                }
                const double wordGap = std::max(opt.spaceMin, med * opt.spaceGap);
                const double longGap = std::max(wordGap * 1.8, med * opt.spaceLong);
                if (gap > longGap) { text += "  "; }
                else if (gap > wordGap) { text += " "; }
                else if (gap > 0.0) {
                    gaps.push_back(gap);
                    if (gaps.size() > 96) { gaps.erase(gaps.begin()); }
                }
            }
            if (!tok[(size_t)ids[i]].empty() && tok[(size_t)ids[i]] != " ") { lastCharSec = tChar; }
            text += tok[(size_t)ids[i]];
            added++;
            totalChars++;
            probSum += std::exp((double)row[ids[i]]);
            agreeSum += agree;
            probCount++;
        }
        std::printf("  committed %d chars from %zu frames\n", added, ready.size());
        lastCommittedFrame = ready.back();
    }

    const std::string hyp = norm(text);
    std::printf("\n==== %s text (%zu chars raw, %zu normalized)\n%s\n",
                opt.mode.c_str(), text.size(), hyp.size(), hyp.c_str());
    if (probCount > 0) {
        std::printf("mean p of committed chars %.3f, mean argmax agreement %.2f\n",
                    probSum / (double)probCount, agreeSum / (double)probCount);
    }
    if (!opt.txt.empty()) {
        const std::string ref = norm(readFile(opt.txt));
        const int errs = levenshtein(ref, hyp);
        std::printf("reference %zu chars, errors %d, CER %.2f%%\n",
                    ref.size(), errs, 100.0 * (double)errs / (double)std::max<size_t>(1, ref.size()));
    }
    // sdrpp_core statics touch GL; skip them in a headless run.
    std::fflush(stdout);
    std::_Exit(0);
}