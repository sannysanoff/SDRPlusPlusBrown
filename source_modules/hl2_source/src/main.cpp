
#ifdef _WIN32
#define _WINSOCKAPI_ // stops windows.h including winsock.h
#endif

#include <imgui.h>
#include <gui/smgui.h>
#include <utils/flog.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <config.h>

#include "hl2_device.h"
#include "utils/stream_tracker.h"
#include "bandconfig.h"
#include <server.h>

#define CONCAT(a, b) ((std::string(a) + (b)).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "hl2_source",
    /* Description:     */ "Hermes Lite 2 module for SDR++",
    /* Author:          */ "sannysanoff",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

ConfigManager config;

// const char* AGG_MODES_STR = "Off\0Low\0High\0";

std::string discoveredToIp(DISCOVERED& d) {
    char str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(d.info.network.address.sin_addr), str, INET_ADDRSTRLEN);
    return str;
}

class HermesLite2SourceModule : public ModuleManager::Instance, public Transmitter {

    int adcGain = 0;
    bool sevenRelays[10]{};

    int nominalPower = 5;
    int redZoneExtraPercent = 100;
    float powerADCScalingFactor = 0.0f;

    bool serverMode = false;
    wav::ComplexDumper txDump;

public:


    static std::string txDumpName() {
        char buf[1024];
        snprintf(buf, sizeof buf, "/tmp/hl2sm_tx_stream_%d.raw", core::args["server"].b() ? 1 : 0);
        return buf;
    }

    explicit HermesLite2SourceModule(const std::string& name) : hermesSamples("hermes samples"), txDump(48000, txDumpName()) {

        this->name = name;

        if (core::args["server"].b()) {
            serverMode = true;
        }

        memset(sevenRelays, 0, sizeof(sevenRelays));

        sampleRate = 48000;

        handler.ctx = this;
        handler.selectHandler = menuSelected;
        handler.deselectHandler = menuDeselected;
        handler.menuHandler = _menuHandler;
        handler.startHandler = start;
        handler.stopHandler = stop;
        handler.tuneHandler = tune;
        handler.stream = &stream;

        initBands();


        config.acquire();
        std::string devSerial = config.conf["device"];
        if (config.conf.contains("staticIp")) {
            std::string staticIpString = config.conf["staticIp"];
            strncpy(staticIp, staticIpString.c_str(), 20);
        }
        if (config.conf.contains("scanIP")) {
            scanIP = config.conf["scanIP"];
        }
        if (config.conf.contains("fastScan")) {
            fastScan = config.conf["fastScan"];
        }
        if (config.conf.contains("directIP")) {
            directIP = config.conf["directIP"];
        }
//        int nominalPower = 5;
//        int redZoneExtraPercent = 100;
//        float powerADCScalingFactor = 1.0f;
        if (config.conf.contains("nominalPower")) {
            nominalPower = config.conf["nominalPower"];
        }
        if (config.conf.contains("redZoneExtraPercent")) {
            redZoneExtraPercent = config.conf["redZoneExtraPercent"];
        }
        if (config.conf.contains("powerADCScalingFactor")) {
            powerADCScalingFactor = config.conf["powerADCScalingFactor"];
        }
        loadBandsConfig(config);
        config.release();

        refresh();
        sigpath::sourceManager.registerSource("Hermes Lite 2", &handler);
        selectFirst();
    }

    ~HermesLite2SourceModule() {
        stop(this);
        sigpath::sourceManager.unregisterSource("Hermes Lite 2");
    }

    void postInit() override {}


    void enable() override {
        enabled = true;
    }

    void disable() override {
        enabled = false;
    }

    bool isEnabled() override {
        return enabled;
    }

    bool refreshing = false;
    std::mutex uiLock;
    std::function<void()> afterRefresh;

