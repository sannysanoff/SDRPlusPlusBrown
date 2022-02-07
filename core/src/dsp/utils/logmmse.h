#pragma once

#include <dsp/utils/arrays.h>
#include <dsp/utils/math.h>

namespace dsp {
    namespace logmmse {

        inline long long currentTimeMillis() {
            std::chrono::system_clock::time_point t1 = std::chrono::system_clock::now();
            long long msec = std::chrono::time_point_cast<std::chrono::milliseconds>(t1).time_since_epoch().count();
            return msec;
        }


        // courtesy of https://github.com/jimmyberg/LowPassFilter
        class LowPassFilter {

        public:
            //constructors
            LowPassFilter(float iCutOffFrequency, float iDeltaTime) : output(0),
                                                                      ePow(1 - exp(-iDeltaTime * 2 * M_PI * iCutOffFrequency)) {
            }

            //functions
            float update(float input) {
                return output += (input - output) * ePow;
            }

            float update(float input, float deltaTime, float cutoffFrequency);

            //get and configure funtions
            float getOutput() const { return output; }

            void reconfigureFilter(float deltaTime, float cutoffFrequency);

        private:
            float output;
            float ePow;
        };


        using namespace ::dsp::arrays;
        using namespace ::dsp::math;


        struct LogMMSE {

            struct SavedParams {
                FloatArray noise_mu2;
                FloatArray Xk_prev;
                FloatArray x_old;

                int Slen;
                int PERC;
                int len1;
                int len2;
                FloatArray win;
                int nFFT;
                float aa = 0.98;
                float mu = 0.98;
                float ksi_min;
            };

            struct SavedParamsC {
                int noise_history_len() {
                    if (nFFT < 1000) {
                        return 2000;
                    } else {
                        return 200;
                    }
                }

                FloatArray noise_history;
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
                float aa = 0.98;
                float mu = 0.98;
                float ksi_min;
                bool allowFullReplace = false;
                bool hold = false;
                int dumpEnabler = 0;
                long long generation = 0;
                float mindb = 0;
                bool stable = false;

                void reset() {
                    Xk_prev.reset();
                    Xn_prev.reset();
                    noise_mu2.reset();
                    x_old.reset();
                    dumpEnabler = 0;
                    generation = 0;
                    stable = false;
                }

                void add_noise_history(FloatArray noise) {
                    for (int q = 0; q < noise->size(); q++) {
                        noise_history->emplace_back(noise->at(q));
                    }
                    if (noise_history->size() > nFFT * noise_history_len()) {
                        noise_history->erase(noise_history->begin(), noise_history->begin() + nFFT);
                        allowFullReplace = true;
                    }
                }

