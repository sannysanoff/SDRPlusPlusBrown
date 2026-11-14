// SPDX-License-Identifier: MIT
// Copyright (C) 2026 TinyCW decoder for SDR++ Brown
//
// Headless entry point for the TinyCW CW decoder.
//
// It feeds a baseband WAV recording through the same Engine the GUI module uses
// (RxVFO channelizer, keying scanner, FastCW model) and prints decoded text in
// batches, so the decoder can be tested and tuned without starting the UI.

// Windows.h defines min/max as macros; the DSP code below uses std::min/std::max.
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <dsp/stream.h>
#include <dsp/types.h>
#include <utils/flog.h>
#include <utils/wav.h>

#include "tinycw_adapt.h"
#include "tinycw_bands.h"
#include "tinycw_engine.h"
#include "tinycw_model.h"

namespace {

// sdrpp_core carries GUI globals whose static destructors need a live GL
// context; skip them when running headless.
[[noreturn]] void finish(int code) {
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(code);
}

struct Options {
    std::string wav;
    std::string model;         // "" = embedded FastCW weights
    std::string band;
    double centerHz = 0.0;
    double focusHz = 0.0;          // > 0: also run the focus (tuned VFO) decoder here
    double rangeLoHz = 0.0;
    double rangeHiHz = 0.0;
    double maxWidthHz = 70000.0;
    double batchSec = 30.0;
    double speed = 0.0;            // 0 = as fast as the CPU allows
    double durationSec = 0.0;      // 0 = whole file
    int channels = 0;              // 0 = auto
    int threads = 0;               // 0 = auto
    double threadsCycleSec = 0.0;  // >0: rotate the worker count (live resize smoke test)
    double restartAtSec = 0.0;     // >0: stop + start the engine once (SDR++ play/stop reset test)
    bool verbose = false;
};

void usage() {
    std::printf(
        "TinyCW CW decoder (headless)\n"
        "\n"
        "  tinycw_cli --wav FILE [options]\n"
        "\n"
        "  --wav PATH          baseband WAV (stereo int16 IQ)\n"
        "  --center HZ         recording center frequency (default: parsed from the file name)\n"
        "  --focus HZ          also run the focus decoder (tuned VFO) at HZ, print it per batch\n"
        "  --band NAME         CW band to decode (20m, 40m, 30m, ...; default: first in view)\n"
        "  --range LO-HI       explicit range in kHz, overrides --band\n"
        "  --max-width KHZ     max decoded width (default 70)\n"
        "  --batch SEC         print decoded text every SEC seconds of audio (default 30)\n"
        "  --speed X           feed rate, 0 = as fast as CPU allows (default 0)\n"
        "  --duration SEC      stop after SEC seconds of audio\n"
        "  --channels N        max concurrent channels (default: derived from width)\n"
        "  --threads N         decode worker threads (default: auto, hw-1 capped at 4)\n"
        "  --threads-cycle SEC rotate the worker count every SEC (live resize test)\n"
        "  --restart-at SEC    stop + start the engine at SEC of audio, as SDR++ play/stop does;\n"
        "                      exit 1 if any CW state survives or decoding does not resume\n"
        "  --model PATH        FastCW weights file (default: embedded weights)\n"
        "  --no-adapt          disable adaptive width\n"
        "  --list-bands        print the CW band table and exit\n"
        "  --verbose           decoder log output\n"
        "  --help\n");
}

bool parseArgs(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto next = [&](const char* what) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", what);
                exit(2);
            }
            return argv[++i];
        };
        if (a == "--help" || a == "-h") { usage(); finish(0); }
        else if (a == "--wav") { o.wav = next("--wav"); }
        else if (a == "--focus") { o.focusHz = std::atof(next("--focus").c_str()); }
        else if (a == "--center") { o.centerHz = std::atof(next("--center").c_str()); }
        else if (a == "--band") { o.band = next("--band"); }
        else if (a == "--range") {
            const std::string v = next("--range");
            const size_t dash = v.find('-');
            if (dash == std::string::npos) { std::fprintf(stderr, "bad --range\n"); return false; }
            o.rangeLoHz = std::atof(v.substr(0, dash).c_str()) * 1000.0;
            o.rangeHiHz = std::atof(v.substr(dash + 1).c_str()) * 1000.0;
        }
        else if (a == "--max-width") { o.maxWidthHz = std::atof(next("--max-width").c_str()) * 1000.0; }
        else if (a == "--batch") { o.batchSec = std::atof(next("--batch").c_str()); }
        else if (a == "--speed") { o.speed = std::atof(next("--speed").c_str()); }
        else if (a == "--duration") { o.durationSec = std::atof(next("--duration").c_str()); }
        else if (a == "--channels") { o.channels = std::atoi(next("--channels").c_str()); }
        else if (a == "--threads") { o.threads = std::atoi(next("--threads").c_str()); }
        else if (a == "--threads-cycle") { o.threadsCycleSec = std::atof(next("--threads-cycle").c_str()); }
        else if (a == "--restart-at") { o.restartAtSec = std::atof(next("--restart-at").c_str()); }
        else if (a == "--model") { o.model = next("--model"); }
        else if (a == "--no-adapt") { /* handled in main() */ }
        else if (a == "--list-bands") {
            std::printf("CW segments (IARU R1):\n");
            for (const auto& b : tinycw::cwBandTable()) {
                std::printf("  %-5s %8.4f - %8.4f MHz\n", b.name, b.loHz / 1e6, b.hiHz / 1e6);
            }
            finish(0);
        }
        else if (a == "--verbose") { o.verbose = true; }
        else {
            std::fprintf(stderr, "unknown option: %s\n", a.c_str());
            usage();
            return false;
        }
    }
    return true;
}

