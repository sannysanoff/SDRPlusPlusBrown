#pragma once

#include "../processor.h"
#include "pcm_type.h"
#include "utils/arrays.h"
#include "utils/strings.h"
#include "dsp/window/blackman.h"
#include "dsp/compression/sample_stream_compressor.h"
#include "dsp/compression/sample_stream_decompressor.h"
#include <cmath>


namespace dsp::compression {


    inline double blackmanWindowElement(int n, int m) {
        // Ensure n is within the valid range
        if (n < 0 || n >= m) {
            throw std::out_of_range("Index n is out of the valid range [0, m-1]");
        }

        const double alpha = 0.42;
        const double beta = 0.5;
        const double gamma = 0.08;

        double factor = 2.0 * M_PI * n / (m - 1);
        return alpha - beta * std::cos(factor) + gamma * std::cos(2 * factor);
    }

    using namespace ::dsp::arrays;

    class ExperimentalFFTCompressor : public Processor<complex_t, complex_t> {
        using base_type = Processor<complex_t, complex_t>;
    public:

        int sampleRate = 0;
        int sliceMsec = 50;
        int fftSize = 1024;
        double lossRate = 1.0;

        Arg<FFTPlan> fftPlan;
        std::shared_ptr<std::vector<complex_t>> inArray;
        std::vector<float> fftWindow;

        std::vector<complex_t> inputBuffer;

        const int minRecents = 10;
        std::vector<ComplexArray> cleanFreqDomain;
        std::vector<FloatArray> cleanMagnitudes;
        std::vector<FloatArray> windowedMagnitudes;

        float hzTick;
        int smallTick;
        int largeTick;
        int signalWidth = 300;

        std::vector<int32_t> maskedFrequencies;


        ExperimentalFFTCompressor() {}

        ExperimentalFFTCompressor(stream<complex_t> *in) { init(in); }

        int getSampleRate() {
            return this->sampleRate;
        }
        void setSampleRate(int sampleRate) {
            if (sampleRate != this->sampleRate) {
                clear();
                this->sampleRate = sampleRate;
                fftSize = sampleRate * sliceMsec / 1000;
                fftSize = pow(2, floor(log2(fftSize)));
                fftPlan = allocateFFTWPlan(false, fftSize);
                inArray = std::make_shared<std::vector<dsp::complex_t>>(fftSize);

                fftWindow.resize(fftSize);
                for (int i = 0; i < fftSize; i++) { fftWindow[i] = blackmanWindowElement(i+5, fftSize+10); }


                hzTick = ((float) sampleRate) / fftSize;
                smallTick = signalWidth / hzTick;
                largeTick = smallTick * 10;
                sharedDataLock.lock();
                noiseFigure.clear();
                sharedDataLock.unlock();
            }
        }

        void clear() {
            inputBuffer.clear();
        }

        bool enabled = false;

        void setEnabled(bool enabled) {
            this->enabled = enabled;
        }

        void setMaskedFrequencies(const std::vector<int32_t> maskedFrequencies) {
            sharedDataLock.lock();
            this->maskedFrequencies = maskedFrequencies;
            sharedDataLock.unlock();
        }

        bool isEnabled() {
            return enabled;
        }

        int iteration = 0;

        const int noiseNPoints = 16;
        std::vector<float> noiseFigure; // mean, variance, mean, variance, mean, variance..
        std::mutex sharedDataLock;
        float prevAllowance = 0;