    void refresh() {

        refreshing = true;
        devices = 0;
        try {
            protocol1_discovery(staticIp, scanIP, fastScan);
        }
        catch (std::exception& e) {
            flog::error("Error while discovering devices: %s", e.what());
            refreshing = false;
            return;
        }
        if (directIP) {
            discovered[devices].info.network.address.sin_addr.s_addr = inet_addr(staticIp);
            discovered[devices].info.network.address.sin_port = htons(1024);
            discovered[devices].protocol = 1;
            strcpy(discovered[0].name, "(direct) Hermes Lite V2");
            discovered[devices].device = DEVICE_HERMES_LITE2;
            // HL2 send max supported receveirs in discovery response.
            discovered[devices].supported_receivers = 1;
            discovered[devices].supported_transmitters = 1;
            discovered[devices].adcs = 1;
            discovered[devices].frequency_min = 0.0;
            discovered[devices].frequency_max = 30720000.0;
            devices++;
        }

        uiLock.lock();
        devListTxt = "";

        for (int i = 0; i < devices; i++) {
            auto ip = discoveredToIp(discovered[i]);

            devListTxt += ip + " - ";
            if (discovered[i].hl2_protocol) {
                devListTxt += "^^";
            }
            //            if (discovered[i].device == DEVICE_HERMES_LITE2 || discovered[i].device == DEVICE_HERMES_LITE) {
            //                devListTxt += std::to_string(discovered[i].supported_receivers)+" * ";
            //            }
            devListTxt += discovered[i].name;

            devListTxt += '\0';
        }
        uiLock.unlock();
        refreshing = false;
    }

    void selectFirst() {
        if (devices != 0) {
            selectByIP(discoveredToIp(discovered[0]));
        }
    }