// Parses "<something>_14034485Hz_" from a recording file name.
double centerFromName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    const std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    const size_t pos = name.find("Hz");
    if (pos == std::string::npos) { return 0.0; }
    size_t start = pos;
    while (start > 0 && (std::isdigit((unsigned char)name[start - 1]) || name[start - 1] == '.')) { start--; }
    if (start == pos) { return 0.0; }
    return std::atof(name.substr(start, pos - start).c_str());
}

std::string nowStamp() {
    char buf[32];
    std::time_t t = std::time(nullptr);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    std::strftime(buf, sizeof buf, "%H:%M:%S", &lt);
    return buf;
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parseArgs(argc, argv, opt)) { finish(2); }

    bool adapt = true;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--no-adapt") { adapt = false; }
    }

    if (opt.wav.empty()) {
        usage();
        finish(2);
    }

    wav::Reader reader(opt.wav);
    if (!reader.isValid()) {
        std::fprintf(stderr, "cannot read WAV: %s\n", reader.error.c_str());
        finish(1);
    }
    const int sr = (int)reader.getSampleRate();
    const int chans = (int)reader.getChannelCount();
    double center = (opt.centerHz > 0.0) ? opt.centerHz : centerFromName(opt.wav);
    if (center <= 0.0) {
        std::fprintf(stderr, "unknown center frequency, pass --center HZ\n");
        finish(1);
    }

    std::printf("TinyCW CLI\n  wav    %s\n  rate   %d Hz, %d ch, center %.0f Hz\n",
                opt.wav.c_str(), sr, chans, center);

    // --- model
    tinycw::Model model;
    const std::string& modelPath = opt.model;   // "" = embedded FastCW weights
    std::string err;
    if (!model.load(modelPath, err)) {
        std::fprintf(stderr, "cannot load model %s: %s\n", modelPath.c_str(), err.c_str());
        finish(1);
    }
    std::printf("  model  %s\n", model.path().c_str());

    // --- range resolution
    tinycw::RangeResolution res;
    if (!opt.band.empty()) {
        const tinycw::CwBandDef* b = tinycw::findBand(opt.band);
        if (b == nullptr) {
            std::fprintf(stderr, "unknown band: %s\n", opt.band.c_str());
            finish(1);
        }
        res.valid = true;
        res.bandName = b->name;
        res.segLoHz = b->loHz;
        res.segHiHz = b->hiHz;
        res.spanLoHz = center - sr * 0.5;
        res.spanHiHz = center + sr * 0.5;
        res.loHz = std::max(b->loHz, opt.rangeLoHz > 0.0 ? opt.rangeLoHz : b->loHz);
        res.hiHz = std::min(b->hiHz, opt.rangeHiHz > 0.0 ? opt.rangeHiHz : b->hiHz);
        res.settingLoHz = res.loHz;
        res.settingHiHz = res.hiHz;
        if (res.hiHz <= res.loHz) { res.valid = false; }
    }
    else {
        res = tinycw::resolveRange(center, (double)sr, opt.maxWidthHz, opt.rangeLoHz, opt.rangeHiHz);
    }
    if (!res.valid) {
        std::fprintf(stderr, "no CW segment of a known band is inside the recording span\n");
        finish(1);
    }

    // Global max width guard rail (same policy as the GUI module).
    tinycw::AdaptiveWidth width;
    width.ceilingHz = opt.maxWidthHz;
    width.autoMode = adapt;
    width.reset(opt.maxWidthHz, adapt);
    if (width.currentHz < res.hiHz - res.loHz) {
        const double mid = (res.loHz + res.hiHz) * 0.5;
        res.loHz = mid - width.currentHz * 0.5;
        res.hiHz = mid + width.currentHz * 0.5;
    }

    // Wide channelizer range: the whole CW segment that the recording covers.
    tinycw::RangeResolution full;
    if (!opt.band.empty()) { full = res; }
    else { full = tinycw::resolveRange(center, (double)sr, 0.0, opt.rangeLoHz, opt.rangeHiHz); }

    std::printf("  band   %s  segment %.4f-%.4f MHz\n", res.bandName.c_str(),
                res.segLoHz / 1e6, res.segHiHz / 1e6);
    std::printf("  decode %.4f-%.4f MHz (%.1f kHz), batch %.0f s, speed %s\n",
                res.loHz / 1e6, res.hiHz / 1e6, (res.hiHz - res.loHz) / 1000.0,
                opt.batchSec, opt.speed > 0.0 ? std::to_string(opt.speed).c_str() : "max");
    std::printf("\n");

    // --- engine
    tinycw::Engine engine(&model);
    engine.setDecodeThreads(opt.threads);
    dsp::stream<dsp::complex_t> iq("tinycw.cli.iq");
    if (!engine.start(&iq, (double)sr, err)) {
        std::fprintf(stderr, "engine start failed: %s\n", err.c_str());
        finish(1);
    }
    engine.setWideRange(center, full.valid ? full.loHz : res.loHz,
                        full.valid ? full.hiHz : res.hiHz);
    engine.setRange(center, res.loHz, res.hiHz);
    int chanCap = opt.channels;
    if (chanCap <= 0) {
        chanCap = std::clamp((int)std::lround((res.hiHz - res.loHz) / 250.0), 8, 160);
    }
    engine.setMaxChannels(chanCap);
    if (opt.focusHz > 0.0) { engine.setFocusFreq(opt.focusHz); }

    // --- feeder
    std::atomic_bool feeding{ true };
    std::atomic_bool eof{ false };
    std::atomic<double> fedSeconds{ 0.0 };
    std::atomic<int64_t> totalFrames{ 0 };

    std::thread feeder([&] {
        const int framesPerChunk = sr / 4;            // 250 ms
        const size_t bytes = (size_t)framesPerChunk * chans * 2;
        std::vector<int16_t> raw((size_t)framesPerChunk * chans);
        std::vector<dsp::complex_t> buf((size_t)framesPerChunk);
        double fed = 0.0;

        while (feeding.load()) {
            const size_t got = reader.readSamples2(raw.data(), bytes);
            if (got == 0) { break; }
            const int frames = (int)(got / (size_t)(chans * 2));
            for (int i = 0; i < frames; i++) {
                float re = raw[(size_t)i * chans] / 32768.0f;
                float im = (chans > 1) ? raw[(size_t)i * chans + 1] / 32768.0f : 0.0f;
                buf[(size_t)i] = dsp::complex_t{ re, im };
            }
            const auto t0 = std::chrono::steady_clock::now();
            std::memcpy(iq.writeBuf, buf.data(), (size_t)frames * sizeof(dsp::complex_t));
            if (!iq.swap(frames)) { break; }
            fed += (double)frames / (double)sr;
            fedSeconds = fed;
            totalFrames += frames;

            if (opt.durationSec > 0.0 && fed >= opt.durationSec) { break; }

            if (opt.speed > 0.0) {
                const double want = (double)frames / ((double)sr * opt.speed);
                const double spent = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - t0).count();
                if (want > spent) {
                    std::this_thread::sleep_for(std::chrono::duration<double>(want - spent));
                }
            }
            else {
                // As fast as the decoder can absorb.
                while (feeding.load() && engine.cpuLoadPercent() > 60.0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            }
        }
        eof = true;
    });

    // --- batching loop (main thread)
    double lastBatchAudio = 0.0;
    int linesTotal = 0;
    int charsTotal = 0;
    double peakCpu = 0.0;
    double lastAdaptWidth = width.currentHz;
    int batches = 0;
    bool restarted = false;
    bool restartClean = false;
    int charsAtRestart = 0;
    auto started = std::chrono::steady_clock::now();
    auto wallSec = 0.0;

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        wallSec = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

        if (adapt && width.autoMode) {
            if (width.update(engine.busyPercent())) {
                if (std::fabs(width.currentHz - lastAdaptWidth) > 500.0) {
                    std::printf("  [adapt] load %.0f%% (%.0f%% of %d00%%) -> max width %.1f kHz\n",
                                engine.cpuLoadPercent(), engine.busyPercent(),
                                tinycw::Engine::hardwareThreads(), width.currentHz / 1000.0);
                    std::fflush(stdout);
                    lastAdaptWidth = width.currentHz;
                }
                const double mid = (full.loHz + full.hiHz) * 0.5;
                const double half = width.currentHz * 0.5;
                const double loNow = std::max(full.loHz, mid - half);
                const double hiNow = std::min(full.hiHz, mid + half);
                engine.setRange(center, loNow, hiNow);
                if (opt.channels <= 0) {
                    engine.setMaxChannels(std::clamp((int)std::lround((hiNow - loNow) / 250.0), 8, 160));
                }
            }
        }

        if (opt.threadsCycleSec > 0.0) {
            const int hw = std::max(1, tinycw::Engine::hardwareThreads());
            const int want = 1 + (int)(wallSec / opt.threadsCycleSec) % std::min(hw, 6);
            if (want != engine.decodeThreads()) {
                engine.setDecodeThreads(want);
                std::printf("  [threads] -> %d\n", engine.decodeThreads());
                std::fflush(stdout);
            }
        }

        const double fed = fedSeconds.load();
        if (opt.restartAtSec > 0.0 && !restarted && fed >= opt.restartAtSec) {
            restarted = true;
            const size_t rowsBefore = engine.decodeLines(0.0).size();
            std::string focusStr;
            std::vector<float> focusConf;
            engine.focusTextConf(focusStr, focusConf);
            const size_t focusBefore = focusStr.size();
            engine.takeBatch();
            engine.stop();
            if (!engine.start(&iq, (double)sr, err)) {
                std::fprintf(stderr, "engine restart failed: %s\n", err.c_str());
                finish(1);
            }
            const size_t rows = engine.decodeLines(0.0).size();
            const size_t marks = engine.activeMarkers().size();
            engine.focusTextConf(focusStr, focusConf);
            const size_t focus = focusStr.size();
            const int chans = engine.liveChannels();
            restartClean = (rows == 0 && marks == 0 && focus == 0 && chans == 0 && engine.spotCount() == 0);
            std::printf("  [restart] at %.0f s: before rows %zu focus %zu chars | after rows %zu markers %zu "
                        "focus %zu chars channels %d -> %s\n",
                        fed, rowsBefore, focusBefore, rows, marks, focus, chans,
                        restartClean ? "clean" : "NOT CLEAN");
            std::fflush(stdout);
            charsAtRestart = charsTotal;
        }
        if (fed - lastBatchAudio >= opt.batchSec) {
            lastBatchAudio = fed;
            auto lines = engine.takeBatch();
            batches++;
            std::printf("===== batch %d @ %s | audio %.0f-%.0f s | %s | channels %d | cpu %.0f%%/%d00%% | width %.1f kHz\n",
                        batches, nowStamp().c_str(),
                        std::max(0.0, fed - opt.batchSec), fed,
                        res.bandName.c_str(), engine.liveChannels(), engine.busyPercent(),
                        tinycw::Engine::hardwareThreads(), width.currentHz / 1000.0);
            {
                const auto st = engine.takePerfStats();
                if (st.passes > 0) {
                    std::printf("  perf: front-end %.2f ms/ch | ctc %.3f ms/ch | model %.1f ms/pass "
                                "for %.1f ch (%.3f ms/ch) | detect %.2f ms/tick (%.3f ms/ch)\n",
                                st.feMsPerChannel, st.ctcMsPerChannel, st.inferMsPerPass,
                                st.channelsPerPass, st.inferMsPerChannel,
                                st.detectMsPerTick, st.detectMsPerChannel);
                    std::printf("  duty over %.1f s: detect %.1f%% of wall | model thread %.1f%% | "
                                "front-end %.1f%% cpu | passes %.2f/s | decode %.1f%%\n",
                                st.windowSec, st.detectDutyPct, st.modelDutyPct, st.feCpuPct,
                                st.roundsPerSec, st.decodeDutyPct);
                    std::printf("  open %.2f ms/tick\n", st.openMsPerTick);
                }
            }
            for (const auto& l : lines) {
                std::printf("  %8.1f kHz | %s\n", l.freqHz / 1000.0, l.text.c_str());
                linesTotal++;
                charsTotal += (int)l.text.size();
            }
            if (opt.focusHz > 0.0) {
                // The wide chain retunes after start: (re)try until the VFO is open.
                if (!engine.focusActive()) { engine.setFocusFreq(opt.focusHz); }
                // Vote confidence < 0.5 is printed in lower case (the GUI draws it darker).
                std::string ft;
                std::vector<float> fc;
                engine.focusTextConf(ft, fc);
                if (fc.size() == ft.size()) {
                    for (size_t i = 0; i < ft.size(); i++) {
                        if (fc[i] < 0.5f) { ft[i] = (char)std::tolower((unsigned char)ft[i]); }
                    }
                }
                std::printf("  focus %.1f kHz (%s) | %s\n", opt.focusHz / 1000.0,
                            engine.focusActive() ? "on" : "off", ft.c_str());
            }
            peakCpu = std::max(peakCpu, engine.busyPercent());
            std::fflush(stdout);
        }

        if (eof.load()) {
            // Drain: wait until every channel has decoded its backlog.
            if (engine.backlogSeconds() <= 0.0) {
                auto lines = engine.takeBatch();
                std::printf("===== final @ %s | audio %.0f s | %s | cpu %.0f%%/%d00%%\n",
                            nowStamp().c_str(), fed, res.bandName.c_str(), engine.busyPercent(),
                            tinycw::Engine::hardwareThreads());
                for (const auto& l : lines) {
                    std::printf("  %8.1f kHz | %s\n", l.freqHz / 1000.0, l.text.c_str());
                    linesTotal++;
                    charsTotal += (int)l.text.size();
                }
                peakCpu = std::max(peakCpu, engine.busyPercent());
                std::fflush(stdout);
                break;
            }
        }

        if (opt.durationSec > 0.0 && fed >= opt.durationSec && engine.backlogSeconds() <= 0.0) {
            break;
        }
    }

    feeding = false;
    iq.stopWriter();
    if (feeder.joinable()) { feeder.join(); }
    engine.stop();

    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::printf("\nprocessed %.0f s of audio in %.1f s wall (%.1fx realtime)\n",
                fedSeconds.load(), wall, wall > 0.0 ? fedSeconds.load() / wall : 0.0);
    std::printf("decoded %d lines / %d chars, peak cpu %.0f%% of %d00%%\n",
                linesTotal, charsTotal, peakCpu, std::max(1, engine.decodeThreads()));
    if (opt.restartAtSec > 0.0) {
        const int after = charsTotal - charsAtRestart;
        const bool ok = restarted && restartClean && after > 0;
        std::printf("restart check: %s (%d chars decoded after the restart)\n", ok ? "PASS" : "FAIL", after);
        finish(ok ? 0 : 1);
    }
    finish(0);
}