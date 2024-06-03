#include <utils/networking.h>
#include <imgui.h>
#include <module.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <regex>
#include <signal_path/signal_path.h>
#include <core.h>
#include <config.h>
#include <websocket.h>
#include <iostream>
#include <ctm.h>
#define CONCAT(a, b) ((std::string(a) + b).c_str())

#define MAX_COMMAND_LENGTH 8192



SDRPP_MOD_INFO{
    /* Name:            */ "tci_server",
    /* Description:     */ "Implementing TCI",
    /* Author:          */ "san",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

enum {
    RECORDER_TYPE_RECORDER,
    RECORDER_TYPE_METEOR_DEMODULATOR
};

static const char* initStr = "2023/01/21 15:15:48 < protocol:esdr,1.6;\n"
                             "2023/01/21 15:15:48 < device:SDRPlusPlusBrown;\n"
                             "2023/01/21 15:15:48 < receive_only:false;\n"
                             "2023/01/21 15:15:48 < trx_count:1;\n"
                             "2023/01/21 15:15:48 < channels_count:2;\n"
                             "2023/01/21 15:15:48 < vfo_limits:0,1600000000;\n"
                             "2023/01/21 15:15:48 < if_limits:-19531,19531;\n"
                             "2023/01/21 15:15:48 < modulations_list:am,sam,dsb,lsb,usb,cw,nfm,digl,digu,wfm,drm;\n"
                             "2023/01/21 15:15:48 < start;\n"
                             //                             "2023/01/21 15:15:48 < dds:0,1900000;\n"
                             //                             "2023/01/21 15:15:48 < dds:1,1900000;\n"
                             //                             "2023/01/21 15:15:48 < if:0,0,0;\n"
                             //                             "2023/01/21 15:15:48 < if:0,1,0;\n"
                             //                             "2023/01/21 15:15:48 < if:1,0,0;\n"
                             //                             "2023/01/21 15:15:48 < if:1,1,0;\n"
                             "2023/01/21 15:15:48 < modulation:0,lsb;\n"
                             //                             "2023/01/21 15:15:48 < modulation:1,lsb;\n"
                             "2023/01/21 15:15:48 < rx_filter_band:0,-3000,-70;\n"
                             //                             "2023/01/21 15:15:48 < rx_filter_band:1,-3000,-70;\n"
                             "2023/01/21 15:15:48 < rx_enable:0,true;\n"
                             //                             "2023/01/21 15:15:48 < rx_enable:1,false;\n"
                             "2023/01/21 15:15:48 < rx_channel_enable:0,0,true;\n"
                             //                             "2023/01/21 15:15:48 < rx_channel_enable:0,1,false;\n"
                             //                             "2023/01/21 15:15:48 < rx_channel_enable:1,0,true;\n"
                             //                             "2023/01/21 15:15:48 < rx_channel_enable:1,1,false;\n"
                             "2023/01/21 15:15:48 < tx_enable:0,true;\n"
                             //                             "2023/01/21 15:15:48 < tx_enable:1,true;\n"
                             "2023/01/21 15:15:48 < trx:0,false;\n"
                             //                             "2023/01/21 15:15:48 < trx:1,false;\n"
                             //                             "2023/01/21 15:15:48 < tune:0,false;\n"
                             //                             "2023/01/21 15:15:48 < tune:1,false;\n"
                             "2023/01/21 15:15:48 < iq_samplerate:48000;\n"
                             "2023/01/21 15:15:48 < iq_start:0;\n"
                             //                             "2023/01/21 15:15:48 < iq_start:1;\n"
                             "2023/01/21 15:15:48 < audio_samplerate:48000;\n";


ConfigManager config;
std::atomic_bool moduleRunning;

typedef struct
{
    //!< номер приёмника
    uint32_t receiver;
    uint32_t sampleRate; //!< частота дискретизации
    uint32_t format;     //!< всегда равен 4 (float 32 bit)
    uint32_t codec;      //!< алгоритм сжатия (не реализовано), всегда 0
    uint32_t crc;        //!< контрольная сумма (не реализовано), всегда 0
    uint32_t length;     //!< длина поля данных
    uint32_t type;       //!< тип потока данных
    uint32_t reserv[9];  //!< зарезервировано
    float data[4096];    //!< поле данных
} DataStream;

std::vector<std::string> split(const std::string& str, const std::string& regex_str) {
    std::regex regexz(regex_str);
    std::sregex_token_iterator token_iter(str.begin(), str.end(), regexz, -1);
    std::sregex_token_iterator end;
    std::vector<std::string> list;
    while (token_iter != end) {
        list.emplace_back(*token_iter++);
    }
    return list;
}


class TCIServerModule : public ModuleManager::Instance {

    EventHandler<std::shared_ptr<SinkManager::StreamHook>> onStreamHandler;

public:
    TCIServerModule(std::string name) : server(this) {
        this->name = name;

        config.acquire();
        if (!config.conf.contains(name)) {
            config.conf[name]["host"] = "127.0.0.1";
            config.conf[name]["port"] = 50001;
            config.conf[name]["tuning"] = true;
            config.conf[name]["recording"] = false;
            config.conf[name]["autoStart"] = false;
            config.conf[name]["vfo"] = "";
            config.conf[name]["recorder"] = "";
        }
        std::string host = config.conf[name]["host"];
        strcpy(hostname, host.c_str());
        port = config.conf[name]["port"];
        tuningEnabled = config.conf[name]["tuning"];
        recordingEnabled = config.conf[name]["recording"];
        autoStart = config.conf[name]["autoStart"];
        selectedVfo = config.conf[name]["vfo"];
        selectedRecorder = config.conf[name]["recorder"];
        config.release(true);

        gui::menu.registerEntry(name, menuHandler, this, NULL);
        onStreamHandler.handler = onStreamEvent;
        onStreamHandler.ctx = this;
        sigpath::sinkManager.onStream.bindHandler(&onStreamHandler);

        std::thread streamReader([=]() {
            while (true) {
                auto r = audioDataStream.read();
                if (r < 0) {
                    break;
                }
                this->server.connectionsLock.lock();
                for (auto& conn : server.connections) {
                    conn->user_data.outgoingDataLock.lock();
                    auto prevSize = conn->user_data.outgoingData.size();
                    conn->user_data.outgoingData.resize(prevSize + r);
                    std::copy(audioDataStream.readBuf, audioDataStream.readBuf + r, conn->user_data.outgoingData.begin() + prevSize);
                    conn->user_data.outgoingDataLock.unlock();
                }
                this->server.connectionsLock.unlock();
                static int count;
                audioDataStream.flush();
                if (count++ % 10000 == 1) {
                    flog::info("....stream data exists...");
                }
            }
        });
        streamReader.detach();
    }

    dsp::stream<dsp::stereo_t> audioDataStream;
    int audioDataSampleRate;
    std::mutex audioDataLock;
    std::vector<dsp::stereo_t> audioDataBuffer;

    static void onStreamEvent(std::shared_ptr<SinkManager::StreamHook> event, void* ctx) {
        auto _this = (TCIServerModule*)ctx;
        _this->audioDataSampleRate = event->sampleRate;
        if (event->sourceType == SinkManager::StreamHook::SOURCE_DEMOD_OUTPUT && event->source == _this->selectedVfo) {
            std::copy(event->stereoData->begin(), event->stereoData->end(), _this->audioDataStream.writeBuf);
            _this->audioDataStream.swap(event->stereoData->size());
        }
    };


    ~TCIServerModule() {
        audioDataStream.stopReader();
        sigpath::sinkManager.onStream.unbindHandler(&onStreamHandler);
        gui::menu.removeEntry(name);
        sigpath::vfoManager.onVfoCreated.unbindHandler(&vfoCreatedHandler);
        sigpath::vfoManager.onVfoDeleted.unbindHandler(&vfoDeletedHandler);
        core::moduleManager.onInstanceCreated.unbindHandler(&modChangedHandler);
        core::moduleManager.onInstanceDeleted.unbindHandler(&modChangedHandler);
        wsserver.reset();
    }

    void postInit() {
        // Refresh modules
        refreshModules();

        // Select VFO and recorder from config
        selectVfoByName(selectedVfo);
        selectRecorderByName(selectedRecorder);

        // Bind handlers
        vfoCreatedHandler.handler = _vfoCreatedHandler;
        vfoCreatedHandler.ctx = this;
        vfoDeletedHandler.handler = _vfoDeletedHandler;
        vfoDeletedHandler.ctx = this;
        modChangedHandler.handler = _modChangeHandler;
        modChangedHandler.ctx = this;
        sigpath::vfoManager.onVfoCreated.bindHandler(&vfoCreatedHandler);
        sigpath::vfoManager.onVfoDeleted.bindHandler(&vfoDeletedHandler);
        core::moduleManager.onInstanceCreated.bindHandler(&modChangedHandler);
        core::moduleManager.onInstanceDeleted.bindHandler(&modChangedHandler);

        // If autostart is enabled, start the server
        if (autoStart) { startServer(); }
    }

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
    static void menuHandler(void* ctx) {
        TCIServerModule* _this = (TCIServerModule*)ctx;
        float menuWidth = ImGui::GetContentRegionAvail().x;

        bool listening = (bool)_this->wsserver;

        if (listening) { style::beginDisabled(); }
        if (ImGui::InputText(CONCAT("##_tci_srv_host_", _this->name), _this->hostname, 1023)) {
            config.acquire();
            config.conf[_this->name]["host"] = std::string(_this->hostname);
            config.release(true);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputInt(CONCAT("##_tci_srv_port_", _this->name), &_this->port, 0, 0)) {
            config.acquire();
            config.conf[_this->name]["port"] = _this->port;
            config.release(true);
        }
        if (listening) { style::endDisabled(); }

        ImGui::LeftLabel("Controlled VFO");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        {
            std::lock_guard lck(_this->vfoMtx);
            if (ImGui::Combo(CONCAT("##_tci_srv_vfo_", _this->name), &_this->vfoId, _this->vfoNamesTxt.c_str())) {
                _this->selectVfoByName(_this->vfoNames[_this->vfoId], false);
                if (!_this->selectedVfo.empty()) {
                    config.acquire();
                    config.conf[_this->name]["vfo"] = _this->selectedVfo;
                    config.release(true);
                }
            }
        }

        ImGui::LeftLabel("Controlled Recorder");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        {
            std::lock_guard lck(_this->vfoMtx);
            if (ImGui::Combo(CONCAT("##_tci_srv_rec_", _this->name), &_this->recorderId, _this->recorderNamesTxt.c_str())) {
                _this->selectRecorderByName(_this->recorderNames[_this->recorderId], false);
                if (!_this->selectedRecorder.empty()) {
                    config.acquire();
                    config.conf[_this->name]["recorder"] = _this->selectedRecorder;
                    config.release(true);
                }
            }
        }

        ImGui::BeginTable(CONCAT("Stop##_tci_srv_tbl_", _this->name), 2);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Checkbox(CONCAT("Tuning##_tci_srv_tune_ena_", _this->name), &_this->tuningEnabled)) {
            config.acquire();
            config.conf[_this->name]["tuning"] = _this->tuningEnabled;
            config.release(true);
        }
        ImGui::TableSetColumnIndex(1);
        if (ImGui::Checkbox(CONCAT("Recording##_tci_srv_tune_ena_", _this->name), &_this->recordingEnabled)) {
            config.acquire();
            config.conf[_this->name]["recording"] = _this->recordingEnabled;
            config.release(true);
        }
        ImGui::EndTable();

        if (ImGui::Checkbox(CONCAT("Listen on startup##_tci_srv_auto_lst_", _this->name), &_this->autoStart)) {
            config.acquire();
            config.conf[_this->name]["autoStart"] = _this->autoStart;
            config.release(true);
        }

        if (listening && ImGui::Button(CONCAT("Stop##_tci_srv_stop_", _this->name), ImVec2(menuWidth, 0))) {
            _this->stopServer();
        }
        else if (!listening && ImGui::Button(CONCAT("Start##_tci_srv_stop_", _this->name), ImVec2(menuWidth, 0))) {
            _this->startServer();
        }

        ImGui::TextUnformatted("Status:");
        ImGui::SameLine();
        if (_this->wsserver && _this->server.clientCount > 0) {
            ImGui::TextColored(ImVec4(0.0, 1.0, 0.0, 1.0), "Connected: %d", _this->server.clientCount);
        }
        else if (_this->wsserver && _this->server.clientCount == 0) {
            ImGui::TextColored(ImVec4(1.0, 1.0, 0.0, 1.0), "Listening");
        }
        else {
            ImGui::TextUnformatted("Idle");
        }
    }

    struct CMDConnData {
        bool login;
        double reportedVFOOffset = 0;
        double reportedAudioSampleRate = 0;
        double reportedAudioStart = 0;
        std::vector<dsp::stereo_t> outgoingData;
        std::mutex outgoingDataLock;
        long long lastSend = 0;
        int sentPackets = 0;

        //        "2023/01/21 15:15:48 < vfo:0,0,1900000;\n"
        //        "2023/01/21 15:15:48 < vfo:0,1,1900000;\n"
        //        "2023/01/21 15:15:48 < vfo:1,0,1900000;\n"
        //        "2023/01/21 15:15:48 < vfo:1,1,1900000;\n"
    };


    struct Server {

        TCIServerModule* mod;

        Server(TCIServerModule* mod) {
            this->mod = mod;
        }

        using WSServer = websocket::WSServer<Server, CMDConnData, true>;
        using WSConn = WSServer::Connection;
        bool running;
        void onWSConnected(WSConn& conn, const char* request_uri, const char* host, const char* origin, const char* protocol,
                           const char* extensions, char* resp_protocol, uint32_t resp_protocol_size, char* resp_extensions,
                           uint32_t resp_extensions_size) {
            auto vec = split(initStr, "\n");
            for (const auto q : vec) {
                auto parts = split(q, " ");
                if (parts.size() == 4) {
                    auto part = parts[3];
                    sendCommand(conn, part);
                }
            };
            sendCommand(conn, "vfo:0,0," + std::to_string((long long)mod->getFrequency()) + ";");
            sendCommand(conn, "ready;");
            clientCount++;
        }

        std::vector<WSConn*> connections;
        std::mutex connectionsLock;

        void sendCommand(WSConn& conn, const std::string& command) {
            flog::info("TCI outgoing: {0}", command);
            conn.send(websocket::OPCODE_TEXT, (const uint8_t*)command.c_str(), command.size());
        }

        void sendData(WSConn& conn, const uint8_t* data, int len) {
            conn.send(websocket::OPCODE_BINARY, data, len);
        }

        bool onWSConnect(WSConn& conn, const char* request_uri, const char* host, const char* origin, const char* protocol,
                         const char* extensions, char* resp_protocol, uint32_t resp_protocol_size, char* resp_extensions,
                         uint32_t resp_extensions_size) {
            struct sockaddr_in addr;
            conn.getPeername(addr);
            flog::info("======= ws connection from: {0}:{1}", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
            connectionsLock.lock();
            connections.emplace_back(&conn);
            connectionsLock.unlock();
            return true;
        }

        void onWSClose(WSConn& conn, uint16_t status_code, const char* reason) {
            connectionsLock.lock();
            auto iter = std::find(connections.begin(), connections.end(), &conn);
            if (iter != connections.end()) {
                connections.erase(iter);
            }
            connectionsLock.unlock();
            flog::info("------- ws close, status_code: {0} reason: {1}", status_code, reason);
            clientCount--;
        }

        void onWSMsg(WSConn& conn, uint8_t opcode, const uint8_t* payload, uint32_t pl_len) {
            if (opcode == websocket::OPCODE_PING) {
                conn.send(websocket::OPCODE_PONG, payload, pl_len);
                return;
            }
            if (opcode != websocket::OPCODE_TEXT) {
                conn.close(1003, "not text msg");
                return;
            }
        }

        void onWSSegment(WSConn& conn, uint8_t opcode, const uint8_t* payload, uint32_t pl_len, uint32_t pl_start_idx,
                         bool fin) {
            std::string str((const char*)payload, pl_len);
            auto pos = str.find(":");
            if (pos != std::string::npos && str[str.size() - 1] == ';') {
                flog::info("TCI incoming cmd: {0}", str);
                auto cmd = str.substr(0, pos);
                str = str.substr(pos + 1);
                str.resize(str.size() - 1);
                auto args = split(str, ",");
                if (!onCommand(conn, cmd, args)) {
                }
            }
            else {
                flog::warn("unparsed TCI data: {0}", str);
            }
        }

        bool onCommand(WSConn& conn, const std::string& cmd, const std::vector<std::string>& args) {
            if (cmd == "trx" && args.size() == 2) {
                sendCommand(conn, cmd + ":" + args[0] + "," + args[1] + ";");
                return true;
            }
            if (cmd == "rx_smeter" && args.size() == 2) {
                sendCommand(conn, cmd + ":" + args[0] + "," + args[1] + "," + "0;");
                return true;
            }
            if (cmd == "vfo" && args.size() == 2) {
                conn.user_data.reportedVFOOffset = -1;   // will report
                return true;
            }
            if (cmd == "audio_start" && args.size() == 1) {
                conn.user_data.reportedAudioStart = 0;
                return true;
            }
            if (cmd == "vfo" && args.size() == 3) {
                long long freq = std::stoll(args[2]);
                conn.user_data.reportedVFOOffset = -1;
                tuner::tune(tuner::TUNER_MODE_NORMAL, mod->selectedVfo, freq);
                //                sendCommand(conn, cmd+":"+args[0]+","+args[1]+","+args[2]+";");
                return true;
            }
            return false;
        }
        int clientCount = 0;

        void reportChanges() {
            auto freq = (long long)mod->getFrequency();
            std::vector<WSConn*> connCopy;
            connectionsLock.lock();
            connCopy = connections;
            connectionsLock.unlock();
            for (auto& connection : connCopy) {
                if (freq != connection->user_data.reportedVFOOffset) {
                    sendCommand(*connection, "vfo:0,0," + std::to_string(freq) + ";");
                    connection->user_data.reportedVFOOffset = freq;
                }
            }
        }
        void stopAudioData(WSConn* conn) {
            sendCommand(*conn, "audio_stop:0;");
            conn->user_data.reportedAudioStart = 0;
        }

        void sendAudioData(WSConn* conn) {
            std::vector<dsp::stereo_t> toSend;
            std::lock_guard x(conn->user_data.outgoingDataLock);
            int currentSampleRate = mod->audioDataSampleRate;
            int toCopy = currentSampleRate / 60;
            DataStream ds;
            if (toCopy > sizeof(ds.data) / sizeof(ds.data[0]) / 2) {
                toCopy = sizeof(ds.data) / sizeof(ds.data[0]) / 2;
            }
            if (currentSampleRate != conn->user_data.reportedAudioSampleRate) {
                conn->user_data.reportedAudioSampleRate = currentSampleRate;
                sendCommand(*conn, "audio_samplerate:" + std::to_string(currentSampleRate) + ";");
                conn->user_data.outgoingData.clear();
                conn->user_data.lastSend = currentTimeMillis();
                return;
            }
            else {
                if (conn->user_data.outgoingData.size() >= toCopy) {
                    toSend.resize(toCopy);
                    std::copy(conn->user_data.outgoingData.begin(), conn->user_data.outgoingData.begin() + toCopy, toSend.begin());
                    conn->user_data.outgoingData.erase(conn->user_data.outgoingData.begin(), conn->user_data.outgoingData.begin() + toCopy);
                }
            }
            if (!toSend.empty()) {
                conn->user_data.lastSend = currentTimeMillis();
                if (!conn->user_data.reportedAudioStart) {
                    conn->user_data.reportedAudioStart = true;
                    sendCommand(*conn, "audio_start:0;");
                }
                ds.receiver = 0;
                ds.sampleRate = currentSampleRate;
                ds.format = 3;
                ds.codec = 0;
                ds.crc = 0;
                ds.length = toCopy * 2;
                ds.type = 1; //RxAudioStream
                for(int i=0; i<toSend.size(); i++) {
                    ds.data[2*i] = toSend[i].l/1e3;
                    ds.data[2*i+1] = toSend[i].r/1e3;
                }
//                memcpy(ds.data, toSend.data(), sizeof(toSend[0]) * toCopy);
                conn->user_data.sentPackets++;
                if (conn->user_data.sentPackets % 100 == 0) {
                    flog::info("Sent sound packets to TCI client: {0}", conn->user_data.sentPackets);
                }
                sendData(*conn, (const uint8_t*)&ds, sizeof(ds));
            }
        }
    };


    std::shared_ptr<Server::WSServer> wsserver;
    std::atomic_bool running;

    Server server;

    int getFrequency() {
        double freq = gui::waterfall.getCenterFrequency();

        // Add the offset of the VFO if it exists
        if (sigpath::vfoManager.vfoExists(selectedVfo)) {
            freq += sigpath::vfoManager.getOffset(selectedVfo);
        }
        return freq;
    }

    void startServer() {
        wsserver = std::make_shared<Server::WSServer>();
        if (!wsserver->init(hostname, port)) {
            flog::error("Could not start tci server: {0}", wsserver->getLastError());
            wsserver.reset();
            return;
        }
        running = true;
        auto ws_thr = std::thread([this]() {
            while (isRunning()) {
                if (wsserver->poll(&server)) {
                    std::this_thread::yield();
                }
                else {
                    auto ctm = currentTimeMillis();
                    std::vector<Server::WSConn*> selected;
                    std::vector<Server::WSConn*> unselected;
                    server.connectionsLock.lock();
                    for (auto& conn : server.connections) {
                        if (!conn->user_data.outgoingData.empty()) {
                            selected.emplace_back(conn);
                        }
                        else {
                            if (conn->user_data.lastSend != 0 && ctm - conn->user_data.lastSend > 500) {
                                unselected.emplace_back(conn);
                            }
                        }
                    }
                    server.connectionsLock.unlock();
                    for (auto& conn : unselected) {
                        server.stopAudioData(conn);
                    }
                    if (selected.empty()) {
                        #ifdef _WIN32
                        Sleep(10);
                        #else
                        usleep(10000);
                        #endif
                    }
                    else {
                        for (auto& conn : selected) {
                            server.sendAudioData(conn);
                        }
                    }
                    if (isRunning()) {
                        server.reportChanges();
                    }
                }
            }
        });
        ws_thr.detach();
    }
    bool isRunning() const {
        return running.load(std::memory_order_relaxed) && moduleRunning.load(std::memory_order_relaxed);
    }

    void stopServer() {
        running = false;
        wsserver.reset();
    }

    void refreshModules() {
        vfoNames.clear();
        vfoNamesTxt.clear();
        recorderNames.clear();
        recorderNamesTxt.clear();

        // List recording capable modules
        for (auto const& [_name, inst] : core::moduleManager.instances) {
            std::string mod = core::moduleManager.getInstanceModuleName(_name);
            if (mod != "recorder" && mod != "meteor_demodulator") { continue; }
            recorderNames.push_back(_name);
            recorderNamesTxt += _name;
            recorderNamesTxt += '\0';
        }

        // List VFOs
        for (auto const& [_name, vfo] : gui::waterfall.vfos) {
            vfoNames.push_back(_name);
            vfoNamesTxt += _name;
            vfoNamesTxt += '\0';
        }
    }

    void selectVfoByName(std::string _name, bool lock = true) {
        if (vfoNames.empty()) {
            if (lock) { std::lock_guard lck(vfoMtx); }
            selectedVfo.clear();
            return;
        }

        // Find the ID of the VFO, if not found, select first VFO in the list
        auto vfoIt = std::find(vfoNames.begin(), vfoNames.end(), _name);
        if (vfoIt == vfoNames.end()) {
            selectVfoByName(vfoNames[0]);
            return;
        }

        // Select the VFO
        {
            if (lock) { std::lock_guard lck(vfoMtx); }
            vfoId = std::distance(vfoNames.begin(), vfoIt);
            selectedVfo = _name;
        }
    }

    void selectRecorderByName(std::string _name, bool lock = true) {
        if (recorderNames.empty()) {
            if (lock) { std::lock_guard lck(recorderMtx); }
            selectedRecorder.clear();
            return;
        }

        // Find the ID of the VFO, if not found, select first VFO in the list
        auto recIt = std::find(recorderNames.begin(), recorderNames.end(), _name);
        if (recIt == recorderNames.end()) {
            selectRecorderByName(recorderNames[0]);
            return;
        }

        std::string type = core::modComManager.getModuleName(_name);


        // Select the VFO
        {
            if (lock) { std::lock_guard lck(recorderMtx); }
            recorderId = std::distance(recorderNames.begin(), recIt);
            selectedRecorder = _name;
            if (type == "meteor_demodulator") {
                recorderType = RECORDER_TYPE_METEOR_DEMODULATOR;
            }
            else {
                recorderType = RECORDER_TYPE_RECORDER;
            }
        }
    }

    static void _vfoCreatedHandler(VFOManager::VFO* vfo, void* ctx) {
        TCIServerModule* _this = (TCIServerModule*)ctx;
        _this->refreshModules();
        _this->selectVfoByName(_this->selectedVfo);
    }

    static void _vfoDeletedHandler(std::string _name, void* ctx) {
        TCIServerModule* _this = (TCIServerModule*)ctx;
        _this->refreshModules();
        _this->selectVfoByName(_this->selectedVfo);
    }

    static void _modChangeHandler(std::string _name, void* ctx) {
        TCIServerModule* _this = (TCIServerModule*)ctx;
        _this->refreshModules();
        _this->selectRecorderByName(_this->selectedRecorder);
    }

    //    static void clientHandler(net::Conn _client, void* ctx) {
    //        SigctlServerModule* _this = (SigctlServerModule*)ctx;
    //        //flog::info("New client!");
    //
    //        _this->client = std::move(_client);
    //        _this->client->readAsync(1024, _this->dataBuf, dataHandler, _this, false);
    //        _this->client->waitForEnd();
    //        _this->client->close();
    //
    //        //flog::info("Client disconnected!");
    //
    //        _this->listener->acceptAsync(clientHandler, _this);
    //    }
    //
    //    static void dataHandler(int count, uint8_t* data, void* ctx) {
    //        SigctlServerModule* _this = (SigctlServerModule*)ctx;
    //
    //        for (int i = 0; i < count; i++) {
    //            if (data[i] == '\n') {
    //                _this->commandHandler(_this->command);
    //                _this->command.clear();
    //                continue;
    //            }
    //            if (_this->command.size() < MAX_COMMAND_LENGTH) { _this->command += (char)data[i]; }
    //        }
    //
    //        _this->client->readAsync(1024, _this->dataBuf, dataHandler, _this, false);
    //    }

    //    void commandHandler(std::string cmd) {
    //        std::string corr = "";
    //        std::vector<std::string> parts;
    //        bool lastWasSpace = false;
    //        std::string resp = "";
    //
    //        // Split command into parts and remove excess spaces
    //        for (char c : cmd) {
    //            if (lastWasSpace && c == ' ') { continue; }
    //            else if (c == ' ') {
    //                parts.push_back(corr);
    //                corr.clear();
    //                lastWasSpace = true;
    //            }
    //            else {
    //                lastWasSpace = false;
    //                corr += c;
    //            }
    //        }
    //        if (!corr.empty()) {
    //            parts.push_back(corr);
    //        }
    //
    //        // NOTE: THIS STUFF ISN'T THREADSAFE AND WILL LIKELY BREAK.
    //
    //        // If the command is empty, do nothing
    //        if (parts.size() == 0) { return; }
    //
    //        // If the command is a compound command, execute each one separately
    //        if (parts[0].size() > 1 && parts[0][0] != '\\' && parts[0] != "AOS" && parts[0] != "LOS") {
    //            std::string arguments;
    //            if (parts.size() > 1) { arguments = cmd.substr(parts[0].size()); }
    //            for (char c : parts[0]) {
    //                commandHandler(c + arguments);
    //            }
    //            return;
    //        }
    //
    //        flog::info("tci command: '{0}'", cmd);
    //
    //        // Otherwise, execute the command
    //        if (parts[0] == "F" || parts[0] == "\\set_freq") {
    //            std::lock_guard lck(vfoMtx);
    //
    //            // if number of arguments isn't correct, return error
    //            if (parts.size() != 2) {
    //                resp = "RPRT 1\n";
    //                client->write(resp.size(), (uint8_t*)resp.c_str());
    //                return;
    //            }
    //
    //            // If not controlling the VFO, return
    //            if (!tuningEnabled) {
    //                resp = "RPRT 0\n";
    //                client->write(resp.size(), (uint8_t*)resp.c_str());
    //                return;
    //            }
    //
    //            // Parse frequency and assign it to the VFO
    //            long long freq = std::stoll(parts[1]);
    //            tuner::tune(tuner::TUNER_MODE_NORMAL, selectedVfo, freq);
    //            resp = "RPRT 0\n";
    //            client->write(resp.size(), (uint8_t*)resp.c_str());
    //        }
    //        else if (parts[0] == "f" || parts[0] == "\\get_freq") {
    //            std::lock_guard lck(vfoMtx);
    //
    //            // Get center frequency of the SDR
    //            double freq = gui::waterfall.getCenterFrequency();
    //
    //            // Add the offset of the VFO if it exists
    //            if (sigpath::vfoManager.vfoExists(selectedVfo)) {
    //                freq += sigpath::vfoManager.getOffset(selectedVfo);
    //            }
    //
    //            // Respond with the frequency
    //            char buf[128];
    //            sprintf(buf, "%" PRIu64 "\n", (uint64_t)freq);
    //            client->write(strlen(buf), (uint8_t*)buf);
    //        }
    //        else if (parts[0] == "M" || parts[0] == "\\set_mode") {
    //            std::lock_guard lck(vfoMtx);
    //            resp = "RPRT 0\n";
    //
    //            // If client is querying, respond accordingly
    //            if (parts.size() >= 2 && parts[1] == "?") {
    //                resp = "FM WFM AM DSB USB CW LSB RAW\n";
    //                client->write(resp.size(), (uint8_t*)resp.c_str());
    //                return;
    //            }
    //
    //            // if number of arguments isn't correct, return error
    //            if (parts.size() != 3) {
    //                resp = "RPRT 1\n";
    //                client->write(resp.size(), (uint8_t*)resp.c_str());
    //                return;
    //            }
    //
    //            // Check that the bandwidth is an integer (0 or -1 for default bandwidth)
    //            int pos = 0;
    //            for (char c : parts[2]) {
    //                if (!std::isdigit(c) && !(c == '-' && !pos)) {
    //                    resp = "RPRT 1\n";
    //                    client->write(resp.size(), (uint8_t*)resp.c_str());
    //                    return;
    //                }
    //                pos++;
    //            }
    //
    //            float newBandwidth = std::atoi(parts[2].c_str());
    //
    //            int newMode;
    //            if (parts[1] == "FM") {
    //                newMode = RADIO_IFACE_MODE_NFM;
    //            }
    //            else if (parts[1] == "WFM") {
    //                newMode = RADIO_IFACE_MODE_WFM;
    //            }
    //            else if (parts[1] == "AM") {
    //                newMode = RADIO_IFACE_MODE_AM;
    //            }
    //            else if (parts[1] == "DSB") {
    //                newMode = RADIO_IFACE_MODE_DSB;
    //            }
    //            else if (parts[1] == "USB") {
    //                newMode = RADIO_IFACE_MODE_USB;
    //            }
    //            else if (parts[1] == "CW") {
    //                newMode = RADIO_IFACE_MODE_CW;
    //            }
    //            else if (parts[1] == "LSB") {
    //                newMode = RADIO_IFACE_MODE_LSB;
    //            }
    //            else if (parts[1] == "RAW") {
    //                newMode = RADIO_IFACE_MODE_RAW;
    //            }
    //            else {
    //                resp = "RPRT 1\n";
    //                client->write(resp.size(), (uint8_t*)resp.c_str());
    //                return;
    //            }
    //
    //            // If tuning is enabled, set the mode and optionally the bandwidth
    //            if (!selectedVfo.empty() && core::modComManager.getModuleName(selectedVfo) == "radio" && tuningEnabled) {
    //                core::modComManager.callInterface(selectedVfo, RADIO_IFACE_CMD_SET_MODE, &newMode, NULL);
    //                if (newBandwidth > 0) {
    //                    core::modComManager.callInterface(selectedVfo, RADIO_IFACE_CMD_SET_BANDWIDTH, &newBandwidth, NULL);
    //                }
    //            }
    //
    //            client->write(resp.size(), (uint8_t*)resp.c_str());
    //        }
    //        else if (parts[0] == "m" || parts[0] == "\\get_mode") {
    //            std::lock_guard lck(vfoMtx);
    //            resp = "RAW\n";
    //
    //            if (!selectedVfo.empty() && core::modComManager.getModuleName(selectedVfo) == "radio") {
    //                int mode;
    //                core::modComManager.callInterface(selectedVfo, RADIO_IFACE_CMD_GET_MODE, NULL, &mode);
    //
    //                if (mode == RADIO_IFACE_MODE_NFM) {
    //                    resp = "FM\n";
    //                }
    //                else if (mode == RADIO_IFACE_MODE_WFM) {
    //                    resp = "WFM\n";
    //                }
    //                else if (mode == RADIO_IFACE_MODE_AM) {
    //                    resp = "AM\n";
    //                }
    //                else if (mode == RADIO_IFACE_MODE_DSB) {
    //                    resp = "DSB\n";
    //                }
    //                else if (mode == RADIO_IFACE_MODE_USB) {
    //                    resp = "USB\n";
    //                }
    //                else if (mode == RADIO_IFACE_MODE_CW) {
    //                    resp = "CW\n";
    //                }
    //                else if (mode == RADIO_IFACE_MODE_LSB) {
    //                    resp = "LSB\n";
    //                }
    //            }
    //
    //            if (!selectedVfo.empty()) {
    //                resp += std::to_string((int)sigpath::vfoManager.getBandwidth(selectedVfo)) + "\n";
    //            }
    //            else {
    //                resp += "0\n";
    //            }
    //
    //            client->write(resp.size(), (uint8_t*)resp.c_str());
    //        }
    //        else if (parts[0] == "V" || parts[0] == "\\set_vfo") {
    //            std::lock_guard lck(vfoMtx);
    //            resp = "RPRT 0\n";
    //
    //            // if number of arguments isn't correct or the VFO is not "VFO", return error
    //            if (parts.size() != 2) {
    //                resp = "RPRT 1\n";
    //                client->write(resp.size(), (uint8_t*)resp.c_str());
    //                return;
    //            }
    //
    //            if (parts[1] == "?") {
    //                resp = "VFO\n";
    //            }
    //            else if (parts[1] != "VFO") {
    //                resp = "RPRT 1\n";
    //            }
    //
    //            client->write(resp.size(), (uint8_t*)resp.c_str());
    //        }
    //        else if (parts[0] == "v" || parts[0] == "\\get_vfo") {
    //            std::lock_guard lck(vfoMtx);
    //            resp = "VFO\n";
    //            client->write(resp.size(), (uint8_t*)resp.c_str());
    //        }
    //        else if (parts[0] == "\\chk_vfo") {
    //            std::lock_guard lck(vfoMtx);
    //            resp = "CHKVFO 0\n";
    //            client->write(resp.size(), (uint8_t*)resp.c_str());
    //        }
    //        else if (parts[0] == "s") {
    //            std::lock_guard lck(vfoMtx);
    //            resp = "0\nVFOA\n";
    //            client->write(resp.size(), (uint8_t*)resp.c_str());
    //        }
    //        else if (parts[0] == "S") {
    //            std::lock_guard lck(vfoMtx);
    //            resp = "RPRT 0\n";
    //            client->write(resp.size(), (uint8_t*)resp.c_str());
    //        }
    //        else if (parts[0] == "AOS" || parts[0] == "\\recorder_start") {
    //            std::lock_guard lck(recorderMtx);
    //
    //            // If not controlling the recorder, return
    //            if (!recordingEnabled) {
    //                resp = "RPRT 0\n";
    //                client->write(resp.size(), (uint8_t*)resp.c_str());
    //                return;
    //            }
    //
    //            // Send the command to the selected recorder
    //            if (recorderType == RECORDER_TYPE_METEOR_DEMODULATOR) {
    //                core::modComManager.callInterface(selectedRecorder, METEOR_DEMODULATOR_IFACE_CMD_START, NULL, NULL);
    //            }
    //            else {
    //                core::modComManager.callInterface(selectedRecorder, RECORDER_IFACE_CMD_START, NULL, NULL);
    //            }
    //
    //            // Respond with a success
    //            resp = "RPRT 0\n";
    //            client->write(resp.size(), (uint8_t*)resp.c_str());
    //        }
    //        else if (parts[0] == "LOS" || parts[0] == "\\recorder_stop") {
    //            std::lock_guard lck(recorderMtx);
    //
    //            // If not controlling the recorder, return
    //            if (!recordingEnabled) {
    //                resp = "RPRT 0\n";
    //                client->write(resp.size(), (uint8_t*)resp.c_str());
    //                return;
    //            }
    //
    //            // Send the command to the selected recorder
    //            if (recorderType == RECORDER_TYPE_METEOR_DEMODULATOR) {
    //                core::modComManager.callInterface(selectedRecorder, METEOR_DEMODULATOR_IFACE_CMD_STOP, NULL, NULL);
    //            }
    //            else {
    //                core::modComManager.callInterface(selectedRecorder, RECORDER_IFACE_CMD_STOP, NULL, NULL);
    //            }
    //
    //            // Respond with a success
    //            resp = "RPRT 0\n";
    //            client->write(resp.size(), (uint8_t*)resp.c_str());
    //        }
    //        else if (parts[0] == "q" || parts[0] == "\\quit") {
    //            // Will close automatically
    //        }
    //        else if (parts[0] == "\\start") {
    //            gui::mainWindow.setPlayState(true);
    //        }
    //        else if (parts[0] == "\\stop") {
    //            gui::mainWindow.setPlayState(false);
    //        }
    //        else if (parts[0] == "\\dump_state") {
    //            std::lock_guard lck(vfoMtx);
    //            resp =
    //                /* tci protocol version */
    //                "0\n"
    //                /* tci model */
    //                "2\n"
    //                /* ITU region */
    //                "1\n"
    //                /* RX/TX frequency ranges
    //                * start, end, modes, low_power, high_power, vfo, ant
    //                *  start/end - Start/End frequency [Hz]
    //                *  modes - Bit field of RIG_MODE's (AM|AMS|CW|CWR|USB|LSB|FM|WFM)
    //                *  low_power/high_power - Lower/Higher RF power in mW,
    //                *                         -1 for no power (ie. rx list)
    //                *  vfo - VFO list equipped with this range (RIG_VFO_A)
    //                *  ant - Antenna list equipped with this range, 0 means all
    //                *  FIXME: get limits from receiver
    //                */
    //                "0.000000 10000000000.000000 0x2ef -1 -1 0x1 0x0\n"
    //                /* End of RX frequency ranges. */
    //                "0 0 0 0 0 0 0\n"
    //                /* End of TX frequency ranges. The SDR++ is reciver only. */
    //                "0 0 0 0 0 0 0\n"
    //                /* Tuning steps: modes, tuning_step */
    //                "0xef 1\n"
    //                "0xef 0\n"
    //                /* End of tuning steps */
    //                "0 0\n"
    //                /* Filter sizes: modes, width
    //                * FIXME: get filter sizes from presets
    //                */
    //                "0x82 500\n"    /* CW | CWR normal */
    //                "0x82 200\n"    /* CW | CWR narrow */
    //                "0x82 2000\n"   /* CW | CWR wide */
    //                "0x221 10000\n" /* AM | AMS | FM normal */
    //                "0x221 5000\n"  /* AM | AMS | FM narrow */
    //                "0x221 20000\n" /* AM | AMS | FM wide */
    //                "0x0c 2700\n"   /* SSB normal */
    //                "0x0c 1400\n"   /* SSB narrow */
    //                "0x0c 3900\n"   /* SSB wide */
    //                "0x40 160000\n" /* WFM normal */
    //                "0x40 120000\n" /* WFM narrow */
    //                "0x40 200000\n" /* WFM wide */
    //                /* End of filter sizes  */
    //                "0 0\n"
    //                /* max_rit  */
    //                "0\n"
    //                /* max_xit */
    //                "0\n"
    //                /* max_ifshift */
    //                "0\n"
    //                /* Announces (bit field list) */
    //                "0\n" /* RIG_ANN_NONE */
    //                /* Preamp list in dB, 0 terminated */
    //                "0\n"
    //                /* Attenuator list in dB, 0 terminated */
    //                "0\n"
    //                /* Bit field list of get functions */
    //                "0\n" /* RIG_FUNC_NONE */
    //                /* Bit field list of set functions */
    //                "0\n" /* RIG_FUNC_NONE */
    //                /* Bit field list of get level */
    //                "0x40000020\n" /* RIG_LEVEL_SQL | RIG_LEVEL_STRENGTH */
    //                /* Bit field list of set level */
    //                "0x20\n" /* RIG_LEVEL_SQL */
    //                /* Bit field list of get parm */
    //                "0\n" /* RIG_PARM_NONE */
    //                /* Bit field list of set parm */
    //                "0\n" /* RIG_PARM_NONE */;
    //            client->write(resp.size(), (uint8_t*)resp.c_str());
    //        }
    //        else {
    //            // If command is not recognized, return error
    //            flog::error("tci client sent invalid command: '{0}'", cmd);
    //            resp = "RPRT 1\n";
    //            client->write(resp.size(), (uint8_t*)resp.c_str());
    //            return;
    //        }
    //    }

    std::string name;
    bool enabled = true;

    char hostname[1024];
    int port = 4532;
    uint8_t dataBuf[1024];

    std::string command = "";

    EventHandler<std::string> modChangedHandler;
    EventHandler<VFOManager::VFO*> vfoCreatedHandler;
    EventHandler<std::string> vfoDeletedHandler;

    std::vector<std::string> vfoNames;
    std::string vfoNamesTxt;
    std::vector<std::string> recorderNames;
    std::string recorderNamesTxt;
    std::mutex vfoMtx;
    std::mutex recorderMtx;

    std::string selectedVfo = "";
    std::string selectedRecorder = "";
    int vfoId = 0;
    int recorderId = 0;
    int recorderType = RECORDER_TYPE_RECORDER;

    bool tuningEnabled = true;
    bool recordingEnabled = false;
    bool autoStart = false;
};

MOD_EXPORT void _INIT_() {
    moduleRunning = true;
    config.setPath(std::string(core::getRoot()) + "/tci_server_config.json");
    config.load(json::object());
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new TCIServerModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (TCIServerModule*)instance;
}

MOD_EXPORT void _END_() {
    moduleRunning = false;
    config.disableAutoSave();
    config.save();
}
