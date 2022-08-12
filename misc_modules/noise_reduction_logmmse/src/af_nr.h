
#pragma once

#pragma once
#include <dsp/block.h>
#include <dsp/stream.h>
#include <dsp/types.h>
#include <dsp/processor.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include "arrays.h"
#include "logmmse.h"

namespace dsp {

    using namespace ::dsp::arrays;
    using namespace ::dsp::logmmse;

    template <int V>
    struct SMAStream {
        std::vector<dsp::complex_t> input;
        std::vector<dsp::complex_t> output;
        void write(dsp::complex_t* values, size_t size) {
            int oldSize = input.size();
            input.resize(oldSize + size);
            memmove(input.data() + oldSize, values, size * sizeof(dsp::complex_t));
            produceSMA();
        }

        void produceSMA() {
            while (output.size() < V && input.size() > V) {
                output.emplace_back(input[output.size()]);
            }
            if (output.size() < V) {
                return;
            }
            complex_t s = { 0.0, 0.0 };
            for (int q = output.size() - V; q < output.size(); q++) {
                s += input[q];
            }
            for (int q = output.size(); q < input.size(); q++) {
                s.re -= input[q - V].re;
                s.im -= input[q - V].im;
                s.re += input[q].re;
                s.im += input[q].im;
                output.emplace_back(complex_t{ s.re / V, s.im / V });
            }
        }

        void read(dsp::complex_t* values, size_t size) {
            if (output.size() < size) {
                abort();
            }
            memmove(values, output.data(), size * sizeof(dsp::complex_t));
            output.erase(output.begin(), output.begin() + size);
            input.erase(input.begin(), input.begin() + size);
        }

        size_t available() {
            return output.size() - V;
        }
    };

    struct AFNRLogMMSE : public Processor<complex_t, complex_t> {

        using base_type = Processor<complex_t, complex_t>;

        ComplexArray worker1c;

        void init(stream<complex_t>* in) {
            base_type::init(in);
        }

        void setInput(stream<complex_t>* in) override {
            base_type ::setInput(in);
            params.reset();
        }

        AFNRLogMMSE() {
            worker1c = std::make_shared<std::vector<complex_t>>();
        }


        LogMMSE::SavedParamsC params;

        double getVFOFrequency() {
            if (gui::waterfall.selectedVFO == "") {
                return gui::waterfall.getCenterFrequency();
            }
            else {
                return gui::waterfall.getCenterFrequency() + sigpath::vfoManager.getOffset(gui::waterfall.selectedVFO);
            }
        }

        double getVFOBandwidth() {
            if (gui::waterfall.selectedVFO == "") {
                return gui::waterfall.getBandwidth();
            }
            else {
                return sigpath::vfoManager.getBandwidth(gui::waterfall.selectedVFO);
            }
        }

        int processingBandwidthHz = 10000;

        std::mutex freqMutex;

        void setProcessingBandwidth(int bandwidthHz) {
            spdlog::info("Refreshing noise profile for AF NR (logmmse)");
            freqMutex.lock();
            this->processingBandwidthHz = bandwidthHz;
            params.reset();
            freqMutex.unlock();
        }

        void refreshNoiseProfile() {
            spdlog::info("Refreshing noise profile for AF NR (logmmse)");
            freqMutex.lock();
            params.reset();
            freqMutex.unlock();
        }


        double lastVFOFrequency = 0.0;
        double lastVFOBandwidth = 0.0;

        bool allowed = false;   // initial value
        int afnrBandwidth = 10; // this is UI model value, just stored there.
        SMAStream<5> sma;

        int run() override {

            if (getVFOFrequency() != lastVFOFrequency) {
                lastVFOFrequency = getVFOFrequency();
                refreshNoiseProfile();
            }
            if (getVFOBandwidth() != lastVFOBandwidth) {
                lastVFOBandwidth = getVFOBandwidth();
                refreshNoiseProfile();
            }

            int count = _in->read();
            if (count < 0) { return -1; }
            static int switchTrigger = 0;
            static int overlapTrigger = -100000;
            auto curSize = worker1c->size();
            worker1c->resize(curSize + count);
            memmove(worker1c->data() + curSize, _in->readBuf, count * sizeof(complex_t));
            switchTrigger += count;
            overlapTrigger += count;
            _in->flush();

            int noiseFrames = 12;
            int fram = processingBandwidthHz / 100;
            int initialDemand = fram * (noiseFrames + 2) * 2;
            if (worker1c->size() < initialDemand && !params.Xk_prev) {
                // pass throug until it fills
                memmove(out.writeBuf, worker1c->data() + curSize, count * sizeof(complex_t));
                if (!out.swap(count)) { return -1; }
                return 0;
            }
            if (!params.Xk_prev) {
                auto Slen = (int)floor(0.02 * 48000);
                spdlog::info("Sampling, total samples: {0}, will be used: {1}", worker1c->size(), noiseFrames * Slen);
                LogMMSE::logmmse_sample(worker1c, processingBandwidthHz, 0.15f, &params, noiseFrames);
                worker1c->erase(worker1c->begin(), worker1c->begin() + curSize); // skip everything already sent to the output before
            }
            int retCount = 0;
            freqMutex.lock();
            if (worker1c->size() >= 4 * params.Slen) {
                auto rv = LogMMSE::logmmse_all(worker1c, 48000, 0.15f, &params);
                int limit = rv->size();
                auto dta = rv->data();

                sma.write(dta, limit);

                if (sma.available() >= limit) {
                    sma.read(out.writeBuf, limit);
//                    static int _seq = 0;
//                    int seq = _seq++;
//                    if (seq < 15) {
//                        for (int i = 0; i < limit; i++) {
//                            auto lp = out.writeBuf[i];
//                            std::cout << i << "\t" << lp.re << "\t" << lp.im << std::endl;
//                        }
//                    }
                    memmove(worker1c->data(), ((complex_t*)worker1c->data()) + rv->size(), sizeof(complex_t) * (worker1c->size() - rv->size()));
                    worker1c->resize(worker1c->size() - rv->size());
                    if (!out.swap(rv->size())) {
                        freqMutex.unlock();
                        return -1;
                    }
                    retCount += rv->size();
                }
            }
            freqMutex.unlock();
            return retCount;
        }
    };


}