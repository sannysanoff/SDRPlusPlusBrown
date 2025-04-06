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

    void SignalDetector::init(stream<complex_t>* in) {
        base_type::init(in);
    }

    void SignalDetector::setSampleRate(double sampleRate) {
        if (this->sampleRate == sampleRate) {
            return;
        }

        this->sampleRate = sampleRate;
        updateFFTSize();
    }

    void SignalDetector::setCenterFrequency(double centerFrequency) {
        if (this->centerFrequency == centerFrequency) {
            return;
        }

        this->centerFrequency = centerFrequency;
        flog::info("Signal detector center frequency set to {0} Hz", centerFrequency);

        // Reset buffer position to start fresh with new frequency
        bufferPos = 0;
    }

    void SignalDetector::updateFFTSize() {
        if (sampleRate <= 0) {
            return;
        }

        // Calculate FFT size as samplerate/10
        int newFFTSize = sampleRate / 10.0;

        // Round to nearest power of 2 for efficiency
        int powerOf2 = 1;
        while (powerOf2 < newFFTSize) {
            powerOf2 *= 2;
        }
        newFFTSize = powerOf2;

        if (newFFTSize == fftSize) {
            return;
        }

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
        fftInArray = std::make_shared<std::vector<complex_t>>(fftSize);

        // Create FFT plan (forward transform)
        fftPlan = dsp::arrays::allocateFFTWPlan(false, fftSize);

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
                auto& inVec = *fftInArray;
                for (int j = 0; j < fftSize; j++) {
                    inVec[j].re = buffer[j].re * fftWindowBuf[j];
                    inVec[j].im = buffer[j].im * fftWindowBuf[j];
                }

                // Execute FFT
                dsp::arrays::npfftfft(fftInArray, fftPlan);

                // Compute magnitude spectrum
                auto mag = dsp::arrays::npabsolute(fftPlan->getOutput());

                // Store in buffer
                if (fftResultCount < N_FFT_ROWS) {
                    fftResultBuffer.push_back(mag);
                    fftResultCount++;
                } else {
                    // Buffer full: call detection before halving
                    perform_detection();

                    // Remove first half
                    int half = N_FFT_ROWS / 2;
                    fftResultBuffer.erase(fftResultBuffer.begin(), fftResultBuffer.begin() + half);
                    fftResultCount -= half;

                    // Append new row
                    fftResultBuffer.push_back(mag);
                    fftResultCount++;
                }

                // Reset buffer position
                bufferPos = 0;
            }

            // Pass through the original data unchanged
            base_type::out.writeBuf[i] = base_type::_in->readBuf[i];
        }

        base_type::_in->flush();
        if (!base_type::out.swap(count)) { return -1; }
        return count;
    }
void SignalDetector::perform_detection() {
    // TODO: implement detection logic
}
}
