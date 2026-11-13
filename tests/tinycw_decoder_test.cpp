// SPDX-License-Identifier: MIT
// Copyright (C) 2026 DeepFist integration for SDR++ Brown
//
// Unit tests for the TinyCW CW decoder building blocks (band plan, range
// resolution, adaptive width policy, front-end DSP, CTC). These run headless in
// the SDR++ test harness:
//   ./sdrpp --test=tinycw_band_table --root <root>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../core/src/utils/flog.h"
#include "test_runner.h"
#include "test_utils.h"

#include "../decoder_modules/tinycw_decoder/src/tinycw_adapt.h"
#include "../decoder_modules/tinycw_decoder/src/tinycw_anchor.h"
#include "../decoder_modules/tinycw_decoder/src/tinycw_bands.h"
#include "../decoder_modules/tinycw_decoder/src/tinycw_calls.h"
#include "../decoder_modules/tinycw_decoder/src/tinycw_conditioner.h"
#include "../decoder_modules/tinycw_decoder/src/tinycw_ctc.h"
#include "../decoder_modules/tinycw_decoder/src/tinycw_fft.h"
#include "../decoder_modules/tinycw_decoder/src/tinycw_mainband.h"
#include "../decoder_modules/tinycw_decoder/src/tinycw_markers.h"
#include "../decoder_modules/tinycw_decoder/src/tinycw_spacing.h"
#include "../decoder_modules/tinycw_decoder/src/tinycw_spectrogram.h"
#include "../decoder_modules/tinycw_decoder/src/tinycw_squelch.h"
#include "../decoder_modules/tinycw_decoder/src/tinycw_textlayout.h"
#include "../decoder_modules/tinycw_decoder/src/tinycw_vote.h"
#include "../decoder_modules/tinycw_decoder/src/tinycw_zone.h"

