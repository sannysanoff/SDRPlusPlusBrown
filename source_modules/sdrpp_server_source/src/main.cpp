#include <imgui.h>
#include <utils/flog.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <gui/style.h>
#include <config.h>
#include <gui/widgets/stepped_slider.h>
#include <utils/optionlist.h>
#include <gui/dialogs/dialog_box.h>
#include "sdrpp_server_client.h"
#include "utils/pbkdf2_sha256.h"
#include <gui/gui.h>

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "sdrpp_server_source",
    /* Description:     */ "SDR++ Server source module for SDR++",
    /* Author:          */ "Ryzerth",
    /* Version:         */ 0, 2, 0,
    /* Max instances    */ 1
};

ConfigManager config;

class SDRPPServerSourceModule : public ModuleManager::Instance {
public:

    SDRPPServerSourceModule(std::string name) {
        this->name = name;

        // Yeah no server-ception, sorry...
        if (core::args["server"].b()) { return; }

        // Initialize lists
        compressionTypeList.define("No compression", server::CT_NONE);
        compressionTypeList.define("Legacy ZSTD compression", server::CT_LEGACY);
        compressionTypeList.define("Lossy compression", server::CT_LOSSY);
        compressionTypeId = compressionTypeList.valueId(server::CT_NONE);

        sampleTypeList.define("Int8", dsp::compression::PCM_TYPE_I8);
        sampleTypeList.define("Int16", dsp::compression::PCM_TYPE_I16);
        sampleTypeList.define("Float32", dsp::compression::PCM_TYPE_F32);
        sampleTypeId = sampleTypeList.valueId(dsp::compression::PCM_TYPE_I16);

        prebufferMsec.define("disabled", 0);
        prebufferMsec.define("100 msec", 100);
        prebufferMsec.define("250 msec", 250);
        prebufferMsec.define("500 msec", 500);
        prebufferMsec.define("750 msec", 700);
        prebufferMsec.define("1000 msec", 1000);
        prebufferMsec.define("1500 msec", 1500);
        prebufferMsec.define("2000 msec", 2000);
        prebufferMsec.define("3000 msec", 3000);
        prebufferMsec.define("5000 msec", 5000);

        serverResample.define("no resample", 0);
        serverResample.define("24 KHz", 24000);
        serverResample.define("48 KHz", 48000);
        serverResample.define("96 KHz", 96000);
        serverResample.define("128 KHz", 128000);
        serverResample.define("196 KHz", 196000);
        serverResample.define("256 KHz", 256000);
        serverResample.define("384 KHz", 384000);

        rxPrebufferId = prebufferMsec.valueId(0);
        txPrebufferId = prebufferMsec.valueId(0);


        handler.ctx = this;
        handler.selectHandler = menuSelected;
        handler.deselectHandler = menuDeselected;
        handler.menuHandler = menuHandler;
        handler.startHandler = start;
        handler.stopHandler = stop;
        handler.tuneHandler = tune;
        handler.stream = &stream;

        // Load config
        config.acquire();
        std::string hostStr = config.conf["hostname"];
        strcpy(hostname, hostStr.c_str());
        port = config.conf["port"];
        config.release();

        sigpath::sourceManager.registerSource("SDR++ Server", &handler);
    }

