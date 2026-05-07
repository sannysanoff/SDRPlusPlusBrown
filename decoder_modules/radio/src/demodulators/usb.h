#pragma once
#include "../demod.h"
#include <dsp/demod/ssb.h>
#include <dsp/convert/mono_to_stereo.h>

namespace demod {
    class USB : public Demodulator {
    public:
        USB() {}

        USB(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, double audioSR) {
            init(name, config, input, bandwidth, audioSR);
        }

        ~USB() {
            stop();
        }

        void init(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, double audioSR) override {
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
            demod.init(input, dsp::demod::SSB<dsp::stereo_t>::Mode::USB, bandwidth, getIFSampleRate(), agcAttack / getIFSampleRate(), agcDecay / getIFSampleRate());
        }

        void start() override { demod.start(); }

        void stop() override { demod.stop(); }

        void showMenu() override {
            float menuWidth = ImGui::GetContentRegionAvail().x;
            ImGui::LeftLabel("AGC Attack");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::SliderFloat(("##_radio_usb_agc_attack_" + name).c_str(), &agcAttack, 1.0f, 200.0f)) {
                demod.setAGCAttack(agcAttack / getIFSampleRate());
                _config->acquire();
                _config->conf[name][getName()]["agcAttack"] = agcAttack;
                _config->release(true);
            }
            ImGui::LeftLabel("AGC Decay");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::SliderFloat(("##_radio_usb_agc_decay_" + name).c_str(), &agcDecay, 0.1f, 20.0f)) {
                demod.setAGCDecay(agcDecay / getIFSampleRate());
                _config->acquire();
                _config->conf[name][getName()]["agcDecay"] = agcDecay;
                _config->release(true);
            }
        }

        void setBandwidth(double bandwidth) override { demod.setBandwidth(bandwidth); }

        void setInput(dsp::stream<dsp::complex_t>* input) override { demod.setInput(input); }

        void AFSampRateChanged(double newSR) override  {}

        // ============= INFO =============

        const char* getName()override  { return "USB"; }
        double getIFSampleRate()override  { return 24000.0; }
        double getAFSampleRate()override  { return getIFSampleRate(); }
        double getDefaultBandwidth()override  { return 2800.0; }
        double getMinBandwidth() override { return 500.0; }
        double getMaxBandwidth() override { return getIFSampleRate() / 2.0; }
        bool getBandwidthLocked() override { return false; }
        double getDefaultSnapInterval() override { return 100.0; }
        int getVFOReference() override { return ImGui::WaterfallVFO::REF_LOWER; }
        bool getDeempAllowed() override { return false; }
        bool getPostProcEnabled() override { return true; }
        int getDefaultDeemphasisMode() override { return DEEMP_MODE_NONE; }
        bool getFMIFNRAllowed() override { return false; }
        bool getNBAllowed() override { return true; }
        bool getHighPassAllowed() override { return true; }
        bool getSquelchAllowed() override { return true; }
        dsp::stream<dsp::stereo_t>* getOutput() override { return &demod.out; }

        void setFrozen(bool frozen) override {
            demod.setAGCFrozen(frozen);
        }


    private:
        dsp::demod::SSB<dsp::stereo_t> demod;

        ConfigManager* _config;

        float agcAttack = 50.0f;
        float agcDecay = 5.0f;

        std::string name;
    };
}