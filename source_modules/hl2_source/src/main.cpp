#include <imgui.h>
#include <spdlog/spdlog.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <gui/style.h>
#include <config.h>
#include <options.h>
#include <airspyhf.h>
#include <gui/widgets/stepped_slider.h>
#include <arpa/inet.h>

#include "hl2_device.h"

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO {
    /* Name:            */ "hl2_source",
    /* Description:     */ "Hermes Lite 2 module for SDR++",
    /* Author:          */ "sannysanoff",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

ConfigManager config;

//const char* AGG_MODES_STR = "Off\0Low\0High\0";

std::string discoveredToIp(DISCOVERED &d) {
    char str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(d.info.network.address.sin_addr), str, INET_ADDRSTRLEN);
    return str;
}


class HermesLite2SourceModule : public ModuleManager::Instance {
public:
    HermesLite2SourceModule(std::string name) {
        this->name = name;

        sampleRate = 48000;

        handler.ctx = this;
        handler.selectHandler = menuSelected;
        handler.deselectHandler = menuDeselected;
        handler.menuHandler = menuHandler;
        handler.startHandler = start;
        handler.stopHandler = stop;
        handler.tuneHandler = tune;
        handler.stream = &stream;

        refresh();

        config.acquire();
        std::string devSerial = config.conf["device"];
        config.release();

        sigpath::sourceManager.registerSource("Hermes Lite 2", &handler);
        selectFirst();
    }

    ~HermesLite2SourceModule() {
        stop(this);
        sigpath::sourceManager.unregisterSource("Hermes Lite 2");
    }

    void postInit() {}

    enum AGCMode {
        AGC_MODE_OFF,
        AGC_MODE_LOW,
        AGC_MODE_HIGG
    };

    void enable() {
        enabled = true;
    }

    void disable() {
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }

    void refresh() {

        devices = 0;
        protocol1_discovery();

        devListTxt = "";

        for (int i = 0; i < devices; i++) {
            auto ip = discoveredToIp(discovered[i]);

            devListTxt += ip +" - " ;
            if (discovered[i].device == DEVICE_HERMES_LITE2 || discovered[i].device == DEVICE_HERMES_LITE) {
                devListTxt += std::to_string(discovered[i].supported_receivers)+" * ";
            }
            devListTxt+=discovered[i].name;

            devListTxt += '\0';
        }
    }

    void selectFirst() {
        if (devices != 0) {
            selectByIP(discoveredToIp(discovered[0]));
        }
    }

    void selectByIP(std::string ipAddr) {

        selectedIP = ipAddr;

        sampleRateList.clear();
        sampleRateListTxt = "";
        sampleRateList.push_back(48000);
        sampleRateList.push_back(96000);
        sampleRateList.push_back(192000);
        sampleRateList.push_back(384000);
        for(auto sr: sampleRateList) {
            sampleRateListTxt += std::to_string(sr);
            sampleRateListTxt += '\0';
        }

        selectedSerStr = ipAddr;

        // Load config here
        config.acquire();
        bool created = false;
        if (!config.conf["devices"].contains(selectedSerStr)) {
            created = true;
            config.conf["devices"][selectedSerStr]["sampleRate"] = sampleRateList[0];
        }

        // Load sample rate
        srId = 0;
//        sampleRate = sampleRateList[3];
        if (config.conf["devices"][selectedSerStr].contains("sampleRate")) {
            int selectedSr = config.conf["devices"][selectedSerStr]["sampleRate"];
            for (int i = 0; i < sampleRateList.size(); i++) {
                if (sampleRateList[i] == selectedSr) {
                    srId = i;
                    sampleRate = selectedSr;
                    break;
                }
            }
        }

        // Load Gains
//        if (config.conf["devices"][selectedSerStr].contains("agcMode")) {
//            agcMode = config.conf["devices"][selectedSerStr]["agcMode"];
//        }
//        if (config.conf["devices"][selectedSerStr].contains("lna")) {
//            hfLNA = config.conf["devices"][selectedSerStr]["lna"];
//        }
//        if (config.conf["devices"][selectedSerStr].contains("attenuation")) {
//            atten = config.conf["devices"][selectedSerStr]["attenuation"];
//        }

        config.release(created);

//        airspyhf_close(dev);
    }

private:

