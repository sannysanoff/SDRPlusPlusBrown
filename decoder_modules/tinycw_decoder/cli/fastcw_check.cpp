// SPDX-License-Identifier: MIT
// Copyright (C) 2026 DeepFist integration for SDR++ Brown
//
// Decodes whole 3200 Hz mono 16-bit WAV clips with the module's single-band
// chain: main tone (tinycw_mainband.h) -> conditioner forced to that tone ->
// spectrogram -> FastCW (tinycw::Model) -> greedy CTC. One line per file:
//   path <TAB> main band Hz <TAB> tone Hz <TAB> text <TAB> ms: main tone, conditioner+spectrogram, model
// Used to check the C++ port against DeepFist fastcw/single_exp.py.

// Windows.h defines min/max as macros; the DSP code below uses std::min/std::max.
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "tinycw_conditioner.h"
#include "tinycw_ctc.h"
#include "tinycw_mainband.h"
#include "tinycw_model.h"
#include "tinycw_spectrogram.h"

using namespace tinycw;

// Reads a PCM16 mono WAV; returns false if the format is not that.
static bool readWav(const char* path, std::vector<float>& out, int& rate) {
    FILE* f = std::fopen(path, "rb");
    if (!f) { return false; }
    std::vector<unsigned char> d;
    unsigned char buf[65536];
    size_t k;
    while ((k = std::fread(buf, 1, sizeof buf, f)) > 0) { d.insert(d.end(), buf, buf + k); }
    std::fclose(f);
    auto u32 = [&](size_t i) { return (uint32_t)d[i] | (uint32_t)d[i + 1] << 8 | (uint32_t)d[i + 2] << 16 | (uint32_t)d[i + 3] << 24; };
    auto u16 = [&](size_t i) { return (uint16_t)(d[i] | d[i + 1] << 8); };
    if (d.size() < 12 || std::memcmp(&d[0], "RIFF", 4) || std::memcmp(&d[8], "WAVE", 4)) { return false; }
    int channels = 0, bits = 0;
    for (size_t i = 12; i + 8 <= d.size();) {
        const uint32_t len = u32(i + 4);
        if (!std::memcmp(&d[i], "fmt ", 4)) {
            channels = u16(i + 10); rate = (int)u32(i + 12); bits = u16(i + 22);
        }
        else if (!std::memcmp(&d[i], "data", 4)) {
            if (channels != 1 || bits != 16) { return false; }
            const size_t n = std::min<size_t>(len, d.size() - i - 8) / 2;
            out.resize(n);
            for (size_t j = 0; j < n; j++) { out[j] = (int16_t)u16(i + 8 + 2 * j) / 32768.0f; }
            return true;
        }
        i += 8 + len + (len & 1);
    }
    return false;
}

int main(int argc, char** argv) {
    Model model;
    std::string err;
    if (!model.load(argc > 2 && std::strcmp(argv[1], "-w") == 0 ? argv[2] : "", err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    Conditioner cond;
    Spectrogram spec;
    std::vector<float> x, y, s, lp;
    for (int a = (argc > 2 && std::strcmp(argv[1], "-w") == 0) ? 3 : 1; a < argc; a++) {
        int rate = 0;
        if (!readWav(argv[a], x, rate) || rate != 3200) {
            std::fprintf(stderr, "%s: need 3200 Hz mono PCM16\n", argv[a]);
            continue;
        }
        const auto t0 = std::chrono::steady_clock::now();
        const MainTone mt = findMainTone(x.data(), (int)x.size());
        const auto t1 = std::chrono::steady_clock::now();
        cond.process(x.data(), (int)x.size(), y, 3200, mt.found ? mt.toneHz : 0.0);
        int frames = 0, outFrames = 0;
        const bool okSpec = spec.compute(y.data(), (int)y.size(), s, frames);
        const auto t2 = std::chrono::steady_clock::now();
        if (!okSpec || !model.infer(s.data(), kFreqBins, frames, lp, outFrames, err)) {
            std::fprintf(stderr, "%s: %s\n", argv[a], err.c_str());
            continue;
        }
        std::vector<int> ids, idx;
        greedyCtc(lp.data(), outFrames, classCount(), ids, idx);
        auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
        const auto t3 = std::chrono::steady_clock::now();
        std::printf("%s\t%.1f\t%.2f\t%s\t%.2f\t%.2f\t%.2f\n", argv[a], mt.bandHz, mt.toneHz,
                    idsToText(ids).c_str(), ms(t0, t1), ms(t1, t2), ms(t2, t3));
    }
    return 0;
}
