#pragma once
#include "../demod.h"
#include <dsp/demodulator.h>
#include <dsp/filter.h>

namespace demod {
    class CW : public Demodulator, public HasAGC {
    public:
        CW() {}

        CW(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, EventHandler<dsp::stream<dsp::stereo_t>*> outputChangeHandler, EventHandler<float> afbwChangeHandler, double audioSR) {
            init(name, config, input, bandwidth, outputChangeHandler, afbwChangeHandler, audioSR);
        }

        ~CW() {
            stop();
        }

        void init(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, EventHandler<dsp::stream<dsp::stereo_t>*> outputChangeHandler, EventHandler<float> afbwChangeHandler, double audioSR) override {
            this->name = name;
            this->_config = config;
            this->_bandwidth = bandwidth;
            this->afbwChangeHandler = afbwChangeHandler;

            // Load config
            config->acquire();
            if (config->conf[name][getName()].contains("tone")) {
                tone = config->conf[name][getName()]["tone"];
            }
            config->release();

            // Define structure
            xlator.init(input, getIFSampleRate(), tone);
            c2r.init(&xlator.out);
            agc.init(&c2r.out, 20.0f, getIFSampleRate());
            m2s.init(&agc.out);
        }

        void start() override  {
            xlator.start();
            c2r.start();
            agc.start();
            m2s.start();
        }

        void stop() override {
            xlator.stop();
            c2r.stop();
            agc.stop();
            m2s.stop();
        }

        void showMenu() override {
            ImGui::LeftLabel("Tone Frequency");
            ImGui::FillWidth();
            if (ImGui::InputInt(("Stereo##_radio_cw_tone_" + name).c_str(), &tone, 10, 100)) {
                tone = std::clamp<int>(tone, 250, 1250);
                xlator.setFrequency(tone);
                afbwChangeHandler.handler(getAFBandwidth(_bandwidth), afbwChangeHandler.ctx);
                _config->acquire();
                _config->conf[name][getName()]["tone"] = tone;
                _config->release(true);
            }
        }

        void setBandwidth(double bandwidth) override { _bandwidth = bandwidth; }

        void setInput(dsp::stream<dsp::complex_t>* input) override {
            xlator.setInput(input);
        }

        void AFSampRateChanged(double newSR) override {}

        // ============= INFO =============

        const char* getName() override  { return "CW"; }
        double getIFSampleRate() override  { return 3000.0; }
        double getAFSampleRate() override  { return getIFSampleRate(); }
        double getDefaultBandwidth() override  { return 500.0; }
        double getMinBandwidth() override  { return 50.0; }
        double getMaxBandwidth() override { return 500.0; }
        bool getBandwidthLocked() override { return false; }
        double getMaxAFBandwidth() override { return getIFSampleRate() / 2.0; }
        double getDefaultSnapInterval() override { return 10.0; }
        int getVFOReference() override { return ImGui::WaterfallVFO::REF_CENTER; }
        bool getDeempAllowed() override { return false; }
        bool getPostProcEnabled() override { return true; }
        int getDefaultDeemphasisMode() override { return DEEMP_MODE_NONE; }
        double getAFBandwidth(double bandwidth) override { return (bandwidth / 2.0) + (float)tone; }
        bool getDynamicAFBandwidth() override { return true; }
        bool getFMIFNRAllowed() override { return false; }
        bool getNBAllowed() override { return false; }
        dsp::stream<dsp::stereo_t>* getOutput() override { return &m2s.out; }

        dsp::AGC &getAGC() override {
            return agc;
        }


    private:
        ConfigManager* _config = NULL;
        dsp::FrequencyXlator<dsp::complex_t> xlator;
        dsp::ComplexToReal c2r;
        dsp::AGC agc;
        dsp::MonoToStereo m2s;

        std::string name;

        int tone = 800;
        double _bandwidth;

        EventHandler<float> afbwChangeHandler;
    };
}