#define DF_CHECK(cond)                                                       \
    do {                                                                     \
        if (!(cond)) {                                                       \
            flog::error("[tinycw test] {}:{} check failed: {}", __FILE__,   \
                        __LINE__, #cond);                                    \
            sdrpp::test::failed = true;                                      \
        }                                                                    \
    } while (0)

#define DF_CHECK_NEAR(a, b, eps)                                             \
    do {                                                                     \
        const double _a = (a);                                               \
        const double _b = (b);                                               \
        if (std::fabs(_a - _b) > (eps)) {                                    \
            flog::error("[tinycw test] {}:{} expected {} ~ {}, diff {}",    \
                        __FILE__, __LINE__, _a, _b, std::fabs(_a - _b));     \
            sdrpp::test::failed = true;                                      \
        }                                                                    \
    } while (0)

namespace {

// --------------------------------------------------------------------------
// Band plan and range resolution
// --------------------------------------------------------------------------
void testBandTable() {
    const auto& table = tinycw::cwBandTable();
    DF_CHECK(table.size() >= 10);

    // Every entry must be sane and ordered.
    for (size_t i = 0; i < table.size(); i++) {
        DF_CHECK(table[i].hiHz > table[i].loHz);
        if (i > 0) { DF_CHECK(table[i].loHz >= table[i - 1].hiHz); }
    }

    DF_CHECK(tinycw::findBand("20m") != nullptr);
    DF_CHECK_NEAR(tinycw::findBand("20m")->loHz, 14000000.0, 1.0);
    DF_CHECK_NEAR(tinycw::findBand("20m")->hiHz, 14070000.0, 1.0);
    DF_CHECK(tinycw::findBand("30m") != nullptr);
    DF_CHECK_NEAR(tinycw::findBand("30m")->loHz, 10100000.0, 1.0);
    // 40m is deliberately widened to cover contest activity.
    DF_CHECK_NEAR(tinycw::findBand("40m")->hiHz, 7060000.0, 1.0);
    DF_CHECK(tinycw::findBand("11m") == nullptr);
}

void testRangeResolve() {
    // 20m recording: center 14.034485 MHz, 192 kHz wide.
    const double centre = 14034485.0;
    const double span = 192000.0;

    {
        // Wide source: 7..17 MHz in view. The VFO band wins over the view centre.
        const tinycw::RangeResolution c = tinycw::resolveRange(12.0e6, 10.0e6, 0.0);
        const tinycw::RangeResolution v = tinycw::resolveRange(12.0e6, 10.0e6, 0.0, 0.0, 0.0, 14.030e6);
        DF_CHECK(v.valid && v.bandName == "20m");
        DF_CHECK(c.valid && c.bandName != "20m");
    }
    tinycw::RangeResolution r = tinycw::resolveRange(centre, span, 20000.0);
    DF_CHECK(r.valid);
    DF_CHECK(r.bandName == "20m");
    DF_CHECK(!r.segmentPartial);            // whole CW segment is in view
    DF_CHECK(r.limitedByMaxWidth);          // 70 kHz segment > 20 kHz limit
    DF_CHECK_NEAR(r.hiHz - r.loHz, 20000.0, 1.0);
    DF_CHECK(r.loHz >= 14000000.0 && r.hiHz <= 14070000.0);

    // No limit: the whole segment is decoded.
    tinycw::RangeResolution full = tinycw::resolveRange(centre, span, 0.0);
    DF_CHECK_NEAR(full.loHz, 14000000.0, 1.0);
    DF_CHECK_NEAR(full.hiHz, 14070000.0, 1.0);
    DF_CHECK(!full.limitedByMaxWidth);

    // User range narrower than the segment.
    tinycw::RangeResolution user = tinycw::resolveRange(centre, span, 0.0, 14020000.0, 14030000.0);
    DF_CHECK(user.valid);
    DF_CHECK_NEAR(user.loHz, 14020000.0, 1.0);
    DF_CHECK_NEAR(user.hiHz, 14030000.0, 1.0);

    // Partially visible segment: the source only reaches 14.040, so decoding is
    // limited to the intersection with the visible span.
    tinycw::RangeResolution partial = tinycw::resolveRange(14020000.0, 40000.0, 0.0);
    DF_CHECK(partial.valid);
    DF_CHECK(partial.segmentPartial);
    DF_CHECK_NEAR(partial.spanLoHz, 14000000.0, 1.0);
    DF_CHECK_NEAR(partial.spanHiHz, 14040000.0, 1.0);
    DF_CHECK_NEAR(partial.loHz, 14000000.0, 1.0);
    DF_CHECK_NEAR(partial.hiHz, 14040000.0, 1.0);

    // A wide span that holds several bands resolves to the band the centre is
    // in, not the lowest one: 2.5-12.5 MHz covers 60m, 40m and 30m.
    tinycw::RangeResolution wide = tinycw::resolveRange(7500000.0, 10000000.0, 0.0);
    DF_CHECK(wide.valid);
    DF_CHECK(wide.bandName == "40m");
    DF_CHECK_NEAR(wide.loHz, 7000000.0, 1.0);
    DF_CHECK_NEAR(wide.hiHz, 7060000.0, 1.0);

    // Same span centred on 20m must pick 20m (30m and 17m are also in view).
    tinycw::RangeResolution wide20 = tinycw::resolveRange(14050000.0, 10000000.0, 0.0);
    DF_CHECK(wide20.valid);
    DF_CHECK(wide20.bandName == "20m");
    DF_CHECK_NEAR(wide20.loHz, 14000000.0, 1.0);
    DF_CHECK_NEAR(wide20.hiHz, 14070000.0, 1.0);

    // Between bands, the nearest overlapping edge wins: 7.5 MHz is outside the
    // 40m segment (7.000-7.060) but inside no other band in view.
    tinycw::RangeResolution between = tinycw::resolveRange(7500000.0, 1000000.0, 0.0);
    DF_CHECK(between.valid);
    DF_CHECK(between.bandName == "40m");

    // A narrow view has exactly one candidate and is unaffected.
    tinycw::RangeResolution narrow = tinycw::resolveRange(14034485.0, 192000.0, 0.0);
    DF_CHECK(narrow.bandName == "20m");

    // 30m works too.
    tinycw::RangeResolution m30 = tinycw::resolveRange(10120000.0, 48000.0, 0.0);
    DF_CHECK(m30.valid);
    DF_CHECK(m30.bandName == "30m");

    // Nothing in view.
    tinycw::RangeResolution none = tinycw::resolveRange(5000000.0, 48000.0, 0.0);
    DF_CHECK(!none.valid);

    // Degenerate user setting (lo == hi) is widened to at least 1 kHz.
    tinycw::RangeResolution degen = tinycw::resolveRange(centre, span, 0.0, 14010000.0, 14010000.0);
    DF_CHECK(degen.valid);
    DF_CHECK(degen.hiHz - degen.loHz >= 1000.0);
}

// --------------------------------------------------------------------------
// Adaptive decoded-width policy (the "slider moves" logic)
// --------------------------------------------------------------------------
void testAdaptiveWidthPolicy() {
    tinycw::AdaptiveWidth w;
    w.ceilingHz = 70000.0;
    w.reset(70000.0);
    // Starts narrow and widens on demand (the CPU budget is unknown on start).
    DF_CHECK_NEAR(w.currentHz, w.minHz, 1.0);

    // Idle CPU must widen, but never past the ceiling.
    for (int i = 0; i < 200; i++) { w.update(10.0); }
    DF_CHECK_NEAR(w.currentHz, 70000.0, 1.0);

    // Saturated CPU must narrow again.
    const double before = w.currentHz;
    for (int i = 0; i < 5; i++) { w.update(99.0); }
    DF_CHECK(w.currentHz < before);

    // Manual mode never moves.
    w.autoMode = false;
    const double manual = w.currentHz;
    for (int i = 0; i < 20; i++) { w.update(99.0); }
    DF_CHECK_NEAR(w.currentHz, manual, 1.0);

    // Floor is respected.
    w.autoMode = true;
    for (int i = 0; i < 500; i++) { w.update(99.0); }
    DF_CHECK(w.currentHz >= w.minHz - 1.0);
    DF_CHECK(w.currentHz >= 2000.0);
}

// --------------------------------------------------------------------------
// Waterfall markers: yellow carrier / cyan decode, 5 s hold
// --------------------------------------------------------------------------
void testMarkerTracker() {
    tinycw::MarkerTracker t(5.0);
    std::vector<tinycw::Marker> out;

    // Carrier only: yellow is visible, cyan is not.
    t.noteCarrier(14040000.0, 100.0);
    t.collect(100.0, out);
    DF_CHECK(out.size() == 1);
    DF_CHECK(out[0].carrier);
    DF_CHECK(!out[0].decode);

    // Holds exactly 5 s, then disappears.
    t.collect(104.9, out);
    DF_CHECK(out.size() == 1 && out[0].carrier);
    t.collect(105.1, out);
    DF_CHECK(out.empty());
    DF_CHECK(t.size() == 0);

    // A decode adds the cyan flag without losing the yellow one.
    t.noteCarrier(7000100.0, 200.0);
    t.noteDecode(7000100.0, 200.5);
    t.collect(201.0, out);
    DF_CHECK(out.size() == 1);
    DF_CHECK(out[0].carrier && out[0].decode);

    // A decode also refreshes the carrier (text implies a carrier), so both run
    // until 205.5 and then expire together.
    t.collect(205.2, out);
    DF_CHECK(out.size() == 1);
    DF_CHECK(out[0].carrier);
    DF_CHECK(out[0].decode);
    t.collect(205.6, out);
    DF_CHECK(out.empty());

    // Nearby notes merge (same station re-detected), distant ones do not.
    t.clear();
    t.noteCarrier(14040000.0, 300.0);
    t.noteDecode(14040030.0, 300.1);
    DF_CHECK(t.size() == 1);
    t.noteCarrier(14043500.0, 300.2);
    DF_CHECK(t.size() == 2);

    // Custom hold time.
    tinycw::MarkerTracker fast(1.0);
    fast.noteCarrier(14000000.0, 0.0);
    fast.collect(0.9, out);
    DF_CHECK(out.size() == 1);
    fast.collect(1.1, out);
    DF_CHECK(out.empty());
}

// --------------------------------------------------------------------------
// Front-end DSP
// --------------------------------------------------------------------------
void testSpectrogram() {
    const int n = 19200;   // 6 s at 3200 Hz
    std::vector<float> audio((size_t)n);
    const double toneHz = 700.0;
    for (int i = 0; i < n; i++) {
        audio[(size_t)i] = (float)(0.5 * std::sin(2.0 * tinycw::kPi * toneHz * i / 3200.0));
    }

    tinycw::Spectrogram spec;
    std::vector<float> out;
    int frames = 0;
    DF_CHECK(spec.compute(audio.data(), n, out, frames));
    DF_CHECK(frames == 401);                       // torch.stft(center=True) geometry
    DF_CHECK(out.size() == (size_t)65 * (size_t)frames);

    // Global standardisation.
    double mean = 0.0;
    for (float v : out) { mean += v; }
    mean /= (double)out.size();
    double var = 0.0;
    for (float v : out) { var += ((double)v - mean) * ((double)v - mean); }
    var /= (double)(out.size() - 1);
    DF_CHECK_NEAR(mean, 0.0, 1e-3);
    DF_CHECK_NEAR(std::sqrt(var), 1.0, 1e-3);

    // The tone must dominate the correct frequency bin (700 Hz -> bin 56).
    const int expectedBin = (int)std::lround(toneHz / (3200.0 / tinycw::kFftSize)) - tinycw::kFreqBinLo;
    DF_CHECK(expectedBin >= 0 && expectedBin < tinycw::kFreqBins);
    double bestRow = -1.0e9;
    int bestRowIdx = -1;
    for (int b = 0; b < tinycw::kFreqBins; b++) {
        double sum = 0.0;
        for (int t = 0; t < frames; t++) { sum += out[(size_t)b * (size_t)frames + (size_t)t]; }
        if (sum > bestRow) { bestRow = sum; bestRowIdx = b; }
    }
    DF_CHECK(bestRowIdx == expectedBin);

    // Short input is rejected instead of crashing.
    std::vector<float> tiny(16, 0.0f);
    DF_CHECK(!spec.compute(tiny.data(), (int)tiny.size(), out, frames));
}

void testConditioner() {
    const int n = 19200;
    const double toneHz = 500.0;
    std::vector<float> audio((size_t)n);
    for (int i = 0; i < n; i++) {
        audio[(size_t)i] = (float)(0.3 * std::sin(2.0 * tinycw::kPi * toneHz * i / 3200.0));
    }

    DF_CHECK_NEAR(tinycw::detectTone(audio.data(), n), toneHz, 1.0);

    tinycw::Conditioner cond;
    std::vector<float> out;
    cond.process(audio.data(), n, out);
    DF_CHECK(out.size() == (size_t)n);

    double peak = 0.0;
    for (float v : out) { peak = std::max(peak, (double)std::fabs(v)); }
    DF_CHECK_NEAR(peak, 1.0, 1e-3);

    // The pitch must be re-centred on 600 Hz.
    DF_CHECK_NEAR(tinycw::detectTone(out.data(), n), tinycw::kOutPitchHz, 2.0);

    // Short windows pass through untouched (same rule as the Python front-end).
    std::vector<float> shortIn(64, 0.25f);
    std::vector<float> shortOut;
    cond.process(shortIn.data(), (int)shortIn.size(), shortOut);
    DF_CHECK(shortOut.size() == shortIn.size());
    DF_CHECK_NEAR(shortOut[10], 0.25, 1e-6);
}

// Keyed tone (dit 60 ms, 50 % duty) on [fromSec, toSec), added to audio.
void addKeyed(std::vector<float>& audio, double hz, double amp, double fromSec, double toSec) {
    for (size_t i = 0; i < audio.size(); i++) {
        const double t = (double)i / 3200.0;
        if (t < fromSec || t >= toSec || ((int)(t / 0.06)) % 2) { continue; }
        audio[i] += (float)(amp * std::sin(2.0 * tinycw::kPi * hz * t));
    }
}

void addNoise(std::vector<float>& audio, double amp) {
    unsigned int seed = 777;
    for (float& v : audio) {
        seed = seed * 1103515245u + 12345u;
        v += (float)((((seed >> 16) & 0x7fff) / 32768.0 - 0.5) * amp);
    }
}

// Presence wins over energy: a weak station keyed for 10 s beats a strong one
// keyed for 2 s, and the tone is refined to the exact FFT peak.
void testMainTone() {
    std::vector<float> audio(12 * 3200, 0.0f);
    addKeyed(audio, 503.0, 1.0, 0.0, 2.0);
    addKeyed(audio, 803.0, 0.1, 1.0, 11.0);
    addNoise(audio, 0.01);
    const tinycw::MainTone mt = tinycw::findMainTone(audio.data(), (int)audio.size());
    DF_CHECK(mt.found);
    {
        // Noise only: no band is present, so no tone (the caller uses its pitch).
        std::vector<float> nz(12 * 3200, 0.0f);
        addNoise(nz, 0.01);
        DF_CHECK(!tinycw::findMainTone(nz.data(), (int)nz.size()).found);
    }
    DF_CHECK_NEAR(mt.bandHz, 800.0, 0.1);
    DF_CHECK_NEAR(mt.toneHz, 803.0, 0.3);
    DF_CHECK(mt.presentSec >= 9);

    // Equal presence: the more energetic band wins.
    std::vector<float> two(12 * 3200, 0.0f);
    addKeyed(two, 503.0, 1.0, 0.0, 12.0);
    addKeyed(two, 803.0, 0.3, 0.0, 12.0);
    addNoise(two, 0.01);
    DF_CHECK_NEAR(tinycw::findMainTone(two.data(), (int)two.size()).bandHz, 500.0, 0.1);

    // Search range = own channel (700 +- 125 Hz): the neighbour at 1003 Hz,
    // present longer and stronger, is not picked.
    std::vector<float> nb(12 * 3200, 0.0f);
    addKeyed(nb, 1003.0, 1.0, 0.0, 12.0);
    addKeyed(nb, 703.0, 0.1, 2.0, 6.0);
    addNoise(nb, 0.01);
    DF_CHECK_NEAR(tinycw::findMainTone(nb.data(), (int)nb.size()).bandHz, 1000.0, 0.1);
    const tinycw::MainTone own = tinycw::findMainTone(nb.data(), (int)nb.size(), 575.0, 825.0);
    DF_CHECK(own.found);
    DF_CHECK_NEAR(own.bandHz, 700.0, 0.1);
    DF_CHECK_NEAR(own.toneHz, 703.0, 0.3);
}

// A forced tone keeps the weak station and rejects the strong one 300 Hz away.
void testConditionerForcedTone() {
    std::vector<float> audio(12 * 3200, 0.0f);
    for (size_t i = 0; i < audio.size(); i++) {
        audio[i] = (float)std::sin(2.0 * tinycw::kPi * 500.0 * (double)i / 3200.0);
    }
    addKeyed(audio, 800.0, 0.1, 0.0, 6.0);
    auto halfRatio = [&](double toneHz) {
        tinycw::Conditioner cond;
        std::vector<float> out;
        cond.process(audio.data(), (int)audio.size(), out, 3200, toneHz);
        const size_t h = out.size() / 2;
        double a = 0.0, b = 0.0;
        for (size_t i = 3200; i < h; i++) { a += (double)out[i] * out[i]; }
        for (size_t i = h + 3200; i < out.size(); i++) { b += (double)out[i] * out[i]; }
        return std::sqrt(b / a);   // second half (strong only) / first half (both)
    };
    const double forced = halfRatio(800.0), afc = halfRatio(0.0);
    flog::info("[tinycw test] forced-tone half ratio forced={} afc={}", forced, afc);
    DF_CHECK(forced < 0.4);
    DF_CHECK(afc > 0.8);
}

void testSquelch() {
    const int n = 19200;
    std::vector<float> keyed((size_t)n, 0.0f);
    for (int i = 0; i < n; i++) {
        const int element = (i / (3200 / 20)) % 4;
        if (element == 0) { keyed[(size_t)i] = (float)(0.6 * std::sin(2.0 * tinycw::kPi * 700.0 * i / 3200.0)); }
    }

    std::vector<float> noise((size_t)n);
    unsigned int seed = 12345;
    for (int i = 0; i < n; i++) {
        seed = seed * 1103515245u + 12345u;
        noise[(size_t)i] = ((float)((seed >> 16) & 0x7fff) / 32768.0f - 0.5f) * 0.1f;
    }

    const double keyedRatio = tinycw::keyingRatio(keyed.data(), n, 3200);
    const double noiseRatio = tinycw::keyingRatio(noise.data(), n, 3200);
    flog::info("[tinycw test] keying ratio keyed={} noise={}", keyedRatio, noiseRatio);
    DF_CHECK(keyedRatio > tinycw::kSquelchThreshold);
    DF_CHECK(noiseRatio < tinycw::kSquelchThreshold);
    DF_CHECK(tinycw::hasSignal(keyed.data(), n, 3200));
    DF_CHECK(!tinycw::hasSignal(noise.data(), n, 3200));

    // Level independence: raw SDR audio has no AGC.
    std::vector<float> quiet = keyed;
    for (int i = 0; i < n; i++) { quiet[(size_t)i] = keyed[(size_t)i] * 1e-5f + noise[(size_t)i] * 1e-6f; }
    std::vector<float> loud = keyed;
    for (int i = 0; i < n; i++) { loud[(size_t)i] = keyed[(size_t)i] + noise[(size_t)i] * 0.1f; }
    const double rq = tinycw::keyingRatio(quiet.data(), n, 3200);
    const double rl = tinycw::keyingRatio(loud.data(), n, 3200);
    flog::info("[tinycw test] keying ratio quiet={} loud={}", rq, rl);
    DF_CHECK(rq > tinycw::kSquelchThreshold);
    DF_CHECK(std::fabs(rq - rl) < 0.05 * rl);

    // Fast CW (30 WPM dots: 40 ms on, 40 ms off, no long gaps): the 10 ms
    // step resolves it; 150 ms steps averaged it into a flat envelope.
    const int n12 = 12 * 3200;
    std::vector<float> fast((size_t)n12, 0.0f);
    for (int i = 0; i < n12; i++) {
        const bool on = ((i / 128) % 2) == 0;   // 128 samples = 40 ms
        fast[(size_t)i] = (on ? 0.5f * (float)std::sin(2.0 * tinycw::kPi * 700.0 * i / 3200.0) : 0.0f) +
                          noise[(size_t)(i % n)] * 0.05f;
    }
    const double rf = tinycw::keyingRatio(fast.data(), n12, 3200);
    flog::info("[tinycw test] keying ratio fast CW={}", rf);
    DF_CHECK(rf > tinycw::kSquelchThreshold);

    // The band follows the pitch: a 1000 Hz station needs pitch 1000.
    std::vector<float> hi((size_t)n, 0.0f);
    for (int i = 0; i < n; i++) {
        const int element = (i / (3200 / 20)) % 4;
        hi[(size_t)i] = (element == 0 ? 0.6f * (float)std::sin(2.0 * tinycw::kPi * 1000.0 * i / 3200.0) : 0.0f) +
                        noise[(size_t)i] * 0.1f;
    }
    DF_CHECK(tinycw::keyingRatio(hi.data(), n, 3200, 1000.0) > tinycw::kSquelchThreshold);
    DF_CHECK(tinycw::keyingRatio(hi.data(), n, 3200, 700.0) < tinycw::kSquelchThreshold);

    // A strong carrier outside the band (1250 Hz, 60 dB over the keyed tone)
    // sets the unit-RMS scale. The fixed 1e-3 guard then swamped the in-band
    // noise: ratio 133 -> 1.4. The guard relative to the in-band noise keeps it.
    std::vector<float> nbClean((size_t)n), nbStrong((size_t)n);
    for (int i = 0; i < n; i++) {
        const int element = (i / (3200 / 20)) % 4;
        const double k = (element == 0) ? 0.01 * std::sin(2.0 * tinycw::kPi * 700.0 * i / 3200.0) : 0.0;
        const double nz = noise[(size_t)i] * 0.01;
        nbClean[(size_t)i] = (float)(k + nz);
        nbStrong[(size_t)i] = (float)(k + nz + 10.0 * std::sin(2.0 * tinycw::kPi * 1250.0 * i / 3200.0));
    }
    const double rc = tinycw::keyingRatio(nbClean.data(), n, 3200);
    const double rs = tinycw::keyingRatio(nbStrong.data(), n, 3200);
    flog::info("[tinycw test] keying ratio neighbour clean={} strong={}", rc, rs);
    DF_CHECK(rs > tinycw::kSquelchThreshold);
    DF_CHECK(rs > 0.7 * rc);
}

// Commit zones: overlapping windows commit each character once, even when its
// frame moves by a frame between windows.
// Voting: 12 s windows every 1 s. Characters at 13.0 s (id 10) and 14.0 s
// (id 11) jitter by +-1 frame; every 3rd window reads 14.0 s as id 12; one
// window inserts a spurious id 13 at 15.0 s.
void testVoteText() {
    const double fp = 0.03, win = 12.0;
    tinycw::VoteText v;
    std::vector<tinycw::VoteText::Slot> frozen;
    for (int w = 4; w <= 30; w++) {
        const double t0 = (double)w;
        const double jit = (w % 2) ? fp : -fp;
        std::vector<int> ids, frames;
        auto put = [&](int id, double sec) {
            const double rel = sec - t0;
            if (rel < 0.0 || rel >= win) { return; }
            ids.push_back(id);
            frames.push_back((int)std::lround(rel / fp));
        };
        put(10, 13.0 + jit);
        put(w % 3 == 0 ? 12 : 11, 14.0 - jit);
        if (w == 8) { put(13, 15.0); }
        v.add(ids, frames, t0, fp, win);
        for (const auto& s : v.takeFrozen()) { frozen.push_back(s); }
        if (w == 10) {
            // still open: both characters, majority symbol, no spurious one
            const auto open = v.open();
            DF_CHECK(open.size() == 2);
            DF_CHECK(open.size() == 2 && open[0].id == 10 && open[1].id == 11);
        }
    }
    flog::info("[tinycw test] vote frozen {} conf {} {}", (int)frozen.size(),
               frozen.empty() ? 0.0 : (double)frozen[0].conf,
               frozen.size() < 2 ? 0.0 : (double)frozen[1].conf);
    DF_CHECK(frozen.size() == 2);
    DF_CHECK(frozen.size() == 2 && frozen[0].id == 10 && frozen[1].id == 11);
    DF_CHECK(frozen.size() == 2 && frozen[0].conf > 0.95f);                       // unanimous
    DF_CHECK(frozen.size() == 2 && frozen[1].conf > 0.5f && frozen[1].conf < 0.8f); // 2 of 3
    DF_CHECK(v.open().empty());

    // End of a transmission: windows without keying (empty votes) keep
    // freezing, so the last characters reach the final text.
    {
        tinycw::VoteText e;
        int got = 0;
        for (int w = 0; w <= 3; w++) {   // characters at 10 s and 11 s
            std::vector<int> ids, frames;
            for (double sec : { 10.0, 11.0 }) {
                if (sec >= w && sec < w + win) { ids.push_back(20); frames.push_back((int)std::lround((sec - w) / fp)); }
            }
            e.add(ids, frames, (double)w, fp, win);
            got += (int)e.takeFrozen().size();
        }
        DF_CHECK(got == 0);
        DF_CHECK(e.open().size() == 2);
        for (int w = 12; w <= 13; w++) {  // the first windows that start after the last character
            e.add({}, {}, (double)w, fp, win);
            got += (int)e.takeFrozen().size();
        }
        DF_CHECK(got == 2);
        DF_CHECK(e.open().empty());
    }

    // Rendering keeps one confidence per byte.
    std::vector<std::string> tok = { "", "A", "<SK>" };
    tinycw::ConfText ct;
    tinycw::RenderState rs;
    tinycw::renderSlots({ { 1, 1.0, 1.0f }, { 2, 1.5, 0.4f } }, tok, ct, rs);
    DF_CHECK(ct.text == "A<SK>");
    DF_CHECK(ct.conf.size() == ct.text.size());
    DF_CHECK(ct.conf.size() == 5 && ct.conf[0] == 1.0f && ct.conf[4] == 0.4f);
}

void testCommitZone() {
    using tinycw::commitZone;
    const double fp = 0.03, win = 12.0, lag = 2.0;
    // characters at absolute 5.00, 9.50 and 9.99 s; 1 s hop between windows
    auto decode = [&](double t0, double jitter) {
        std::vector<int> ids, frames;
        const double at[3] = { 5.00, 9.50, 9.99 };
        for (int k = 0; k < 3; k++) {
            const double rel = at[k] + jitter - t0;
            if (rel < 0.0 || rel >= win) { continue; }
            ids.push_back(10 + k);
            frames.push_back((int)std::lround(rel / fp));
        }
        return std::make_pair(ids, frames);
    };
    tinycw::ZoneState st;
    std::vector<int> got;
    for (int w = 0; w < 6; w++) {
        const double t0 = (double)w;                        // windows [0,12) .. [5,17)
        const double jitter = (w % 2) ? 0.03 : -0.03;       // one frame back and forth
        auto d = decode(t0, jitter);
        for (const auto& c : commitZone(st, d.first, d.second, t0, fp, win, lag, t0 + 0.4)) {
            got.push_back(c.id);
        }
    }
    flog::info("[tinycw test] commit zone chars {}", (int)got.size());
    DF_CHECK(got.size() == 3);
    DF_CHECK(got.size() == 3 && got[0] == 10 && got[1] == 11 && got[2] == 12);
    DF_CHECK_NEAR(st.zoneEndSec, 15.0, 1e-9);

    // the same symbol within kZoneDupSec across a zone border is dropped
    tinycw::ZoneState s2;
    DF_CHECK(commitZone(s2, { 7 }, { 330 }, 0.0, fp, win, lag, 0.4).size() == 1);   // 9.90 s
    DF_CHECK(commitZone(s2, { 7 }, { 300 }, 1.0, fp, win, lag, 1.4).size() == 0);   // 10.00 s
}

// Word spaces from timing: synthetic keying at 20 WPM (dit 60 ms), characters
// placed with the fitted emission offsets, then the common-word fix.
std::string renderTimed(const std::string& text, double wordGapDits, double& wpm) {
    const std::vector<std::string>& tok = tinycw::tokens();
    const double dit = 0.060;
    std::vector<tinycw::VoteText::Slot> slots;
    double start = 1.0;
    bool first = true;
    for (char c : text) {
        if (c == ' ') { start += (wordGapDits - 3.0) * dit; continue; }
        const std::string sym(1, c);
        if (!first) { start += 3.0 * dit; }
        first = false;
        int id = -1;
        for (size_t i = 0; i < tok.size(); i++) {
            if (tok[i] == sym) { id = (int)i; }
        }
        slots.push_back({ id, start + tinycw::emitOffsetDits(sym) * dit, 1.0f });
        start += tinycw::morseDits(sym) * dit;
    }
    std::sort(slots.begin(), slots.end(),
              [](const tinycw::VoteText::Slot& a, const tinycw::VoteText::Slot& b) { return a.sec < b.sec; });
    tinycw::ConfText out;
    tinycw::RenderState st;
    tinycw::renderSlots(slots, tok, out, st);
    wpm = st.timer.wpm();
    std::string t = out.text;
    std::vector<float> c = out.conf;
    tinycw::fixWords(t, c);
    DF_CHECK(c.size() == t.size());
    return t;
}

bool endsWith(const std::string& s, const std::string& tail) {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

void testSpacing() {
    DF_CHECK(tinycw::morseDits("E") == 1);
    DF_CHECK(tinycw::morseDits("T") == 3);
    DF_CHECK(tinycw::morseDits("0") == 19);
    DF_CHECK(tinycw::morseDits("<SK>") == 15);
    DF_CHECK(tinycw::morseDits(" ") == 0);

    // Word fix: rejoin split common words, split glued ones, keep the rest.
    DF_CHECK(tinycw::fixWords("C Q T EST DL6KVA") == "CQ TEST DL6KVA");
    DF_CHECK(tinycw::fixWords("CQTESTDL6KVA") == "CQ TEST DL6KVA");
    DF_CHECK(tinycw::fixWords("5B4APLTEST") == "5B4APL TEST");
    DF_CHECK(tinycw::fixWords("TU5NN 314") == "TU 5NN 314");
    DF_CHECK(tinycw::fixWords("YB2MM 5N N 31") == "YB2MM 5NN 31");
    DF_CHECK(tinycw::fixWords("T U  DL6KVA") == "TU  DL6KVA");
    DF_CHECK(tinycw::fixWords("C QTEST DL6KVA") == "CQ TEST DL6KVA");
    DF_CHECK(tinycw::fixWords("C QUN0LM") == "CQ UN0LM");        // UN0LM calls CQ
    DF_CHECK(tinycw::fixWords("T EM") == "T EM");                // TEM is no common word
    DF_CHECK(tinycw::fixWords("CQ TEST") == "CQ TEST");          // two words stay two
    DF_CHECK(tinycw::fixWords("T  EST") == "T  EST");            // no join across a pause
    DF_CHECK(tinycw::fixWords("ATEST STUFF") == "ATEST STUFF");  // rest is no call
    DF_CHECK(tinycw::fixWords(" TEST ") == " TEST ");
    {
        std::string t = "C Q DE";
        std::vector<float> c = { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f };
        tinycw::fixWords(t, c);
        DF_CHECK(t == "CQ DE");
        DF_CHECK(c.size() == t.size());
        DF_CHECK(c[0] == 0.1f && c[1] == 0.3f);   // the joined space is gone
    }

    // Timing: the first kSpaceMinHistory pairs only learn the speed.
    double wpm = 0.0;
    const std::string std7 = renderTimed("DL6KVA DL6KVA CQ TEST DL6KVA 5NN 14", 7.0, wpm);
    flog::info("[tinycw test] spacing (word gap 7): '{}' wpm {}", std7, wpm);
    DF_CHECK(endsWith(std7, "CQ TEST DL6KVA 5NN 14"));
    DF_CHECK(std::fabs(wpm - 20.0) < 2.0);
    // Contest keyers shorten the word gap; 5 dits still splits.
    const std::string short5 = renderTimed("DL6KVA DL6KVA CQ TEST DL6KVA 5NN 14", 5.0, wpm);
    flog::info("[tinycw test] spacing (word gap 5): '{}'", short5);
    DF_CHECK(endsWith(short5, "CQ TEST DL6KVA 5NN 14"));
    // No word gaps at all: one run, no spaces made up by the timing.
    const std::string none = renderTimed("DL6KVADL6KVAIZ8GCB", 3.0, wpm);
    DF_CHECK(none.find(' ') == std::string::npos);
}

void testCalls() {
    using tinycw::isCall;
    DF_CHECK(isCall("DL6KVA"));
    DF_CHECK(isCall("5B4APL"));
    DF_CHECK(isCall("GM3ABC"));
    DF_CHECK(isCall("EA8/DL6KVA"));
    DF_CHECK(isCall("DL6KVA/P"));
    DF_CHECK(isCall("F/DL6KVA/P"));
    DF_CHECK(!isCall("TEST"));
    DF_CHECK(!isCall("5NN"));
    DF_CHECK(!isCall("599"));
    DF_CHECK(!isCall("DL6KVAD6"));
    DF_CHECK(!isCall("D/"));
    DF_CHECK(!isCall("A/B/C/D1A"));

    // Repeats, with misdecodes and glue: the vote finds the real calls.
    std::vector<std::string> segs = { "CQTESTDL6KVADL6KVA", "CQTESTL6KVADL6KVA", "TESTDL6KVADL6KVAD",
                                      "CQUN0LMTEST", "UN0LMUN0LM", "TUC0LM", "DL6KVA5NN14" };
    const std::vector<std::string> calls = tinycw::voteCalls(segs);
    DF_CHECK(std::find(calls.begin(), calls.end(), "DL6KVA") != calls.end());
    DF_CHECK(std::find(calls.begin(), calls.end(), "UN0LM") != calls.end());
    DF_CHECK(std::find(calls.begin(), calls.end(), "L6KVA") == calls.end());
    DF_CHECK(std::find(calls.begin(), calls.end(), "DL6KVAD") == calls.end());   // self-glue
    DF_CHECK(std::find(calls.begin(), calls.end(), "C0LM") == calls.end());      // variant of UN0LM
    DF_CHECK(std::find(calls.begin(), calls.end(), "DL6KVADL6KVA") == calls.end());

    // Display pass.
    const std::vector<std::string> lex = { "DL6KVA", "UN0LM", "SD6M" };
    auto run = [&](const std::string& t) { return tinycw::fixWords(tinycw::callPass(t, lex)); };
    DF_CHECK(run("DL6KVADL6KVA") == "DL6KVA DL6KVA");
    DF_CHECK(run("CQUN0LMTEST") == "CQ UN0LM TEST");
    DF_CHECK(run("CQUN 0LM") == "CQ UN0LM");
    DF_CHECK(run("SD6MSD6M") == "SD6M SD6M");
    DF_CHECK(run("TEST DL6KVD TEST") == "TEST DL6KVA TEST");     // one edit
    DF_CHECK(run("TEST L6KVA") == "TEST DL6KVA");                // first character lost
    DF_CHECK(run("TU DL6KVA  CQ TEST") == "TU DL6KVA  CQ TEST"); // a pause stays
    DF_CHECK(run("GM OM") == "GM OM");                           // free text keeps its spaces
    DF_CHECK(run("TESTTEST") == "TEST TEST");

    // The conf vector follows the text.
    std::string t = "CQUN 0LM";
    std::vector<float> c(t.size(), 0.9f);
    c[5] = 0.2f;
    tinycw::callPass(t, c, lex);
    DF_CHECK(t == "CQ UN0LM");
    DF_CHECK(c.size() == t.size());
    DF_CHECK(c.back() < 0.5f);   // the call takes its weakest character

    // GM: a common word, but not before a digit (Scottish prefix).
    DF_CHECK(tinycw::fixWords("G M DL6KVA") == "GM DL6KVA");
    DF_CHECK(tinycw::fixWords("GMDL6KVA") == "GM DL6KVA");
    DF_CHECK(tinycw::fixWords("TUGM") == "TU GM");
    DF_CHECK(tinycw::fixWords("GM3ABC") == "GM3ABC");

    // The lexicon votes from the raw characters, again after kLexiconUpdateSec.
    tinycw::CallLexicon lx;
    double sec = 1.0;
    for (int r = 0; r < 4; r++) {
        for (char ch : std::string("CQTESTDL6KVADL6KVA")) {
            lx.add(std::string(1, ch), sec);
            sec += 0.2;
        }
        sec += 3.0;
    }
    lx.update(sec);
    DF_CHECK(lx.calls().size() == 1 && lx.calls()[0] == "DL6KVA");
}

void testBandPowerFloat() {
    // The float band power matches a double DFT of the same frames.
    using namespace tinycw::mainband;
    std::vector<float> x(3200 * 3);
    uint32_t seed = 12345;
    for (size_t i = 0; i < x.size(); i++) {
        seed = seed * 1664525u + 1013904223u;
        const double noise = ((double)(seed >> 8) / (double)(1u << 24) - 0.5) * 0.02;
        x[i] = (float)(0.5 * std::sin(2.0 * M_PI * 610.0 * (double)i / 3200.0) + noise);
    }
    const auto db = tinycw::bandPowerDb(x.data(), (int)x.size());
    const std::vector<double> win = tinycw::hannSymmetric(kMbFrame);
    double maxErr = 0.0;
    for (int t = 0; t < (int)db[0].size(); t += 7) {
        for (int k = 0; k < kMbBins; k++) {
            double re = 0.0, im = 0.0;
            for (int m = 0; m < kMbFrame; m++) {
                const double v = (double)x[(size_t)(t * kMbHop + m)] * win[(size_t)m];
                re += v * std::cos(2.0 * M_PI * k * m / kMbFrame);
                im -= v * std::sin(2.0 * M_PI * k * m / kMbFrame);
            }
            const double ref = 10.0 * std::log10(re * re + im * im + 1e-12);
            if (ref > -60.0) { maxErr = std::max(maxErr, std::fabs(ref - db[(size_t)k][(size_t)t])); }
        }
    }
    flog::info("[tinycw test] band power float max error {} dB", maxErr);
    DF_CHECK(maxErr < 0.01);
}

void testTextLayout() {
    float adv[256];
    for (int c = 0; c < 256; c++) { adv[c] = 10.0f; }
    adv[(unsigned char)'W'] = 14.0f;
    const std::vector<float> none;

    // Fits: one run, width = sum of advances times the scale.
    tinycw::TextLayout a = tinycw::layoutText("CQ TEST", "", none, adv, 0.5f, 1000.0f);
    DF_CHECK(a.chars == "CQ TEST");
    DF_CHECK(a.runs.size() == 1 && a.runs[0].level == tinycw::kConfLevels - 1 && !a.runs[0].fresh);
    DF_CHECK_NEAR(a.width, 35.0f, 1e-4);

    // Too wide: characters are cut from the left.
    tinycw::TextLayout b = tinycw::layoutText("WWAB", "", none, adv, 1.0f, 25.0f);
    DF_CHECK(b.chars == "AB");
    DF_CHECK_NEAR(b.width, 20.0f, 1e-4);

    // Fresh suffix: a separate run; the confidence picks the level.
    std::vector<float> conf = { 1.0f, 1.0f, 0.0f, 1.0f, 1.0f };
    tinycw::TextLayout c = tinycw::layoutText("ABCDE", "DE", conf, adv, 1.0f, 1000.0f);
    DF_CHECK(c.chars == "ABCDE");
    DF_CHECK(c.runs.size() == 3);
    DF_CHECK(c.runs[0].a == 0 && c.runs[0].b == 2 && c.runs[0].level == tinycw::kConfLevels - 1);
    DF_CHECK(c.runs[1].a == 2 && c.runs[1].b == 3 && c.runs[1].level == 0);
    DF_CHECK(c.runs[2].a == 3 && c.runs[2].b == 5 && c.runs[2].fresh);
    DF_CHECK_NEAR(c.runs[2].x, 30.0f, 1e-4);

    // Cut into the fresh part: the confidence index still follows the text.
    std::vector<float> conf2 = { 1.0f, 1.0f, 1.0f, 0.0f, 1.0f };
    tinycw::TextLayout d = tinycw::layoutText("ABCDE", "CDE", conf2, adv, 1.0f, 30.0f);
    DF_CHECK(d.chars == "CDE");
    DF_CHECK(d.runs.size() == 3 && d.runs[1].level == 0 && d.runs[1].fresh);

    // Stale fresh text (not a suffix): only the fresh text, at full level.
    tinycw::TextLayout e = tinycw::layoutText("ABC", "XY", conf2, adv, 1.0f, 1000.0f);
    DF_CHECK(e.chars == "XY" && e.runs.size() == 1 && e.runs[0].fresh);

    // No room: nothing.
    DF_CHECK(tinycw::layoutText("ABC", "", none, adv, 1.0f, 0.0f).chars.empty());
}

void testRowAnchor() {
    tinycw::RowAnchor a;
    const float rowH = 15.0f;
    // First frame: the pad is scrolled out of view.
    DF_CHECK(a.update(0.0f, 3, 14030700.0, 300.0f, rowH) == 300.0f);
    // A row comes in above the focused row (index 3 -> 4): scroll one row down.
    DF_CHECK(a.update(300.0f, 4, 14030700.0, 300.0f, rowH) == 315.0f);
    // Two rows go away above it (4 -> 2).
    DF_CHECK(a.update(315.0f, 2, 14030720.0, 300.0f, rowH) == 285.0f);
    // A row below it: no change. The user scroll stays.
    DF_CHECK(a.update(250.0f, 2, 14030700.0, 300.0f, rowH) == 250.0f);
    // The focus moves to another station: no change.
    DF_CHECK(a.update(250.0f, 7, 14042000.0, 300.0f, rowH) == 250.0f);
    // The window grows by 40 px: the pad grows, the rows stay in place.
    DF_CHECK(a.update(250.0f, 7, 14042000.0, 340.0f, rowH) == 290.0f);
    // No focused row: no change.
    DF_CHECK(a.update(290.0f, -1, 0.0, 340.0f, rowH) == 290.0f);
    // The scroll never goes below zero.
    tinycw::RowAnchor b;
    b.update(0.0f, 5, 7000000.0, 0.0f, rowH);
    DF_CHECK(b.update(0.0f, 1, 7000000.0, 0.0f, rowH) == 0.0f);
}

void testCtc() {
    DF_CHECK(tinycw::classCount() == 48);
    DF_CHECK(tinycw::tokens()[0].empty());       // blank
    DF_CHECK(tinycw::tokens()[1] == " ");
    DF_CHECK(tinycw::tokens()[2] == "A");
    DF_CHECK(tinycw::tokens()[28] == "1");
    DF_CHECK(tinycw::tokens()[37] == "0");
    DF_CHECK(tinycw::tokens()[46] == "<SK>");

    // "HH_EE_LL_LL_OO" -> "HELLO" (blank = 0, repeats collapse).
    const int C = tinycw::classCount();
    const int T = 15;
    std::vector<float> logProbs((size_t)T * C, -10.0f);
    auto set = [&](int t, int id) { logProbs[(size_t)t * C + (size_t)id] = 0.0f; };
    int idH = -1, idE = -1, idL = -1, idO = -1;
    for (int i = 0; i < C; i++) {
        if (tinycw::tokens()[i] == "H") { idH = i; }
        if (tinycw::tokens()[i] == "E") { idE = i; }
        if (tinycw::tokens()[i] == "L") { idL = i; }
        if (tinycw::tokens()[i] == "O") { idO = i; }
    }
    DF_CHECK(idH > 0 && idE > 0 && idL > 0 && idO > 0);

    const int seq[T] = { idH, idH, 0, idE, idE, 0, idL, idL, 0, idL, 0, idO, idO, 0, idO };
    for (int t = 0; t < T; t++) { set(t, seq[t]); }

    std::vector<int> ids;
    std::vector<int> frames;
    tinycw::greedyCtc(logProbs.data(), T, C, ids, frames);
    // HH | blank | EE | blank | LL | blank | L | blank | OO | blank | O
    DF_CHECK(tinycw::idsToText(ids) == "HELLOO");
    DF_CHECK(ids.size() == 6);
    DF_CHECK(frames.size() == ids.size());
}

} // namespace

// --------------------------------------------------------------------------
// Harness wiring: every unit test runs headless in the first stable frame.
// --------------------------------------------------------------------------
namespace {
void runAll() {
    testBandTable();
    testRangeResolve();
    testAdaptiveWidthPolicy();
    testMarkerTracker();
    testSpectrogram();
    testConditioner();
    testMainTone();
    testConditionerForcedTone();
    testSquelch();
    testVoteText();
    testCommitZone();
    testCtc();
    testSpacing();
    testCalls();
    testRowAnchor();
    testTextLayout();
    testBandPowerFloat();
    if (!sdrpp::test::failed) { flog::info("[tinycw test] all unit checks passed"); }
}
} // namespace

static void setup_tinycw_unit() {
    sdrpp::test::renderLoopHook.verifyResultsFrames = 12;
    sdrpp::test::renderLoopHook.onStableRender = []() { runAll(); };
}

REGISTER_TEST(tinycw_units, ::setup_tinycw_unit);