#pragma once

#include "fftw_mshv_plug.h"
#include <vector>
#include <utils/flog.h>

struct FFT_PLAN_IMPL {
    FFT_PLAN_IMPL() = default;
    fftwf_plan plan = nullptr;
    fftwf_complex *inputC = 0;
    fftwf_complex *outputC = 0;
    float * inputF = 0;
    float * outputF = 0;
    int nfft;
    bool alive = false;
};

struct PlanStorage {
    std::vector<FFT_PLAN_IMPL> allPlans;
    std::mutex plansLock;
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


};

template<typename Allocs>
FFT_PLAN Fftplug_allocate_plan_c2c(PlanStorage &s, int nfft, bool forward, Allocs &allocs) {
    std::lock_guard g(s.plansLock);
    int ix = s.allocatePlan();
    auto &p = s.allPlans[ix];
    p.inputC = allocs.arrayAllocatorComplex(nfft);
    p.outputC = allocs.arrayAllocatorComplex(nfft);
    p.nfft = nfft;
    memset(p.inputC, 0, nfft * sizeof(*p.inputC));
    memset(p.outputC, 0, nfft * sizeof(*p.outputC));
    p.plan = fftwf_plan_dft_1d(nfft, p.inputC, p.outputC, forward ? FFTW_FORWARD: FFTW_BACKWARD, FFTW_ESTIMATE_PATIENT);
    return FFT_PLAN {ix};
}

template<typename Allocs>
FFT_PLAN Fftplug_allocate_plan_c2r(PlanStorage &s, int nfft, Allocs &allocs) {
    std::lock_guard g(s.plansLock);
    int ix = s.allocatePlan();
    auto &p = s.allPlans[ix];
    p.inputC = allocs.arrayAllocatorComplex(nfft);
    p.outputF = allocs.arrayAllocatorFloat(nfft);
    p.nfft = nfft;
    p.plan = fftwf_plan_dft_c2r_1d(nfft, p.inputC, p.outputF, FFTW_ESTIMATE_PATIENT);
    return FFT_PLAN {ix};
}

template<typename Allocs>
FFT_PLAN Fftplug_allocate_plan_r2c(PlanStorage &s, int nfft,  Allocs &allocs) {
    std::lock_guard g(s.plansLock);
    int ix = s.allocatePlan();
    auto &p = s.allPlans[ix];
    p.inputF = allocs.arrayAllocatorFloat(nfft);
    p.outputC = allocs.arrayAllocatorComplex(nfft);
    p.nfft = nfft;
    p.plan = fftwf_plan_dft_r2c_1d(nfft, p.inputF, p.outputC, FFTW_ESTIMATE_PATIENT);
    return FFT_PLAN {ix};
}


inline float *Fftplug_get_float_input(PlanStorage &s, FFT_PLAN plan) {
    std::lock_guard g(s.plansLock);
    return s.allPlans[plan.handle].inputF;
}

inline plug_complex_float *Fftplug_get_complex_input(PlanStorage &s, FFT_PLAN plan) {
    std::lock_guard g(s.plansLock);
    return (plug_complex_float *)s.allPlans[plan.handle].inputC;
}

inline float *Fftplug_get_float_output(PlanStorage &s, FFT_PLAN plan) {
    std::lock_guard g(s.plansLock);
    return s.allPlans[plan.handle].outputF;
}

inline plug_complex_float *Fftplug_get_complex_output(PlanStorage &s, FFT_PLAN plan) {
    std::lock_guard g(s.plansLock);
    return (plug_complex_float *)s.allPlans[plan.handle].outputC;
}

inline void Fftplug_free_plan(PlanStorage &s, FFT_PLAN plan) {
    std::lock_guard g(s.plansLock);
    fftwf_destroy_plan(s.allPlans[plan.handle].plan);
    s.freePlan(plan.handle);
}

inline void Fftplug_execute_plan(PlanStorage &s, FFT_PLAN plan) {
    fftwf_plan fplan;
    fftwf_complex *cfrom;
    fftwf_complex *cto;
    int nfft;
    {
        std::lock_guard g(s.plansLock);
        auto pi = &s.allPlans[plan.handle];
        cfrom = pi->inputC;
        cto = pi->outputC;
        nfft = pi->nfft;
        fplan = pi->plan;
    }
    if(plan.handle != 0) {
        // printf("*** exec plan %d\n", plan.handle);
    }
    if (cfrom && cto) {
        char dbg[102400];
        dbg[0] = 0;
        /*
        for(int q=0; q<nfft;q++) {
            sprintf(dbg+strlen(dbg), "in[%d] = %f %f\n", q, cfrom[q][0], cfrom[q][1]);
            if (isnan(cfrom[q][0]) || isnan(cfrom[q][1])) {
                cfrom[q][0] = 0;
                cfrom[q][1] = 0;
            }
        }
        printf("%s", dbg);
        */
        // fftwf_execute_dft(fplan, cfrom, cto);
        fftwf_execute(fplan);
        if(plan.handle != 0) {
            // printf("*** exec plan c2c(%d) done %d\n", nfft, plan.handle);
        }
    } else {
        fftwf_execute(fplan);
    }
}



extern PlanStorage nativeStorage;