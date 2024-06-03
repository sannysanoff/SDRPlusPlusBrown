#pragma once

#include "utils/arrays.h"
#include "math.h"
#include "bgnoise.h"
#include <array>
#include <list>
#include <ctm.h>

namespace dsp {

#if 0
#define ALLOC_AND_CHECK(x, sz, point) \
    { \
        std::vector<float> lower(440, 0); \
        if (x->size() != sz) { \
            flog::info("Abort in {}", point); \
            abort(); \
        } \
    }
#else
#define ALLOC_AND_CHECK(x, sz, point)
#endif


    // ported from https://github.com/rajivpoddar/logmmse/  by sannysanoff

    namespace logmmse {

        // courtesy of https://github.com/jimmyberg/LowPassFilter
        class LowPassFilter {

        public:
            LowPassFilter(float iCutOffFrequency, float iDeltaTime) : output(0), ePow(1 - exp(-iDeltaTime * 2 * M_PI * iCutOffFrequency)) {
            }

            float update(float input) {
                return output += (input - output) * ePow;
            }

            float update(float input, float deltaTime, float cutoffFrequency);

            float getOutput() const { return output; }

            void reconfigureFilter(float deltaTime, float cutoffFrequency);

        private:
            float output;
            float ePow;
        };

        using namespace ::dsp::arrays;
        using namespace ::dsp::math;

        struct LogMMSE {

            struct SavedParamsC {
                int noise_history_len() {
                    if (nFFT < 1000) {
                        return 2000;
                    } else {
                        return 200;
                    }
                }

                std::list<FloatArray> noise_history;      // chunks of nFFT*float
                std::list<FloatArray> dev_history;
                FloatArray sums;      // sliding sum of last N noise_history
                FloatArray devs;      // sliding sum of dev_history
                ComplexArray Xn_prev; // remaining noise

                FloatArray noise_mu2;
                FloatArray Xk_prev;
                ComplexArray x_old;
                bool forceAudio = false;
                bool forceWideband = false;
                int forceSampleRate = 0;


                int Slen;
                int PERC;
                int len1;
                int len2;
                FloatArray win;
                int nFFT;
                Arg<FFTPlan> forwardPlan;
                Arg<FFTPlan> reversePlan;
                float aa = 0.98;
                float mu = 0.98;
                float ksi_min;
                bool hold = false;
                long long generation = 0;
                float mindb = 0;
                float maxdb = 0;
                bool stable = false;

//                float *devsD = nullptr;
//                float *hiD = nullptr;
//                float *diffD = nullptr;

                void reset() {
                    noise_history.clear();
                    dev_history.clear();
                    Xk_prev.reset();
                    Xn_prev.reset();
                    noise_mu2.reset();
                    x_old.reset();
                    generation = 0;
                    stable = false;
                    backgroundNoiseCaltulator.reset();

                }

                void add_noise_history(const FloatArray &noise) {
                    if (hold) {
                        return;
                    }
                    if (noise->size() != nFFT) {
                        flog::info("ERROR noise->size() != nFFT: {} {}", (int)noise->size(), nFFT);
                        return;
                    }
                    noise_history.emplace_back(noise);
                    volk_32f_x2_add_32f(sums->data(), sums->data(), noise->data(), nFFT);
                    while (noise_history.size() > noise_history_len()) {
                        volk_32f_x2_subtract_32f(sums->data(), sums->data(), noise_history.front()->data(), nFFT);
                        noise_history.pop_front();
                    }

                    auto noiseAvg = div(sums, (float)noise_history.size());

                    auto diff = subeach(noise, noiseAvg);
                    diff = muleach(diff, diff);
                    dev_history.emplace_back(diff);

                    devs = addeach(devs, diff);

                    while (dev_history.size() > noise_history_len()) {
                        devs = subeach(devs, dev_history.front());
                        dev_history.pop_front();
                    }

                }

                BackgroundNoiseCaltulator backgroundNoiseCaltulator;


#define ADD_STEP_STATS()          ctm2 = currentTimeNanos(); muSum[statIndex++] += ctm2-ctm; ctm = ctm2

