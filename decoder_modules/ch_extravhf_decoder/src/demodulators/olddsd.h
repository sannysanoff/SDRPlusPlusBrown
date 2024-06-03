#pragma once
#include "../demod.h"
#include <dsp/demod/fm.h>
#include <dsp/correction/dc_blocker.h>
#include <dsp/loop/agc.h>
#include <dsp/multirate/rational_resampler.h>
#include <dsp/demod/psk.h>
#include <dsp/buffer/reshaper.h>
#include <dsp/routing/splitter.h>
#include <dsp/stream.h>
#include <dsp/sink/handler_sink.h>
#include <gui/widgets/constellation_diagram.h>
#include "../dsp/dsd.h"
#include <dsp/clock_recovery/fd.h>
#include <dsp/clock_recovery/mm.h>

#define CONCAT(a, b) ((std::string(a) + b).c_str())

namespace demod {
    //P25p1 = 13 kHz
    //DStar = 7 kHz
    //NXDN48 = 7 kHz
    //NXDN96 = 13 kHz
    //ProVoice = ???(assumed 13 kHz)
    //DMR = 13 kHz
    //X2-TDMA = 13 kHz
    //DPMR = 7 kHz
    //YSF = 17 kHz
    class OldDSD : public Demodulator {
    public:
        OldDSD() {}

        OldDSD(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, double audioSR) {
            init(name, config, input, bandwidth, audioSR);
        }

        ~OldDSD() {
            stop();
        }

        void init(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, double audioSR) {
            this->name = name;

            // Define structure

            fmdemod.init(input, getIFSampleRate(), bandwidth, true, false);
            inputDcBlock.init(&fmdemod.out, 100.0f / getIFSampleRate());
            inputConv.init(&inputDcBlock.out);
            inputPacker.init(&inputConv.out, 120);
            dsdDec.init(&inputPacker.out);
            outputConv.init(&dsdDec.out);
            outputMts.init(&outputConv.out);
        }

        void start() {
            fmdemod.start();
            inputDcBlock.start();
            inputConv.start();
            inputPacker.start();
            dsdDec.start();
            outputConv.start();
            outputMts.start();
        }

        void stop() {
            fmdemod.stop();
            inputDcBlock.stop();
            inputConv.stop();
            inputPacker.stop();
            dsdDec.stop();
            outputConv.stop();
            outputMts.stop();
        }

        void showMenu() {
            ImVec4 color;
            if(dsdDec.status_sync) {
                color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
            } else {
                color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
            }
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "Inlvl: %d %%", dsdDec.status_lvl);
            ImGui::TextColored(color, "Mode: %s", dsdDec.status_last_proto.c_str());
            ImVec4 p25_color;
            ImVec4 dmr_color;
            ImVec4 nxdn_color;
            ImVec4 bar_color;
            if(dsdDec.status_sync && (dsdDec.status_last_proto == "+P25p1" || dsdDec.status_last_proto == "-P25p1")) {
                p25_color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
            } else {
                p25_color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
            }
            if(dsdDec.status_sync && (dsdDec.status_last_proto == "+DMR" || dsdDec.status_last_proto == "-DMR")) {
                dmr_color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
            } else {
                dmr_color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
            }
            if(dsdDec.status_sync && (dsdDec.status_last_proto == "+NXDN48" || dsdDec.status_last_proto == "-NXDN48" || dsdDec.status_last_proto == "+NXDN96" || dsdDec.status_last_proto == "-NXDN96")) {
                nxdn_color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
            } else {
                nxdn_color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
            }
            if(dsdDec.status_sync && dsdDec.status_mbedecoding) {
                bar_color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
            } else {
                bar_color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
            }
            ImGui::TextColored(p25_color, "Last P25 nac: %d src: %d tg: %d DUID: %s", dsdDec.status_last_nac, dsdDec.status_last_src, dsdDec.status_last_tg, dsdDec.status_last_p25_duid.c_str());
            ImGui::TextColored(dmr_color, "Last DMR S0: %s S1: %s", dsdDec.status_last_dmr_slot0_burst.c_str(), dsdDec.status_last_dmr_slot1_burst.c_str());
            ImGui::TextColored(nxdn_color, "Last NXDN type: %s", dsdDec.status_last_nxdn_type.c_str());
            ImGui::TextColored(bar_color, "Voice err: %s", dsdDec.status_errorbar.c_str());
        }

        void setBandwidth(double bandwidth) {
            fmdemod.setBandwidth(bandwidth);
        }

        void setInput(dsp::stream<dsp::complex_t>* input) {
            fmdemod.setInput(input);
        }

        void AFSampRateChanged(double newSR) {}

        // ============= INFO =============

        const char* getName() { return "OldDSD"; }
        double getIFSampleRate() { return 48000.0; }
        double getAFSampleRate() { return 8000.0; }
        double getDefaultBandwidth() { return bw; }
        double getMinBandwidth() { return 3000.0; }
        double getMaxBandwidth() { return 12500.0; }
        bool getBandwidthLocked() { return false; }
        double getMaxAFBandwidth() { return 4000.0; }
        double getDefaultSnapInterval() { return 500.0; }
        int getVFOReference() { return ImGui::WaterfallVFO::REF_CENTER; }
        bool getDeempAllowed() { return false; }
        bool getPostProcEnabled() { return true; }
        int getDefaultDeemphasisMode() { return DEEMP_MODE_NONE; }
        double getAFBandwidth(double bandwidth) { return 4000.0; }
        bool getDynamicAFBandwidth() { return false; }
        bool getFMIFNRAllowed() { return false; }
        bool getNBAllowed() { return false; }
        dsp::stream<dsp::stereo_t>* getOutput() { return &outputMts.out; }

    private:

        float bw = 12500.0;

        dsp::stream<dsp::complex_t> nullStream;
        dsp::demod::FM<float> fmdemod;
        dsp::correction::DCBlocker<float> inputDcBlock;
        dsp::FloatToInt16 inputConv;
        dsp::buffer::Packer<int16_t> inputPacker;
        dsp::DSD dsdDec;
        dsp::Int16ToFloat outputConv;
        dsp::convert::MonoToStereo outputMts;

        std::string name;

    };
}
