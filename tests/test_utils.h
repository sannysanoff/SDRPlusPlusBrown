#pragma once
#include <functional>
#include <vector>
#include <mutex>

namespace sdrpp {
    namespace test {
        // Test hook class for SDR++ tests
        class RenderLoopHook {
        public:
            bool shouldExitRenderLoop() {
                return shouldExit;
            }

            void insideRenderLoop() {
                renderLoopIterations++;
                if (renderLoopIterations == 1) {
                    if (onFirstRender) {
                        onFirstRender();
                    }
                }
                if (renderLoopIterations == 10) {
                    if (onStableRender) {
                        onStableRender();
                    }
                }
                if (renderLoopIterations > 10) {
                    if (onEachRender) {
                        onEachRender();
                    }
                }
                if (renderLoopIterations == verifyResultsFrames) {
                    if (onVerifyResults) {
                        onVerifyResults();
                    }
                    shouldExit = true;
                }
            }

        public:
            int renderLoopIterations = 0;
            bool shouldExit = false;
            std::function<void()> onFirstRender;
            std::function<void()> onStableRender;
            std::function<void()> onEachRender;
            std::function<void()> onVerifyResults;
            int verifyResultsFrames = 120;

            // SNR measurement variables
            float maxSnr = -100.0f;
            std::mutex snrMutex;

            float getMaxSnr() {
                std::lock_guard<std::mutex> lock(snrMutex);
                return maxSnr;
            }

            void reset() {
                std::lock_guard<std::mutex> lock(snrMutex);
                maxSnr = -100.0f;
            }
        };

        extern RenderLoopHook renderLoopHook;

        void setup_test(std::function<void()> setupFunc, int nframes, std::function<void()> verifyFunc);
        void selectWavFile(const std::string& TEST_FILE_NAME);
        void selectRadioMode(int mode);

    } // namespace test
} // namespace sdrpp
