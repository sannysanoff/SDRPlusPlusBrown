#pragma once
#include "../demod.h"
#include <dsp/demodulator.h>
#include <dsp/filter.h>

namespace demod {
    class AM : public Demodulator, public HasAGC {
    public:
        AM() {}

        AM(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, EventHandler<dsp::stream<dsp::stereo_t>*> outputChangeHandler, EventHandler<float> afbwChangeHandler, double audioSR) {
            init(name, config, input, bandwidth, outputChangeHandler, afbwChangeHandler, audioSR);
        }

        ~AM() {
            stop();
        }

        void init(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, EventHandler<dsp::stream<dsp::stereo_t>*> outputChangeHandler, EventHandler<float> afbwChangeHandler, double audioSR) override {
            this->name = name;

            // Define structure
            demod.init(input);
            agc.init(&demod.out, 20.0f, getIFSampleRate());
            m2s.init(&agc.out);
        }

        void start() override {
            demod.start();
            agc.start();
            m2s.start();
        }

        void stop() override {
            demod.stop();
            agc.stop();
            m2s.stop();
        }

        void showMenu() override {
            // TODO: Adjust AGC settings
        }

        void setBandwidth(double bandwidth) override {}

        void setInput(dsp::stream<dsp::complex_t>* input) override {
            demod.setInput(input);
        }

        void AFSampRateChanged(double newSR) override {}

        // ============= INFO =============

        const char* getName() override { return "AM"; }
        double getIFSampleRate() override { return 15000.0; }
        double getAFSampleRate() override { return getIFSampleRate(); }
        double getDefaultBandwidth() override { return 10000.0; }
        double getMinBandwidth() override { return 1000.0; }
        double getMaxBandwidth() override { return getIFSampleRate(); }
        bool getBandwidthLocked() override { return false; }
        double getMaxAFBandwidth() override { return getIFSampleRate() / 2.0; }
        double getDefaultSnapInterval() override { return 1000.0; }
        int getVFOReference() override { return ImGui::WaterfallVFO::REF_CENTER; }
        bool getDeempAllowed() override { return false; }
        bool getPostProcEnabled() override { return true; }
        int getDefaultDeemphasisMode() override { return DEEMP_MODE_NONE; }
        double getAFBandwidth(double bandwidth) override { return bandwidth / 2.0; }
        bool getDynamicAFBandwidth() override { return true; }
        bool getFMIFNRAllowed() override { return false; }
        bool getNBAllowed() override { return false; }
        dsp::stream<dsp::stereo_t>* getOutput() override { return &m2s.out; }

        dsp::AGC &getAGC() override {
            return agc;
        }

    private:
        dsp::AMDemod demod;
        dsp::AGC agc;
        dsp::MonoToStereo m2s;

        std::string name;
    };
}