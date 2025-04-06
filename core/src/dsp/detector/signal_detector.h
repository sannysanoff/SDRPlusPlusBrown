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
        dsp::arrays::ComplexArray fftInArray;
        dsp::arrays::Arg<dsp::arrays::FFTPlan> fftPlan;

        void updateFFTSize();
        void generateWindow();
    };
}
