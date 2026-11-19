// The OFDM engine's transform, on the application's own FFT.
//
// This translation unit is the only place that mentions the core's FFT: it is
// compiled where the core headers and the core library are available (the
// plugin, which links the core anyway, and the headless CLI, which links it to
// resolve these symbols). Nothing here discovers or links an FFT library of its
// own, so the receiver keeps exactly one FFT implementation - the one its
// waterfall and frontend already use, vDSP on macOS when selected.

#include "dabplus_fft.h"

#include <mutex>
#include <vector>

#include <utils/arrays.h>

namespace dabplus {

    struct FFT::Impl {
        Impl(std::size_t size, bool backward) : size(size), backward(backward) {
            plan = dsp::arrays::allocateFFTWPlan(backward, int(size));
            in = dsp::arrays::npzeros_c(int(size));
        }

        std::size_t size;
        bool backward;
        dsp::arrays::Arg<dsp::arrays::FFTPlan> plan;
        dsp::arrays::ComplexArray in;
        // The plan owns mutable buffers, so one transform runs at a time.
        std::mutex mtx;
    };

    void SetAcceleratedFFT(bool enabled) {
#ifdef __APPLE__
        enableAcceleratedFFT = enabled;
#else
        (void)enabled;
#endif
    }

    FFT::FFT(std::size_t size, bool backward) : impl(new Impl(size, backward)) {}

    FFT::~FFT() = default;

    void FFT::execute(const std::complex<float>* src, std::complex<float>* dst) {
        Impl& self = *impl;
        std::lock_guard<std::mutex> lck(self.mtx);
        dsp::complex_t* box = self.in->data();
        for (std::size_t i = 0; i < self.size; i++) {
            box[i] = dsp::complex_t{ src[i].real(), src[i].imag() };
        }
        const dsp::arrays::ComplexArray out = self.plan->npfftfft(self.in);
        // The app's plan normalises the inverse by the length; the OFDM maths is
        // written against the unnormalised convention, so that factor is put back.
        const float scale = self.backward ? float(self.size) : 1.0f;
        const dsp::complex_t* res = out->data();
        for (std::size_t i = 0; i < self.size; i++) {
            dst[i] = std::complex<float>(res[i].re * scale, res[i].im * scale);
        }
    }

}