    static void menuSelected(void* ctx) {
        HermesLite2SourceModule* _this = (HermesLite2SourceModule*)ctx;
        core::setInputSampleRate(_this->sampleRate);
        spdlog::info("HermerList2SourceModule '{0}': Menu Select!", _this->name);
    }

    static void menuDeselected(void* ctx) {
        HermesLite2SourceModule* _this = (HermesLite2SourceModule*)ctx;
        spdlog::info("HermerList2SourceModule '{0}': Menu Deselect!", _this->name);
    }

    std::vector<dsp::complex_t> incomingBuffer;

    void incomingSample(double i, double q) {
        incomingBuffer.emplace_back(dsp::complex_t{(float)q, (float)i});
        if (incomingBuffer.size() >= 2048) {
            memcpy(stream.writeBuf, incomingBuffer.data(), incomingBuffer.size() * sizeof(dsp::complex_t));
            stream.swap(incomingBuffer.size());
            incomingBuffer.clear();
        }

    }

    static void start(void* ctx) {
        HermesLite2SourceModule* _this = (HermesLite2SourceModule*)ctx;
        if (_this->running) {
            return; }
        if (_this->selectedIP.empty()) {
            spdlog::error("Tried to start HL2 source with null serial");
            return;
        }

        _this->device.reset();
        for(int i=0; i<devices; i++) {
            if (_this->selectedIP == discoveredToIp(discovered[i])) {
                _this->device = std::make_shared<HL2Device>(discovered[i], [=](double i, double q) {
                    _this->incomingSample(i, q);
                });
            }

        }

        if (_this->device) {
            _this->device->setRxSampleRate(_this->sampleRate);
            _this->device->start();
        }


//        int err = airspyhf_open_sn(&_this->openDev, _this->selectedSerial);
//        if (err != 0) {
//            char buf[1024];
//            sprintf(buf, "%016" PRIX64, _this->selectedSerial);
//            spdlog::error("Could not open Hermes Lite 2 {0}", buf);
//            return;
//        }
//
//        airspyhf_set_samplerate(_this->openDev, _this->sampleRateList[_this->srId]);
//        airspyhf_set_freq(_this->openDev, _this->freq);
//        airspyhf_set_hf_agc(_this->openDev, (_this->agcMode != 0));
//        if (_this->agcMode > 0) {
//            airspyhf_set_hf_agc_threshold(_this->openDev, _this->agcMode - 1);
//        }
//        airspyhf_set_hf_att(_this->openDev, _this->atten / 6.0f);
//        airspyhf_set_hf_lna(_this->openDev, _this->hfLNA);
//
//        airspyhf_start(_this->openDev, callback, _this);

        _this->running = true;
        spdlog::info("HL2SourceModule '{0}': Start!", _this->name);
    }

    static void stop(void* ctx) {
        HermesLite2SourceModule* _this = (HermesLite2SourceModule*)ctx;
        if (!_this->running) { return; }
        _this->running = false;
        _this->device->stop();
        _this->stream.stopWriter();
        _this->stream.clearWriteStop();
        spdlog::info("HermerList2SourceModule '{0}': Stop!", _this->name);
        _this->device.reset();
    }

    static void tune(double freq, void* ctx) {
        HermesLite2SourceModule* _this = (HermesLite2SourceModule*)ctx;
        _this->freq = freq;
        if (_this->device) {
            _this->device->setFrequency((int)freq);
        }
        spdlog::info("HermerList2SourceModule '{0}': Tune: {1}!", _this->name, freq);
    }

