#pragma once
#include "../processor.h"
#include "pcm_type.h"
#include "utils/arrays.h"
#include <cmath>
#include <random>
#include "dsp/window/blackman.h"
#include "experimental_fft_compressor.h"



namespace dsp::compression {

    using namespace ::dsp::arrays;

    class ExperimentalFFTDeCompressor : public Processor<complex_t, complex_t> {
        using base_type = Processor<complex_t, complex_t>;
    public:

        int fftSize = 0;
        Arg<FFTPlan> fftPlan;
        ComplexArray inArray;

        std::random_device rd;
        std::mt19937 rgen;
        float noiseMultiplierDB = 0.0;
        std::vector<float> fftWindow;

        ExperimentalFFTDeCompressor() : Processor<complex_t, complex_t>(), rgen(rd()) {

        }

        void setFFTSize(int fftSize) {
            if (this->fftSize != fftSize) {
                this->fftSize = fftSize;
                fftPlan = allocateFFTWPlan(true, fftSize);
                inArray = npzeros_c(fftSize);
                fftWindow.resize(fftSize);
                for (int i = 0; i < fftSize; i++) { fftWindow[i] = 1.0/blackmanWindowElement(i+5, fftSize+10); }
            }
        }

        bool enabled = false;

        void setEnabled(bool enabled) {
            this->enabled = enabled;
        }

        bool isEnabled() {
            return enabled;
        }

        float db_to_linear(float decibels) {
            float linear_scale = std::pow(10, decibels / 20);
            return linear_scale;
        }

        int run() {
            int count = base_type::_in->read();
            if (count < 0) { return -1; }

            if (!enabled) {
                std::copy(_in->readBuf, _in->readBuf + count, this->out.writeBuf);
                base_type::_in->flush();
                if (count > 0) {
                    if (!base_type::out.swap(count)) { return -1; }
                }
                return count;
            } else {

                setFFTSize(count); // always right size.
                inArray->clear();
                inArray->insert(inArray->begin(), _in->readBuf, _in->readBuf + count);
                base_type::_in->flush();
                auto indata = inArray->data();

                auto nd = std::normal_distribution<>(0, 1);
                auto ad = std::normal_distribution<>(-M_PI, M_PI);
                if (noiseFigure.size() > 0) {
                    int nparts = noiseFigure.size();
                    auto avgmag = inArray->data();
                    int singlePart = fftSize / nparts;
                    float  noiseMult = 0;
                    float  prevnoiseMult = 0;
                    int prevd = -1;
                    int istart = 0;

                    for (int i = 0; i < fftSize; i++) {
                        if (indata[i].re == 0 && indata[i].im == 0) {
                            int d = i / singlePart;
                            if (d != prevd) {
                                if (d >= nparts) {
                                    d = nparts - 1;
                                }
                                prevnoiseMult = noiseMult;
                                noiseMult = db_to_linear(noiseFigure[d] + noiseMultiplierDB) * fftSize;
                                prevd = d;
                                istart = i;
                            }
                            float prog = (i-istart)/(float)singlePart;
                            float cnoiseMult = prog*noiseMult + (1-prog)*prevnoiseMult;
                            float angle = ad(rgen);
                            float length = nd(rgen) * cnoiseMult;
                            indata[i].re = length * sin(angle);
                            indata[i].im = length * cos(angle);
                        } else {
                            auto compressedAmp = indata[i].amplitude();
                            auto origAmp = compressedAmp * compressedAmp * compressedAmp * compressedAmp;
                            auto scale =  origAmp / compressedAmp;
                            indata[i] *= scale;
                        }
                    }
                } else {
                    for (int i = 0; i < fftSize; i++) {
                        if (indata[i].re != 0 && indata[i].im != 0) {
                            auto compressedAmp = indata[i].amplitude();
                            auto origAmp = compressedAmp * compressedAmp * compressedAmp * compressedAmp;
                            auto scale =  origAmp / compressedAmp;
                            indata[i] *= scale;
                        }
                    }
                }


                swapfft(inArray); // was swapped
                auto out = fftPlan->npfftfft(inArray);
                std::copy(out->begin(), out->begin() + fftSize, this->out.writeBuf);
                if (!base_type::out.swap(fftSize)) { return -1; }
                //flog::info("experimental FFT decompressor: inCount={} outCount={}", count, fftSize);
                return fftSize;
            }
        }

        std::vector<float> noiseFigure;

        float lowpass(float oldd, float neww) {
            float delay = 0.8;
            return oldd * delay + neww * (1-delay);
        }

        void setNoiseFigure(std::vector<float> figure) {
            if (noiseFigure.size() != figure.size()) {
                noiseFigure = figure;
            } else {
                for(int q=0; q<figure.size(); q++) {
                    noiseFigure[q] = lowpass(noiseFigure[q], figure[q]);
                }
            }
        }
    };
}