    void selectByIP(const std::string& ipAddr) {

        selectedIP = ipAddr;

        sampleRateList.clear();
        sampleRateListTxt = "";
        sampleRateList.push_back(48000);
        sampleRateList.push_back(96000);
        sampleRateList.push_back(192000);
        sampleRateList.push_back(384000);
        for (auto sr : sampleRateList) {
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

        memset(sevenRelays, 0, sizeof(sevenRelays));
        if (config.conf["devices"][selectedSerStr].contains("sevenRelays")) {
            int q = config.conf["devices"][selectedSerStr]["sevenRelays"];
            if (q >= 0) {
                sevenRelays[q] = true;
            }
        }

        if (config.conf["devices"][selectedSerStr].contains("adcGain")) {
            adcGain = config.conf["devices"][selectedSerStr]["adcGain"];
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
        this->menuSelected(this);
    }

private:
    static void menuSelected(void* ctx) {
        auto* _this = (HermesLite2SourceModule*)ctx;
        if (_this->sampleRate >= 48000) {
            core::setInputSampleRate(_this->sampleRate);
        }
        flog::info("HermerList2SourceModule '{0}': Menu Select!", _this->name);
    }

    static void menuDeselected(void* ctx) {
        auto* _this = (HermesLite2SourceModule*)ctx;
        flog::info("HermerList2SourceModule '{0}': Menu Deselect!", _this->name);
    }

    std::vector<dsp::complex_t> incomingBuffer;

    StreamTracker hermesSamples;
    bool showMore = false;
    bool directIP = false;
    bool scanIP = true;
    bool fastScan = true;
    char staticIp[20] = { 0 };

    void incomingSample(double i, double q) {
        incomingBuffer.emplace_back(dsp::complex_t{ (float)q, (float)i });
        if (incomingBuffer.size() >= 512 - 8) {
            flushIncomingSamples();
        }
    }

    void flushIncomingSamples() {
        memcpy(stream.writeBuf, incomingBuffer.data(), incomingBuffer.size() * sizeof(dsp::complex_t));
        if (stream.willSwap()) {
            stream.swap((int) incomingBuffer.size());
        }
        incomingBuffer.clear();
    }

    int lastReportedFrequency = -1;

    static void start(void* ctx) {
        auto* _this = (HermesLite2SourceModule*)ctx;
        if (_this->running) {
            return;
        }
        if (_this->selectedIP.empty()) {
            flog::error("Tried to start HL2 source with null serial");
            return;
        }
        _this->device.reset();
        for (int i = 0; i < devices; i++) {
            if (_this->selectedIP == discoveredToIp(discovered[i])) {
                _this->device = std::make_shared<HL2Device>(discovered[i], [=](int currentFrequency, double i, double q) {
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
                    if (_this->lastReportedFrequency != currentFrequency) {
                        _this->lastReportedFrequency = currentFrequency;
                        if (_this->serverMode) {
                            _this->flushIncomingSamples();
                            server::setInputCenterFrequencyCallback(currentFrequency);  // notify server next samples are for different frequency
                        }
                    }
                    _this->incomingSample(i, q);
                });
            }
        }

        if (_this->device) {
            _this->device->setRxSampleRate(_this->sampleRate);
            _this->device->setADCGain(_this->adcGain);
            for (int q = 0; q < 6; q++) {
                if (_this->sevenRelays[q]) {
                    _this->device->setSevenRelays(1 << q);
                }
            }
            _this->prevBand = -1; // update ui
            _this->updateBandRelays();
            _this->device->start();
        }
        _this->running = true;
        sigpath::transmitter = _this;
        flog::info("HL2SourceModule '{0}': Start!", _this->name);
    }

    static void stop(void* ctx) {
        auto* _this = (HermesLite2SourceModule*)ctx;
        if (!_this->running) { return; }
        _this->running = false;
        _this->device->stop();
        _this->stream.stopWriter();
        _this->stream.clearWriteStop();
        flog::info("HermerList2SourceModule '{0}': Stop!", _this->name);
        _this->device.reset();

        sigpath::transmitter = nullptr;
    }


    int prevBand = -1;
    void updateBandRelays() {
        if (device) {
            auto istx = this->getTXStatus();
            auto [bits, band] = getBitsForBand(tunedFrequency, istx);
            device->setSevenRelays(bits);
            if (band != prevBand) {
                prevBand = band;
                if (serverMode) {
                    server::sendUnsolicitedUI(); // to reflect current band on checkboxes.
                }
            }
        }
    }

    static void tune(double freq, void* ctx) {
        auto* _this = (HermesLite2SourceModule*)ctx;
        if (_this->device) {
            _this->device->setFrequency((int)freq);
            _this->tunedFrequency = (int)freq;
            _this->updateBandRelays();
        }
        flog::info("HermerList2SourceModule '{0}': Tune: {1}!", _this->name, freq);
    }

    static void _menuHandler(void* ctx) {
        auto* _this = (HermesLite2SourceModule*)ctx;
        _this->menuHandler();
    }

    void menuHandler() {
        if (running) { SmGui::BeginDisabled(); }

        //        SmGui::SetNextItemWidth(100);
        uiLock.lock();
        if (SmGui::Combo(CONCAT("##_hl2_dev_sel_", name), &devId, devListTxt.c_str())) {
            selectByIP(discoveredToIp(discovered[devId]));
            core::setInputSampleRate(sampleRate);
            if (!selectedSerStr.empty()) {
                config.acquire();
                config.conf["device"] = selectedSerStr;
                config.release(true);
            }
        }
        uiLock.unlock();

        auto updateSampleRate = [&](int srid) {
            sampleRate = (int)sampleRateList[srId];
            core::setInputSampleRate(sampleRate);
            if (!selectedSerStr.empty()) {
                config.acquire();
                config.conf["devices"][selectedSerStr]["sampleRate"] = sampleRate;
                config.release(true);
            }
        };
        if (SmGui::Combo(CONCAT("##_hl2_sr_sel_", name), &srId, sampleRateListTxt.c_str())) {
            updateSampleRate(srId);
        }

        SmGui::SameLine();
        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Button(CONCAT(refreshing ? "[Refreshing..]##_hl2_refr_" : "Refresh##_hl2_refr_", name))) {
            if (!refreshing) {
                std::thread refreshThread([&]() {
                    refresh();
                    afterRefresh = [&]() {
                        if (devices > 0) {
                            selectFirst();
                        }
                    };
                    if (serverMode) {
                        server::sendUnsolicitedUI();
                    }
                });
                refreshThread.detach();
            }
        }
        if (afterRefresh) {
            afterRefresh();
            afterRefresh = nullptr;
        }

        if (running) { SmGui::EndDisabled(); }
        bool overload = device && device->isADCOverload();
        if (overload) {
            SmGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0, 0, 1.0f));
        }
        SmGui::LeftLabel("ADC Gain");
        if (overload) {
            SmGui::PopStyleColor(1);
        }
        SmGui::SameLine();
        //        SmGui::SetNextItemWidth(100);
        if (SmGui::SliderInt(("##_radio_agc_gain_" + name).c_str(), &adcGain, -12, +48, SmGui::FMT_STR_INT_DB)) {
            flog::info("ADC Gain changed: {}", adcGain);
            if (device) {
                device->setADCGain(adcGain);
                config.acquire();
                config.conf["devices"][selectedSerStr]["adcGain"] = adcGain;
                config.release(true);
            }
        }

