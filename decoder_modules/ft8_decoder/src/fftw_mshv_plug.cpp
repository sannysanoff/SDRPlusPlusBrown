#include "fftw_mshv_plug.h"

#ifdef __wasm__

#else


#include "fftw_mshv_plug_original.h"


struct LocalAllocs {
    static float *arrayAllocatorFloat(int count) {
        return new float[count];
    }
    static fftwf_complex *arrayAllocatorComplex(int count) {
        return new fftwf_complex[count];
    }
    static void deallocatorFloat(float *f) {
        return delete[] f;
    }
    static void deallocatorComplex(fftwf_complex *f) {
        return delete[] f;
    }
};


std::shared_ptr<PlanStorage> nativeStorage=std::make_shared<PlanStorage>();
LocalAllocs localAllocs;


// not thread safe
extern "C" {
    FFT_PLAN fftplug_allocate_plan_c2c(int nfft, bool forward) {
        return Fftplug_allocate_plan_c2c<>(*nativeStorage, nfft, forward, localAllocs);
    }

    FFT_PLAN fftplug_allocate_plan_r2c(int nfft) {
        return Fftplug_allocate_plan_r2c<>(*nativeStorage, nfft, localAllocs);
    }

    // FFT_PLAN fftplug_allocate_plan_c2r(int nfft) {
    //     return Fftplug_allocate_plan_c2r<>(*nativeStorage, nfft, localAllocs);
    // }
    //
    // access to buffer (must match plan format)
    void fftplug_free_plan(FFT_PLAN plan) {
        Fftplug_free_plan(*nativeStorage, plan);
    }

    void fftplug_execute_plan(FFT_PLAN plan, void *source, int sourceSize, void *dest, int destSize) {
        Fftplug_execute_plan(*nativeStorage, plan, source, sourceSize, dest, destSize);
    }

}

long long planAllocTime;
long long planExecTime;


#endif