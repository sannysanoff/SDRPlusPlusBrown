#pragma once
#include "../demod.h"

#include <dsp/demod/fm.h>
#include <dsp/correction/dc_blocker.h>
#include <dsp/buffer/packer.h>
#include <dsp/loop/agc.h>
#include <dsp/multirate/rational_resampler.h>
#include <dsp/demod/psk.h>
#include <dsp/buffer/reshaper.h>
#include <dsp/routing/splitter.h>
#include <dsp/stream.h>
#include <dsp/sink/handler_sink.h>
#include <gui/widgets/constellation_diagram.h>
#include "../dsp/dsd.h"
#include "../dsp/slicer.h"
#include "gui/style.h"
#include <dsp/clock_recovery/fd.h>
#include <dsp/clock_recovery/mm.h>

#define CONCAT(a, b) ((std::string(a) + b).c_str())

#define INSR (4800.0f * 2)
#define CLOCK_RECOVERY_BW 0.1f
#define CLOCK_RECOVERY_DAMPN_F 1.0f
#define CLOCK_RECOVERY_REL_LIM 0.001f
#define RRC_TAP_COUNT 65
#define RRC_ALPHA 0.23f

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
    class DSD : public Demodulator {
    public:
        DSD() {}

        DSD(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, double audioSR) {
            init(name, config, input, bandwidth, audioSR);
        }

        ~DSD() {
            stop();
            dsp::taps::free(rrcTaps);
        }

        void init(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, double audioSR) {
            this->name = name;

            // Define structure

            float recov_bandwidth = CLOCK_RECOVERY_BW;
            float recov_dampningFactor = CLOCK_RECOVERY_DAMPN_F;
            float recov_denominator = (1.0f + 2.0*recov_dampningFactor*recov_bandwidth + recov_bandwidth*recov_bandwidth);
            float recov_mu = (4.0f * recov_dampningFactor * recov_bandwidth) / recov_denominator;
            float recov_omega = (4.0f * recov_bandwidth * recov_bandwidth) / recov_denominator;
            quadDemod.init(input, 1944.0f, INSR);
            dcBlock.init(&quadDemod.out, 1.0f / INSR);
            rrcTaps = dsp::taps::rootRaisedCosine<float>(RRC_TAP_COUNT, RRC_ALPHA, 4800.0f, INSR);
            rrcFilt.init(&dcBlock.out, rrcTaps);
            clockRecov.init(&rrcFilt.out, INSR / 4800.0f, recov_omega, recov_mu, CLOCK_RECOVERY_REL_LIM, 32, 8);
            constDiagSplitter.init(&clockRecov.out);
            constDiagSplitter.bindStream(&constDiagStream);
            constDiagSplitter.bindStream(&demodStream);

            constDiagReshaper.init(&constDiagStream, 1024, 0);
            constDiagSink.init(&constDiagReshaper.out, _constDiagSinkHandler, this);

            slicer.init(&demodStream);

            decoder.init(&slicer.out);
            outputConv.init(&decoder.out);
            outputMts.init(&outputConv.out);
        }

        void start() {
            constDiagSplitter.start();
            quadDemod.start();
            dcBlock.start();
            rrcFilt.start();
            clockRecov.start();
            constDiagReshaper.start();
            constDiagSink.start();
            slicer.start();
            decoder.start();
            outputConv.start();
            outputMts.start();
        }

        void stop() {
            constDiagSplitter.stop();
            quadDemod.stop();
            dcBlock.stop();
            rrcFilt.stop();
            clockRecov.stop();
            constDiagReshaper.stop();
            constDiagSink.stop();
            slicer.stop();
            decoder.stop();
            outputConv.stop();
            outputMts.stop();
        }

        void showMenu() {
            float menuWidth = ImGui::GetContentRegionAvail().x;
            ImGui::Text("Signal constellation+slicer levels: ");
            ImGui::SetNextItemWidth(menuWidth);
            constDiag.draw(ImVec2(0, 20));

            dsp::NewDSD::Frame_status fr_st = decoder.getFrameSyncStatus();
            ImVec4 color = fr_st.sync ? (ImVec4(0.4f, 1.0f, 0.4f, 1.0f)) : (ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
            switch(fr_st.lasttype) {
                case dsp::NewDSD::Frame_status::LAST_P25: {
                    ImGui::TextColored(color, "Mode: P25p1");
                    if(!fr_st.sync) {
                        style::beginDisabled();
                    }
                    dsp::NewDSD::P25_status p25_st = decoder.getP25Status();
                    ImGui::Text("NAC     : 0x%03x", p25_st.p25_status_nac);
                    ImGui::Text("DUID    : %u%u %s", p25_st.p25_status_lastduid[0], p25_st.p25_status_lastduid[1], p25_st.p25_status_lasttype.c_str());
                    if(p25_st.p25_status_irr_err) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), " IRRECOV ERROR!");
                    }
                    ImGui::Text("SRC     : %u", p25_st.p25_status_src);
                    ImGui::Text("TG       : (id %u) %u (others %u %u %u)", p25_st.p25_status_tgid, p25_st.p25_status_tg, p25_st.p25_status_othertg1, p25_st.p25_status_othertg2, p25_st.p25_status_othertg3);
                    ImGui::Text("ALGID : 0x%02x", p25_st.p25_status_algid);
                    if(p25_st.p25_status_algid != 0x80) {
                        //Encrypted
                        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "ENCR? KID: 0x%04x", p25_st.p25_status_kid);
                    } else {
                        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "UNENCR");
                    }
                    ImGui::Text("MFID    : 0x%02x", p25_st.p25_status_mfid);
                    ImGui::TextColored((p25_st.p25_status_emr ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f) : ImVec4(1.0f, 0.4f, 0.4f, 1.0f)), "EMR");
                    ImGui::Text("LCFORMAT: 0x%02x", p25_st.p25_status_lcformat);
                    ImGui::Text("LCINFO: 0x%016llx", p25_st.p25_status_lcinfo);
                    ImGui::Text("MI(INV): 0x%016llx %04x", p25_st.p25_status_mi_0, p25_st.p25_status_mi_1);
                    if(!fr_st.sync) {
                        style::endDisabled();
                    }
                    break;
                }
                case dsp::NewDSD::Frame_status::LAST_DMR: {
                    ImGui::TextColored(color, "Mode: DMR");
                    if(!fr_st.sync) {
                        style::beginDisabled();
                    }
                    dsp::NewDSD::DMR_status dmr_st = decoder.getDMRStatus();
                    ImGui::Text("SLOT0: (%02d) %s", dmr_st.dmr_status_s0_lastburstt, dmr_st.dmr_status_s0_lasttype.c_str());
                    ImGui::Text("SLOT1: (%02d) %s", dmr_st.dmr_status_s1_lastburstt, dmr_st.dmr_status_s1_lasttype.c_str());
                    ImGui::Text("CC: 0x%02x %02x", dmr_st.dmr_status_s1_lastburstt, dmr_st.dmr_status_cc);
                    if(!fr_st.sync) {
                        style::endDisabled();
                    }
                    break;
                }
                default:
                    ImGui::TextColored(color, "Mode: -");
                    break;
            }
            if(!fr_st.sync) {
                style::beginDisabled();
            }
            dsp::NewDSD::MBE_status mbe_st = decoder.getMBEStatus();
            ImGui::TextColored((mbe_st.mbe_status_decoding ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f) : ImVec4(1.0f, 0.4f, 0.4f, 1.0f)), "MBE ERR: %s", mbe_st.mbe_status_errorbar.c_str());
            if(!fr_st.sync) {
                style::endDisabled();
            }
        }

        void setBandwidth(double bandwidth) {}

        void setInput(dsp::stream<dsp::complex_t>* input) {
            quadDemod.setInput(input);
        }

        void AFSampRateChanged(double newSR) {}

        // ============= INFO =============

        const char* getName() { return "DSD"; }
        double getIFSampleRate() { return INSR; }
        double getAFSampleRate() { return 8000.0; }
        double getDefaultBandwidth() { return bw; }
        double getMinBandwidth() { return 3000.0; }
        double getMaxBandwidth() { return 12500.0; }
        bool getBandwidthLocked() { return true; }
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

        dsp::demod::Quadrature quadDemod;
        dsp::correction::DCBlocker<float> dcBlock;
        dsp::filter::FIR<float, float> rrcFilt;
        dsp::tap<float> rrcTaps;
        dsp::clock_recovery::FD clockRecov;
        dsp::routing::Splitter<float> constDiagSplitter;
        dsp::stream<float> constDiagStream;
        dsp::buffer::Reshaper<float> constDiagReshaper;
        dsp::sink::Handler<float> constDiagSink;
        ImGui::ConstellationDiagram constDiag;

        dsp::stream<float> demodStream;
        dsp::FourFSKExtractor slicer;
        dsp::NewDSD decoder;
        dsp::Int16ToFloat outputConv;
        dsp::convert::MonoToStereo outputMts;

        std::string name;

        static void _constDiagSinkHandler(float* data, int count, void* ctx) {
            DSD* _this = (DSD*)ctx;
            dsp::complex_t* cdBuff = _this->constDiag.acquireBuffer();
            if(count == 1024) {
                for(int i = 0; i < 1021; i++) {
                    cdBuff[i].re = data[i];
                    cdBuff[i].im = 0;
                }
//                cdBuff[1019].re = _this->slicer.max;
//                cdBuff[1019].im = 0.5f;
//                cdBuff[1020].re = _this->slicer.min;
//                cdBuff[1020].im = 0.5f;
                //Display slicer ranges too
                cdBuff[1021].re = _this->slicer.center;
                cdBuff[1021].im = 1.0f;
                cdBuff[1022].re = _this->slicer.umid;
                cdBuff[1022].im = 1.0f;
                cdBuff[1023].re = _this->slicer.lmid;
                cdBuff[1023].im = 1.0f;
            }
            _this->constDiag.releaseBuffer();
        }

    };
}
