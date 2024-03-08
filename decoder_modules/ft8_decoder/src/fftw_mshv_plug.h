#pragma once

#include <stdint.h>


struct FFT_PLAN {

    int32_t handle;

};

struct plug_complex_float {
    float re;
    float im;
};

extern "C" FFT_PLAN fftplug_allocate_plan_c2c(int nfft, bool forward);
extern "C" FFT_PLAN fftplug_allocate_plan_r2c(int nfft);
extern "C" FFT_PLAN fftplug_allocate_plan_c2r(int nfft);

// access to buffer (must match plan format)
extern "C" float *fftplug_get_float_input(FFT_PLAN plan);
extern "C" plug_complex_float* fftplug_get_complex_input(FFT_PLAN plan);
extern "C" float *fftplug_get_float_output(FFT_PLAN plan);
extern "C" plug_complex_float* fftplug_get_complex_output(FFT_PLAN plan);


extern "C" void fftplug_execute_plan(FFT_PLAN plan);

// release
extern "C" void fftplug_free_plan(FFT_PLAN plan);
