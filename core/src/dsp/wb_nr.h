#pragma once
#include <dsp/block.h>
#include <dsp/stream.h>
#include <dsp/types.h>
#include <dsp/window.h>
#include <dsp/utils/logmmse.h>
#include <gui/widgets/snr_meter.h>


namespace dsp {

    using namespace ::dsp::arrays;
    using namespace ::dsp::logmmse;

    class WidebandNoiseReduction : public generic_block<WidebandNoiseReduction> {
    public:
        WidebandNoiseReduction() {}

        WidebandNoiseReduction(stream<complex_t>* in) { init(in); }

        ~WidebandNoiseReduction() {
            freeIt();
        }

        void freeIt() {
//            if (forwardPlan) fftwf_destroy_plan(forwardPlan);
//            if (backwardPlan) fftwf_destroy_plan(backwardPlan);
//            if (fft_in) fftwf_free(fft_in);
//            if (fft_cout) fftwf_free(fft_cout);
//            if (fft_out) fftwf_free(fft_out);
//            fft_out = nullptr;
//            fft_cout = nullptr;
//            fft_in = nullptr;
//            backwardPlan = nullptr;
//            forwardPlan = nullptr;
        }

        void init(stream<complex_t>* in) {
            _in = in;
            generic_block<WidebandNoiseReduction>::registerInput(_in);
            generic_block<WidebandNoiseReduction>::registerOutput(&out);
            generic_block<WidebandNoiseReduction>::_block_init = true;
        }

    public:

        void setInput(stream<complex_t>* in) {
            assert(generic_block<WidebandNoiseReduction>::_block_init);
            std::lock_guard<std::mutex> lck(generic_block<WidebandNoiseReduction>::ctrlMtx);
            generic_block<WidebandNoiseReduction>::tempStop();
            generic_block<WidebandNoiseReduction>::unregisterInput(_in);
            _in = in;
            generic_block<WidebandNoiseReduction>::registerInput(_in);
            generic_block<WidebandNoiseReduction>::tempStart();
        }

//        void setCorrectionRate(float rate) {
//        }

        void setEffectiveSampleRate(int rate) {
            freq = rate;
            params.reset();
        }

        ComplexArray worker1c;
        ComplexArray trailForSample;
        int freq = 192000;
        LogMMSE::SavedParamsC params;
        LogMMSE::SavedParamsC paramsUnder;
        std::mutex freqMutex;

        void doStart() override {
            generic_block<WidebandNoiseReduction>::doStart();
            worker1c.reset();
        }

        void setHold(bool hold) {
            params.hold = hold;
            paramsUnder.hold = hold;
        }


        int runMMSE(stream <complex_t> *_in, stream <complex_t> &out) {
            static int switchTrigger = 0;
            static int overlapTrigger = -100000;
            bool enableAutoSwitch = false;
            if (!worker1c) {
                worker1c = npzeros_c(0);
                trailForSample = npzeros_c(0);
                params.reset();
                switchTrigger = 0;
                overlapTrigger = 0;
            }
            int count = _in->read();
            if (count < 0) { return -1; }
            for (int i = 0; i < count; i++) {
                worker1c->emplace_back(_in->readBuf[i]);
                trailForSample->emplace_back(_in->readBuf[i]);
                switchTrigger++;
                overlapTrigger++;
            }
            _in->flush();
//            if (lastFrequency != getCurrentFrequency()) {
//                worker1c->clear();
//                params.reset();
//                lastFrequency = getCurrentFrequency();
//                return 0;
//            }

            int noiseFrames = 12;
            int switchInterval = 1000000;
            int overlapInterval = 40000;
            int fram = freq / 100;
            int initialDemand = fram * 2;
            if (!params.Xk_prev) {
                initialDemand = fram * (noiseFrames + 2) * 2;
            }
            if (worker1c->size() < initialDemand) {
                return 0;
            }
            int retCount = 0;
            freqMutex.lock();
            if (!params.Xk_prev) {
                std::cout << std::endl << "Sampling initially" << std::endl;
                LogMMSE::logmmse_sample(worker1c, freq, 0.15f, &params, noiseFrames);
                paramsUnder = params;
                overlapTrigger = -100000000;
                switchTrigger = 0;
            }
            if (switchTrigger > switchInterval && enableAutoSwitch) {
                float maxv = ImGui::SNRMeterGetMaxInWindow(50);     // assuming SNR repaint rate constant 60
                float minv = ImGui::SNRMeterGetMinInWindow(50);
                float spread = maxv-minv;
                if (spread < 12.0f) {
                    std::cout << std::endl << currentTimeMillis() << " Begin noise transition... silence indicator: " << spread << std::endl;
                    trailForSample->erase(trailForSample->begin(), trailForSample->begin() + (trailForSample->size() - fram * (noiseFrames + 2) * 2));
                    LogMMSE::logmmse_sample(trailForSample, freq, 0.15f, &paramsUnder, noiseFrames);
                    overlapTrigger = 0;
                    switchTrigger = 0;
                } else {
                    static int lastSpread = -1;
                    if (lastSpread != (int)spread) {
                        lastSpread = (int)spread;
                        std::cout << lastSpread <<" ";
                        std::flush(std::cout);
                    }
                }
            }
            if (overlapTrigger > overlapInterval && enableAutoSwitch) {
                std::cout << std::endl << currentTimeMillis() << " Switching params" << std::endl;
                params = paramsUnder;
                switchTrigger = 0;
                overlapTrigger = -0x7FFFFFFF;
            }
            auto rv = LogMMSE::logmmse_all(worker1c, 48000, 0.15f, &params);
            if (enableAutoSwitch) {
                rv = LogMMSE::logmmse_all(worker1c, 48000, 0.15f, &paramsUnder);
            }
            freqMutex.unlock();

            int limit = rv->size();
            auto dta = rv->data();
            for (int i = 0; i < limit; i++) {
                auto lp = dta[i];
                out.writeBuf[i] = lp * 4;
            }
            memmove(worker1c->data(), ((complex_t *) worker1c->data()) + rv->size(), sizeof(complex_t) * (worker1c->size() - rv->size()));
            worker1c->resize(worker1c->size() - rv->size());
            if (!out.swap(rv->size())) { return -1; }
            retCount += rv->size();
            return retCount;

        }

        int run() override {
            int count = _in->read();
            if (count < 0) { return -1; }

            if (bypass) {
                memcpy(out.writeBuf, _in->readBuf, count * sizeof(complex_t));
                _in->flush();
                if (!out.swap(count)) { return -1; }
                return count;
            }

            runMMSE(_in, out);
            return count;
        }

        stream<complex_t> out;

        bool bypass = false;
        bool hold = false;

    private:
        stream<complex_t>* _in;
    };



}