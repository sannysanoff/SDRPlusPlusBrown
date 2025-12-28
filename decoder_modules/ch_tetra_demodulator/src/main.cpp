#define GImGui (ImGui::GetCurrentContext())

#include <imgui.h>
#include <config.h>
#include <core.h>
#include <gui/style.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <module.h>
// #include <unistd.h>
#include <fstream>

#include <dsp/demod/psk.h>
#include <dsp/buffer/packer.h>
#include <dsp/routing/splitter.h>
#include <dsp/stream.h>
#include <dsp/convert/mono_to_stereo.h>

#include <gui/widgets/constellation_diagram.h>
#include <gui/widgets/file_select.h>
#include <gui/widgets/volume_meter.h>

#include <utils/flog.h>
#include <utils/net.h>

#include "dsp/bit_unpacker.h"
#include "dsp/dqpsk_sym_extr.h"
#include "dsp/pi4dqpsk.h"
#include "dsp/osmotetra_dec.h"
#include "gui_widgets.h"


#define CONCAT(a, b)    ((std::string(a) + b).c_str())

#define VFO_SAMPLERATE 36000
#define VFO_BANDWIDTH 30000
#define CLOCK_RECOVERY_BW 0.00628f
#define CLOCK_RECOVERY_DAMPN_F 0.707f
#define CLOCK_RECOVERY_REL_LIM 0.02f
#define RRC_TAP_COUNT 65
#define RRC_ALPHA 0.35f
#define AGC_RATE 0.02f
#define COSTAS_LOOP_BANDWIDTH 0.01f
#define FLL_LOOP_BANDWIDTH 0.006f

SDRPP_MOD_INFO {
    /* Name:            */ "ch_tetra_demodulator",
    /* Description:     */ "Tetra demodulator for SDR++(output can be fed to tetra-rx from osmo-tetra)",
    /* Author:          */ "cropinghigh",
    /* Version:         */ 0, 2, 0,
    /* Max instances    */ -1
};

ConfigManager config;

class TetraDemodulatorModule : public ModuleManager::Instance {
public:
    TetraDemodulatorModule(std::string name) {
        this->name = name;

        // Load config
        config.acquire();
        if (!config.conf.contains(name) || !config.conf[name].contains("mode")) {
            config.conf[name]["mode"] = decoder_mode;
            config.conf[name]["hostname"] = "localhost";
            config.conf[name]["port"] = 8355;
            config.conf[name]["sending"] = false;
        }
        decoder_mode = config.conf[name]["mode"];
        strcpy(hostname, std::string(config.conf[name]["hostname"]).c_str());
        port = config.conf[name]["port"];
        bool startNow = config.conf[name]["sending"];
        config.release(true);

        vfo = sigpath::vfoManager.createVFO(name, ImGui::WaterfallVFO::REF_CENTER, 0, VFO_BANDWIDTH, VFO_SAMPLERATE, VFO_BANDWIDTH, VFO_BANDWIDTH, true);

        //Clock recov coeffs
        float recov_bandwidth = CLOCK_RECOVERY_BW;
        float recov_dampningFactor = CLOCK_RECOVERY_DAMPN_F;
        float recov_denominator = (1.0f + 2.0*recov_dampningFactor*recov_bandwidth + recov_bandwidth*recov_bandwidth);
        float recov_mu = (4.0f * recov_dampningFactor * recov_bandwidth) / recov_denominator;
        float recov_omega = (4.0f * recov_bandwidth * recov_bandwidth) / recov_denominator;

        mainDemodulator.init(vfo->output, 18000, VFO_SAMPLERATE, RRC_TAP_COUNT, RRC_ALPHA, AGC_RATE, COSTAS_LOOP_BANDWIDTH, FLL_LOOP_BANDWIDTH, recov_omega, recov_mu, CLOCK_RECOVERY_REL_LIM);
        constDiagSplitter.init(&mainDemodulator.out);
        constDiagSplitter.bindStream(&constDiagStream);
        constDiagSplitter.bindStream(&demodStream);
        constDiagReshaper.init(&constDiagStream, 1024, 0);
        constDiagSink.init(&constDiagReshaper.out, _constDiagSinkHandler, this);
        symbolExtractor.init(&demodStream);
        bitsUnpacker.init(&symbolExtractor.out);

        demodSink.init(&bitsUnpacker.out, _demodSinkHandler, this);

        osmotetradecoder.init(&bitsUnpacker.out);
        resamp.init(&osmotetradecoder.out, 8000.0, audioSampleRate);
        outconv.init(&resamp.out);

        // Initialize the sink
        srChangeHandler.ctx = this;
        srChangeHandler.handler = sampleRateChangeHandler;
        stream.init(&srChangeHandler, audioSampleRate);
        stream.setInput(&outconv.out);
        sigpath::sinkManager.registerStream(name, &stream);

        mainDemodulator.start();
        constDiagSplitter.start();
        constDiagReshaper.start();
        constDiagSink.start();
        symbolExtractor.start();
        bitsUnpacker.start();
        setMode();
        resamp.start();
        outconv.start();
        stream.start();
        gui::menu.registerEntry(name, menuHandler, this, this);

        if(startNow) {
            startNetwork();
        }
    }