                void update_noise_mu2(const ComplexArray &x) {
                    auto sz = x->size();
                    ALLOC_AND_CHECK(x, sz, "update_noise_mu2 point 1.5a")
                    static long long muSum[30] = {0,}, muCount = 0; auto ctm = currentTimeNanos();long long ctm2; auto statIndex = 0;
                    auto nframes = noise_history.size();
                    bool audioFrequency = nFFT < 1200;
                    if (forceAudio) audioFrequency = true;
                    if (forceWideband) audioFrequency = false;
//                    auto dump = dumpEnabler == 10;
                    ALLOC_AND_CHECK(x, sz, "update_noise_mu2 point 0")

                    if (nframes > 100 && !hold) {
//                        if (dump) {
//                            std::cout << "Mu2 history" << std::endl;
//                        }
                        if (audioFrequency) {
                            ALLOC_AND_CHECK(x, sz, "update_noise_mu2 point 1")

                            // recalculate noise floor
                            if (generation > 0) {
                                std::vector<float> lower(nFFT, 0);
                                ALLOC_AND_CHECK(x, sz, "update_noise_mu2 point 1.5")
                                const int nlower = 12;
                                int ix = 0;
                                for(auto &it: noise_history) {
                                    if (ix >= nframes - nlower) {
                                        auto nhFrame = it->data();
                                        for (auto w = 0; w < nFFT; w++) {
                                            lower[w] += nhFrame[w];
                                        }
                                    }
                                    ix++;
                                }
                                ALLOC_AND_CHECK(x, sz, "update_noise_mu2 point 2")
                                for (auto w = 0; w < nFFT; w++) {
                                    lower[w] /= nlower;
                                    lower[w] *= lower[w];
                                }
                                ALLOC_AND_CHECK(x, sz, "update_noise_mu2 point 3")
                                auto tnm = std::make_shared<std::vector<float>>(lower);
                                auto tnoise_mu2 = npmavg(tnm, 6);
                                auto tmindb = *std::min_element(tnoise_mu2->begin(), tnoise_mu2->end());
                                auto tmaxdb = *std::max_element(tnoise_mu2->begin(), tnoise_mu2->end());
                                ALLOC_AND_CHECK(x, sz, "update_noise_mu2 point 4")
                                if (tmindb + tmaxdb < mindb + maxdb) {
                                    
//                                    spdlog::info("Updated noise floor...{0} ( {1}, {2} )", (tmindb + tmaxdb)/2, tmindb, tmaxdb);
                                    mindb = tmindb;
                                    maxdb = tmaxdb;
                                    noise_mu2 = tnm;
                                    stable = true;
                                }
                                ALLOC_AND_CHECK(x, sz, "update_noise_mu2 point 5")
                            }

                            if (!stable) {

                                // scale the noise figure
                                if (generation == 0) {
                                    auto tnoise_mu2 = npmavg(noise_mu2, 6);
                                    mindb = *std::min_element(tnoise_mu2->begin(), tnoise_mu2->end());
                                    maxdb = *std::max_element(tnoise_mu2->begin(), tnoise_mu2->end());
                                    std::cout << "Inited noise floor..." << mindb << std::endl;
                                }
                                ALLOC_AND_CHECK(x, sz, "update_noise_mu2 point 6")

                            }
                            generation++;
                        } else {

                            auto noise_mu2_copy = *noise_mu2;

                            auto noiseAvg = mul(sums, 1 / (float)nframes);

                            ADD_STEP_STATS();

                            auto hi = mul(devs, 1 / (float)nframes);
                            auto devSquare = muleach(hi, hi);
                            auto devSquareD = devSquare->data();
                            ADD_STEP_STATS();
                            for (int z = 0; z < nFFT; z++) {
                                if (abs(z - nFFT/2) < nFFT * 15 / 100) {
                                    // after fft, rightmost and leftmost sides of real frequencies range are at the center of the resulting table.
                                    // We exclude middle of the table from lookup
                                    devSquareD[z] = BackgroundNoiseCaltulator::ERASED_SAMPLE;
                                }
                            }
                            memset(noise_mu2->data(), 0, nFFT*sizeof(noise_mu2->at(0)));
                            ADD_STEP_STATS();
                            std::vector<float> devs(devSquareD, devSquareD +nFFT);
                            float detectedNoise = backgroundNoiseCaltulator.addFrame(devs);
                            ADD_STEP_STATS();
                            auto acceptible_stdev = detectedNoise;
                            auto nmu2 = noise_mu2->data();
                            auto navg =  noiseAvg->data();
                            for(int q=0; q < nFFT; q++) {
                                if (devs[q] < acceptible_stdev) {
                                    nmu2[q] = navg[q] * navg[q];
                                }
                            }

                            if (!linearInterpolateHoles(nmu2, nFFT)) {
                                *noise_mu2 = noise_mu2_copy;
                            }

                            ADD_STEP_STATS();

                        }  // end if audio frequency



                    }
                    // 192 non-volk: 984000 0    888000 32000 8000 316000
                    // 192 volk:      136000 4000 340000 12000 8000 260000
                    // 192 volk:      130000 4000 300000 12000 8000 260000  // after volk_alloc
                    // 192 mu2:       146625 2250 377750 3125  4250 333875  averages after np** conv
                    // 384             211	3	534	5	7	494             // avg
                    //                 274	3	684	8	6	618	41          // ? fixed alloc
                    //                 604	4	718	5	9	248	37
                    // 768 mu2:        8 8 16 753 133
                    // 768 mu2:        8 9 10 557 176
                    // 768 mu2:        16 18 23 347 225         // after sample count instead of sort
                    // 768 mu2:        13 8 12 52 195         // after dropping each 10th frame for noise dev calculation
                    // 768 mu2:        13 8 12 52 115         // replaced at() with direct data access.
                    muCount++;
                    if (muCount == 1000 && false) {
                        std::cout << "mu2: ";
                        for(int z=0; z<statIndex; z++) {
                            std::cout << " " << std::to_string(muSum[z] / 1000);
                            muSum[z] = 0;
                        }
                        std::cout << std::endl;
                        muCount = 0;
                    }
                }
            };