        if (running) { SmGui::BeginDisabled(); }
        if (SmGui::CollapsingHeader("HL2 advanced discovery")) {
            SmGui::LeftLabel("Probe IP:");
            SmGui::SameLine();
            SmGui::FillWidth();
            if (SmGui::InputText(CONCAT("##_fixed_ip_hl2_source_", name), staticIp, 19)) {
                config.acquire();
                config.conf["staticIp"] = staticIp;
                config.release(true);
            }
            if (SmGui::Checkbox("Hardcode this IP##hl2_direct_ip", &directIP)) {
                config.acquire();
                config.conf["directIP"] = directIP;
                config.release(true);
            }
            if (SmGui::Checkbox("Full UDP scan /24##hl2_scan_ip", &scanIP)) {
                config.acquire();
                config.conf["scanIP"] = scanIP;
                config.release(true);
            }
            SmGui::SameLine();
            if (SmGui::Checkbox("Fast scan##hl2_scan_ip_first", &fastScan)) {
                config.acquire();
                config.conf["fastScan"] = fastScan;
                config.release(true);
            }
        }
        if (running) { SmGui::EndDisabled(); }

        if (SmGui::CollapsingHeader("HL2 bands config")) {
            if (bandsEditor(config, running && getTXStatus(), tunedFrequency)) {
                updateBandRelays();
            }
        }

        if (SmGui::CollapsingHeader("HL2 power calibrate")) {
            if (SmGui::SliderInt("nominal PWR", &nominalPower, 5, 100, SmGui::FMT_STR_WATTS_INT) || // "%d W"
                SmGui::SliderInt("red zone %", &redZoneExtraPercent, 20, 100, SmGui::FMT_STR_PLUS_INT_PERCENT) || // " + %d %%"
                SmGui::SliderFloat("PWR calibrate", &powerADCScalingFactor, -2, +2, SmGui::FMT_STR_TEN_POWER_FLOAT )) // "10 ^ %.2f"
            {
                config.acquire();
                config.conf["nominalPower"] = nominalPower;
                config.conf["redZoneExtraPercent"] = redZoneExtraPercent;
                config.conf["powerADCScalingFactor"] = powerADCScalingFactor;
                config.release(true);
            }
        }


        /*
        for(int q=0; q<6; q++) {
            char strr[100];
            switch(q) {
            case 0: snprintf(strr, sizeof(strr), "=160"); break;
            case 1: snprintf(strr, sizeof(strr), "=80"); break;
            case 2: snprintf(strr, sizeof(strr), "=40"); break;
            case 3: snprintf(strr, sizeof(strr), "=20"); break;
            case 4: snprintf(strr, sizeof(strr), "=15"); break;
            case 5: snprintf(strr, sizeof(strr), "=10"); break;
            default: snprintf(strr, sizeof(strr), "???"); break;
            }
            if (q >=0) {
                if (SmGui::RadioButton(strr, sevenRelays[q])) {
                    if (sevenRelays[q]) {
                        memset(sevenRelays, 0, sizeof(sevenRelays));
                        if (device) {
                            device->setSevenRelays(0);
                            config.acquire();
                            config.conf["devices"][selectedSerStr]["sevenRelays"] = -1;
                            config.release(true);
                        }
                    }
                    else {
                        memset(sevenRelays, 0, sizeof(sevenRelays));
                        sevenRelays[q] = !sevenRelays[q];
                        if (device) {
                            device->setSevenRelays(1 << q);
                            config.acquire();
                            config.conf["devices"][selectedSerStr]["sevenRelays"] = q;
                            config.release(true);
                        }
                    }
                }
            }
            if (q != 5 && q != 2) {
                SmGui::SameLine();
            }
            */

        //}
    }

    std::string name;
    bool enabled = true;
    dsp::stream<dsp::complex_t> stream = "hl2.stream";
    dsp::stream<dsp::complex_t> txstream = "hl2.txstream";
    int sampleRate;
    SourceManager::SourceHandler handler{};
    bool running = false;
    std::string selectedIP;
    int devId = 0;
    int srId = 0;
    std::string selectedSerStr;

    std::string devListTxt;
    std::vector<uint32_t> sampleRateList;
    std::string sampleRateListTxt;
    std::shared_ptr<HL2Device> device;
    int tunedFrequency = 0;


