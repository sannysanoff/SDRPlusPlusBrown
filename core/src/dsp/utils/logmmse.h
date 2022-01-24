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
            };




// taken from logmmse python
            static void logmmse_sample(const FloatArray &x, int Srate, float eta, SavedParams *params, int noise_frames) {
//            std::cout << "Sampling piece..." << std::endl;
                params->Slen = floor(0.02 * Srate);
                if (params->Slen % 2 == 1) params->Slen++;
                params->PERC = 50;
                params->len1 = floor(params->Slen * params->PERC / 100);
                params->len2 = params->Slen - params->len1;         // len1+len2
                params->win = nphanning(params->Slen);
                params->win = div(mul(params->win, params->len2), npsum(params->win));
                params->nFFT = 2 * params->Slen;
                auto Nframes = floor(x->size() / params->len2) - floor(params->Slen / params->len2);
                auto xfinal = npzeros(Nframes * params->len2);
                auto noise_mean = npzeros(params->nFFT);
                for (int j = 0; j < params->Slen * noise_frames; j += params->Slen) {
                    noise_mean = addeach(noise_mean, npabsolute(npfftfft(tocomplex(muleach(params->win, nparange(x, j, j + params->Slen))), params->nFFT, 0)));
                }
                params->noise_mu2 = div(noise_mean, noise_frames);
                params->noise_mu2 = muleach(params->noise_mu2, params->noise_mu2);
                params->Xk_prev = npzeros(params->len1);
                params->x_old = npzeros(params->len1);
                params->ksi_min = ::pow(10, -25.0 / 10.0);
//            std::cout << "sample: noisemu: " << sampleArr(params->noise_mu2) << std::endl;
            }


            static void logmmse_sample(const ComplexArray &x, int Srate, float eta, SavedParamsC *params, int noise_frames) {
//            std::cout << "Sampling piece..." << std::endl;
                params->Slen = floor(0.02 * Srate);
                if (params->Slen % 2 == 1) params->Slen++;
                params->PERC = 50;
                params->len1 = floor(params->Slen * params->PERC / 100);
                params->len2 = params->Slen - params->len1;         // len1+len2
                params->win = nphanning(params->Slen);
                params->win = div(mul(params->win, params->len2), npsum(params->win));
                params->nFFT = 2 * params->Slen;
                auto Nframes = floor(x->size() / params->len2) - floor(params->Slen / params->len2);
                auto xfinal = npzeros(Nframes * params->len2);
                auto noise_mean = npzeros(params->nFFT);
                for (int j = 0; j < params->Slen * noise_frames; j += params->Slen) {
                    noise_mean = addeach(noise_mean, npabsolute(npfftfft((muleach(params->win, nparange(x, j, j + params->Slen))), params->nFFT, 0)));
                }
                params->noise_mu2 = div(noise_mean, noise_frames);
                int ix = 0;
//                for(auto q: *params->noise_mu2) {
//                    std::cout << "Noise\t" << (ix++) << "\t" << q << std::endl;
//                }
                params->noise_mu2 = muleach(params->noise_mu2, params->noise_mu2);
                params->Xk_prev = npzeros(params->len1);
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
                static FloatArray temporaryIn = std::make_shared<std::vector<float>>();
                static FloatArray temporaryOut = std::make_shared<std::vector<float>>();
                auto Nframes = floor(x->size() / params->len2) - floor(params->Slen / params->len2);
                auto xfinal = npzeros_c(Nframes * params->len2);
                for (int k = 0; k < Nframes * params->len2; k += params->len2) {
                    auto insign = muleach(params->win, nparange(x, k, k + params->Slen));
                    auto spec = npfftfft(insign, params->nFFT, 0);
                    auto sig = npabsolute(spec);
                    for(auto z=1; z<sig->size(); z++) {
                        if ((*sig)[z] == 0) {
                            (*sig)[z] = (*sig)[z-1];      // for some reason fft returns 0 instead if small value
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


            static FloatArray logmmse_all(const FloatArray &x, int Srate, float eta, SavedParams *params) {
                static FloatArray temporaryIn = std::make_shared<std::vector<float>>();
                static FloatArray temporaryOut = std::make_shared<std::vector<float>>();
                auto Nframes = floor(x->size() / params->len2) - floor(params->Slen / params->len2);
                auto xfinal = npzeros(Nframes * params->len2);
                for (int k = 0; k < Nframes * params->len2; k += params->len2) {
                    auto insign = muleach(params->win, nparange(x, k, k + params->Slen));
                    auto spec = npfftfft(tocomplex(insign), params->nFFT, 0);
                    auto sig = npabsolute(spec);
                    for(auto z=1; z<sig->size(); z++) {
                        if ((*sig)[z] == 0) {
                            (*sig)[z] = (*sig)[z-1];      // for some reason fft returns 0 instead if small value
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
                    auto log_sigma_k = addeach(diveach(muleach(gammak, ksi), add(ksi, 1)), neg(nplog(add(ksi, 1))));
                    auto vad_decision = npsum(log_sigma_k) / params->Slen;
                    if (vad_decision < eta) {
                        params->noise_mu2 = addeach(mul(params->noise_mu2, params->mu), mul(sig2, (1 - params->mu)));
                    }

                    auto A = diveach(ksi, add(ksi, 1));
                    auto vk = muleach(A, gammak);
                    auto ei_vk = mul(scipyspecialexpn(vk), 0.5);
                    auto hw = muleach(A, npexp(ei_vk));
                    sig = muleach(sig, hw);
                    params->Xk_prev = muleach(sig, sig);
                    auto hwmulspec = muleach(hw, spec);
                    auto xi_w0 = npfftifft(hwmulspec, params->nFFT, 0);
                    auto xi_w = npreal(xi_w0);
                    auto final = addeach(params->x_old, nparange(xi_w, 0, params->len1));
                    nparangeset(xfinal, k, final);
                    params->x_old = nparange(xi_w, params->len1, params->Slen);
                }
                return xfinal;
            }


        };

    }
}