                void update_noise_mu2() {
                    int nframes = noise_history->size() / nFFT;
                    bool audioFrequency = nFFT < 1200;
                    auto dump = dumpEnabler == 10;
                    if (nframes > 100 && !hold) {

                        if (dump) {
                            std::cout << "Mu2 history" << std::endl;
                        }
                        if (audioFrequency) {

                            // recalculate noise floor
                            if (generation > 0) {
                                std::vector<float> lower(nFFT, 0);
                                const int nlower = 12;
                                for (auto q = nframes - nlower; q < nframes; q++) {
                                    auto offs = q * nFFT;
                                    for (auto w = 0; w < nFFT; w++) {
                                        lower[w] += noise_history->at(offs + w);
                                    }
                                }
                                for (auto w = 0; w < nFFT; w++) {
                                    lower[w] /= nlower;
                                    lower[w] *= lower[w];
                                }
                                auto tnm = std::make_shared<std::vector<float>>(lower);
                                auto tnoise_mu2 = npmavg(tnm, 6);
                                auto tmindb = *std::min_element(tnoise_mu2->begin(), tnoise_mu2->end());
                                if (tmindb < mindb) {
                                    std::cout << "Updated noise floor..." << mindb << std::endl;
                                    mindb = tmindb;
                                    noise_mu2 = tnm;
                                    stable = true;
                                }
                            }

                            if (!stable) {
                                std::vector<std::vector<float>> hist(nFFT);
                                for (auto q = 0; q < nframes; q++) {
                                    auto offs = q * nFFT;
                                    for (auto w = 0; w < nFFT; w++) {
                                        hist[w].emplace_back(noise_history->at(offs + w));
                                    }
                                }

                                // scale the noise figure
                                if (generation == 0) {
                                    noise_mu2 = npmavg(noise_mu2, 6);
                                    mindb = *std::min_element(noise_mu2->begin(), noise_mu2->end());
                                    std::cout << "Inited noise floor..." << mindb << std::endl;
                                }

                                int percent = 5;
                                for (auto w = 0; w < nFFT; w++) {
                                    std::sort(hist[w].begin(), hist[w].end());
                                    float su = 0;
                                    int limit = nframes * percent / 100;
                                    for (int l = 0; l < limit; l++) {
                                        su += hist[w][l];
                                    }
                                    su /= limit;
                                    su *= (100.0 / percent);
                                    su = su * su;
                                    noise_mu2->at(w) = su;
                                }
                                auto tnoise_mu2 = npmavg(noise_mu2, 6);
                                auto mindb2 = *std::min_element(tnoise_mu2->begin(), tnoise_mu2->end());
                                auto rescale = mindb / mindb2; // 0.2 / 0.1
                                for (auto &v: *noise_mu2) {
                                    v *= rescale;
                                }
                            }
                            if (nframes > noise_history_len() - 10) {
                                dumpEnabler++;
                            }
                            if (dump) {
                                for (int ix = 0; ix < noise_mu2->size(); ix++) {
                                    std::cout << "RNoise\t" << (ix) << "\t" << noise_mu2->at(ix) << std::endl;
                                }
                            }
                            generation++;
                        } else {
                            // finding minimum value of the noise inside the history array
                            std::vector<float> sums(nFFT, 0);
                            for (int q = 0; q < nframes; q++) {
                                int off = q * nFFT;
                                for (int z = 0; z < nFFT; z++) {
                                    auto v = noise_history->at(off + z);
//                                if (noise_history->at(off + z) < noise_history->at(z)) {
//                                    noise_history->at(z) = noise_history->at(off + z);
//                                }
                                    sums[z] += v;
                                    if (dump) {
                                        std::cout << noise_history->at(off + z) << "\t";
                                    }
                                }
                                if (dump) {
                                    std::cout << std::endl;
                                }
                            }
                            for (int z = 0; z < nFFT; z++) {
                                sums[z] /= nframes;
                            }
                            std::vector<float> hi(nFFT, 0);
                            for (int q = 0; q < nframes; q++) {
                                int off = q * nFFT;
                                for (int z = 0; z < nFFT; z++) {
                                    auto v = noise_history->at(off + z);
                                    auto diff = v - sums[z];
                                    hi[z] += diff * diff;
                                }
                            }
                            std::vector<float> devs(nFFT, 0);
                            std::vector<int> devsort(nFFT, 0);
                            for (int z = 0; z < nFFT; z++) {
                                devs[z] = sqrt(hi[z] / nframes);
                                if ((!audioFrequency) && abs(z - nFFT/2) < nFFT * 15 / 100) {
                                    // after fft, rightmost and leftmost sides of real frequencies range are at the center of the resulting table.
                                    // We exclude middle of the table from lookup
                                    devs[z] = 1000000;
                                }
                                devsort[z] = z;
                                noise_mu2->at(z) = 0;
                            }
                            std::sort(devsort.begin(), devsort.begin() + devsort.size(), [&](int a, int b) {
                                return devs[a] < devs[b];
                            });
                            // take 90% percentile
                            auto acceptible_stdev = devs[devsort.size()/10];
                            if (audioFrequency) {
                                acceptible_stdev = devs[devsort.size() - devsort.size()/10];
                            }
                            acceptible_stdev *= 1.2;    // surplus 20%
                            // now devsort[0] is most stable noise
                            for(int q=0; q < nFFT && devs[devsort[q]] < acceptible_stdev; q++) {
                                noise_mu2->at(devsort[q]) = sums[devsort[q]] * sums[devsort[q]];
                            }
                            int firstV = -1;
                            int lastV = -1;
                            for(int q=0; q<nFFT; q++) {
                                float val = noise_mu2->at(q);
                                if(firstV < 0 && val != 0) {
                                    firstV = q;
                                }
                                if (val != 0) {
                                    if (lastV != -1) {
                                        if (q - lastV > 1) {
                                            // fill the gap
                                            auto d = (val - noise_mu2->at(lastV)) / (q - lastV);
                                            auto running = noise_mu2->at(lastV);
                                            for(int w=lastV+1; w<q; w++) {
                                                running += d;
                                                noise_mu2->at(w) = running;
                                            }
                                        }
                                    }
                                    lastV = q;
                                }
                            }
                            for(int q=firstV-1; q>=0; q--) {
                                noise_mu2->at(q) = noise_mu2->at(firstV);
                            }
                            for(int q=lastV+1; q<noise_mu2->size(); q++) {
                                noise_mu2->at(q) = noise_mu2->at(lastV);
                            }
                            if (dump) {
                                for (int ix = 0; ix < noise_mu2->size(); ix++) {
                                    std::cout << "RNoise\t" << (ix) << "\t" << noise_mu2->at(ix) << std::endl;
                                }

                                for (int ix = 0; ix < sums.size(); ix++) {
                                    std::cout << "Sums^2\t" << (ix) << "\t" << sums.at(ix) * sums.at(ix) << std::endl;
                                }
                            }

                        }



                    }
                }
            };




// taken from logmmse python
//            static void logmmse_sample(const FloatArray &x, int Srate, float eta, SavedParams *params, int noise_frames) {
////            std::cout << "Sampling piece..." << std::endl;
//                params->Slen = floor(0.02 * Srate);
//                if (params->Slen % 2 == 1) params->Slen++;
//                params->PERC = 50;
//                params->len1 = floor(params->Slen * params->PERC / 100);
//                params->len2 = params->Slen - params->len1;         // len1+len2
//                params->win = nphanning(params->Slen);
//                params->win = div(mul(params->win, params->len2), npsum(params->win));
//                params->nFFT = 2 * params->Slen;
//                auto Nframes = floor(x->size() / params->len2) - floor(params->Slen / params->len2);
//                auto xfinal = npzeros(Nframes * params->len2);
//                auto noise_mean = npzeros(params->nFFT);
//                for (int j = 0; j < params->Slen * noise_frames; j += params->Slen) {
//                    noise_mean = addeach(noise_mean, npabsolute(npfftfft(tocomplex(muleach(params->win, nparange(x, j, j + params->Slen))), params->nFFT, 0)));
//                }
//                params->noise_mu2 = div(noise_mean, noise_frames);
//                params->noise_mu2 = muleach(params->noise_mu2, params->noise_mu2);
//                params->Xk_prev = npzeros(params->len1);
//                params->x_old = npzeros(params->len1);
//                params->ksi_min = ::pow(10, -25.0 / 10.0);
////            std::cout << "sample: noisemu: " << sampleArr(params->noise_mu2) << std::endl;
//            }