    ~TetraDemodulatorModule() {
        if(isEnabled()) {
            disable();
        }
        gui::menu.removeEntry(name);
        sigpath::sinkManager.unregisterStream(name);
    }

    void postInit() {}

    void enable() {
        vfo = sigpath::vfoManager.createVFO(name, ImGui::WaterfallVFO::REF_CENTER, 0, 29000, 36000, 29000, 29000, true);
        mainDemodulator.setInput(vfo->output);
        mainDemodulator.start();
        constDiagSplitter.start();
        constDiagReshaper.start();
        constDiagSink.start();
        symbolExtractor.start();
        bitsUnpacker.start();
        setMode();
        resamp.start();
        outconv.start();
        stream.start();

        enabled = true;
    }

    void disable() {
        mainDemodulator.stop();
        constDiagSplitter.stop();
        constDiagReshaper.stop();
        constDiagSink.stop();
        symbolExtractor.stop();
        bitsUnpacker.stop();
        osmotetradecoder.stop();
        demodSink.stop();
        resamp.stop();
        outconv.stop();
        stream.stop();
        sigpath::vfoManager.deleteVFO(vfo);
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }

private:

    void startNetwork() {
        stopNetwork();
        try {
            conn = net::openudp(hostname, port);
        } catch (std::runtime_error& e) {
            flog::error("Network error: %s\n", e.what());
        }
    }

    void stopNetwork() {
        if (conn) { conn->close(); }
    }

    void setMode() {
        if(decoder_mode == 0) {
            //osmo-tetra
            demodSink.stop();
            osmotetradecoder.start();
        } else {
            //network syms
            osmotetradecoder.stop();
            demodSink.start();
        }
        config.acquire();
        config.conf[name]["mode"] = decoder_mode;
        config.release(true);
    }

