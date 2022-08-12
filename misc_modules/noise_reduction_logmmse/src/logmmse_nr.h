
#pragma once

#include <cmath>
#include "dsp/block.h"
#include <dsp/utils/window_functions.h>
#include <dsp/utils/logmmse.h>
#include <fftw3.h>
#include <stdio.h>
#include <iostream>
#include "signal_path/signal_path.h"
#include "../../recorder/src/wav.h"


namespace dsp {

    using namespace ::dsp::arrays;
    using namespace ::dsp::logmmse;


    struct LogMMSENoiseReduction : public generic_block<LogMMSENoiseReduction> {

        stream<complex_t> *_in;
        stream<complex_t> out;

        ComplexArray worker1c;

        void init(stream<complex_t> *in) {
            _in = in;
            generic_block<LogMMSENoiseReduction>::registerInput(_in);
            generic_block<LogMMSENoiseReduction>::registerOutput(&out);
            generic_block<LogMMSENoiseReduction>::_block_init = true;
            worker1c = std::make_shared<std::vector<complex_t>>();
        }

        void setInput(stream<complex_t> *in) {
            assert(generic_block<LogMMSENoiseReduction>::_block_init);
            std::lock_guard<std::mutex> lck(generic_block<LogMMSENoiseReduction>::ctrlMtx);
            generic_block<LogMMSENoiseReduction>::tempStop();
            generic_block<LogMMSENoiseReduction>::unregisterInput(_in);
            _in = in;
            generic_block<LogMMSENoiseReduction>::registerInput(_in);

            params.reset();
            generic_block<LogMMSENoiseReduction>::tempStart();
        }


        virtual ~LogMMSENoiseReduction() {

        }


        LogMMSE::SavedParamsC params;
        LogMMSE::SavedParamsC paramsUnder;

        double getCurrentFrequency() {
            if (gui::waterfall.selectedVFO == "") {
                return gui::waterfall.getCenterFrequency();
            } else {
                return gui::waterfall.getCenterFrequency() + sigpath::vfoManager.getOffset(gui::waterfall.selectedVFO);
            }
        }


        int bandwidthHz = 10000;

        std::mutex freqMutex;

        void setBandwidth(int bandwidthHz) {
            freqMutex.lock();
            this->bandwidthHz = bandwidthHz;
            params.reset();
            freqMutex.unlock();
        }

        bool switchRequested = false;

        void refreshNoiseProfile() {
            switchRequested = true;
            paramsUnder.reset();
        }


        double lastFrequency = 0.0;
        int lastWidebandNR = -1;

        int run() override {

            if (getCurrentFrequency() != lastFrequency) {
                lastFrequency = getCurrentFrequency();
                refreshNoiseProfile();
            }

            int count = _in->read();
            if (count < 0) { return -1; }
            static int switchTrigger = 0;
            static int overlapTrigger = -100000;
            for (int i = 0; i < count; i++) {
                worker1c->emplace_back(_in->readBuf[i]);
                switchTrigger++;
                overlapTrigger++;
            }
            _in->flush();
//            if (lastFrequency != getCurrentFrequency()) {
//                worker1c->clear();
//                params.Xk_prev.reset();
//                lastFrequency = getCurrentFrequency();
//                return 0;
//            }

            int noiseFrames = 12;
            int fram = bandwidthHz / 100;
            int initialDemand = fram * (noiseFrames + 2) * 2;
            if (worker1c->size() < initialDemand) {
                return 0;
            }
            int retCount = 0;
            freqMutex.lock();
            if (!params.Xk_prev) {
                LogMMSE::logmmse_sample(worker1c, bandwidthHz, 0.15f, &params, noiseFrames);
//                params.hold = true;
                printf("logmsse: sampled\n");
                overlapTrigger = -1000000;
                switchTrigger = 0;
            }
            if (switchRequested && !paramsUnder.Xk_prev) {
                LogMMSE::logmmse_sample(worker1c, bandwidthHz, 0.15f, &paramsUnder, noiseFrames);
                overlapTrigger = 0;
                switchTrigger = 0;
                printf("sample under\n");
            }
            if (switchRequested && paramsUnder.stable) {
                params = paramsUnder;
                switchRequested = false;
                paramsUnder.reset();
                printf("activating under\n");
            }
            auto rv = LogMMSE::logmmse_all(worker1c, 48000, 0.15f, &params);
            if (switchRequested && paramsUnder.stable) {
                rv = LogMMSE::logmmse_all(worker1c, 48000, 0.15f, &paramsUnder);
            }
            freqMutex.unlock();
            int limit = rv->size();
            auto dta = rv->data();
            for (int i = 0; i < limit; i++) {
                auto lp = dta[i];
                out.writeBuf[i] = lp;
            }
            memmove(worker1c->data(), ((complex_t *) worker1c->data()) + rv->size(), sizeof(complex_t) * (worker1c->size() - rv->size()));
            worker1c->resize(worker1c->size() - rv->size());
            if (!out.swap(rv->size())) { return -1; }
            retCount += rv->size();
            return retCount;
        }

    };


}