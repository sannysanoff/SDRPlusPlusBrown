#pragma once
#include "../demod.h"
#include <dsp/demodulator.h>
#include <dsp/filter.h>

namespace demod {
    class USB : public Demodulator, public HasAGC {
    public:
        USB() {}

        USB(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, EventHandler<dsp::stream<dsp::stereo_t>*> outputChangeHandler, EventHandler<float> afbwChangeHandler, double audioSR) {
            init(name, config, input, bandwidth, outputChangeHandler, afbwChangeHandler, audioSR);
        }

        ~USB() {
            stop();
        }

        void init(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, EventHandler<dsp::stream<dsp::stereo_t>*> outputChangeHandler, EventHandler<float> afbwChangeHandler, double audioSR) override {
            this->name = name;

            // Define structure
            demod.init(input, getIFSampleRate(), bandwidth, dsp::SSBDemod::MODE_USB);
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

        void setBandwidth(double bandwidth) override {
            demod.setBandWidth(bandwidth);
        }

        void setInput(dsp::stream<dsp::complex_t>* input) override {
            demod.setInput(input);
        }

        void AFSampRateChanged(double newSR) override {}

        // ============= INFO =============

        const char* getName() override { return "USB"; }
        double getIFSampleRate() override { return 24000.0; }
        double getAFSampleRate() override { return getIFSampleRate(); }
        double getDefaultBandwidth() override { return 2800.0; }
        double getMinBandwidth() override { return 500.0; }
        double getMaxBandwidth() override { return getIFSampleRate() / 2.0; }
        bool getBandwidthLocked() override { return false; }
        double getMaxAFBandwidth() override { return getIFSampleRate() / 2.0; }
        double getDefaultSnapInterval() override { return 100.0; }
        int getVFOReference() override { return ImGui::WaterfallVFO::REF_LOWER; }
        bool getDeempAllowed() override { return false; }
        bool getPostProcEnabled() override { return true; }
        int getDefaultDeemphasisMode() override { return DEEMP_MODE_NONE; }
        double getAFBandwidth(double bandwidth) override { return bandwidth; }
        bool getDynamicAFBandwidth() override { return true; }
        bool getFMIFNRAllowed() override { return false; }
        bool getNBAllowed() override { return true; }
        dsp::stream<dsp::stereo_t>* getOutput() override { return &m2s.out; }

        dsp::AGC &getAGC() override {
            return agc;
        }

    private:
        dsp::SSBDemod demod;
        dsp::AGC agc;
        dsp::MonoToStereo m2s;

        std::string name;
    };
}