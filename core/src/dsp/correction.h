#pragma once
#include <dsp/block.h>
#include <dsp/stream.h>
#include <dsp/types.h>
#include <dsp/window.h>

namespace dsp {
    class IQCorrector : public generic_block<IQCorrector> {
    public:
        IQCorrector() {}

        IQCorrector(stream<complex_t>* in, float rate) { init(in, rate); }

        ~IQCorrector() {
            freeIt();
        }

        void freeIt() {
            if (forwardPlan) fftwf_destroy_plan(forwardPlan);
            if (backwardPlan) fftwf_destroy_plan(backwardPlan);
            if (fft_in) fftwf_free(fft_in);
            if (fft_cout) fftwf_free(fft_cout);
            if (fft_out) fftwf_free(fft_out);
            fft_out = nullptr;
            fft_cout = nullptr;
            fft_in = nullptr;
            backwardPlan = nullptr;
            forwardPlan = nullptr;
        }

        void init(stream<complex_t>* in, float rate) {
            _in = in;
            correctionRate = rate;
            offset.re = 0;
            offset.im = 0;
            generic_block<IQCorrector>::registerInput(_in);
            generic_block<IQCorrector>::registerOutput(&out);
            generic_block<IQCorrector>::_block_init = true;
        }

        void setInput(stream<complex_t>* in) {
            assert(generic_block<IQCorrector>::_block_init);
            std::lock_guard<std::mutex> lck(generic_block<IQCorrector>::ctrlMtx);
            generic_block<IQCorrector>::tempStop();
            generic_block<IQCorrector>::unregisterInput(_in);
            _in = in;
            generic_block<IQCorrector>::registerInput(_in);
            generic_block<IQCorrector>::tempStart();
        }

        void setCorrectionRate(float rate) {
            correctionRate = rate;
        }

        struct NormalizationWindow {
            std::vector<complex_t> &dest;
            int windowLength;
            float sumAmplitude = 0;
            int countAmplitude = 0;
            std::vector<complex_t> window;
            int windowWrite = 0;
            int windowRead = 0;
            NormalizationWindow(std::vector<complex_t> &dest, int windowLength) : dest(dest), windowLength(windowLength) {
                window.resize(windowLength, {0.0f, 0.0f});
            }

            void addSample(complex_t sample) {
                auto oldValue = window[windowWrite];
                window[windowWrite] = sample;
                windowWrite = (windowWrite + 1) % window.size();
                auto newAmp = log10(sample.re*sample.re + sample.im * sample.im);
                auto oldAmp = log10(oldValue.re*oldValue.re + oldValue.im * oldValue.im);
                if (!std::isinf(newAmp) && !std::isinf(oldAmp)) {
                    sumAmplitude += newAmp;
                    sumAmplitude -= oldAmp;
                    if (countAmplitude < windowLength)
                        countAmplitude++;
                }
                if (countAmplitude == windowLength) {
                    float currentAmp = sumAmplitude/(float)countAmplitude;
                    dest.emplace_back(oldValue / pow(10.0f, currentAmp) * 2.0);
                }
            }
        };


        fftwf_plan forwardPlan = nullptr;
        fftwf_plan backwardPlan = nullptr;
        fftwf_complex *fft_in = nullptr;
        fftwf_complex *fft_cout = nullptr;
        fftwf_complex *fft_out = nullptr;
        int tapCount;
        std::vector<complex_t> inputBuffer;
        std::vector<complex_t> outputBuffer;
        std::shared_ptr<NormalizationWindow> nw;

        void setEffectiveSampleRate(int rate) {
            tapCount = sampleRate/10;
            sampleRate = rate;
            freeIt();
            fft_in = (fftwf_complex *)fftwf_malloc(sizeof(complex_t) * tapCount);
            fft_cout = (fftwf_complex *)fftwf_malloc(sizeof(complex_t) * tapCount);
            fft_out = (fftwf_complex *)fftwf_malloc(sizeof(complex_t) * tapCount);
            forwardPlan = fftwf_plan_dft_1d(tapCount, fft_in, fft_cout, FFTW_FORWARD, FFTW_ESTIMATE);
            backwardPlan = fftwf_plan_dft_1d(tapCount, fft_cout, fft_out, FFTW_BACKWARD, FFTW_ESTIMATE);
            nw = std::make_shared<NormalizationWindow>(outputBuffer, 3000);
        }