            static void logmmse_sample(const ComplexArray &x, int Srate, float eta, SavedParamsC *params, int noise_frames) {
                params->Slen = floor(0.02 * Srate);
                if (params->Slen % 2 == 1) params->Slen++;
                params->PERC = 50;
                params->len1 = floor(params->Slen * params->PERC / 100);
                params->noise_history.clear();
                params->dev_history.clear();
                params->len2 = params->Slen - params->len1;         // len1+len2
                auto audioFrequency = Srate <= 24000;
                if (params->forceAudio) audioFrequency = true;
                if (params->forceWideband) audioFrequency = false;
                if (audioFrequency) {
                    // probably audio frequency
                    params->win = nphanning(params->Slen);
                    params->win = div(mul(params->win, params->len2), npsum(params->win));
                } else {
                    // probably wide band
                    params->win = nphanning(params->Slen);
                    params->win = div(mul(params->win, params->len2), npsum(params->win));
//                    params->win = npzeros(params->Slen);
//                    for (int i = 0; i < params->win->size(); i++) {
//                        params->win->at(i) = 1.0;
//                    }
                }
                params->nFFT = 2 * params->Slen;
                params->forwardPlan = allocateFFTWPlan(false, params->nFFT);
                params->reversePlan = allocateFFTWPlan(true, params->nFFT);
                params->sums = npzeros(params->nFFT);
                params->devs = npzeros(params->nFFT);

                std::cout << "Sampling piece... srate=" << Srate << " Slen=" << params->Slen << " nFFT=" << params->nFFT << std::endl;
                auto Nframes = floor(x->size() / params->len2) - floor(params->Slen / params->len2);
                auto xfinal = npzeros(Nframes * params->len2);
                auto noise_mean = npzeros(params->nFFT);
                for (int j = 0; j < params->Slen * noise_frames; j += params->Slen) {
                    npfftfft((muleach(params->win, nparange(x, j, j + params->Slen))), params->forwardPlan);
                    auto noise = npabsolute(params->forwardPlan->getOutput());
                    params->add_noise_history(noise);
                    noise_mean = addeach(noise_mean, noise);
                }
                params->noise_mu2 = div(noise_mean, noise_frames);
                if (!audioFrequency) {
                    params->noise_mu2 = npmavg(params->noise_mu2, 120);
                }
                params->noise_mu2 = muleach(params->noise_mu2, params->noise_mu2);
//                for (int ix = 0; ix < params->noise_mu2->size(); ix++) {
//                    std::cout << "Noise\t" << (ix) << "\t" << params->noise_mu2->at(ix) << std::endl;
//                }
                params->Xk_prev = npzeros(params->len1);
                params->Xn_prev = npzeros_c(0);
                params->x_old = npzeros_c(params->len1);
                params->ksi_min = ::pow(10, -25.0 / 10.0);
//            std::cout << "sample: noisemu: " << sampleArr(params->noise_mu2) << std::endl;
            }

