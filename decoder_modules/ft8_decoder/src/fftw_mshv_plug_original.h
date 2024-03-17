#pragma once

#include <fftw3.h>
#include "fftw_mshv_plug.h"
#include <vector>
#include <utils/flog.h>

struct FFT_PLAN_IMPL {
    FFT_PLAN_IMPL() = default;
    fftwf_plan plan = nullptr;
    bool inputC = 0;
    bool outputC = 0;
    bool inputF = 0;
    bool outputF = 0;
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
    p.inputC = true;
    p.outputC = true;
    p.nfft = nfft;
    p.plan = fftwf_plan_dft_1d(nfft, nullptr, nullptr, forward ? FFTW_FORWARD: FFTW_BACKWARD, FFTW_ESTIMATE_PATIENT);
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
    p.plan = fftwf_plan_dft_r2c_1d(nfft, nullptr, nullptr, FFTW_ESTIMATE_PATIENT);
    return FFT_PLAN {ix};
}


inline void Fftplug_free_plan(PlanStorage &s, FFT_PLAN plan) {
    std::lock_guard g(s.plansLock);
    fftwf_destroy_plan(s.allPlans[plan.handle].plan);
    s.freePlan(plan.handle);
}

inline void Fftplug_execute_plan(PlanStorage &s, FFT_PLAN plan, void *source, int sourceSize, void *dest, int destSize) {
    fftwf_plan fplan;
    bool cfrom, cto;
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
        fftwf_execute_dft(fplan, (fftwf_complex *)source, (fftwf_complex *)dest);
        if(plan.handle != 0) {
            // printf("*** exec plan c2c(%d) done %d\n", nfft, plan.handle);
        }
    } else {
        fftwf_execute_dft_r2c(fplan, (float*)source, (fftwf_complex*)dest);
    }
}



extern std::shared_ptr<PlanStorage> nativeStorage;