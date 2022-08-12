#pragma once

#include "arrays.h"
#include "math.h"
#include "bgnoise.h"
#include <array>
#include <list>

namespace dsp {
    namespace logmmse {

        inline long long currentTimeMillis() {
            std::chrono::system_clock::time_point t1 = std::chrono::system_clock::now();
            long long msec = std::chrono::time_point_cast<std::chrono::milliseconds>(t1).time_since_epoch().count();
            return msec;
        }

        inline long long currentTimeNanos() {
#ifdef __linux__
            timespec time1;
            clock_gettime(CLOCK_MONOTONIC_COARSE, &time1);
            return time1.tv_nsec + time1.tv_sec * 1000000000L;
#else
            std::chrono::system_clock::time_point t1 = std::chrono::system_clock::now();
            long long msec = std::chrono::time_point_cast<std::chrono::nanoseconds>(t1).time_since_epoch().count();
            return msec;
#endif
        }


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

                int Slen;
                int PERC;
                int len1;
                int len2;
                FloatArray win;
                int nFFT;
                Arg<fftwPlan> forwardPlan;
                Arg<fftwPlan> reversePlan;
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
                    if (noise->size() != nFFT) {
                        abort(); // because

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
                void update_noise_mu2() {
                    static long long muSum[30] = {0,}, muCount = 0; auto ctm = currentTimeNanos();long long ctm2; auto statIndex = 0;
                    auto nframes = noise_history.size();
                    bool audioFrequency = nFFT < 1200;
//                    auto dump = dumpEnabler == 10;

                    if (nframes > 100 && !hold) {

//                        if (dump) {
//                            std::cout << "Mu2 history" << std::endl;
//                        }
                        if (audioFrequency) {

                            // recalculate noise floor
                            if (generation > 0) {
                                std::vector<float> lower(nFFT, 0);
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
                                for (auto w = 0; w < nFFT; w++) {
                                    lower[w] /= nlower;
                                    lower[w] *= lower[w];
                                }
                                auto tnm = std::make_shared<std::vector<float>>(lower);
                                auto tnoise_mu2 = npmavg(tnm, 6);
                                auto tmindb = *std::min_element(tnoise_mu2->begin(), tnoise_mu2->end());
                                auto tmaxdb = *std::max_element(tnoise_mu2->begin(), tnoise_mu2->end());
                                if (tmindb + tmaxdb < mindb + maxdb) {
                                    
//                                    spdlog::info("Updated noise floor...{0} ( {1}, {2} )", (tmindb + tmaxdb)/2, tmindb, tmaxdb);
                                    mindb = tmindb;
                                    maxdb = tmaxdb;
                                    noise_mu2 = tnm;
                                    stable = true;
                                }
                            }

                            if (!stable) {

                                // scale the noise figure
                                if (generation == 0) {
                                    auto tnoise_mu2 = npmavg(noise_mu2, 6);
                                    mindb = *std::min_element(tnoise_mu2->begin(), tnoise_mu2->end());
                                    maxdb = *std::max_element(tnoise_mu2->begin(), tnoise_mu2->end());
                                    std::cout << "Inited noise floor..." << mindb << std::endl;
                                }

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
                            int firstV = -1;
                            int lastV = -1;
                            int countZeros = 0;
                            for(int q=0; q<nFFT; q++) {
                                float val = nmu2[q];
                                if(firstV < 0 && val != 0) {
                                    firstV = q;
                                }
                                if (val != 0) {
                                    if (lastV != -1) {
                                        if (q - lastV > 1) {
                                            // fill the gap
                                            auto d = (val - nmu2[lastV]) / (q - lastV);
                                            auto running = nmu2[lastV];
                                            for(int w=lastV+1; w<q; w++) {
                                                running += d;
                                                nmu2[w] = running;
                                            }
                                        }
                                    }
                                    lastV = q;
                                } else {
                                    countZeros++;
                                }
                            }
                            if (firstV < 0 || lastV < 0) {
//                                std::cerr << "Bad noise_mu2 figure. countZeros="+std::to_string(countZeros)+"\n";
                                *noise_mu2 = noise_mu2_copy;
//                                abort();
                            } else {
                                for (int q = firstV - 1; q >= 0; q--) {
                                    nmu2[q] = nmu2[firstV];
                                }
                                for (int q = lastV + 1; q < noise_mu2->size(); q++) {
                                    nmu2[q] = nmu2[lastV];
                                }
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
                    if (muCount == 1000) {
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
                if (audioFrequency) {
                    // probably audio frequency
                    params->win = nphanning(params->Slen);
                    params->win = div(mul(params->win, params->len2), npsum(params->win));
                } else {
                    // probably wide band
                    params->win = npzeros(params->Slen);
                    for (int i = 0; i < params->win->size(); i++) {
                        params->win->at(i) = 1.0;
                    }
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
                    auto noise = npabsolute(npfftfft((muleach(params->win, nparange(x, j, j + params->Slen))), params->forwardPlan));
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
                static long long muSum[30] = {0,}, muCount = 0; auto ctm = currentTimeNanos();long long ctm2; auto statIndex = 0;

                auto Nframes = floor(x->size() / params->len2) - floor(params->Slen / params->len2);
                ADD_STEP_STATS();
                params->update_noise_mu2();
                ADD_STEP_STATS();
                auto xfinal = npzeros_c(Nframes * params->len2);
                for (int k = 0; k < Nframes * params->len2; k += params->len2) {
                    auto insign = muleach(params->win, nparange(x, k, k + params->Slen));
                    auto spec = npfftfft(insign, params->forwardPlan);
                    auto sig = npabsolute(spec);
                    auto sigD = sig->data();
                    for (auto z = 1; z < sig->size(); z++) {
                        if (sigD[z] == 0) {
                            sigD[z] = sigD[z - 1];      // for some reason fft returns 0 instead if small value
                        }
                    }
                    params->add_noise_history(sig);
                    auto sig2 = muleach(sig, sig);

                    auto gammak = npminimum_(diveach(sig2, params->noise_mu2), 40);
                    FloatArray ksi;
                    if (!npall(params->Xk_prev)) {
                        ksi = add(mul(npmaximum_(add(gammak, -1), 0), 1 - params->aa), params->aa);
                    } else {
                        const FloatArray d1 = diveach(mul(params->Xk_prev, params->aa), params->noise_mu2);
                        const FloatArray m1 = mul(npmaximum_(add(gammak, -1), 0), (1 - params->aa));
                        ksi = addeach(d1, m1);
                        ksi = npmaximum_(ksi, params->ksi_min);
                    }
                    auto A = diveach(ksi, add(ksi, 1));
                    auto vk = muleach(A, gammak);
                    auto ei_vk = mul(scipyspecialexpn(vk), 0.5);
                    auto hw = muleach(A, npexp(ei_vk));
                    sig = muleach(sig, hw);
                    params->Xk_prev = muleach(sig, sig);
                    auto hwmulspec = muleach(hw, spec);
                    auto xi_w0 = npfftfft(hwmulspec, params->reversePlan);
                    auto final = addeach(params->x_old, nparange(xi_w0, 0, params->len1));
                    nparangeset(xfinal, k, final);
                    params->x_old = nparange(xi_w0, params->len1, params->Slen);
                }
                ADD_STEP_STATS();
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
                    std::cout << "logmmse_all: ";
                    for(int z=0; z<statIndex; z++) {
                        std::cout << " " << std::to_string(muSum[z] / 1000);
                        muSum[z] = 0;
                    }
                    std::cout << std::endl;
                    muCount = 0;
                }
                return xfinal;
            }

        };

    }
}