        int run() {
            int count = _in->read();
            if (count < 0) { return -1; }

            if (bypass) {
                memcpy(out.writeBuf, _in->readBuf, count * sizeof(complex_t));

                _in->flush();

                if (!out.swap(count)) { return -1; }

                return count;
            }

            if (true) { // pass through this

                auto preSize = inputBuffer.size();
                inputBuffer.resize(preSize+count);
                memcpy(inputBuffer.data()+preSize, _in->readBuf, sizeof(complex_t)*count);
                _in->flush();

                while (inputBuffer.size() >= tapCount) {
                    memcpy(fft_in, inputBuffer.data(), sizeof(complex_t) * tapCount);
                    inputBuffer.erase(inputBuffer.begin(), inputBuffer.begin() + tapCount);
                    fftwf_execute(forwardPlan);
                    fftwf_execute(backwardPlan);
//                    memcpy(out.writeBuf, fft_out, sizeof(complex_t) * tapCount);
                    for(int i=0; i<tapCount; i++) {
                        nw->addSample(((complex_t *)fft_out)[i] / tapCount);
//                        outputBuffer.emplace_back();
                    }
                }
                if (outputBuffer.size() >= count) {
                    memcpy(out.writeBuf, outputBuffer.data(), count * sizeof(complex_t));
                    outputBuffer.erase(outputBuffer.begin(), outputBuffer.begin()+count);
                    if (!out.swap(count)) { return -1; }
                    return count;
                }
                return 0;
            } else {
                for (int i = 0; i < count; i++) {
                    out.writeBuf[i] = _in->readBuf[i] - offset;
                    offset = offset + (out.writeBuf[i] * correctionRate);
                }
                if (!out.swap(count)) { return -1; }
            }

            return count;
        }

        stream<complex_t> out;

        // TEMPORARY FOR DEBUG PURPOSES
        bool bypass = false;
        complex_t offset;

    private:
        stream<complex_t>* _in;
        float correctionRate = 0.00001;
        int sampleRate = 384000;
    };

    class DCBlocker : public generic_block<DCBlocker> {
    public:
        DCBlocker() {}

        DCBlocker(stream<float>* in, float rate) { init(in, rate); }

        void init(stream<float>* in, float rate) {
            _in = in;
            correctionRate = rate;
            offset = 0;
            generic_block<DCBlocker>::registerInput(_in);
            generic_block<DCBlocker>::registerOutput(&out);
            generic_block<DCBlocker>::_block_init = true;
        }

        void setInput(stream<float>* in) {
            assert(generic_block<DCBlocker>::_block_init);
            std::lock_guard<std::mutex> lck(generic_block<DCBlocker>::ctrlMtx);
            generic_block<DCBlocker>::tempStop();
            generic_block<DCBlocker>::unregisterInput(_in);
            _in = in;
            generic_block<DCBlocker>::registerInput(_in);
            generic_block<DCBlocker>::tempStart();
        }

        void setCorrectionRate(float rate) {
            correctionRate = rate;
        }

        int run() {
            int count = _in->read();
            if (count < 0) { return -1; }

            if (bypass) {
                memcpy(out.writeBuf, _in->readBuf, count * sizeof(complex_t));

                _in->flush();

                if (!out.swap(count)) { return -1; }

                return count;
            }

            for (int i = 0; i < count; i++) {
                out.writeBuf[i] = _in->readBuf[i] - offset;
                offset = offset + (out.writeBuf[i] * correctionRate);
            }

            _in->flush();

            if (!out.swap(count)) { return -1; }

            return count;
        }

        stream<float> out;

        // TEMPORARY FOR DEBUG PURPOSES
        bool bypass = false;
        float offset;

    private:
        stream<float>* _in;
        float correctionRate = 0.00001;
    };


}