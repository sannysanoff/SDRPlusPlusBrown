//  _  ___  _   _____ _     _
// / |/ _ \/ | |_   _| |__ (_)_ __   __ _ ___
// | | | | | |   | | | '_ \| | '_ \ / _` / __|
// | | |_| | |   | | | | | | | | | | (_| \__ \.
// |_|\___/|_|   |_| |_| |_|_|_| |_|\__, |___/
//                                  |___/
//
// Dawson CW Decoder - FFT Processor Implementation

#include "fft_processor.h"

FFTProcessor::FFTProcessor() : initialized(false) {
    // Initialize twiddle factors for max size (1024)
    uint16_t n_over_2 = 1 << (MAX_M - 1);  // 512 for n=1024
    for (int i = 0; i < n_over_2; ++i) {
        fixed_cos_table[i] = cosf((float)i * M_PI / n_over_2);
        fixed_sin_table[i] = sinf((float)i * M_PI / n_over_2);
    }
    initialized = true;
}

void FFTProcessor::bit_reverse(float* reals, float* imaginaries, uint16_t n) {
    uint16_t j = 0;
    for (uint16_t i = 0; i < n; i++) {
        if (i < j) {
            float temp_real = reals[i];
            float temp_imag = imaginaries[i];
            reals[i] = reals[j];
            imaginaries[i] = imaginaries[j];
            reals[j] = temp_real;
            imaginaries[j] = temp_imag;
        }
        
        uint16_t k = n >> 1;
        while (k & j) {
            j ^= k;
            k >>= 1;
        }
        j |= k;
    }
}

void FFTProcessor::process(float* reals, float* imaginaries, uint16_t m) {
    const uint16_t n = 1 << m;
    
    // Bit reverse
    bit_reverse(reals, imaginaries, n);
    
    // FFT butterflies
    for (uint16_t stage = 0; stage < m; ++stage) {
        uint16_t subdft_size = 2 << stage;
        uint16_t span = subdft_size >> 1;
        // Scale twiddle factor index based on current FFT size vs max size
        uint16_t twiddle_scale = MAX_M - m;
        uint16_t quarter_turn = 1 << (stage - 1);
        
        for (uint16_t j = 0; j < span; ++j) {
            if (j == 0) {
                // Rotation by 0 - special case
                for (uint16_t i = j; i < n; i += subdft_size) {
                    uint16_t ip = i + span;
                    float top_real = reals[i];
                    float top_imag = imaginaries[i];
                    float temp_real = reals[ip];
                    float temp_imag = imaginaries[ip];
                    
                    reals[ip] = (top_real - temp_real) * 0.5f;
                    imaginaries[ip] = (top_imag - temp_imag) * 0.5f;
                    reals[i] = (top_real + temp_real) * 0.5f;
                    imaginaries[i] = (top_imag + temp_imag) * 0.5f;
                }
            } else if (j == quarter_turn) {
                // Rotation by 1/4 - special case
                for (uint16_t i = j; i < n; i += subdft_size) {
                    uint16_t ip = i + span;
                    float top_real = reals[i];
                    float top_imag = imaginaries[i];
                    float bottom_real = reals[ip];
                    float bottom_imag = imaginaries[ip];
                    
                    float temp_real = bottom_imag;
                    float temp_imag = -bottom_real;
                    
                    reals[ip] = (top_real - temp_real) * 0.5f;
                    imaginaries[ip] = (top_imag - temp_imag) * 0.5f;
                    reals[i] = (top_real + temp_real) * 0.5f;
                    imaginaries[i] = (top_imag + temp_imag) * 0.5f;
                }
            } else {
                // Full complex multiply - scale twiddle index for variable FFT size
                uint16_t twiddle_idx = j << (MAX_M - m - stage);
                float real_twiddle = fixed_cos_table[twiddle_idx];
                float imag_twiddle = -fixed_sin_table[twiddle_idx];
                
                for (uint16_t i = j; i < n; i += subdft_size) {
                    uint16_t ip = i + span;
                    float top_real = reals[i];
                    float top_imag = imaginaries[i];
                    float bottom_real = reals[ip];
                    float bottom_imag = imaginaries[ip];
                    
                    float temp_real = bottom_real * real_twiddle - bottom_imag * imag_twiddle;
                    float temp_imag = bottom_real * imag_twiddle + bottom_imag * real_twiddle;
                    
                    reals[ip] = (top_real - temp_real) * 0.5f;
                    imaginaries[ip] = (top_imag - temp_imag) * 0.5f;
                    reals[i] = (top_real + temp_real) * 0.5f;
                    imaginaries[i] = (top_imag + temp_imag) * 0.5f;
                }
            }
        }
    }
}

void FFTProcessor::calculate_magnitude(float* real, float* imag, float* magnitude, uint16_t n) {
    for (uint16_t i = 0; i < n; i++) {
        magnitude[i] = magnitude_estimate(real[i], imag[i]);
    }
}

void FFTProcessor::apply_window(float* data, uint16_t n) {
    for (uint16_t i = 0; i < n; i++) {
        float multiplier = 0.5f * (1.0f - cosf(2.0f * M_PI * i / n));
        data[i] *= multiplier;
    }
}
