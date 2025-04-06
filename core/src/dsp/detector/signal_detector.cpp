#include "signal_detector.h"
#include "../window/blackman.h"
#include <utils/flog.h>
#include <volk/volk.h>

namespace dsp::detector {
    SignalDetector::SignalDetector() {
    }

    SignalDetector::~SignalDetector() {
        if (!base_type::_block_init) { return; }
        base_type::stop();

        if (fftWindowBuf) {
            delete[] fftWindowBuf;
        }

        if (fftPlan) {
            fftwf_destroy_plan(fftPlan);
        }

        if (fftInBuf) {
            fftwf_free(fftInBuf);
        }

        if (fftOutBuf) {
            fftwf_free(fftOutBuf);
        }
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

        // Clean up old FFT resources
        if (fftWindowBuf) {
            delete[] fftWindowBuf;
            fftWindowBuf = nullptr;
        }

        if (fftPlan) {
            fftwf_destroy_plan(fftPlan);
            fftPlan = nullptr;
        }

        if (fftInBuf) {
            fftwf_free(fftInBuf);
            fftInBuf = nullptr;
        }

        if (fftOutBuf) {
            fftwf_free(fftOutBuf);
            fftOutBuf = nullptr;
        }

        // Allocate new FFT resources
        fftWindowBuf = new float[fftSize];
        fftInBuf = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * fftSize);
        fftOutBuf = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * fftSize);

        // Create FFT plan
        fftPlan = fftwf_plan_dft_1d(fftSize, fftInBuf, fftOutBuf, FFTW_FORWARD, FFTW_ESTIMATE);

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
                for (int j = 0; j < fftSize; j++) {
                    fftInBuf[j][0] = buffer[j].re * fftWindowBuf[j];
                    fftInBuf[j][1] = buffer[j].im * fftWindowBuf[j];
                }

                // Execute FFT
                fftwf_execute(fftPlan);

                // FFT result is discarded for now as per requirements
                // In the future, signal detection logic would be implemented here

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
}
