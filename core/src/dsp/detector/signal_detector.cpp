#include "signal_detector.h"
#include "../window/blackman.h"
#include <utils/flog.h>
#include <volk/volk.h>

namespace dsp::detector {


    SignalDetector::SignalDetector() {
        fftResultBuffer.reserve(N_FFT_ROWS);
        fftResultCount = 0;
    }

    SignalDetector::~SignalDetector() {
        if (!base_type::_block_init) { return; }
        base_type::stop();

        if (fftWindowBuf) {
            delete[] fftWindowBuf;
            fftWindowBuf = nullptr;
        }

        fftInArray.reset();
        fftPlan.reset();
    }

    void SignalDetector::init(stream<complex_t> *in) {
        base_type::init(in);
    }

    void SignalDetector::setSampleRate(double sampleRate) {
        if (this->sampleRate == sampleRate) {
            return;
        }

        this->sampleRate = sampleRate;
        updateFFTSize();
        clear();
    }

    void SignalDetector::setCenterFrequency(double centerFrequency) {
        if (this->centerFrequency == centerFrequency) {
            return;
        }

        this->centerFrequency = centerFrequency;
        flog::info("Signal detector center frequency set to {0} Hz", centerFrequency);

        // Reset buffer position to start fresh with new frequency
        clear();
    }

    void SignalDetector::clear() {
        bufferPos = 0;
    }

    void SignalDetector::updateFFTSize() {
        if (sampleRate <= 0) {
            return;
        }

        // Calculate FFT size as samplerate/10
        int newFFTSize = sampleRate * TIME_SLICE;

        fftSize = newFFTSize;
        flog::info("Signal detector FFT size set to {0}", fftSize);

        // Reset buffer
        buffer.resize(fftSize);
        bufferPos = 0;

        // Clean up old window buffer
        if (fftWindowBuf) {
            delete[] fftWindowBuf;
            fftWindowBuf = nullptr;
        }

        // Allocate window buffer
        fftWindowBuf = new float[fftSize];

        // Allocate new input array
        fftInArray = std::make_shared<std::vector<complex_t> >(fftSize);

        // Create FFT plan (forward transform)
        fftPlan = dsp::arrays::allocateFFTWPlan(false, fftSize);

        // Resize rolling FFT magnitude buffer
        fftResultBuffer.resize(N_FFT_ROWS * fftSize, 0.0f);
        fftResultCount = 0;

        // Generate window function
        generateWindow();
    }

    void SignalDetector::generateWindow() {
        if (!fftWindowBuf || fftSize <= 0) {
            return;
        }

        // Generate Blackman window
        for (int i = 0; i < fftSize; i++) {
            fftWindowBuf[i] = window::blackman(i, fftSize);
        }
    }

    int SignalDetector::run() {
        int count = base_type::_in->read();
        if (count < 0) {
            return -1;
        }

        // If FFT is not initialized, just pass through the data
        if (fftSize <= 0 || !fftPlan) {
            memcpy(base_type::out.writeBuf, base_type::_in->readBuf, count * sizeof(complex_t));
            base_type::_in->flush();
            if (!base_type::out.swap(count)) { return -1; }
            return count;
        }

        // Process samples and perform FFT when buffer is full
        for (int i = 0; i < count; i++) {
            // Store sample in buffer
            if (bufferPos < fftSize) {
                buffer[bufferPos++] = base_type::_in->readBuf[i];
            }

            // When buffer is full, perform FFT
            if (bufferPos >= fftSize) {
                // Apply window function
                auto &inVec = *fftInArray;
                for (int j = 0; j < fftSize; j++) {
                    inVec[j].re = buffer[j].re * fftWindowBuf[j];
                    inVec[j].im = buffer[j].im * fftWindowBuf[j];
                }

                // Execute FFT
                dsp::arrays::npfftfft(fftInArray, fftPlan);
                
                // Swap FFT output so that 0 frequency is at the center
                dsp::arrays::swapfft(fftPlan->getOutput());

                // Compute magnitude spectrum
                auto mag = dsp::arrays::npabsolute(fftPlan->getOutput());

                // Convert magnitude to logarithmic scale (dB)
                for (int j = 0; j < fftSize; j++) {
                    // Add small value (1e-10) to avoid log of zero, multiply by 20 to convert to dB
                    (*mag)[j] = 20.0f * log10f((*mag)[j] + 1e-10f);
                }

                std::copy(mag->begin(), mag->end(), fftResultBuffer.begin() + fftResultCount * fftSize);
                processSingleRow(fftResultCount);
                fftResultCount++;

                if (fftResultCount > MIN_DETECT_FFT_ROWS) {
                    // Buffer full enough: call detection before halving
                    aggregateAndDetect();
                }
                if (fftResultCount  == N_FFT_ROWS) {
                    // Shift second half to front
                    int deleteSize = (int)(1 / TIME_SLICE);
                    std::memmove(
                        fftResultBuffer.data(),
                        fftResultBuffer.data() + deleteSize * fftSize,
                        (N_FFT_ROWS - deleteSize) * fftSize * sizeof(float)
                    );
                    fftResultCount = N_FFT_ROWS - deleteSize;
                }
            }
            // Pass through the original data unchanged
            base_type::out.writeBuf[i] = base_type::_in->readBuf[i];
        }

        base_type::_in->flush();
        if (!base_type::out.swap(count)) { return -1; }
        return count;
    }

    void SignalDetector::aggregateAndDetect() {
        float *start = &fftResultBuffer.at(fftResultCount * fftSize);
        float *end = start + fftSize;
    }

    void SignalDetector::processSingleRow(int rowCount) {

    }
}
