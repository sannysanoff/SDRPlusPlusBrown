#include "../core/src/gui/widgets/waterfall.h"
#include "../core/src/imgui/imgui.h"
#include "../core/src/imgui/imgui_internal.h"
#include <algorithm>

#include <chrono>
#include <thread>
#include <fstream>
#include <cstdlib>
#include <string>
#include <memory>
#include "../core/src/gui/main_window.h"
#include "../core/src/gui/tuner.h"
#include "../core/src/gui/gui.h"
#include "../core/src/core.h"
#include "../core/src/signal_path/signal_path.h"

#include "../decoder_modules/radio/src/radio_module_interface.h"
#include "test_utils.h"

#include "test_runner.h"

class FileSourceModule;
// Path to test file
const std::string TEST_FILE_NAME = "baseband_13975142Hz_12-03-56_17-02-2024-cw-contest.wav";

static double maxSnr = 0;

static void checkMaxSnr() {
    if (gui::waterfall.selectedVFOSNR > maxSnr) {
        maxSnr = gui::waterfall.selectedVFOSNR;
    }
}


// Test that plays a file and checks that SNR is never above 30 dB
int setup_baseband_file_play(long long freq) {

    sdrpp::test::renderLoopHook.verifyResultsFrames = 40;

    sdrpp::test::renderLoopHook.onStableRender = [=]() {
        {
            sdrpp::test::selectWavFile(TEST_FILE_NAME);

            sdrpp::test::selectRadioMode(RADIO_DEMOD_USB);


            // Start playback
            gui::mainWindow.setPlayState(true);

            tuner::tune(tuner::TUNER_MODE_NORMAL, gui::waterfall.selectedVFO, freq);
        }
    };

    sdrpp::test::renderLoopHook.onEachRender = []() {
        checkMaxSnr();
    };


    return 0;
}

static void setup_baseband_file_play_noise() {
    setup_baseband_file_play(13975800);
    sdrpp::test::renderLoopHook.onVerifyResults = []() {
        if (maxSnr == 0) {
            flog::error("ERROR MaxSNR is not captured");
            sdrpp::test::failed = true;
        }
        if (maxSnr > 20) {
            flog::error("ERROR MaxSNR is above threshold");
            sdrpp::test::failed = true;
        }
    };
}

static void setup_baseband_file_play_signal() {
    setup_baseband_file_play(13990000);
    sdrpp::test::renderLoopHook.onVerifyResults = []() {
        if (maxSnr == 0) {
            flog::error("ERROR MaxSNR is not captured");
            sdrpp::test::failed = true;
        }
        if (maxSnr < 20) {
            flog::error("ERROR MaxSNR is belowthreshold");
            sdrpp::test::failed = true;
        }
    };
}

REGISTER_TEST(baseband_file_play_noise, ::setup_baseband_file_play_noise);
REGISTER_TEST(baseband_file_play_signal, ::setup_baseband_file_play_signal);

namespace sdrpp {
    namespace test {

    } // namespace test
} // namespace sdrpp
