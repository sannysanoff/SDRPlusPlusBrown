// Standalone regression: clang++ -std=c++17 -O2 tests/vowel_squelch_timing.cpp core/src/utils/flog.cpp
//   -Icore/src -I/opt/homebrew/include -L/opt/homebrew/lib -lfftw3f -lvolk -o /tmp/vsq-timing
// Run without NDEBUG: this checks a short voiced burst, next-sample opening, exact replay and hold.
#include <dsp/noise_reduction/vowel_squelch.h>
#include <cassert>
#include <cstdio>
#include <cmath>
#include <vector>

// A large negative SSB offset leaves only the upper voice harmonics inside the selected sideband.
// Their movement still follows the original (inaudible) fundamental, rather than frequency/spacing indices.
static void testShiftedMotion(bool rigid) {
    constexpr int rate = 48000, block = 480;
    dsp::noise_reduction::VowelSquelch squelch;
    squelch.init(nullptr, rate);
    squelch.setSensitivity(0.6);
    std::vector<dsp::stereo_t> input(block), output(block);
    double voicePhase = 0.0, offsetPhase = 0.0;
    for (int pos = 0; pos < 3 * rate; pos += block) {
        for (int i = 0; i < block; i++) {
            const double t = (double)(pos + i) / rate;
            const double pitch = rigid ? 130.0 : 130.0 + 12.0 * std::sin(2.0 * M_PI * 2.0 * t);
            const double offset = -650.0 + (rigid ? 10.0 * std::sin(2.0 * M_PI * 2.0 * t) : 0.0);
            voicePhase += 2.0 * M_PI * pitch / rate;
            offsetPhase += 2.0 * M_PI * offset / rate;
            double value = 0.0;
            // Lower partials fall outside the selected sideband; do not fold them into real audio.
            for (int n = 6; n <= 11; n++) { value += 0.04 * std::sin(n * voicePhase + offsetPhase + 0.17 * n) / std::sqrt((double)n); }
            input[(size_t)i] = dsp::stereo_t{ (float)value, (float)value };
        }
        squelch.process(block, input.data(), output.data());
    }
    if (rigid) { assert(squelch.detectionCount() == 0); }
    else { assert(squelch.detectionCount() > 0.75 * squelch.hopCount()); }
    printf("PASS: -650 Hz SSB offset, %s: %d/%d detections\n", rigid ? "rigid drift stays closed" : "voiced pitch stays detectable", squelch.detectionCount(), squelch.hopCount());
}

int main() {
    constexpr int rate = 48000;
    constexpr int count = 3 * rate;
    std::vector<dsp::stereo_t> input(count), output(count);
    double phase = 0.0;
    for (int i = 0; i < count; i++) {
        const double t = (double)i / rate;
        double value = 0.001;
        if (t >= 0.7 && t < 0.78) {
            phase += 2.0 * M_PI * (130.0 + 200.0 * (t - 0.7)) / rate;
            for (int h = 1; h <= 10; h++) { value += 0.08 * std::sin(h * phase + 0.17 * h) / std::sqrt((double)h); }
        }
        input[(size_t)i] = dsp::stereo_t{ (float)value, (float)value };
    }
    const int marker = rate / 2;
    input[(size_t)marker] = dsp::stereo_t{ 0.07f, 0.04f };
    dsp::noise_reduction::VowelSquelch squelch;
    squelch.init(nullptr, rate);
    squelch.setSensitivity(0.6);
    squelch.setDecay(1.0);
    int lastCount = 0, firstDetection = -1, lastDetection = -1, firstOpen = -1, lastClose = -1;
    bool wasOpen = false;
    for (int i = 0; i < count; i++) {
        squelch.process(1, &input[(size_t)i], &output[(size_t)i]);
        if (squelch.detectionCount() != lastCount) {
            if (firstDetection < 0) { firstDetection = i; }
            lastDetection = i;
            lastCount = squelch.detectionCount();
        }
        const bool open = squelch.isOpen();
        if (open && firstOpen < 0) { firstOpen = i; }
        if (!open && wasOpen) { lastClose = i; }
        wasOpen = open;
    }
    assert(firstDetection >= 0);
    assert(firstDetection <= (int)std::lround(0.78 * rate));
    const auto diagnostics = squelch.diagnosticsSince(0);
    bool shortPathDetected = false;
    for (const auto& d : diagnostics) {
        if (d.detected && d.bands[d.selectedBand].trackingMs < 60) { shortPathDetected = true; }
    }
    assert(shortPathDetected);
    assert(firstOpen == firstDetection + 1);
    const int delay = (int)std::lround(squelch.kPrerollSec * rate);
    assert(delay == rate / 2);
    assert(firstOpen < marker + delay);
    assert(output[(size_t)(marker + delay)].l == input[(size_t)marker].l);
    assert(output[(size_t)(marker + delay)].r == input[(size_t)marker].r);
    assert(lastClose == lastDetection + (int)std::lround((1.0 + squelch.kPrerollSec) * rate));
    for (int i = 0; i < delay; i++) { assert(output[(size_t)i].l == 0.0f && output[(size_t)i].r == 0.0f); }
    printf("PASS: 80 ms voiced burst detected at %.6f s, open %.6f s (next sample), marker replay exactly +0.500 s, source-audio hold exactly 1.000 s\n", (double)firstDetection / rate, (double)firstOpen / rate);
    testShiftedMotion(false);
    testShiftedMotion(true);
    return 0;
}
