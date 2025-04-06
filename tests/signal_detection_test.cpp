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

// Path to test file
const std::string TEST_FILE_NAME = "baseband_14174296Hz_11-08-47_24-02-2024-contest-ssb-small.wav";


static void setup_test_signal_detection() {

    sdrpp::test::renderLoopHook.onStableRender = [=]() {
        {
            sdrpp::test::selectWavFile(TEST_FILE_NAME);

            sigpath::iqFrontEnd.togglePreprocessor(&sigpath::iqFrontEnd.detectorPreprocessor, true);

            // Start playback
            gui::mainWindow.setPlayState(true);


        }
    };

    sdrpp::test::renderLoopHook.onEachRender = []() {
    };

    sdrpp::test::renderLoopHook.onVerifyResults = []() {
    };
}

REGISTER_TEST(test_signal_detection, ::setup_test_signal_detection);

namespace sdrpp {
    namespace test {

    } // namespace test
} // namespace sdrpp
