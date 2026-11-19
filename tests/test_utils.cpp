#include "test_utils.h"

#include <core.h>
#include <functional>
#include <signal_path/signal_path.h>
#include <fstream>
#include "../source_modules/file_source/src/file_source.h"
#include "../source_modules/file_source/src/file_source.h"
#include "../decoder_modules/radio/src/radio_interface.h"

struct FileSourceInterface;

namespace sdrpp {
    namespace test {
        // Initialize the global test hook
        RenderLoopHook renderLoopHook;

        bool failed = false;

        void setup_test(std::function<void()> setupFunc, int nframes, std::function<void()> verifyFunc) {
            renderLoopHook.onStableRender = setupFunc;
            renderLoopHook.onVerifyResults = verifyFunc;
            renderLoopHook.verifyResultsFrames = nframes;

            //renderLoopHook.
        }

        void selectWavFile(const std::string& TEST_FILE_NAME) {

            // Recordings live in the repository, e2e/recordings, and never under
            // the application's root directory: that root is the config folder
            // (~/.config/sdrpp-brown), which holds no test data.
            std::string testDir = SDRPP_E2E_RECORDINGS_DIR;

            // An explicit --test_root names the directory holding the recordings.
            if (core::args["test_root"].type == CLI_ARG_TYPE_STRING && !core::args["test_root"].s().empty()) {
                testDir = core::args["test_root"].s();
            }

            std::string testFilePath = testDir + "/" + TEST_FILE_NAME;

            // Verify the test file exists
            std::ifstream file(testFilePath);
            if (!file.good()) {
                flog::error("Test file not found: {} (recordings live in e2e/recordings)", testFilePath);
                return;
            }
            file.close();


            // Select "File" input source
            sigpath::sourceManager.selectSource("File");
            auto fileSources = core::moduleManager.getAllInterfaces<FileSourceInterface>("FileSourceInterface");
            fileSources[0]->openPath(testFilePath);


        }

        void selectRadioMode(int mode) {
            // Select USB decoder for the first radio
            auto radios = core::modComManager.findInterfaces("radio");
            if (!radios.empty()) {
                std::string radioName = radios[0];
                core::modComManager.callInterface(radioName, RADIO_IFACE_CMD_SET_MODE, &mode, NULL);
                flog::info("Set radio demodulator to USB mode");
            } else {
                flog::error("No radio interfaces found. Cannot set USB mode.");
            }

        }

    } // namespace test
} // namespace sdrpp
