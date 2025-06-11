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

            // Get the path to the test file
            std::string testDir;

            // Check if test_root parameter is provided
            if (core::args["test_root"].type == CLI_ARG_TYPE_STRING && !core::args["test_root"].s().empty()) {
                // Use the provided test_root parameter
                testDir = core::args["test_root"].s() + "/test_files";
            } else {
                // Fallback to the default path
                testDir = std::string(core::getRoot()) + "/tests/test_files";
                flog::warn("test_root parameter not provided, using default path: {}", testDir);
            }

            std::string testFilePath = testDir + "/" + TEST_FILE_NAME;

            // Verify the test file exists
            std::ifstream file(testFilePath);
            if (!file.good()) {
                flog::error("Test file not found: {}", testFilePath);
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