    static void menuHandler(void* ctx) {
        HermesLite2SourceModule* _this = (HermesLite2SourceModule*)ctx;
        float menuWidth = ImGui::GetContentRegionAvailWidth();

        if (_this->running) { style::beginDisabled(); }

        ImGui::SetNextItemWidth(menuWidth);
        if (ImGui::Combo(CONCAT("##_hl2_dev_sel_", _this->name), &_this->devId, _this->devListTxt.c_str())) {
            _this->selectByIP(discoveredToIp(discovered[_this->devId]));
            core::setInputSampleRate(_this->sampleRate);
            if (_this->selectedSerStr != "") {
                config.acquire();
                config.conf["device"] = _this->selectedSerStr;
                config.release(true);
            }
        }

        if (ImGui::Combo(CONCAT("##_hl2_sr_sel_", _this->name), &_this->srId, _this->sampleRateListTxt.c_str())) {
            _this->sampleRate = _this->sampleRateList[_this->srId];
            core::setInputSampleRate(_this->sampleRate);
            if (_this->selectedSerStr != "") {
                config.acquire();
                config.conf["devices"][_this->selectedSerStr]["sampleRate"] = _this->sampleRate;
                config.release(true);
            }
        }

        ImGui::SameLine();
        float refreshBtnWdith = menuWidth - ImGui::GetCursorPosX();
        if (ImGui::Button(CONCAT("Refresh##_hl2_refr_", _this->name), ImVec2(refreshBtnWdith, 0))) {
            _this->refresh();
            config.acquire();
            std::string devSerial = config.conf["device"];
            config.release();
//            _this->selectByString(devSerial);
            core::setInputSampleRate(_this->sampleRate);
        }

        if (_this->running) { style::endDisabled(); }

//        ImGui::LeftLabel("AGC Mode");
//        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
//        if (ImGui::Combo(CONCAT("##_hl2_agc_", _this->name), &_this->agcMode, AGG_MODES_STR)) {
//            if (_this->running) {
//                airspyhf_set_hf_agc(_this->openDev, (_this->agcMode != 0));
//                if (_this->agcMode > 0) {
//                    airspyhf_set_hf_agc_threshold(_this->openDev, _this->agcMode - 1);
//                }
//            }
//            if (_this->selectedSerStr != "") {
//                config.acquire();
//                config.conf["devices"][_this->selectedSerStr]["agcMode"] = _this->agcMode;
//                config.release(true);
//            }
//        }
//
//        ImGui::LeftLabel("Attenuation");
//        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
//        if (ImGui::SliderFloatWithSteps(CONCAT("##_hl2_attn_", _this->name), &_this->atten, 0, 48, 6, "%.0f dB")) {
//            if (_this->running) {
//                airspyhf_set_hf_att(_this->openDev, _this->atten / 6.0f);
//            }
//            if (_this->selectedSerStr != "") {
//                config.acquire();
//                config.conf["devices"][_this->selectedSerStr]["attenuation"] = _this->atten;
//                config.release(true);
//            }
//        }
//
//        if (ImGui::Checkbox(CONCAT("HF LNA##_hl2_lna_", _this->name), &_this->hfLNA)) {
//            if (_this->running) {
//                airspyhf_set_hf_lna(_this->openDev, _this->hfLNA);
//            }
//            if (_this->selectedSerStr != "") {
//                config.acquire();
//                config.conf["devices"][_this->selectedSerStr]["lna"] = _this->hfLNA;
//                config.release(true);
//            }
//        }
    }

    static int callback(airspyhf_transfer_t* transfer) {
        HermesLite2SourceModule* _this = (HermesLite2SourceModule*)transfer->ctx;
        memcpy(_this->stream.writeBuf, transfer->samples, transfer->sample_count * sizeof(dsp::complex_t));
        if (!_this->stream.swap(transfer->sample_count)) { return -1; }
        return 0;
    }

    std::string name;
    airspyhf_device_t* openDev;
    bool enabled = true;
    dsp::stream<dsp::complex_t> stream;
    int sampleRate;
    SourceManager::SourceHandler handler;
    bool running = false;
    double freq;
    std::string selectedIP;
    int devId = 0;
    int srId = 0;
    int agcMode = AGC_MODE_OFF;
    bool hfLNA = false;
    float atten = 0.0f;
    std::string selectedSerStr = "";

    std::string devListTxt;
    std::vector<uint32_t> sampleRateList;
    std::string sampleRateListTxt;
    std::shared_ptr<HL2Device> device;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    def["devices"] = json({});
    def["device"] = "";
    config.setPath(options::opts.root + "/hl2_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new HermesLite2SourceModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(ModuleManager::Instance* instance) {
    delete (HermesLite2SourceModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}