    ~SDRPPServerSourceModule() {
        stop(this);
        sigpath::sourceManager.unregisterSource("SDR++ Server");
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

private:
    std::string getBandwdithScaled(double bw) {
        char buf[1024];
        if (bw >= 1000000.0) {
            snprintf(buf, sizeof buf, "%.1lfMHz", bw / 1000000.0);
        }
        else if (bw >= 1000.0) {
            snprintf(buf, sizeof buf, "%.1lfKHz", bw / 1000.0);
        }
        else {
            snprintf(buf, sizeof buf, "%.1lfHz", bw);
        }
        return std::string(buf);
    }

    static void menuSelected(void* ctx) {
        SDRPPServerSourceModule* _this = (SDRPPServerSourceModule*)ctx;
        if (_this->client) {
            core::setInputSampleRate(_this->client->getSampleRate());
        }
        gui::mainWindow.playButtonLocked = !(_this->client && _this->client->isOpen());
        flog::info("SDRPPServerSourceModule '{0}': Menu Select!", _this->name);
    }

    static void menuDeselected(void* ctx) {
        SDRPPServerSourceModule* _this = (SDRPPServerSourceModule*)ctx;
        gui::mainWindow.playButtonLocked = false;
        flog::info("SDRPPServerSourceModule '{0}': Menu Deselect!", _this->name);
    }

    static void start(void* ctx) {
        SDRPPServerSourceModule* _this = (SDRPPServerSourceModule*)ctx;
        if (_this->running) { return; }

        // Try to connect if not already connected (Play button is locked anyway so not sure why I put this here)
        if (!_this->connected()) {
            _this->tryConnect();
            if (!_this->connected()) { return; }
        }

        if (!_this->client->challenge.empty() && _this->client->hmacKeyToUse.empty()) {
            HMAC_SHA256_CTX pbkdf_hmac;
            _this->client->hmacKeyToUse.resize(256 / 8);
            flog::info("Computing auth signing key..");
            pbkdf2_sha256(&pbkdf_hmac, (uint8_t *)_this->securePassword, strlen(_this->securePassword), (uint8_t*)server::passwordSalt.data(), server::passwordSalt.length(), 20000, (uint8_t*)_this->client->hmacKeyToUse.data(), _this->client->hmacKeyToUse.size());
            flog::info("Computed auth signing key..");
        }

        // Set configuration
        _this->client->setFrequency(_this->freq);
        _this->lastStartSent = currentTimeMillis();
        _this->lastStopSent = 0;
        _this->client->start();

        _this->running = true;
        flog::info("SDRPPServerSourceModule '{0}': Start!", _this->name);
    }

    static void stop(void* ctx) {
        SDRPPServerSourceModule* _this = (SDRPPServerSourceModule*)ctx;
        if (!_this->running) { return; }

        if (_this->connected()) { _this->client->stop(); }

        _this->running = false;
        _this->lastStopSent = currentTimeMillis();
        flog::info("SDRPPServerSourceModule '{0}': Stop!", _this->name);
    }

    static void tune(double freq, void* ctx) {
        SDRPPServerSourceModule* _this = (SDRPPServerSourceModule*)ctx;
        if (_this->running && _this->connected()) {
            _this->client->setFrequency(freq);
        }
        _this->freq = freq;
        flog::info("SDRPPServerSourceModule '{0}': Tune: {1}!", _this->name, freq);
    }

    static void menuHandler(void* ctx) {
        SDRPPServerSourceModule* _this = (SDRPPServerSourceModule*)ctx;
        float menuWidth = ImGui::GetContentRegionAvail().x;

        bool connected = _this->connected();
        gui::mainWindow.playButtonLocked = !connected;
        if (gui::mainWindow.isPlaying() && !connected) {
            gui::mainWindow.setPlayState(false);  // Stop playing on disconnect.
            if (sigpath::transmitter) {
                auto saved= sigpath::transmitter;
                sigpath::transmitter = nullptr;
                delete saved;   // various threads when stopping will not attempt anything.
            }
        }

        ImGui::GenericDialog("##sdrpp_srv_src_err_dialog", _this->serverBusy, GENERIC_DIALOG_BUTTONS_OK, [=](){
            ImGui::TextUnformatted("This server is already in use.");
        });

        if (connected) { style::beginDisabled(); }
        if (ImGui::InputText(CONCAT("##sdrpp_srv_srv_host_", _this->name), _this->hostname, 1023)) {
            config.acquire();
            config.conf["hostname"] = _this->hostname;
            config.release(true);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputInt(CONCAT("##sdrpp_srv_srv_port_", _this->name), &_this->port, 0, 0)) {
            config.acquire();
            config.conf["port"] = _this->port;
            config.release(true);
        }
        if (connected) { style::endDisabled(); }

        if (_this->running) { style::beginDisabled(); }
        if (!connected && ImGui::Button("Connect##sdrpp_srv_source", ImVec2(menuWidth, 0))) {
            _this->tryConnect();
        }
        else if (connected && ImGui::Button("Disconnect##sdrpp_srv_source", ImVec2(menuWidth, 0))) {
            _this->client->close();
        }
        if (_this->running) { style::endDisabled(); }


        if (connected) {
            if (_this->client->challenge.size() > 0) {
                ImGui::LeftLabel("Password");
                ImGui::FillWidth();
                if (ImGui::InputText("##sdrpp_srv_source_password", _this->securePassword,
                                     sizeof _this->securePassword - 1, ImGuiInputTextFlags_Password)) {
                    _this->client->hmacKeyToUse.clear();
                    // Save config
                    config.acquire();
                    config.conf["hosts"][std::string(_this->hostname)]["securePassword"] = std::string(
                            _this->securePassword);
                    config.release(true);
                }
                if (strlen(_this->securePassword) == 0) {
                    ImGui::TextColored(ImVec4(1.0f, 0, 0, 1.0f), "%s", "Password required");
                }
            }
            if (_this->lastStartSent != 0 && _this->lastStopSent == 0 && _this->client->secureChallengeReceived > _this->lastStartSent) {
                ImGui::TextColored(ImVec4(1.0f, 0, 0, 1.0f), "%s", "server: invalid password.");
                if (gui::mainWindow.isPlaying()) {
                    gui::mainWindow.setPlayState(false);
                    _this->lastStopSent = 0;
                }
            }
            ImGui::LeftLabel("Sample type");
            ImGui::FillWidth();
            if (ImGui::Combo("##sdrpp_srv_source_samp_type", &_this->sampleTypeId, _this->sampleTypeList.txt)) {
                _this->client->setSampleType(_this->sampleTypeList[_this->sampleTypeId]);

                // Save config
                config.acquire();
                config.conf["servers"][_this->devConfName]["sampleType"] = _this->sampleTypeList.key(_this->sampleTypeId);
                config.release(true);
            }
            if (_this->client->transmitterSupported != -1) { // means sdr++ brown version
                ImGui::LeftLabel("RX resample");
                ImGui::FillWidth();
                if (ImGui::Combo("##sdrpp_srv_source_rx_resample", &_this->rxResampleId, _this->serverResample.txt)) {
                    auto spsValue = _this->serverResample.value(_this->rxResampleId);
                    config.acquire();
                    config.conf["servers"][_this->devConfName]["rxResample"] = spsValue;
                    config.release(true);
                    if (_this->client) {
                        _this->client->setRxResample(spsValue);
                    }
                }
            }
            ImGui::LeftLabel("RX prebuffer length");
            ImGui::FillWidth();
            if (ImGui::Combo("##sdrpp_srv_source_rx_prebuf", &_this->rxPrebufferId, _this->prebufferMsec.txt)) {
                auto msecValue = _this->prebufferMsec.value(_this->rxPrebufferId);
                config.acquire();
                config.conf["servers"][_this->devConfName]["rxPrebuffer"] = msecValue;
                config.release(true);
                if (_this->client) {
                    _this->client->setRxPrebufferMsec(msecValue);
                }
            }
            if (_this->client->transmitterSupported != -1) { // means sdr++ brown version
                ImGui::LeftLabel("TX prebuffer length");
                ImGui::FillWidth();
                if (ImGui::Combo("##sdrpp_srv_source_tx_prebuf", &_this->txPrebufferId, _this->prebufferMsec.txt)) {
                    config.acquire();
                    config.conf["servers"][_this->devConfName]["txPrebuffer"] = _this->prebufferMsec.value(
                            _this->txPrebufferId);
                    config.release(true);
                    _this->client->txPrebufferMsec = _this->prebufferMsec.value(_this->txPrebufferId);
                    if (gui::mainWindow.isPlaying()) {
                        gui::mainWindow.setPlayState(false);  // because this setting is set on start command.
                    }

                }
            }


            if (ImGui::Combo("##sdrpp_srv_source_compr_typ", &_this->compressionTypeId, _this->compressionTypeList.txt)) {
                _this->client->setCompressionType(_this->compressionTypeList[_this->compressionTypeId]);
                // Save config
                config.acquire();
                config.conf["servers"][_this->devConfName]["compressionType"] = _this->compressionTypeList.value(_this->compressionTypeId);
                config.release(true);
            }
            if (_this->compressionTypeList.value(_this->compressionTypeId) == server::CT_LOSSY) {
                if (_this->client->transmitterSupported != -1) { // means sdr++ brown version
                    ImGui::LeftLabel("Loss factor");
                    ImGui::FillWidth();
                    if (ImGui::SliderFloat("##sdrpp_srv_source_efft_mult", &_this->lossFactor, 0, 20)) {
                        if (_this->client) {
                            _this->client->setLossFactor(_this->lossFactor);
                        }
                    }
                    ImGui::LeftLabel("Noise filler multiplier, db");
                    ImGui::FillWidth();
                    if (ImGui::SliderFloat("##sdrpp_srv_source_noise_mult", &_this->noiseMultiplerDB, -20, 3)) {
                        if (_this->client) {
                            _this->client->setNoiseMultiplierDB(_this->noiseMultiplerDB);
                        }
                    }
                } else {
                    ImGui::TextUnformatted("Compression: server not compatible.");
                }
            }


            bool dummy = true;
            style::beginDisabled();
            ImGui::Checkbox("Full IQ", &dummy);
            style::endDisabled();

            // Calculate datarate
            _this->frametimeCounter += ImGui::GetIO().DeltaTime;
            if (_this->frametimeCounter >= 0.2f) {
                _this->datarate = ((float)_this->client->bytes / (_this->frametimeCounter * 1024.0f * 1024.0f)) * 8;
                _this->frametimeCounter = 0;
                _this->client->bytes = 0;
            }

            ImGui::TextUnformatted("Status:");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Connected (%.3f Mbit/s), %d%% buffer", _this->datarate, _this->client ? _this->client->getBufferPercentFull() : 0);
            ImGui::CollapsingHeader("Source [REMOTE]", ImGuiTreeNodeFlags_DefaultOpen);

            _this->client->showMenu();

            if (_this->client) {
                std::vector<int32_t> offsets;
                for (auto v : gui::waterfall.vfos) {
                    auto from = v.second->lowerOffset;
                    auto to = v.second->upperOffset;
                    offsets.emplace_back((int32_t)from);
                    offsets.emplace_back((int32_t)to);
                }
                _this->client->setMaskedFrequencies(offsets);
            }



        }
        else {
            ImGui::TextUnformatted("Status:");
            ImGui::SameLine();
            ImGui::TextUnformatted("Not connected (--.--- Mbit/s)");
        }

    }

    bool connected() {
        return client && client->isOpen();
    }

    void tryConnect() {
        try {
            if (client) { client.reset(); }
            client = server::connect(hostname, port, &stream);
            onConnectionEstablished();
            // client->setRxPrebufferMsec(prebufferMsec.value(rxPrebufferId));
        }
        catch (const std::exception& e) {
            flog::error("Could not connect to SDR: {}", e.what());
            if (!strcmp(e.what(), "Server busy")) { serverBusy = true; }
        }
    }

    void onConnectionEstablished() {
        // Gene rate the config name
        char buf[4096];
        snprintf(buf, sizeof buf, "%s:%05d", hostname, port);
        devConfName = buf;

        config.acquire();



        // Load settings
        sampleTypeId = sampleTypeList.valueId(dsp::compression::PCM_TYPE_I16);
        auto &cfg = config.conf["servers"][devConfName];
        auto &hcfg = config.conf["hosts"][hostname];
        if(hcfg.contains("securePassword")) {
            std::string securePassword = hcfg["securePassword"];
            strcpy(this->securePassword, securePassword.c_str());
            client->hmacKeyToUse.clear();
        } else {
            this->securePassword[0] = 0;
        }
        if (cfg.contains("sampleType")) {
            std::string key = cfg["sampleType"];
            if (sampleTypeList.keyExists(key)) { sampleTypeId = sampleTypeList.keyId(key); }
        }
        if (cfg.contains("compressionType")) {
            compressionTypeId = compressionTypeList.valueId(cfg["compressionType"]);
        }
        if (cfg.contains("rxPrebuffer")) {
            int rxPrebufferMsec = cfg["rxPrebuffer"];
            rxPrebufferId = prebufferMsec.valueId(rxPrebufferMsec);
        }
        if (cfg.contains("rxResample")) {
            int sps = cfg["rxResample"];
            rxResampleId = serverResample.valueId(sps);
        }
        if (cfg.contains("txPrebuffer")) {
            txPrebufferId = prebufferMsec.valueId(cfg["txPrebuffer"]);
        }

        config.release();

        // Set settings
        client->setSampleType(sampleTypeList[sampleTypeId]);
        client->setCompressionType(compressionTypeList[compressionTypeId]);
        client->setLossFactor(lossFactor);
        client->setRxResample(serverResample.value(rxResampleId));
        client->setRxPrebufferMsec(prebufferMsec.value(rxPrebufferId));
        client->txPrebufferMsec = prebufferMsec.value(txPrebufferId);
    }

    std::string name;
    bool enabled = true;
    bool running = false;
    
    double freq;
    bool serverBusy = false;

    float datarate = 0;
    float frametimeCounter = 0;

    char hostname[1024];
    int port = 50000;
    long long lastStartSent = 0L;
    long long lastStopSent = 0L;
    std::string devConfName = "";

    dsp::stream<dsp::complex_t> stream;
    SourceManager::SourceHandler handler;

    OptionList<std::string, dsp::compression::PCMType> sampleTypeList;
    OptionList<std::string, server::CompressionType> compressionTypeList;
    OptionList<std::string, int> prebufferMsec;
    OptionList<std::string, int> serverResample;
    char securePassword[65] = {0};
    int sampleTypeId;
    int compressionTypeId;
    int txPrebufferId;
    int rxPrebufferId;
    int rxResampleId;
    float lossFactor = 10;
    float noiseMultiplerDB = 0;
    std::shared_ptr<server::Client> client;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    def["hostname"] = "localhost";
    def["port"] = 5259;
    def["servers"] = json::object();
    config.setPath(std::string(core::getRoot()) + "/sdrpp_server_source_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new SDRPPServerSourceModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(ModuleManager::Instance* instance) {
    delete (SDRPPServerSourceModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}