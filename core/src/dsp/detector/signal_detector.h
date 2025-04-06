#pragma once
#include "../processor.h"
#include <fftw3.h>
#include <vector>

namespace dsp::detector {
    class SignalDetector : public Processor<complex_t, complex_t> {
        using base_type = Processor<complex_t, complex_t>;
    public:
        SignalDetector();
        ~SignalDetector();

        void init(stream<complex_t>* in);
        void setSampleRate(double sampleRate);
        void setCenterFrequency(double centerFrequency);

        int run();

    private:
        double sampleRate = 0.0;
        double centerFrequency = 0.0;
        int fftSize = 0;
        int bufferPos = 0;

        std::vector<complex_t> buffer;
        float* fftWindowBuf = nullptr;
        fftwf_complex *fftInBuf = nullptr, *fftOutBuf = nullptr;
        fftwf_plan fftPlan = nullptr;

        void updateFFTSize();
        void generateWindow();
    };
}
