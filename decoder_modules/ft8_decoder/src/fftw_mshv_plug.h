#pragma once

#include <stdint.h>
#include "ft8_etc/wasm_defines.h"


struct FFT_PLAN {

    int32_t handle;

};



typedef float plug_complex_float[2];

extern "C" WASM_IMPORT("fftplug_allocate_plan_c2c") FFT_PLAN fftplug_allocate_plan_c2c(int nfft, bool forward);
extern "C" WASM_IMPORT("fftplug_allocate_plan_r2c") FFT_PLAN fftplug_allocate_plan_r2c(int nfft);
// extern "C" WASM_IMPORT("fftplug_allocate_plan_c2r") FFT_PLAN fftplug_allocate_plan_c2r(int nfft);

// // access to buffer (must match plan format)
// extern "C" WASM_IMPORT("fftplug_get_float_input") float *fftplug_get_float_input(FFT_PLAN plan);
// extern "C" WASM_IMPORT("fftplug_get_complex_input") plug_complex_float* fftplug_get_complex_input(FFT_PLAN plan);
// extern "C" WASM_IMPORT("fftplug_get_float_output") float *fftplug_get_float_output(FFT_PLAN plan);
// extern "C" WASM_IMPORT("fftplug_get_complex_output")  plug_complex_float* fftplug_get_complex_output(FFT_PLAN plan);


extern "C" WASM_IMPORT("fftplug_execute_plan") void fftplug_execute_plan(FFT_PLAN plan, void *input, int sizeInput, void *output, int sizeOutput);

// release
extern "C" WASM_IMPORT("fftplug_free_plan") void fftplug_free_plan(FFT_PLAN plan);