            static ComplexArray logmmse_all(const ComplexArray &x, int Srate, float eta, SavedParamsC *params) {
                int sz = x->size();
                ALLOC_AND_CHECK(x, sz, "logmmse_all point -2.1")
                static long long muSum[30] = {0,}, muCount = 0; auto ctm = currentTimeNanos();long long ctm2; auto statIndex = 0;
                ALLOC_AND_CHECK(x, sz, "logmmse_all point -1")

                auto Nframes = floor(x->size() / params->len2) - floor(params->Slen / params->len2);
                ALLOC_AND_CHECK(x, sz, "logmmse_all point -1.5")
                ADD_STEP_STATS();
                ALLOC_AND_CHECK(x, sz, "logmmse_all point -1.7")
                params->update_noise_mu2(x);
                ALLOC_AND_CHECK(x, sz, "logmmse_all point -1.8")
                ADD_STEP_STATS();
                auto xfinal = npzeros_c(Nframes * params->len2);
                ALLOC_AND_CHECK(x, sz, "logmmse_all point 0")
                for (int k = 0; k < Nframes * params->len2; k += params->len2) {
                    ALLOC_AND_CHECK(x, sz, "logmmse_all point 1")
                    auto insign = muleach(params->win, nparange(x, k, k + params->Slen));
                    npfftfft(insign, params->forwardPlan);
                    auto spec = params->forwardPlan->getOutput();
                    auto sig = npabsolute(spec);
                    ALLOC_AND_CHECK(x, sz, "logmmse_all point 2")
                    auto sigD = sig->data();
                    for (auto z = 1; z < sig->size(); z++) {
                        if (sigD[z] == 0) {
                            sigD[z] = sigD[z - 1];      // for some reason fft returns 0 instead if small value
                        }
                    }
                    ALLOC_AND_CHECK(x, sz, "logmmse_all point 3")
                    params->add_noise_history(sig);
                    ALLOC_AND_CHECK(x, sz, "logmmse_all point 4")
                    auto sig2 = muleach(sig, sig);
                    ALLOC_AND_CHECK(x, sz, "logmmse_all point 5")

                    auto gammak = npminimum_(diveach(sig2, params->noise_mu2), 40);
                    ALLOC_AND_CHECK(x, sz, "logmmse_all point 6")
                    FloatArray ksi;
                    if (!npall(params->Xk_prev)) {
                        ksi = add(mul(npmaximum_(add(gammak, -1), 0), 1 - params->aa), params->aa);
                    } else {
                        const FloatArray d1 = diveach(mul(params->Xk_prev, params->aa), params->noise_mu2);
                        const FloatArray m1 = mul(npmaximum_(add(gammak, -1), 0), (1 - params->aa));
                        ksi = addeach(d1, m1);
                        ksi = npmaximum_(ksi, params->ksi_min);
                    }
                    ALLOC_AND_CHECK(x, sz, "logmmse_all point 10")
                    auto A = diveach(ksi, add(ksi, 1));
                    ALLOC_AND_CHECK(x, sz, "logmmse_all point 11")
                    auto vk = muleach(A, gammak);
                    ALLOC_AND_CHECK(x, sz, "logmmse_all point 12")
                    auto ei_vk = mul(scipyspecialexpn(vk), 0.5);
                    ALLOC_AND_CHECK(x, sz, "logmmse_all point 13")
                    auto hw = muleach(A, npexp(ei_vk));
                    ALLOC_AND_CHECK(x, sz, "logmmse_all point 14")
                    sig = muleach(sig, hw);
                    ALLOC_AND_CHECK(x, sz, "logmmse_all point 15")
                    params->Xk_prev = muleach(sig, sig);
                    auto hwmulspec = muleach(hw, spec);
                    ALLOC_AND_CHECK(x, sz, "logmmse_all point 16")
                    npfftfft(hwmulspec, params->reversePlan);
                    auto xi_w0 = params->reversePlan->getOutput();
                    ALLOC_AND_CHECK(x, sz, "logmmse_all point 17")
                    auto final = addeach(params->x_old, nparange(xi_w0, 0, params->len1));
                    ALLOC_AND_CHECK(x, sz, "logmmse_all point 18")
                    nparangeset(xfinal, k, final);
                    params->x_old = nparange(xi_w0, params->len1, params->Slen);
                    ALLOC_AND_CHECK(x, sz, "logmmse_all point 19")
                }
                ALLOC_AND_CHECK(x, sz, "logmmse_all point 20")
                ADD_STEP_STATS();
                ALLOC_AND_CHECK(x, sz, "logmmse_all point 21")
                muCount++;

                if (muCount == 1000) {
                    // 192 logmmse_all:  371000 684000 806000 (avgs)
                    // 192 logmmse_all:  307000 892000 286000 (avgs) - some np moved to volk
                    // 192 logmmse_all:  332000 886000 247000 (avgs) - all np moved to volk
                    // 384               606	1284	441          avg
                    //                   836	1635	598          ?? fixed
                    //                   819	1620	580
                    //                   874	1612	534         // file source, local allocs (!)
                    //                   0	    1738	790         // radio src
                    // 768 logmmse_all:  0      920     786
                    // 768 logmmse_all:  0      762     841         //
                    if (false) {
                        std::cout << "logmmse_all: ";
                        for (int z = 0; z < statIndex; z++) {
                            std::cout << " " << std::to_string(muSum[z] / 1000);
                            muSum[z] = 0;
                        }
                        std::cout << std::endl;
                    }
                    muCount = 0;
                }
                ADD_STEP_STATS();
                ALLOC_AND_CHECK(x, sz, "logmmse_all point 23")
                return xfinal;
            }

        };

    }
}