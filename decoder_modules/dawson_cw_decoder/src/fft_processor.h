//  _  ___  _   _____ _     _
// / |/ _ \/ | |_   _| |__ (_)_ __   __ _ ___
// | | | | | |   | | | '_ \| | '_ \ / _` / __|
// | | |_| | |   | | | | | | | | | | (_| \__ \.
// |_|\___/|_|   |_| |_| |_|_|_| |_|\__, |___/
//                                  |___/
//
// Dawson CW Decoder - FFT Processor
// Uses standard FFT (can be optimized with volk/fftw)

#ifndef __FFT_PROCESSOR_H__
#define __FFT_PROCESSOR_H__

#include <cstdint>
#include <complex>
#include <vector>
#include <cmath>

// Fixed-point FFT implementation (same as HamFist)
class FFTProcessor {
public:
    static const uint16_t FRAME_SIZE = 64;
    static const uint16_t MAX_M = 6;  // log2(64)
    
    FFTProcessor();
    
    // Process 64-point FFT
    void process(float* input_real, float* input_imag, uint16_t m = 6);
    
    // Calculate magnitude from FFT output
    static void calculate_magnitude(float* real, float* imag, float* magnitude, uint16_t n);
    
    // Apply Hann window
    static void apply_window(float* data, uint16_t n);
    
private:
    void bit_reverse(float* reals, float* imaginaries, uint16_t n);
    
    float fixed_cos_table[32];  // n/2 for n=64
    float fixed_sin_table[32];
    bool initialized;
};

// Utility functions
inline float magnitude_estimate(float i, float q) {
    const float absi = fabs(i);
    const float absq = fabs(q);
    return absi > absq ? absi + absq / 4.0f : absq + absi / 4.0f;
}

#endif