//    int getInputStreamFramerate() override {
//        return 48000;
//    }

    bool lastPTT = false;
    void setTransmitStatus(bool status) override {
        device->setTune(false);
        device->setPTT(status);
        updateBandRelays();
        sigpath::txState.emit(status);
        if (lastPTT != status) {
            lastPTT = status;
            if (status) {
                txDump.clear();
            }
        }
    }


    void setTransmitStream(dsp::stream<dsp::complex_t>* astream) override {
        flog::info("hl2 transmit stream feed NEW STREAM");
        std::thread([this, astream]() {
            SetThreadName("hl2_tx_strm_rd");
            auto debug = true;
            std::vector<dsp::complex_t> buffer;
            int addedBlocks = 0;
            int readSamples = 0;
            int nreads = 0;
            long long lastTransmit = currentTimeMillis();
            while (true) {
                int rd = astream->read();
                if (rd < 0) {
                    flog::info("End iq stream for tx: astream read < 0");
                    break;
                }
                txDump.dump(astream->readBuf, rd);
                readSamples += rd;
                auto ctm = currentTimeMillis();
                if (lastTransmit < ctm - 1000) {
                    //flog::info("hl2 transmit stream feed: got samples/sec: {}", readSamples * 1000 / (ctm - lastTransmit));
                    readSamples = 0;
                    lastTransmit = ctm;
                }
                nreads++;
                for (int q = 0; q < rd; q++) {
                    buffer.push_back(astream->readBuf[q]);
                    if (buffer.size() == 63) {
                        addedBlocks++;
                        if (addedBlocks % 1000 == 0) {
                            //                            flog::info("Added {} blocks to tx buffer, rd samples {}  ndreads {}", addedBlocks, readSamples, nreads);
                        }
                        device->samplesToSend.fillFrom(buffer.data(), 63);
                        buffer.clear();
                    }
                }
                astream->flush();
            }
            flog::info("hl2 transmit stream feed END, {}", (int)core::args["server"].b() ? 1 : 0);
        }).detach();
    }
    void setTransmitSoftwareGain(unsigned char gain) override {
        device->setSoftwarePower(gain);
    }
    void setTransmitFrequency(int freq) override {
        device->setTxFrequency(freq);
    }

public:
    void setTransmitHardwareGain(unsigned char gain) override {
        device->setHardwarePower(gain);
    }
    unsigned char getTransmitHardwareGain() override {
        return device->hardwarePower;
    }

public:
//    int getTransmittedBufferLatency() override {
//        return device->bufferLatency;
//    }
//    void setTransmittedBufferLatency(int latency) override {
//        device->bufferLatency = latency;
//        device->setHangLatency(device->pttHangTime, device->bufferLatency);
//    }
//    int getTransmittedPttDelay() override {
//        return device->pttHangTime;
//    }
//    void setTransmittedPttDelay(int delay) override {
//        device->pttHangTime = delay;
//        device->setHangLatency(device->pttHangTime, device->bufferLatency);
//    }

private:
//    void startGenerateTone(int frequency) override {
//        device->setFrequency(frequency);
//        device->setTxFrequency(frequency);
//        device->setTune(true);
//        device->setPTT(true);
//    }

    void setPAEnabled(bool paenabled) override {
        if (device) {
            device->setPAEnabled(paenabled);
        }
    }

    int getTXStatus() override {
        if (!device) {
            return false;
        }
        return device->transmitMode;
    }
    float getTransmitPower() override {
        if (!device) {
            return 0;
        }
        return device->fwd * pow(10, powerADCScalingFactor);
        //        device->updateSWR();
        //        return device->fwd+device->rev;
    }

public:
    float getReflectedPower() override {
        if (!device) {
            return 0;
        }
        return device->rev * pow(10, powerADCScalingFactor);
    }

    float getTransmitSWR() override {
        if (!device) {
            return 0;
        }
        return device->swr;
    }
    int getNormalZone() override {
        return nominalPower;
    }
    int getRedZone() override {
        return int(nominalPower * ((100 + redZoneExtraPercent) / 100.0));
    }

    float getFillLevel() {
        if (!device) {
            return 0;
        }
        return (float)device->fill_level;
    }

    std::string& getTransmitterName() override {
        static std::string name = "Hermes Lite 2";
        return name;
    }
};

MOD_EXPORT void _INIT_() {
#ifdef WIN32
    int iResult;

    // Initialize Winsock
    WSADATA wsaData;

    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        flog::error("WSAStartup failed: %d\n", iResult);
        exit(1);
    }
#endif
    json def = json({});
    def["devices"] = json({});
    def["device"] = "";
    config.setPath(std::string(core::getRoot()) + "/hl2_config.json");
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