    static void menuHandler(void* ctx) {
        TetraDemodulatorModule* _this = (TetraDemodulatorModule*)ctx;
        float menuWidth = ImGui::GetContentRegionAvail().x;

        if(!_this->enabled) {
            style::beginDisabled();
        }

        ImGui::Text("Signal constellation: ");
        ImGui::SetNextItemWidth(menuWidth);
        _this->constDiag.draw();

        float avg = 1.0f - _this->symbolExtractor.standarderr;
        ImGui::Text("Signal quality: ");
        ImGui::SameLine();
        ImGui::SigQualityMeter(avg, 0.5f, 1.0f);
        ImGui::BoxIndicator(ImGui::GetFontSize()*2, _this->symbolExtractor.sync ? IM_COL32(5, 230, 5, 255) : IM_COL32(230, 5, 5, 255));
        ImGui::SameLine();
        ImGui::Text(" Sync");

        ImGui::BeginGroup();
        ImGui::Columns(2, CONCAT("TetraModeColumns##_", _this->name), false);
        if (ImGui::RadioButton(CONCAT("OSMO-TETRA##_", _this->name), _this->decoder_mode == 0) && _this->decoder_mode != 0) {
            _this->decoder_mode = 0; //osmo-tetra
            _this->setMode();
        }
        ImGui::NextColumn();
        if (ImGui::RadioButton(CONCAT("NETSYMS##_", _this->name), _this->decoder_mode == 1) && _this->decoder_mode != 1) {
            _this->decoder_mode = 1; //network symbol streaming
            _this->setMode();
        }
        ImGui::Columns(1, CONCAT("EndTetraModeColumns##_", _this->name), false);
        ImGui::EndGroup();

        if(_this->decoder_mode == 0) {
            //OSMO-TETRA
            int dec_st = _this->osmotetradecoder.getRxState();
            ImGui::BoxIndicator(ImGui::GetFontSize()*2, (dec_st == 0) ? IM_COL32(230, 5, 5, 255) : ((dec_st == 2) ? IM_COL32(5, 230, 5, 255) : IM_COL32(230, 230, 5, 255)));
            ImGui::SameLine();
            ImGui::Text(" Decoder:  %s", (dec_st == 0) ? "Unlocked" : ((dec_st == 2) ? "Locked" : "Know next start"));
            if(dec_st != 2) {
                style::beginDisabled();
            }
            ImGui::Text("Hyperframe: "); ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95, 0.95, 0.05, 1.0), "%05d", _this->osmotetradecoder.getCurrHyperframe()); ImGui::SameLine();
            ImGui::Text(" | Multiframe: "); ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95, 0.95, 0.05, 1.0), "%02d", _this->osmotetradecoder.getCurrMultiframe()); ImGui::SameLine();
            ImGui::Text("| Frame: "); ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95, 0.95, 0.05, 1.0), "%02d", _this->osmotetradecoder.getCurrFrame());
            ImGui::Text("Timeslots: ");
            for(int i = 0; i < 4; i++) {
                switch(_this->osmotetradecoder.getTimeslotContent(i)) {
                    case 0:
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.8, 0.8, 0.8, 1.0), "   UL  ");
                        break;
                    case 1:
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.95, 0.05, 0.95, 1.0), " DATA  ");
                        break;
                    case 2:
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.95, 0.95, 0.05, 1.0), "  NDB  ");
                        break;
                    case 3:
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.05, 0.95, 0.95, 1.0), " SYNC  ");
                        break;
                    case 4:
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.05, 0.95, 0.05, 1.0), " VOICE ");
                        break;
                }
            }
            int crc_failed = _this->osmotetradecoder.getLastCrcFail();
            if(crc_failed) {
                ImGui::BoxIndicator(ImGui::GetFontSize()*2, IM_COL32(230, 5, 5, 255));
                ImGui::SameLine();
                ImGui::Text(" CRC: "); ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.95, 0.05, 0.05, 1.0), "FAIL");
                style::beginDisabled();
            } else {
                ImGui::BoxIndicator(ImGui::GetFontSize()*2, IM_COL32(5, 230, 5, 255));
                ImGui::SameLine();
                ImGui::Text(" CRC: "); ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.05, 0.95, 0.05, 1.0), "PASS");
            }
            int dl_usg = _this->osmotetradecoder.getDlUsage();
            int ul_usg = _this->osmotetradecoder.getUlUsage();
            ImGui::Text("DL:");ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95, 0.95, 0.05, 1.0), "%7.3f", ((float)_this->osmotetradecoder.getDlFreq()/1000000.0f));ImGui::SameLine();
            ImGui::Text(" MHz ");ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95, 0.95, 0.05, 1.0), (dl_usg == 0 ? "Unalloc" : (dl_usg == 1 ? "Assigned ctl" : (dl_usg == 2 ? "Common ctl" : (dl_usg == 3 ? "Reserved" : "Traffic")))));
            ImGui::Text("UL:");ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95, 0.95, 0.05, 1.0), "%7.3f", ((float)_this->osmotetradecoder.getUlFreq()/1000000.0f));ImGui::SameLine();
            ImGui::Text(" MHz ");ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95, 0.95, 0.05, 1.0), (ul_usg == 0 ? "Unalloc" : "Traffic"));
            ImGui::Text("Access1: ");ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95, 0.95, 0.05, 1.0), "%c", _this->osmotetradecoder.getAccess1Code());ImGui::SameLine();
            ImGui::Text("/");ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95, 0.95, 0.05, 1.0), "%d", _this->osmotetradecoder.getAccess1());ImGui::SameLine();
            ImGui::Text("| Access2: ");ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95, 0.95, 0.05, 1.0), "%c", _this->osmotetradecoder.getAccess2Code());ImGui::SameLine();
            ImGui::Text("/");ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95, 0.95, 0.05, 1.0), "%d", _this->osmotetradecoder.getAccess2());
            ImGui::Text("MCC: ");ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95, 0.95, 0.05, 1.0), "%03d", _this->osmotetradecoder.getMcc());ImGui::SameLine();
            ImGui::Text("| MNC: ");ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95, 0.95, 0.05, 1.0), "%03d", _this->osmotetradecoder.getMnc());ImGui::SameLine();
            ImGui::Text("| CC: ");ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95, 0.95, 0.05, 1.0), "0x%02x", _this->osmotetradecoder.getCc());
            ImVec4 on_color = ImVec4(0.05, 0.95, 0.05, 1.0);
            ImVec4 off_color = ImVec4(0.95, 0.05, 0.05, 1.0);
            ImGui::TextColored(_this->osmotetradecoder.getAdvancedLink() ? on_color : off_color, "Adv. link  ");ImGui::SameLine();
            ImGui::TextColored(_this->osmotetradecoder.getAirEncryption() ? on_color : off_color, "Encryption  ");ImGui::SameLine();
            ImGui::TextColored(_this->osmotetradecoder.getSndcpData() ? on_color : off_color, "SNDCP");
            ImGui::TextColored(_this->osmotetradecoder.getCircuitData() ? on_color : off_color, "Circuit data  ");ImGui::SameLine();
            ImGui::TextColored(_this->osmotetradecoder.getVoiceService() ? on_color : off_color, "Voice  ");ImGui::SameLine();
            ImGui::TextColored(_this->osmotetradecoder.getNormalMode() ? on_color : off_color, "Normal mode");
            ImGui::TextColored(_this->osmotetradecoder.getMigrationSupported() ? on_color : off_color, "Migration  ");ImGui::SameLine();
            ImGui::TextColored(_this->osmotetradecoder.getNeverMinimumMode() ? on_color : off_color, "Never min mode  ");ImGui::SameLine();
            ImGui::TextColored(_this->osmotetradecoder.getPriorityCell() ? on_color : off_color, "Priority cell");
            ImGui::TextColored(_this->osmotetradecoder.getDeregMandatory() ? on_color : off_color, "Dereg req.  ");ImGui::SameLine();
            ImGui::TextColored(_this->osmotetradecoder.getRegMandatory() ? on_color : off_color, "Reg req.");
            if(crc_failed) {
                style::endDisabled();
            }
            if(dec_st != 2) {
                style::endDisabled();
            }
        } else {
            //NETWORK SYM STREAMING
            ImGui::BoxIndicator(menuWidth, _this->tsfound ? IM_COL32(5, 230, 5, 255) : IM_COL32(230, 5, 5, 255));
            ImGui::SameLine();
            ImGui::Text(" Training sequences");

            bool netActive = (_this->conn && _this->conn->isOpen());
            if(netActive) { style::beginDisabled(); }
            if (ImGui::InputText(CONCAT("UDP ##_tetrademod_host_", _this->name), _this->hostname, 1023)) {
                config.acquire();
                config.conf[_this->name]["hostname"] = _this->hostname;
                config.release(true);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::InputInt(CONCAT("##_tetrademod_port_", _this->name), &(_this->port), 0, 0)) {
                config.acquire();
                config.conf[_this->name]["port"] = _this->port;
                config.release(true);
            }
            if(netActive) { style::endDisabled(); }

            if (netActive && ImGui::Button(CONCAT("Net stop##_tetrademod_net_stop_", _this->name), ImVec2(menuWidth, 0))) {
                _this->stopNetwork();
                config.acquire();
                config.conf[_this->name]["sending"] = false;
                config.release(true);
            } else if (!netActive && ImGui::Button(CONCAT("Net start##_tetrademod_net_stop_", _this->name), ImVec2(menuWidth, 0))) {
                _this->startNetwork();
                config.acquire();
                config.conf[_this->name]["sending"] = true;
                config.release(true);
            }

            ImGui::TextUnformatted("Net status:");
            ImGui::SameLine();
            if (netActive) {
                ImGui::TextColored(ImVec4(0.0, 1.0, 0.0, 1.0), "Sending");
            } else {
                ImGui::TextUnformatted("Idle");
            }
        }
        if(!_this->enabled) {
            style::endDisabled();
        }
    }

    static void _constDiagSinkHandler(dsp::complex_t* data, int count, void* ctx) {
        TetraDemodulatorModule* _this = (TetraDemodulatorModule*)ctx;
        dsp::complex_t* cdBuff = _this->constDiag.acquireBuffer();
        if(count == 1024) {
            memcpy(cdBuff, data, count * sizeof(dsp::complex_t));
        }
        _this->constDiag.releaseBuffer();
    }

    static void _demodSinkHandler(uint8_t* data, int count, void* ctx) {
        TetraDemodulatorModule* _this = (TetraDemodulatorModule*)ctx;
        if(_this->conn && _this->conn->isOpen()) {
            _this->conn->send(data, count);
        }
        for(int j = 0; j < count; j++) {
            for(int i = 0; i < 44; i++) {
                _this->tsfind_buffer[i] = _this->tsfind_buffer[i+1];
            }
            _this->tsfind_buffer[44] = data[j];
            if(!memcmp(_this->tsfind_buffer, training_seq_n, sizeof(training_seq_n)) ||
                !memcmp(_this->tsfind_buffer, training_seq_p, sizeof(training_seq_p)) ||
                !memcmp(_this->tsfind_buffer, training_seq_q, sizeof(training_seq_q)) ||
                !memcmp(_this->tsfind_buffer, training_seq_N, sizeof(training_seq_N)) ||
                !memcmp(_this->tsfind_buffer, training_seq_P, sizeof(training_seq_P)) ||
                !memcmp(_this->tsfind_buffer, training_seq_x, sizeof(training_seq_x)) ||
                !memcmp(_this->tsfind_buffer, training_seq_X, sizeof(training_seq_X)) ||
                !memcmp(_this->tsfind_buffer, training_seq_y, sizeof(training_seq_y))
            ) {
                _this->tsfound = true;
                _this->symsbeforeexpire = 2048;
            }
            if(_this->symsbeforeexpire > 0) {
                _this->symsbeforeexpire--;
                if(_this->symsbeforeexpire == 0) {
                    _this->tsfound = false;
                }
            }
        }
    }

    static void sampleRateChangeHandler(float sampleRate, void* ctx) {
        TetraDemodulatorModule* _this = (TetraDemodulatorModule*)ctx;
        _this->audioSampleRate = sampleRate;
        _this->resamp.stop();
        _this->resamp.setOutSamplerate(_this->audioSampleRate);
        _this->resamp.start();
    }

    std::string name;
    bool enabled = true;

    VFOManager::VFO* vfo;

    dsp::demod::PI4DQPSK mainDemodulator;
    dsp::routing::Splitter<dsp::complex_t> constDiagSplitter;
    dsp::stream<dsp::complex_t> constDiagStream;
    dsp::buffer::Reshaper<dsp::complex_t> constDiagReshaper;
    dsp::sink::Handler<dsp::complex_t> constDiagSink;
    ImGui::ConstellationDiagram constDiag;

    dsp::stream<dsp::complex_t> demodStream;

    dsp::DQPSKSymbolExtractor symbolExtractor;
    dsp::BitUnpacker bitsUnpacker;

    dsp::sink::Handler<uint8_t> demodSink;

    dsp::osmotetradec osmotetradecoder;

    EventHandler<float> srChangeHandler;
    dsp::multirate::RationalResampler<float> resamp;
    dsp::convert::MonoToStereo outconv;
    SinkManager::Stream stream;
    double audioSampleRate = 48000.0;


    int decoder_mode = 0;


    //Sequences from osmo-tetra-sq5bpf source
    /* 9.4.4.3.2 Normal Training Sequence */
    static const constexpr uint8_t training_seq_n[22] = { 1,1, 0,1, 0,0, 0,0, 1,1, 1,0, 1,0, 0,1, 1,1, 0,1, 0,0 };
    static const constexpr uint8_t training_seq_p[22] = { 0,1, 1,1, 1,0, 1,0, 0,1, 0,0, 0,0, 1,1, 0,1, 1,1, 1,0 };
    static const constexpr uint8_t training_seq_q[22] = { 1,0, 1,1, 0,1, 1,1, 0,0, 0,0, 0,1, 1,0, 1,0, 1,1, 0,1 };
    static const constexpr uint8_t training_seq_N[33] = { 1,1,1, 0,0,1, 1,0,1, 1,1,1, 0,0,0, 1,1,1, 1,0,0, 0,1,1, 1,1,0, 0,0,0, 0,0,0 };
    static const constexpr uint8_t training_seq_P[33] = { 1,0,1, 0,1,1, 1,1,1, 1,0,1, 0,1,0, 1,0,1, 1,1,0, 0,0,1, 1,0,0, 0,1,0, 0,1,0 };

    /* 9.4.4.3.3 Extended training sequence */
    static const constexpr uint8_t training_seq_x[30] = { 1,0, 0,1, 1,1, 0,1, 0,0, 0,0, 1,1, 1,0, 1,0, 0,1, 1,1, 0,1, 0,0, 0,0, 1,1 };
    static const constexpr uint8_t training_seq_X[45] = { 0,1,1,1,0,0,1,1,0,1,0,0,0,0,1,0,0,0,1,1,1,0,1,1,0,1,0,1,0,1,1,1,1,1,0,1,0,0,0,0,0,1,1,1,0 };

    /* 9.4.4.3.4 Synchronization training sequence */
    static const constexpr uint8_t training_seq_y[38] = { 1,1, 0,0, 0,0, 0,1, 1,0, 0,1, 1,1, 0,0, 1,1, 1,0, 1,0, 0,1, 1,1, 0,0, 0,0, 0,1, 1,0, 0,1, 1,1 };
    uint8_t tsfind_buffer[45];
    bool tsfound = false;
    int symsbeforeexpire = 0;

    char hostname[1024];
    int port = 8355;

    std::shared_ptr<net::Socket> conn;

};

MOD_EXPORT void _INIT_() {
    std::string root = (std::string)core::args["root"];
    json def = json({});
    config.setPath(root + "/tetra_demodulator_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new TetraDemodulatorModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (TetraDemodulatorModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
