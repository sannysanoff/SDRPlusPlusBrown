// SPDX-License-Identifier: MIT
// Copyright (C) 2026 DeepFist integration for SDR++ Brown
//
// End-to-end test: plays a real CW contest baseband recording through the file
// source, lets the module decode the whole CW segment, and checks that
//   * the model was loaded (weights present in <root>/tinycw/),
//   * the keying scanner opened channels,
//   * decoded text arrives on the module interface,
//   * the adaptive width policy produced a CPU measurement.
//
// Run (needs a display / audio device, so use lldb if the app aborts headless):
//   ./sdrpp -r root_dev -t tinycw_wav_e2e
//
// The recording is expected in <test_root>/test_files/:
//   baseband_14034485Hz_19-09-12_13-08-2022---cw-contest-good-quality-.wav

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>

#include "../core/src/core.h"
#include "../core/src/gui/gui.h"
#include "../core/src/gui/main_window.h"
#include "../core/src/gui/tuner.h"
#include "../core/src/module.h"
#include "../core/src/module_com.h"
#include "../core/src/utils/flog.h"
#include "test_runner.h"
#include "test_utils.h"

#include "../decoder_modules/tinycw_decoder/src/tinycw_interface.h"

namespace {

const char* kWavName = "baseband_14034485Hz_19-09-12_13-08-2022---cw-contest-good-quality-.wav";
const double kCenterHz = 14034485.0;

struct E2EState {
    bool started = false;
    bool modelSeen = false;
    bool channelsSeen = false;
    bool textSeen = false;
    bool cpuSeen = false;
    int linesTotal = 0;
    int charsTotal = 0;
    std::string sample;
};

E2EState g_state;
std::chrono::steady_clock::time_point g_startedAt;

TinyCWInterface* findModule() {
    auto ifaces = core::moduleManager.getAllInterfaces<TinyCWInterface>("TinyCWInterface");
    if (ifaces.empty()) { return nullptr; }
    return ifaces[0];
}

} // namespace

static void collectState() {
        TinyCWInterface* mod = findModule();
        if (mod == nullptr) { return; }

        if (mod->isReady()) { g_state.modelSeen = true; }

        const int live = mod->liveChannels();
        if (live > 0) { g_state.channelsSeen = true; }

        const double cpu = mod->busyPercent();
        if (cpu > 0.0) { g_state.cpuSeen = true; }

        auto lines = mod->takeDecodedLines();
        for (const auto& line : lines) {
            g_state.linesTotal++;
            const size_t bar = line.find('|');
            const std::string text = (bar == std::string::npos) ? line : line.substr(bar + 1);
            g_state.charsTotal += (int)text.size();
            if (g_state.sample.size() < 200) {
                g_state.sample += text.substr(0, 60) + " / ";
            }
            if (text.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ") != std::string::npos) {
                g_state.textSeen = true;
            }
        }
}

static void setup_tinycw_wav_e2e() {
    int seconds = 90;   // the module emits its first batch after 30 s
    if (const char* env = std::getenv("TINYCW_E2E_SECONDS")) {
        const int v = std::atoi(env);
        if (v > 0) { seconds = v; }
    }

    // The render loop runs faster than realtime, so the test stops on wall clock
    // rather than on a frame count.
    sdrpp::test::renderLoopHook.verifyResultsFrames = 100000000;
    sdrpp::test::renderLoopHook.onFirstRender = []() {
        g_startedAt = std::chrono::steady_clock::now();
    };

    sdrpp::test::renderLoopHook.onStableRender = []() {
        // The harness only creates instances listed in the config; make sure the
        // decoder exists for this test regardless of the root config.
        if (findModule() == nullptr) {
            // createInstance returns 0 on success (not a bool).
            if (core::moduleManager.createInstance("TinyCW", "tinycw_decoder") != 0) {
                flog::error("[tinycw e2e] cannot create the tinycw_decoder instance");
                sdrpp::test::failed = true;
                return;
            }
        }
        sdrpp::test::selectWavFile(kWavName);
        // The file source does not move the waterfall by itself; tune to the
        // recording center so the CW band plan resolves to 20m.
        tuner::tune(tuner::TUNER_MODE_NORMAL, gui::waterfall.selectedVFO, kCenterHz);
        gui::mainWindow.setPlayState(true);
        g_state.started = true;
        flog::info("[tinycw e2e] playback started at the recording center");
    };

    sdrpp::test::renderLoopHook.onEachRender = [seconds]() {
        collectState();
        const double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - g_startedAt).count();
        if (elapsed < (double)seconds) { return; }

        flog::info("[tinycw e2e] module={} model={} channels={} text={} cpu={} lines={} chars={}",
                   findModule() != nullptr, g_state.modelSeen, g_state.channelsSeen,
                   g_state.textSeen, g_state.cpuSeen, g_state.linesTotal, g_state.charsTotal);
        if (!g_state.sample.empty()) {
            flog::info("[tinycw e2e] sample: {}", g_state.sample);
        }

        if (findModule() == nullptr) {
            flog::error("[tinycw e2e] module interface not found (is tinycw_decoder.dylib installed?)");
            sdrpp::test::failed = true;
            sdrpp::test::renderLoopHook.shouldExit = true;
            return;
        }
        if (!g_state.started) {
            flog::error("[tinycw e2e] playback never started");
            sdrpp::test::failed = true;
            sdrpp::test::renderLoopHook.shouldExit = true;
            return;
        }
        if (!g_state.modelSeen) {
            flog::error("[tinycw e2e] model never became ready (weights in <root>/tinycw/?)");
            sdrpp::test::failed = true;
            sdrpp::test::renderLoopHook.shouldExit = true;
            return;
        }
        if (!g_state.channelsSeen) {
            flog::error("[tinycw e2e] scanner never opened a channel");
            sdrpp::test::failed = true;
            sdrpp::test::renderLoopHook.shouldExit = true;
            return;
        }
        if (!g_state.textSeen) {
            flog::error("[tinycw e2e] no decoded characters arrived");
            sdrpp::test::failed = true;
            sdrpp::test::renderLoopHook.shouldExit = true;
            return;
        }
        if (!g_state.cpuSeen) {
            flog::error("[tinycw e2e] decode thread never reported CPU usage");
            sdrpp::test::failed = true;
            sdrpp::test::renderLoopHook.shouldExit = true;
            return;
        }
        sdrpp::test::renderLoopHook.shouldExit = true;
    };
}

REGISTER_TEST(tinycw_wav_e2e, ::setup_tinycw_wav_e2e);