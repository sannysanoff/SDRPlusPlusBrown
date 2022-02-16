
#define _WINSOCKAPI_    // stops windows.h including winsock.h

#include <imgui.h>
#include <gui/smgui.h>
#include <spdlog/spdlog.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <gui/style.h>
#include <config.h>
#include <options.h>
#include <gui/widgets/stepped_slider.h>
#include <iostream>

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

static long long currentTimeMillis() {
    std::chrono::system_clock::time_point t1 = std::chrono::system_clock::now();
    long long msec = std::chrono::time_point_cast<std::chrono::milliseconds>(t1).time_since_epoch().count();
    return msec;
}

class HermesLite2SourceModule : public ModuleManager::Instance {

    int adcGain = 0;
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

        txhandler.ctx = this;
        txhandler.selectHandler = txmenuSelected;
        txhandler.deselectHandler = txmenuDeselected;
        txhandler.menuHandler = txmenuHandler;
        txhandler.startHandler = start;
        txhandler.stopHandler = stop;
        txhandler.tuneHandler = txtune;
        txhandler.stream = &txstream;

        refresh();

        config.acquire();
        std::string devSerial = config.conf["device"];
        config.release();

        sigpath::sourceManager.registerSource("Hermes Lite 2", &handler);
        sigpath::transmitterManager.registerTransmitter("Hermes Lite 2", &txhandler);
        selectFirst();
    }

    ~HermesLite2SourceModule() {
        stop(this);
        sigpath::sourceManager.unregisterSource("Hermes Lite 2");
    }

    void postInit() {}


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

    static void txmenuSelected(void* ctx) {
        HermesLite2SourceModule* _this = (HermesLite2SourceModule*)ctx;
//        core::setInputSampleRate(_this->sampleRate);
        spdlog::info("HermerList2SourceModule '{0}': TX Menu Select!", _this->name);
    }

    static void menuDeselected(void* ctx) {
        HermesLite2SourceModule* _this = (HermesLite2SourceModule*)ctx;
        spdlog::info("HermerList2SourceModule '{0}': Menu Deselect!", _this->name);
    }

    static void txmenuDeselected(void* ctx) {
        HermesLite2SourceModule* _this = (HermesLite2SourceModule*)ctx;
        spdlog::info("HermerList2SourceModule '{0}': TX Menu Deselect!", _this->name);
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
                    static auto lastCtm = currentTimeMillis();
                    static auto totalCount = 0LL;
                    totalCount++;
                    if (totalCount % 10000 == 0) {
                        if (lastCtm < currentTimeMillis() - 1000) {
                            static auto lastLastTotalCount = 0LL;
                            auto nowCtm = currentTimeMillis();
//                            std::cout << "HL2: Speed: IQ pairs/sec: ~ " << (totalCount - lastLastTotalCount) * 1000 / (nowCtm - lastCtm) << std::endl;
                            lastLastTotalCount = totalCount;
                            lastCtm = nowCtm;
                        }
                    }
                    _this->incomingSample(i, q);
                });
            }

        }

        if (_this->device) {
            _this->device->setRxSampleRate(_this->sampleRate);
            _this->device->setADCGain(_this->adcGain);
            _this->device->start();
        }
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

    static void txtune(double freq, void* ctx) {
        HermesLite2SourceModule* _this = (HermesLite2SourceModule*)ctx;
        _this->txfreq = freq;
        if (_this->device) {
            _this->device->setTxFrequency((int)freq);
        }
        spdlog::info("HermerList2SourceModule '{0}': TxTune: {1}!", _this->name, freq);
    }

    bool hardTune = false;

    static void txmenuHandler(void* ctx) {
        HermesLite2SourceModule* _this = (HermesLite2SourceModule*)ctx;
        if (!_this->running) { style::beginDisabled(); }

        int drawHardTune = _this->hardTune;
        if (drawHardTune) {
            SmGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0, 0, 1.0f));
            SmGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0, 0, 1.0f));
            SmGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0, 0, 1.0f));
        }
        if (SmGui::Button("Hard Tune")) {
            _this->hardTune = !_this->hardTune;
            _this->device->setTxFrequency((int) _this->freq);
            _this->device->doTuneActive(_this->hardTune);
            std::cout << "_this->hardTune=" << _this->hardTune << std::endl;
        }
        if (drawHardTune) {
            SmGui::PopStyleColor(3);
        }

        if (!_this->running) { style::endDisabled(); }

    }

    static void menuHandler(void* ctx) {
        HermesLite2SourceModule* _this = (HermesLite2SourceModule*)ctx;

        if (_this->running) { style::beginDisabled(); }

        SmGui::SetNextItemWidth(100);
        if (SmGui::Combo(CONCAT("##_hl2_dev_sel_", _this->name), &_this->devId, _this->devListTxt.c_str())) {
            _this->selectByIP(discoveredToIp(discovered[_this->devId]));
            core::setInputSampleRate(_this->sampleRate);
            if (_this->selectedSerStr != "") {
                config.acquire();
                config.conf["device"] = _this->selectedSerStr;
                config.release(true);
            }
        }

        if (SmGui::Combo(CONCAT("##_hl2_sr_sel_", _this->name), &_this->srId, _this->sampleRateListTxt.c_str())) {
            _this->sampleRate = _this->sampleRateList[_this->srId];
            core::setInputSampleRate(_this->sampleRate);
            if (_this->selectedSerStr != "") {
                config.acquire();
                config.conf["devices"][_this->selectedSerStr]["sampleRate"] = _this->sampleRate;
                config.release(true);
            }
        }

        SmGui::SameLine();
        float refreshBtnWdith = 100;
        if (SmGui::Button(CONCAT("Refresh##_hl2_refr_", _this->name), ImVec2(refreshBtnWdith, 0))) {
            _this->refresh();
            config.acquire();
            std::string devSerial = config.conf["device"];
            config.release();
            core::setInputSampleRate(_this->sampleRate);
        }

        if (_this->running) { style::endDisabled(); }
        bool overload = _this->device && _this->device->isADCOverload();
        if (overload) {
            SmGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0, 0, 1.0f));
        }
        SmGui::LeftLabel("ADC Gain");
        if (overload) {
            SmGui::PopStyleColor(1);
        }
        SmGui::SameLine();
        SmGui::SetNextItemWidth(100);
        if (SmGui::SliderInt(("##_radio_agc_gain_" + _this->name).c_str(), &_this->adcGain, -12, +48, SmGui::FMT_STR_INT_DB)) {
            if (_this->device) {
                _this->device->setADCGain(_this->adcGain);
            }
        }

    }

    std::string name;
    bool enabled = true;
    dsp::stream<dsp::complex_t> stream;
    dsp::stream<dsp::complex_t> txstream;
    int sampleRate;
    SourceManager::SourceHandler handler;
    TransmitterManager::TransmitterHandler txhandler;
    bool running = false;
    double freq;
    double txfreq;
    std::string selectedIP;
    int devId = 0;
    int srId = 0;
    float atten = 0.0f;
    std::string selectedSerStr = "";

    std::string devListTxt;
    std::vector<uint32_t> sampleRateList;
    std::string sampleRateListTxt;
    std::shared_ptr<HL2Device> device;
};

MOD_EXPORT void _INIT_() {
#ifdef WIN32
    int iResult;

// Initialize Winsock
    WSADATA wsaData;

    iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (iResult != 0) {
        spdlog::error("WSAStartup failed: %d\n", iResult);
        exit(1);
    }
#endif
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