            static void logmmse_sample(const ComplexArray &x, int Srate, float eta, SavedParamsC *params, int noise_frames) {
                params->Slen = floor(0.02 * Srate);
                if (params->Slen % 2 == 1) params->Slen++;
                params->PERC = 50;
                params->len1 = floor(params->Slen * params->PERC / 100);
                params->noise_history = npzeros(0);
                params->len2 = params->Slen - params->len1;         // len1+len2
                auto audioFrequency = Srate <= 12000;
                if (audioFrequency && false) {
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
                std::cout << "Sampling piece... srate=" << Srate << " Slen=" << params->Slen << " nFFT=" << params->nFFT << std::endl;
                auto Nframes = floor(x->size() / params->len2) - floor(params->Slen / params->len2);
                auto xfinal = npzeros(Nframes * params->len2);
                auto noise_mean = npzeros(params->nFFT);
                for (int j = 0; j < params->Slen * noise_frames; j += params->Slen) {
                    auto noise = npabsolute(npfftfft((muleach(params->win, nparange(x, j, j + params->Slen))), params->nFFT, 0));
                    params->add_noise_history(noise);
                    noise_mean = addeach(noise_mean, noise);
                }
                params->noise_mu2 = div(noise_mean, noise_frames);
                if (!audioFrequency) {
                    params->noise_mu2 = npmavg(params->noise_mu2, 120);
                }
                params->noise_mu2 = muleach(params->noise_mu2, params->noise_mu2);
                for (int ix = 0; ix < params->noise_mu2->size(); ix++) {
                    std::cout << "Noise\t" << (ix) << "\t" << params->noise_mu2->at(ix) << std::endl;
                }
                params->Xk_prev = npzeros(params->len1);
                params->Xn_prev = npzeros_c(0);
                params->x_old = npzeros_c(params->len1);
                params->ksi_min = ::pow(10, -25.0 / 10.0);
//            std::cout << "sample: noisemu: " << sampleArr(params->noise_mu2) << std::endl;
            }




//            static void writeWav(const FloatArray &x, const std::string &name) {
//                std::string fname = "audio_7177000Hz_17-21-21_25-12-2021.wav";
//                std::string rdfile = "/db/recordings/" + fname;
//                FILE *f = fopen(rdfile.c_str(), "rb");
//                WavWriter::WavHeader_t hdr;
//                fread(&hdr, 1, sizeof(WavWriter::WavHeader_t), f);
//                fclose(f);
//                f = fopen(name.c_str(), "wb");
//                fwrite(&hdr, 1, sizeof(WavWriter::WavHeader_t), f);
//                std::vector<int16_t> i16(x->size(), 0);
//                volk_32f_s32f_convert_16i(i16.data(), x->data(), 32767, x->size());
//                std::vector<int16_t> i16_2(2 * x->size(), 0);
//                for (int i = 0; i < x->size(); i++) {
//                    i16_2[2 * i + 0] = i16[i];
//                    i16_2[2 * i + 1] = i16[i];
//                }
//                fwrite(i16_2.data(), 1, sizeof(int16_t) * 2 * x->size(), f);
//                fclose(f);
//            }

            static ComplexArray logmmse_all(const ComplexArray &x, int Srate, float eta, SavedParamsC *params) {
                int consumed = 0;
                params->Xn_prev->insert(params->Xn_prev->end(), x->begin(), x->end());
                for (int j = 0; j < params->Xn_prev->size() - params->Slen; j += params->Slen) {
                    auto noise = npabsolute(npfftfft((muleach(params->win, nparange(params->Xn_prev, j, j + params->Slen))), params->nFFT, 0));
                    params->add_noise_history(noise);
                    consumed += params->Slen;
                }
                params->Xn_prev = nparange(params->Xn_prev, consumed, params->Xn_prev->size() - consumed);
                auto Nframes = floor(x->size() / params->len2) - floor(params->Slen / params->len2);
                params->update_noise_mu2();
                auto xfinal = npzeros_c(Nframes * params->len2);
                for (int k = 0; k < Nframes * params->len2; k += params->len2) {
                    auto insign = muleach(params->win, nparange(x, k, k + params->Slen));
                    auto spec = npfftfft(insign, params->nFFT, 0);
                    auto sig = npabsolute(spec);
                    for (auto z = 1; z < sig->size(); z++) {
                        if ((*sig)[z] == 0) {
                            (*sig)[z] = (*sig)[z - 1];      // for some reason fft returns 0 instead if small value
                        }
                    }
                    auto sig2 = muleach(sig, sig);

                    auto gammak = npminimum(diveach(sig2, params->noise_mu2), 40);
                    FloatArray ksi;
                    if (!npall(params->Xk_prev)) {
                        ksi = add(mul(npmaximum(add(gammak, -1), 0), 1 - params->aa), params->aa);
                    } else {
                        const FloatArray d1 = diveach(mul(params->Xk_prev, params->aa), params->noise_mu2);
                        const FloatArray m1 = mul(npmaximum(add(gammak, -1), 0), (1 - params->aa));
                        ksi = addeach(d1, m1);
                        ksi = npmaximum(ksi, params->ksi_min);
                    }
//                    auto log_sigma_k = addeach(diveach(muleach(gammak, ksi), add(ksi, 1)), neg(nplog(add(ksi, 1))));
//                    auto vad_decision = npsum(log_sigma_k) / params->Slen;
//                    if (vad_decision < eta) {
//                        params->noise_mu2 = addeach(mul(params->noise_mu2, params->mu), mul(sig2, (1 - params->mu)));
//                    }

                    auto A = diveach(ksi, add(ksi, 1));
                    auto vk = muleach(A, gammak);
                    auto ei_vk = mul(scipyspecialexpn(vk), 0.5);
                    auto hw = muleach(A, npexp(ei_vk));
                    sig = muleach(sig, hw);
                    params->Xk_prev = muleach(sig, sig);
                    auto hwmulspec = muleach(hw, spec);
                    auto xi_w0 = npfftifft(hwmulspec, params->nFFT, 0);
                    auto xi_w = xi_w0;
                    auto final = addeach(params->x_old, nparange(xi_w, 0, params->len1));
                    nparangeset(xfinal, k, final);
                    params->x_old = nparange(xi_w, params->len1, params->Slen);
                }
                return xfinal;
            }


//            static FloatArray logmmse_all(const FloatArray &x, int Srate, float eta, SavedParams *params) {
//                static FloatArray temporaryIn = std::make_shared<std::vector<float>>();
//                static FloatArray temporaryOut = std::make_shared<std::vector<float>>();
//                auto Nframes = floor(x->size() / params->len2) - floor(params->Slen / params->len2);
//                auto xfinal = npzeros(Nframes * params->len2);
//                for (int k = 0; k < Nframes * params->len2; k += params->len2) {
//                    auto insign = muleach(params->win, nparange(x, k, k + params->Slen));
//                    auto spec = npfftfft(tocomplex(insign), params->nFFT, 0);
//                    auto sig = npabsolute(spec);
//                    for(auto z=1; z<sig->size(); z++) {
//                        if ((*sig)[z] == 0) {
//                            (*sig)[z] = (*sig)[z-1];      // for some reason fft returns 0 instead if small value
//                        }
//                    }
//                    auto sig2 = muleach(sig, sig);
//
//                    auto gammak = npminimum(diveach(sig2, params->noise_mu2), 40);
//                    FloatArray ksi;
//                    if (!npall(params->Xk_prev)) {
//                        ksi = add(mul(npmaximum(add(gammak, -1), 0), 1 - params->aa), params->aa);
//                    } else {
//                        const FloatArray d1 = diveach(mul(params->Xk_prev, params->aa), params->noise_mu2);
//                        const FloatArray m1 = mul(npmaximum(add(gammak, -1), 0), (1 - params->aa));
//                        ksi = addeach(d1, m1);
//                        ksi = npmaximum(ksi, params->ksi_min);
//                    }
//                    auto log_sigma_k = addeach(diveach(muleach(gammak, ksi), add(ksi, 1)), neg(nplog(add(ksi, 1))));
//                    auto vad_decision = npsum(log_sigma_k) / params->Slen;
//                    if (vad_decision < eta) {
//                        params->noise_mu2 = addeach(mul(params->noise_mu2, params->mu), mul(sig2, (1 - params->mu)));
//                    }
//
//                    auto A = diveach(ksi, add(ksi, 1));
//                    auto vk = muleach(A, gammak);
//                    auto ei_vk = mul(scipyspecialexpn(vk), 0.5);
//                    auto hw = muleach(A, npexp(ei_vk));
//                    sig = muleach(sig, hw);
//                    params->Xk_prev = muleach(sig, sig);
//                    auto hwmulspec = muleach(hw, spec);
//                    auto xi_w0 = npfftifft(hwmulspec, params->nFFT, 0);
//                    auto xi_w = npreal(xi_w0);
//                    auto final = addeach(params->x_old, nparange(xi_w, 0, params->len1));
//                    nparangeset(xfinal, k, final);
//                    params->x_old = nparange(xi_w, params->len1, params->Slen);
//                }
//                return xfinal;
//            }


        };

    }
}