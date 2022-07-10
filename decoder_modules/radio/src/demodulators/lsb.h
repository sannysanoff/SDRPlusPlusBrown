#pragma once
#include "../demod.h"
#include <dsp/demod/ssb.h>

namespace demod {
    class LSB : public Demodulator, public HasAGC {
    public:
        LSB() {}

        LSB(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, double audioSR) {
            init(name, config, input, bandwidth, audioSR);
        }

        ~LSB() {
            stop();
        }

<<<<<<< HEAD
        void init(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, EventHandler<dsp::stream<dsp::stereo_t>*> outputChangeHandler, EventHandler<float> afbwChangeHandler, double audioSR) override {
=======
        void init(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, double audioSR) {
>>>>>>> master
            this->name = name;
            _config = config;

            // Load config
            config->acquire();
            if (config->conf[name][getName()].contains("agcAttack")) {
                agcAttack = config->conf[name][getName()]["agcAttack"];
            }
            if (config->conf[name][getName()].contains("agcDecay")) {
                agcDecay = config->conf[name][getName()]["agcDecay"];
            }
            config->release();

            // Define structure
            demod.init(input, dsp::demod::SSB<dsp::stereo_t>::Mode::LSB, bandwidth, getIFSampleRate(), agcAttack / getIFSampleRate(), agcDecay / getIFSampleRate());
        }

<<<<<<< HEAD
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
=======
        void start() { demod.start(); }

        void stop() { demod.stop(); }

        void showMenu() {
            float menuWidth = ImGui::GetContentRegionAvail().x;
            ImGui::LeftLabel("AGC Attack");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::SliderFloat(("##_radio_lsb_agc_attack_" + name).c_str(), &agcAttack, 1.0f, 200.0f)) {
                demod.setAGCAttack(agcAttack / getIFSampleRate());
                _config->acquire();
                _config->conf[name][getName()]["agcAttack"] = agcAttack;
                _config->release(true);
            }
            ImGui::LeftLabel("AGC Decay");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::SliderFloat(("##_radio_lsb_agc_decay_" + name).c_str(), &agcDecay, 1.0f, 20.0f)) {
                demod.setAGCDecay(agcDecay / getIFSampleRate());
                _config->acquire();
                _config->conf[name][getName()]["agcDecay"] = agcDecay;
                _config->release(true);
            }
        }

        void setBandwidth(double bandwidth) { demod.setBandwidth(bandwidth); }

        void setInput(dsp::stream<dsp::complex_t>* input) { demod.setInput(input); }
>>>>>>> master

        void AFSampRateChanged(double newSR) override {}

        // ============= INFO =============

<<<<<<< HEAD
        const char* getName() override { return "LSB"; }
        double getIFSampleRate() override { return 24000.0; }
        double getAFSampleRate() override { return getIFSampleRate(); }
        double getDefaultBandwidth() override { return 2800.0; }
        double getMinBandwidth() override { return 500.0; }
        double getMaxBandwidth() override { return getIFSampleRate() / 2.0; }
        bool getBandwidthLocked() override { return false; }
        double getMaxAFBandwidth() override { return getIFSampleRate() / 2.0; }
        double getDefaultSnapInterval() override { return 100.0; }
        int getVFOReference() override  { return ImGui::WaterfallVFO::REF_UPPER; }
        bool getDeempAllowed() override  { return false; }
        bool getPostProcEnabled() override  { return true; }
        int getDefaultDeemphasisMode() override  { return DEEMP_MODE_NONE; }
        double getAFBandwidth(double bandwidth) override  { return bandwidth; }
        bool getDynamicAFBandwidth() override { return true; }
        bool getFMIFNRAllowed() override  { return false; }
        bool getNBAllowed() override  { return true; }
        dsp::stream<dsp::stereo_t>* getOutput() override { return &m2s.out; }

        dsp::AGC &getAGC() override{
            return agc;
        }
=======
        const char* getName() { return "LSB"; }
        double getIFSampleRate() { return 24000.0; }
        double getAFSampleRate() { return getIFSampleRate(); }
        double getDefaultBandwidth() { return 2800.0; }
        double getMinBandwidth() { return 500.0; }
        double getMaxBandwidth() { return getIFSampleRate() / 2.0; }
        bool getBandwidthLocked() { return false; }
        double getDefaultSnapInterval() { return 100.0; }
        int getVFOReference() { return ImGui::WaterfallVFO::REF_UPPER; }
        bool getDeempAllowed() { return false; }
        bool getPostProcEnabled() { return true; }
        int getDefaultDeemphasisMode() { return DEEMP_MODE_NONE; }
        bool getFMIFNRAllowed() { return false; }
        bool getNBAllowed() { return true; }
        dsp::stream<dsp::stereo_t>* getOutput() { return &demod.out; }
>>>>>>> master

    private:
        dsp::demod::SSB<dsp::stereo_t> demod;

        ConfigManager* _config;

        float agcAttack = 50.0f;
        float agcDecay = 5.0f;

        std::string name;
    };
}