        // returns noise floor
        FloatArray filterSignal(FloatArray windowedMags, FloatArray clearMags, dsp::complex_t *unfiltered) {
            //
            auto mvar = dsp::arrays::movingVariance(windowedMags, noiseNPoints);
            auto newAllowance = lossRate * percentile::percentile_sampling(*clone(mvar), .15);
            auto allowance = newAllowance * 0.1 + prevAllowance * 0.9;
            prevAllowance = allowance;

            auto cma = centeredSma(windowedMags, largeTick);


            for (int i = 0; i < fftSize; i++) {
                if (mvar->at(i) > allowance) {
                    cma->at(i) = 0;
                }
            }
            dsp::math::linearInterpolateHoles(cma->data(), cma->size());

            cma = centeredSma(cma, largeTick);          // cleared noise floor.
            auto cmax = centeredSma(cma, 5*largeTick);          // cleared noise floor.
            auto diff = subeach(cma, cmax);
            for (int i = 0; i < fftSize; i++) {
                diff->at(i) = fabs(diff->at(i));
            }
            auto cmaxAllow = percentile::percentile_sampling(*clone(diff), .15);
            for (int i = 0; i < fftSize; i++) {
                if (diff->at(i)  > cmaxAllow) {
                    cma->at(i) = 0;
                }
            }
            dsp::math::linearInterpolateHoles(cma->data(), cma->size());
            cma = centeredSma(cma, largeTick);          // cleared noise floor.

            auto mask = npzeros(fftSize);
            if (!txMode) {
                // for rx mode, add good signals to mask, for tx assume no signals at all. Mask will be used to filter signal.
                for (int i = 0; i < fftSize; i++) {
                    if (clearMags->at(i) > cma->at(i) + allowance) {     // allowance = normal noise variance
                        mask->at(i) = 1;
                    }
                }
            }
            for(int i=0; i<maskedFrequencies.size(); i+=2) {
                int from = maskedFrequencies[i+0];
                int to = maskedFrequencies[i+1];
                int tickFrom = fftSize/2 + from / hzTick;
                int tickTo = fftSize/2 + to / hzTick;
                for (int j = tickFrom; j < tickTo; j++) {
                    if (j >= 0 && j < fftSize) {
                        mask->at(j) = 1;
                    }
                }
            }
            mask = centeredSma(mask, signalWidth / 8);      // add some around the signal
            int nfiltered = 0;
            for (int i = 0; i < fftSize; i++) {
                if (mask->at(i) == 0) {
                    unfiltered[i] = {0, 0};
                    nfiltered++;
                }
            }
//            flog::info("compressor: filtered {} of {}", nfiltered, fftSize);
            return cma;
        }

        int zz = 0;

        std::vector<float> estimateNoise(FloatArray noiseFloor) {
            int nslices = 30;
            int slice = fftSize / nslices;
            auto rv = std::vector<float>(nslices);
            for(int i=0; i<nslices; i++) {
                rv[i] = 7 + noiseFloor->at(i * slice + slice/2); // 7db is due to averaging, probably
            }
            return rv;
        }

        bool txMode = false;
        void setTxMode(int txMode) {
            this->txMode = txMode;
        }

