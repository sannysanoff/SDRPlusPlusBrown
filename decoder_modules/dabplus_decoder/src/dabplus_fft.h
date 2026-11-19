#pragma once

// The OFDM engine's forward and inverse transform.
//
// The implementation lives in dabplus_fft_core.cpp and calls the FFT the
// application already runs (dsp::arrays::allocateFFTWPlan: FFTW3f everywhere,
// vDSP on macOS when the accelerated FFT is enabled). Keeping the interface here
// and the implementation with the app-facing translation unit means the plugin
// inherits the receiver's FFT through its existing link against the core, and
// the engine never names an FFT library itself.

#include <complex>
#include <cstddef>
#include <memory>

namespace dabplus {

    // On macOS the app can run its FFT on vDSP (Accelerate) instead of FFTW;
    // the accelerated FFT is a user setting in the display menu. Tests and
    // offline checks use this to run the engine on both backends.
    void SetAcceleratedFFT(bool enabled);

    class FFT {
    public:
        // backward selects the inverse transform, as fftwf_plan_dft_1d does.
        FFT(std::size_t size, bool backward);
        ~FFT();

        FFT(const FFT&) = delete;
        FFT& operator=(const FFT&) = delete;

        // Out-of-place transform of `size` samples, unnormalised as the OFDM
        // maths was written: the inverse is not scaled by the length.
        void execute(const std::complex<float>* src, std::complex<float>* dst);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

}
