#include "fftw_mshv_plug.h"

#include <fftw3.h>
#include <vector>
#include <utils/flog.h>

struct FFT_PLAN_IMPL {
    FFT_PLAN_IMPL() = default;
    fftwf_plan plan = nullptr;
    fftwf_complex *inputC = nullptr;
    fftwf_complex *outputC = nullptr;
    float *inputF = nullptr;
    float *outputF = nullptr;
    bool alive = false;
};

static std::vector<FFT_PLAN_IMPL> allPlans;

std::mutex plansLock;

// not thread safe
int allocatePlan() {
    for(int i=0; i<allPlans.size(); i++) {
        if (!allPlans[i].alive) {
            return i;
        }
    }
    FFT_PLAN_IMPL impl;
    impl.alive = true;
    allPlans.emplace_back(impl);
    return allPlans.size() - 1;
}

void freePlan(int i) {
    if (i >= allPlans.size()) {
        flog::error("freePlan: not allocated");
        return;
    }
    auto &p = allPlans[i];
    p.alive = false;
}

extern "C" {
FFT_PLAN fftplug_allocate_plan_c2c(int nfft, bool forward) {
    std::lock_guard g(plansLock);
    int ix = allocatePlan();
    auto &p = allPlans[ix];
    p.inputC = new fftwf_complex[nfft];
    p.outputC = new fftwf_complex[nfft];
    p.plan = fftwf_plan_dft_1d(nfft, p.inputC, p.outputC, forward ? FFTW_FORWARD: FFTW_BACKWARD, FFTW_ESTIMATE_PATIENT);
    return FFT_PLAN {ix};
}

FFT_PLAN fftplug_allocate_plan_r2c(int nfft) {
    std::lock_guard g(plansLock);
    int ix = allocatePlan();
    auto &p = allPlans[ix];
    p.inputF = new float[nfft];
    p.outputC = new fftwf_complex[nfft];
    p.plan = fftwf_plan_dft_r2c_1d(nfft, p.inputF, p.outputC, FFTW_ESTIMATE_PATIENT);
    return FFT_PLAN {ix};
}

FFT_PLAN fftplug_allocate_plan_c2r(int nfft) {
    std::lock_guard g(plansLock);
    int ix = allocatePlan();
    auto &p = allPlans[ix];
    p.inputC = new fftwf_complex[nfft];
    p.outputF = new float[nfft];
    p.plan = fftwf_plan_dft_c2r_1d(nfft, p.inputC, p.outputF, FFTW_ESTIMATE_PATIENT);
    return FFT_PLAN {ix};
}

// access to buffer (must match plan format)
float *fftplug_get_float_input(FFT_PLAN plan) {
    std::lock_guard g(plansLock);
    return allPlans[plan.handle].inputF;
}

plug_complex_float *fftplug_get_complex_input(FFT_PLAN plan) {
    std::lock_guard g(plansLock);
    return (plug_complex_float *)allPlans[plan.handle].inputC;
}

float *fftplug_get_float_output(FFT_PLAN plan) {
    std::lock_guard g(plansLock);
    return allPlans[plan.handle].outputF;
}

plug_complex_float *fftplug_get_complex_output(FFT_PLAN plan) {
    std::lock_guard g(plansLock);
    return (plug_complex_float *)allPlans[plan.handle].outputC;
}

void fftplug_free_plan(FFT_PLAN plan) {
    std::lock_guard g(plansLock);
    fftwf_destroy_plan(allPlans[plan.handle].plan);
    freePlan(plan.handle);
}

void fftplug_execute_plan(FFT_PLAN plan) {
    fftwf_plan fplan;
    {
        std::lock_guard g(plansLock);
        fplan = allPlans[plan.handle].plan;
    }
    fftwf_execute(fplan);
}

}