        inline int process() {

            if (inputBuffer.size() < fftSize) {
                return 0;
            }

            inArray->clear();
            inArray->insert(inArray->begin(), inputBuffer.begin(), inputBuffer.begin() + fftSize);
            inputBuffer.erase(inputBuffer.begin(), inputBuffer.begin() + fftSize);
            auto out = fftPlan->npfftfft(inArray);
            swapfft(out);
            cleanFreqDomain.emplace_back(clone(out));

            auto spectrumOut = npzeros(fftSize);
            volk_32fc_s32f_power_spectrum_32f(spectrumOut->data(), (const lv_32fc_t *) out->data(), fftSize, fftSize);
            if (!txMode) {
                cleanMagnitudes.emplace_back(spectrumOut);
            }

            // applying window to get windowed windowedSpectrum
            volk_32fc_32f_multiply_32fc((lv_32fc_t*)inArray->data(), (lv_32fc_t*)inArray->data(), fftWindow.data(), fftSize);
            out = fftPlan->npfftfft(inArray);
            swapfft(out);
            auto windowedSpectrumOut = npzeros(fftSize);
            volk_32fc_s32f_power_spectrum_32f(windowedSpectrumOut->data(), (const lv_32fc_t *) out->data(), fftSize, fftSize);
            if (!txMode) {
                windowedMagnitudes.emplace_back(windowedSpectrumOut);
            }

            if (cleanFreqDomain.size() < minRecents) {
                return 0;
            }

            std::copy(cleanFreqDomain[0]->begin(), cleanFreqDomain[0]->end(), this->out.writeBuf);

            auto windowedSpectrum = npzeros(fftSize);
            for (int r = 0; r < windowedMagnitudes.size(); r++) {
                windowedSpectrum = addeach(windowedSpectrum, windowedMagnitudes[r]);
            }
            windowedSpectrum = div(windowedSpectrum, windowedMagnitudes.size());

            auto clearSpectrum = npzeros(fftSize);
            for (int r = 0; r < cleanMagnitudes.size(); r++) {
                clearSpectrum = addeach(clearSpectrum, cleanMagnitudes[r]);
            }
            clearSpectrum = div(clearSpectrum, cleanMagnitudes.size());

            std::vector<dsp::complex_t> noise(fftSize, {0, 0});

            if (lossRate > 0) {
                auto nf = filterSignal(windowedSpectrum, clearSpectrum, this->out.writeBuf); // result is filtered writebuf
                if (!txMode) {
                    auto newNoiseFigure = estimateNoise(nf); // i/q variance estimate, output is in noise figure.
                    sharedDataLock.lock();
                    noiseFigure = newNoiseFigure;
                    sharedDataLock.unlock();
                }
            }
            auto unfiltered = this->out.writeBuf;
            for(int i=0; i<fftSize; i++) {
                // make amplitude logarithmic, for better for 8bit scaling
                if (unfiltered[i].re == 0 && unfiltered[i].im == 0) {

                } else {
                    auto amp = unfiltered[i].amplitude();
                    auto namp = sqrt(sqrt(amp));
                    unfiltered[i] *= (namp / amp);
                }
            }

            if (fftSize < 16000 && false) {
                uint8_t  buf[30000];
                dsp::complex_t tbuf[30000];
                int cnt = SampleStreamCompressor::process(fftSize, PCM_TYPE_I8, unfiltered, buf);
                int ocnt = SampleStreamDecompressor::process(cnt, buf, tbuf);
                float maxdelta = 0;
                for(int q=0; q<fftSize; q++) {
                    auto delta = (tbuf[q] - unfiltered[q]).amplitude();
                    if (delta > maxdelta) {
                        maxdelta = delta;
                    }
                }
                flog::info("ocnt = {}  maxerr={}", ocnt, maxdelta);
            }


            // averaged windowedSpectrum ready, normalized around 0. Calculating noise floor

//            auto smallSma = centeredSma(windowedSpectrum, smallTick);
//            auto largeSma = centeredSma(windowedSpectrum, largeTick);
//            auto mask = npzeros(fftSize);
//            int rawNoiseCount = 0;
//            auto sdata = windowedSpectrum->data();;
//            auto sigmaskdata = mask->data();
//            for(int i=1; i<fftSize-1; i++) {
//                if (sdata[i] * sdata[i-1] < 0 && sdata[i] * sdata[i+1] < 0) {
//                    // noikse
//                    rawNoiseCount++;
//                } else {
//                    sigmaskdata[i] = 1.0;
//                }
//            }
//            for(int i=0; i<fftSize-2; i++) {
//                if (sigmaskdata[i] == 0 && sigmaskdata[i+2] == 0 && sigmaskdata[i+1] != 0) {
//                    sigmaskdata[i+1] = 0;       // remove singular signals, which are not.
//                    rawNoiseCount++;
//                }
//            }
//
//            int noiseCount = 0;
//            int sliceSize = fftSize / noiseNPoints;
//
//            if (rawNoiseCount > 30 * noiseNPoints) {
//                auto spectrumVars = npzeros(noiseNPoints);  // variances
//                for (int p = 0; p < noiseNPoints; p++) {
//                    float sum = 0;
//                    int cnt = 0;
//                    for (int q = 0; q < sliceSize; q++) {
//                        if (sigmaskdata[q + p * sliceSize] == 0) {
//                            double dbl = sdata[q + p * sliceSize];
//                            sum += dbl * dbl;
//                            cnt++;
//                        }
//                    }
//                    if (cnt == 0) {
//                        cnt = 1; // value will be 0
//                    }
//                    spectrumVars->at(p) = sum / cnt;
//                }
//
//                math::linearInterpolateHoles(spectrumVars->data(), spectrumVars->size());
//                spectrumVars->emplace_back(spectrumVars->back()); // duplicate last item.
//                spectrumVars = centeredSma(spectrumVars, 5);
//
//
//                // build noise figure.
//
//                auto iMean = npzeros(noiseNPoints);
//                auto qMean = npzeros(noiseNPoints);
//                auto iDev = npzeros(noiseNPoints);
//                auto qDev = npzeros(noiseNPoints);
//                char logg[10000] = {0};
//                for (int p = 0; p < noiseNPoints; p++) {
//                    dsp::complex_t iqSum = {0, 0};
//                    int cnt = 0;
//                    for (int q = 0; q < sliceSize; q++) {
//                        int srcIndex = q + p * sliceSize;
//                        if (smallSma->at(srcIndex) < spectrumVars->at(p) * lossRate && lossRate > 0) {
//                            iqSum += this->out.writeBuf[srcIndex];
//                            cnt++;
//                        }
//                    }
//                    sprintf(logg + strlen(logg), "%d: cnt1=%d", p, cnt);
//                    if (cnt == 0) {
//                        cnt = 1;
//                    }
//                    dsp::complex_t mean = {iqSum.re / cnt, iqSum.im / cnt};
//                    iMean->at(p) = mean.re;
//                    qMean->at(p) = mean.im;
//
//                    dsp::complex_t iqVar = {0, 0};
//                    cnt = 0;
//                    for (int q = 0; q < sliceSize; q++) {
//                        int srcIndex = q + p * sliceSize;
//                        if (smallSma->at(srcIndex) < spectrumVars->at(p) * lossRate && lossRate > 0) {
//                            auto dd = this->out.writeBuf[q + p * sliceSize] - mean;
//                            iqVar += {dd.re * dd.re, dd.im * dd.im};
//                            cnt++;
//                        }
//                    }
//                    sprintf(logg + strlen(logg), " cnt2=%d iqv=%f+i*%f .", cnt, iqVar.re, iqVar.im);
//                    if (cnt == 0) {
//                        cnt = 1;
//                    }
//                    dsp::complex_t iqDev = {sqrt(iqVar.re / cnt), sqrt(iqVar.im / cnt)};
//                    iDev->at(p) = iqDev.re;
//                    qDev->at(p) = iqDev.im;
//                }
////                flog::info("comp: {}", logg);
//                math::linearInterpolateHoles(iMean->data(), iMean->size());
//                math::linearInterpolateHoles(qMean->data(), qMean->size());
//                math::linearInterpolateHoles(iDev->data(), iDev->size());
//                math::linearInterpolateHoles(qDev->data(), qDev->size());
//                iMean = centeredSma(iMean, 5);
//                qMean = centeredSma(qMean, 5);
//                iDev = centeredSma(iDev, 5);
//                qDev = centeredSma(qDev, 5);
//
//                if (lossRate > 0) {
//                    sharedDataLock.lock();
//                    noiseFigure.clear();
//                    for (int p = 0; p < noiseNPoints; p++) {
//                        dsp::complex_t iqMean = {iMean->at(p), qMean->at(p)};
//                        dsp::complex_t iqDev = {iDev->at(p), qDev->at(p)};
//                        if (isnan(iqMean.im) || isnan(iqMean.re) || isnan(iqDev.im) || isnan(iqDev.re)) {
//                            flog::info("noise figure is nan: {} {} {} {}", iqMean.im, iqMean.re, iqDev.im, iqDev.re);
//                        }
//                        noiseFigure.emplace_back(iqMean);
//                        noiseFigure.emplace_back(iqDev);
//                    }
//                    sharedDataLock.unlock();
//                }
//
//                for(int i=0; i<fftSize; i++) {
//                    if (smallSma->at(i) < spectrumVars->at(i / sliceSize) * lossRate && lossRate > 0) {
//                        // remove signals below the noise level.
//                        this->out.writeBuf[i] = {0, 0};
//                        noiseCount++;
//                    }
//                }
//
//            }


//            flog::info("RawNoiseCount={} NoiseCount={} ", rawNoiseCount, noiseCount);

            iteration++;
            if (iteration == -10) {
                FILE *t = fopen("/tmp/arr.bin", "wb");
                if (t) {
                    for (int r = 0; r < minRecents; r++) {
                        fwrite(cleanFreqDomain[r].get()->data(), sizeof(dsp::complex_t), fftSize, t);
                    }
                    fclose(t);
                }
            }

            cleanFreqDomain.erase(cleanFreqDomain.begin() + 0);
            if (cleanMagnitudes.size() > minRecents * 2) {
                cleanMagnitudes.erase(cleanMagnitudes.begin() + 0);
                windowedMagnitudes.erase(windowedMagnitudes.begin() + 0);
            }

            return fftSize;
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

                inputBuffer.insert(inputBuffer.end(), base_type::_in->readBuf, base_type::_in->readBuf + count);
                base_type::_in->flush();

                int outCount = process();

                // Swap if some data was generated
                if (outCount) {
                    if (!base_type::out.swap(outCount)) { return -1; }
                }
//                flog::info("experimental FFT compr: inCount={} outCount={} bufSize={}", count, outCount, (int)inputBuffer.size());
                return outCount;
            }
        }


    };
}