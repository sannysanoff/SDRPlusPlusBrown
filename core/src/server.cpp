#include "server.h"
#include "core.h"
#include <utils/flog.h>
#include <version.h>
#include <config.h>
#include <filesystem>
#include <dsp/types.h>
#include <utils/wav.h>
#include <signal_path/signal_path.h>
#include <gui/smgui.h>
#include <utils/optionlist.h>
#include "dsp/compression/sample_stream_compressor.h"
#include <dsp/buffer/prebuffer.h>
#include <dsp/buffer/packer.h>
#include "dsp/compression/experimental_fft_compressor.h"
#include "dsp/sink/handler_sink.h"
#include "dsp/loop/agc.h"
#include "dsp/multirate/rational_resampler.h"
#include <zstd.h>

#ifdef __linux__
#include <signal.h>
#include <csignal>
#endif

#define PBKDF2_SHA256_IMPLEMENTATION
#include "utils/pbkdf2_sha256.h"

namespace server {
    dsp::stream<dsp::complex_t> dummyInput("server::dummyInput");
    dsp::multirate::RationalResampler<dsp::complex_t> forcedResampler;
    dsp::compression::ExperimentalFFTCompressor fftCompressor;
    dsp::compression::SampleStreamCompressor comp;
    dsp::sink::Handler<uint8_t> hnd;
    net::Conn client;
    uint8_t* rbuf = NULL;
    uint8_t* sbuf = NULL;
    uint8_t* bbuf = NULL;

    PacketHeader* r_pkt_hdr = NULL;
    uint8_t* r_pkt_data = NULL;
    CommandHeader* r_cmd_hdr = NULL;
    uint8_t* r_cmd_data = NULL;

    PacketHeader* s_pkt_hdr = NULL;
    uint8_t* s_pkt_data = NULL;
    CommandHeader* s_cmd_hdr = NULL;
    uint8_t* s_cmd_data = NULL;

    PacketHeader* bb_pkt_hdr = NULL;
    uint8_t* bb_pkt_data = NULL;

    SmGui::DrawListElem dummyElem;

    ZSTD_CCtx* cctx;

    net::Listener listener;

    OptionList<std::string, std::string> sourceList;
    int sourceId = 0;
    bool running = false;
    bool compression = false;
    double sampleRate = 1000000.0;
    int forcedSampleRate = 0;

    std::vector<int8_t> authSigningKey;
    std::string challenge;

    static const int CLIENT_CAPS_BASEDATA_METADATA = 0x0001;        // wants frequency and samplerate along with each IQ batch (otherwise, network latency decouples freq request from baseband)
    static const int CLIENT_CAPS_FFT_WANTED = 0x0002;        // not yet done

    StartCommandArguments startCommandArguments;
    double lastTunedFrequency = 0;
    double lastCallbackFrequency = -1;
    double lastSampleRate = 0;

    bool transmitDataRunning = false;
    bool txPressed = false;
    dsp::stream<dsp::complex_t> transmitDataStream;
    dsp::multirate::RationalResampler<dsp::complex_t> txStreamUpsampler;
    dsp::buffer::Prebuffer<dsp::complex_t> transmitPrebufferer;
    dsp::buffer::Packer<dsp::complex_t> transmitPacker;

    int main() {
        flog::info("=====| SERVER MODE |=====");
#ifdef __linux__
        signal(SIGPIPE, SIG_IGN);
#endif


        std::string password = (std::string)core::args["password"];
        if (password != "") {
            HMAC_SHA256_CTX pbkdf_hmac;
            authSigningKey.resize(256 / 8);
            flog::info("Computing auth signing key..");
            pbkdf2_sha256(&pbkdf_hmac, (uint8_t *)password.data(), password.length(), (uint8_t*)passwordSalt.data(), passwordSalt.length(), 20000, (uint8_t*)authSigningKey.data(), authSigningKey.size());
        }

            // Init DSP
        forcedResampler.init(&dummyInput, 1000000, 48000);
        fftCompressor.init(&forcedResampler.out);
        fftCompressor.setEnabled(true);
        comp.init(&fftCompressor.out, dsp::compression::PCM_TYPE_I8);
        hnd.init(&comp.out, _testServerHandler, NULL);
        rbuf = new uint8_t[SERVER_MAX_PACKET_SIZE];
        sbuf = new uint8_t[SERVER_MAX_PACKET_SIZE];
        bbuf = new uint8_t[SERVER_MAX_PACKET_SIZE];
        comp.start();
        hnd.start();
        fftCompressor.start();
        forcedResampler.start();

        if (true) {
            txStreamUpsampler.init(&transmitDataStream, TX_WIRE_SAMPLERATE, 48000);
            txStreamUpsampler.start();
            transmitPrebufferer.init(&txStreamUpsampler.out);
        } else {
            transmitPrebufferer.init(&transmitDataStream);
        }
//        transmitPrebufferer.logging = true;
        transmitPrebufferer.start();
        transmitPacker.init(&transmitPrebufferer.out, 2048);
        transmitPacker.start();

        // Initialize headers
        r_pkt_hdr = (PacketHeader*)rbuf;
        r_pkt_data = &rbuf[sizeof(PacketHeader)];
        r_cmd_hdr = (CommandHeader*)r_pkt_data;
        r_cmd_data = &rbuf[sizeof(PacketHeader) + sizeof(CommandHeader)];

        s_pkt_hdr = (PacketHeader*)sbuf;
        s_pkt_data = &sbuf[sizeof(PacketHeader)];
        s_cmd_hdr = (CommandHeader*)s_pkt_data;
        s_cmd_data = &sbuf[sizeof(PacketHeader) + sizeof(CommandHeader)];

        bb_pkt_hdr = (PacketHeader*)bbuf;
        bb_pkt_data = &bbuf[sizeof(PacketHeader)];

        // Initialize compressor
        cctx = ZSTD_createCCtx();

        // Load config
        core::configManager.acquire();
        std::string modulesDir = core::configManager.conf["modulesDirectory"];
        std::vector<std::string> modules = core::configManager.conf["modules"];
        auto modList = core::configManager.conf["moduleInstances"].items();
        std::string sourceName = core::configManager.conf["source"];
        core::configManager.release();
        modulesDir = std::filesystem::absolute(modulesDir).string();

        // Initialize SmGui in server mode
        SmGui::init(true);

        flog::info("Loading modules");
        // Load modules and check type to only load sources ( TODO: Have a proper type parameter int the info )
        // TODO LATER: Add whitelist/blacklist stuff
        if (std::filesystem::is_directory(modulesDir)) {
            for (const auto& file : std::filesystem::directory_iterator(modulesDir)) {
                std::string path = file.path().generic_string();
                std::string fn = file.path().filename().string();
                if (file.path().extension().generic_string() != SDRPP_MOD_EXTENTSION) {
                    continue;
                }
                if (!file.is_regular_file()) { continue; }
                if (fn.find("source") == std::string::npos) { continue; }

                flog::info("Loading {0}", path);
                core::moduleManager.loadModule(path);
            }
        }
        else {
            flog::warn("Module directory {0} does not exist, not loading modules from directory", modulesDir);
        }

        // Load additional modules through the config ( TODO: Have a proper type parameter int the info )
        // TODO LATER: Add whitelist/blacklist stuff
        for (auto const& apath : modules) {
            std::filesystem::path file = std::filesystem::absolute(apath);
            std::string path = file.generic_string();
            std::string fn = file.filename().string();
            if (file.extension().generic_string() != SDRPP_MOD_EXTENTSION) {
                continue;
            }
            if (!std::filesystem::is_regular_file(file)) { continue; }
            if (fn.find("source") == std::string::npos) { continue; }

            flog::info("Loading {0}", path);
            core::moduleManager.loadModule(path);
        }
        // Create module instances
        for (auto const& [name, _module] : modList) {
            std::string mod = _module["module"];
            bool enabled = _module["enabled"];
            if (core::moduleManager.modules.find(mod) == core::moduleManager.modules.end()) { continue; }
            flog::info("Initializing {0} ({1})", name, mod);
            core::moduleManager.createInstance(name, mod);
            if (!enabled) { core::moduleManager.disableInstance(name); }
        }

        // Do post-init
        core::moduleManager.doPostInitAll();

        // Generate source list
        auto list = sigpath::sourceManager.getSourceNames();
        for (auto& name : list) {
            sourceList.define(name, name);
        }

        // Load sourceId from config
        sourceId = 0;
        if (sourceList.keyExists(sourceName)) { sourceId = sourceList.keyId(sourceName); }
        sigpath::sourceManager.selectSource(sourceList[sourceId]);

        // TODO: Use command line option
        std::string host = (std::string)core::args["addr"];
        int port = (int)core::args["port"];
        listener = net::listen(host, port);
        listener->acceptAsync(_clientHandler, NULL);

        flog::info("Ready, listening on {0}:{1}", host, port);
        while(1) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); }

        return 0;
    }

    std::string transmitterStatusToString(Transmitter *transmitter) {
        auto rv = json({});
        rv["normalZone"] = transmitter->getNormalZone();
        rv["redZone"] = transmitter->getRedZone();
        rv["reflectedPower"] = transmitter->getReflectedPower();
        rv["swr"] = transmitter->getTransmitSWR();
        rv["transmitPower"] = transmitter->getTransmitPower();
        rv["hardwareGain"] = transmitter->getTransmitHardwareGain();
        rv["transmitterName"] = transmitter->getTransmitterName();
        return rv.dump();
    }

    void maybeSendTransmitterState() {
        if (sigpath::transmitter) {
            auto state = transmitterStatusToString(sigpath::transmitter);
            strcpy((char*)s_cmd_data, state.c_str());
            sendCommand(COMMAND_SET_TRANSMITTER_SUPPORTED, state.length()+1);
        } else {
            sendCommand(COMMAND_SET_TRANSMITTER_NOT_SUPPORTED, 0);
        }
    }

    void maybeSendChallenge() {
        if (authSigningKey.size() > 0) {
            challenge = std::to_string(currentTimeMillis());
            challenge.resize(256 / 8, ' ');
            memcpy(s_cmd_data, challenge.c_str(), challenge.size());
            sendCommand(COMMAND_SECURE_CHALLENGE, challenge.size());
        }
    }

    void _clientHandler(net::Conn conn, void* ctx) {
        // Reject if someone else is already connected
        if (client && client->isOpen()) {
            if (running) {
                flog::info("REJECTED Connection from {0}, another client is already connected.", conn->getPeerName());

                // Issue a disconnect command to the client
                uint8_t buf[sizeof(PacketHeader) + sizeof(CommandHeader)];
                PacketHeader *tmp_phdr = (PacketHeader *) buf;
                CommandHeader *tmp_chdr = (CommandHeader *) &buf[sizeof(PacketHeader)];
                tmp_phdr->size = sizeof(PacketHeader) + sizeof(CommandHeader);
                tmp_phdr->type = PACKET_TYPE_COMMAND;
                tmp_chdr->cmd = COMMAND_DISCONNECT;
                conn->write(tmp_phdr->size, buf);

                // TODO: Find something cleaner
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                conn->close();

                // Start another async accept
                listener->acceptAsync(_clientHandler, NULL);
                return;
            } else {
                // idle existing client is sent away.
                client->close();
                client.reset();
            }
        }

        flog::info("Connection from {0}", conn->getPeerName());
        client = std::move(conn);
        client->readAsync(sizeof(PacketHeader), rbuf, _packetHandler, NULL);

        // Perform settings reset
        sigpath::sourceManager.stop();
        comp.setPCMType(dsp::compression::PCM_TYPE_I16);
        compression = false;

        sendSampleRate(forcedSampleRate != 0 ? forcedSampleRate: sampleRate);

        if (authSigningKey.size() > 0) {
            maybeSendChallenge();
        }
        maybeSendTransmitterState();


        // TODO: Wait otherwise someone else could connect

        listener->acceptAsync(_clientHandler, NULL);
    }

    void sendTransmitAction() {
        auto rv = json({});
        rv["transmitStatus"] = (bool)sigpath::transmitter->getTXStatus();;
        auto str = rv.dump();
        strcpy((char*)s_cmd_data, str.c_str());
        sendCommand(COMMAND_TRANSMIT_ACTION, str.length()+1); // including zero, for conv.
    }


    int lastSentTXStatus = 0;
    void setTxStatus(bool transmitFlag) {
        fftCompressor.setTxMode(transmitFlag);
        sigpath::transmitter->setTransmitStatus(transmitFlag);
        int currentTXStatus = sigpath::transmitter->getTXStatus();
        if (currentTXStatus != lastSentTXStatus) {
            sendTransmitAction();
            lastSentTXStatus = currentTXStatus;
        }
    }

    wav::ComplexDumper txDump(48000, "/tmp/svr_txaudio_in.raw");

    void _packetHandler(int count, uint8_t* buf, void* ctx) {
        PacketHeader* hdr = (PacketHeader*)buf;

        // Read the rest of the data (TODO: CHECK SIZE OR SHIT WILL BE FUCKED + ADD TIMEOUT)
        int len = 0;
        int read = 0;
        int goal = hdr->size - sizeof(PacketHeader);
        while (len < goal) {
            read = client->read(goal - len, &buf[sizeof(PacketHeader) + len]);
            if (read < 0) { return; };
            len += read;
        }

        // Parse and process
        if (hdr->type == PACKET_TYPE_TRANSMIT_DATA && sigpath::transmitter) {
            int nSamples = (hdr->size - sizeof(PacketHeader) - sizeof(float)) / (2*sizeof(int16_t));
            if (nSamples) {
                if (!transmitDataRunning) {
                    txDump.clear();
                    transmitDataRunning = true;
                    transmitPacker.out.clearReadStop();
                    transmitPrebufferer.setSampleRate(48000);
                    transmitPrebufferer.clear();
                    transmitPacker.clear();
                    transmitPrebufferer.setPrebufferMsec(startCommandArguments.txPrebufferMsec);
                    sigpath::transmitter->setTransmitStream(&transmitPacker.out);
                }
                float *fptr = (float *)(buf + sizeof(PacketHeader));
                float maxx = *fptr;
                int16_t *iptr = (int16_t *)(buf + sizeof(PacketHeader) + sizeof(float));
                for(int z=0; z<nSamples; z++) {
                    transmitDataStream.writeBuf[z] = { (iptr[2 * z] * maxx) / 32767.0f, (iptr[2 * z + 1] * maxx) / 32767.0f};
                }
                //memcpy(transmitDataStream.writeBuf, buf + sizeof(PacketHeader), nSamples * sizeof(dsp::complex_t));
                //txDump.dump(buf + sizeof(PacketHeader), nSamples);
//                flog::info("received samples for tx data: {}", (int)nSamples);
                transmitDataStream.swap(nSamples);
                if (txPressed && sigpath::transmitter->getTXStatus() == 0 && (transmitPrebufferer.bufferReached || startCommandArguments.txPrebufferMsec == 0)) {
                    flog::info("sigpath::transmitter->setTransmitStatus(true): buffer reached: {} {}",
                               (int) transmitPrebufferer.buffer.size(), (int) transmitPrebufferer.getNeededBufferSize());
                    setTxStatus(true);
                }
            } else {
                // end of data.
                if (transmitDataRunning) {
                    flog::info("sigpath::transmitter->setTransmitStatus(true): buffer reached: {} {}",
                               (int) transmitPrebufferer.buffer.size(), (int) transmitPrebufferer.getNeededBufferSize());
                    transmitDataRunning = false;
                }

            }
            maybeSendTransmitterState();
        } else if (hdr->type == PACKET_TYPE_COMMAND && hdr->size >= sizeof(PacketHeader) + sizeof(CommandHeader)) {
            CommandHeader* chdr = (CommandHeader*)&buf[sizeof(PacketHeader)];
            commandHandler((Command)chdr->cmd, &buf[sizeof(PacketHeader) + sizeof(CommandHeader)], hdr->size - sizeof(PacketHeader) - sizeof(CommandHeader));
        }
        else {
            sendError(ERROR_INVALID_PACKET);
        }

        // Start another async read
        client->readAsync(sizeof(PacketHeader), rbuf, _packetHandler, NULL);
    }

    int frameCount = 0;

    void _testServerHandler(uint8_t* data, int count, void* ctx) {
        frameCount++;
        // in main loop, stop TX when buffer has finished or when tx depressed+nobuffer
        if (sigpath::transmitter) {
            if (!txPressed && sigpath::transmitter->getTXStatus() == 1 && (!transmitPrebufferer.bufferReached || startCommandArguments.txPrebufferMsec == 0)) {
                flog::info("sigpath::transmitter->setTransmitStatus(false): buffer empty, stop transmitting.");
                transmitPacker.out.stopReader();
                setTxStatus(false);
            }
        }
        // Compress data if needed and fill out header fields
        if ((startCommandArguments.clientCapsRequested & CLIENT_CAPS_BASEDATA_METADATA)) {
            bb_pkt_hdr->type = PACKET_TYPE_BASEBAND_WITH_METADATA;
            StreamMetadata *sm = (StreamMetadata*)&bbuf[sizeof(PacketHeader)];
            sm->version = 1;
            sm->size = sizeof(StreamMetadata);
            sm->frequency = lastCallbackFrequency != -1 ? lastCallbackFrequency : lastTunedFrequency; // for any driver, supporting callback frequency or not.
            sm->sampleRate = lastSampleRate;
            sm->fftCompressed = fftCompressor.isEnabled();
            auto dataOffset = sizeof(PacketHeader) + sizeof(StreamMetadata);
            if (sm->fftCompressed) {
                count = ZSTD_compressCCtx(cctx, &bbuf[dataOffset], SERVER_MAX_PACKET_SIZE-dataOffset, data, count, 1);
            } else {
                memcpy(&bbuf[dataOffset], data, count);
            }
            bb_pkt_hdr->size = dataOffset + count;
        } else if (fftCompressor.isEnabled()) {
            bb_pkt_hdr->type = PACKET_TYPE_BASEBAND_EXPERIMENTAL_FFT;
            auto dataOffset = sizeof(PacketHeader);
            count = ZSTD_compressCCtx(cctx, &bbuf[dataOffset], SERVER_MAX_PACKET_SIZE-dataOffset, data, count, 1);
            bb_pkt_hdr->size = sizeof(PacketHeader) + count;
        } else if (compression) {
            bb_pkt_hdr->type = PACKET_TYPE_BASEBAND_COMPRESSED;
            bb_pkt_hdr->size = sizeof(PacketHeader) + (uint32_t)ZSTD_compressCCtx(cctx, &bbuf[sizeof(PacketHeader)], SERVER_MAX_PACKET_SIZE-sizeof(PacketHeader), data, count, 1);
        } else {
            bb_pkt_hdr->type = PACKET_TYPE_BASEBAND;
            bb_pkt_hdr->size = sizeof(PacketHeader) + count;
            memcpy(&bbuf[sizeof(PacketHeader)], data, count);
        }

        // Write to network
        if (client) {
            if (client->isOpen()) {
                client->write(bb_pkt_hdr->size, bbuf);
                if (fftCompressor.isEnabled() && frameCount % 20 == 1 && (sigpath::transmitter == nullptr || sigpath::transmitter->getTXStatus() == 0)) {
                    fftCompressor.sharedDataLock.lock();
                    auto nbytes = fftCompressor.noiseFigure.size() * sizeof(fftCompressor.noiseFigure[0]);
                    memcpy(s_cmd_data, fftCompressor.noiseFigure.data(), nbytes);
                    fftCompressor.sharedDataLock.unlock();
                    sendCommand(COMMAND_EFFT_NOISE_FIGURE, nbytes);
                }
            } else {
                sigpath::sourceManager.stop();
                running = false;
                client.reset();
                flog::error("Client is gone, stopping SDR.");
            }
        }
    }

    void updateResampler() {
        if (forcedSampleRate == 0) {
            forcedResampler.setRates(sampleRate, sampleRate);
            fftCompressor.setSampleRate(sampleRate);
        } else {
            forcedResampler.setRates(sampleRate, forcedSampleRate);
            fftCompressor.setSampleRate(forcedSampleRate);
        }
    }

    void setInput(dsp::stream<dsp::complex_t>* stream) {
        lastCallbackFrequency = -1;
        forcedResampler.setInput(stream);
    }

    void setInputCenterFrequencyCallback(int centerFrequency) {
        if (lastCallbackFrequency != centerFrequency) {
            flog::info("Confirmed frequency from device: {}", centerFrequency);
        }
        lastCallbackFrequency = centerFrequency;
    }

    void commandHandler(Command cmd, uint8_t* data, int len) {
        if (cmd == COMMAND_GET_UI) {
            sendUI(COMMAND_GET_UI, "", dummyElem);
        }
        else if (cmd == COMMAND_UI_ACTION && len >= 3) {
            // Check if sending back data is needed
            int i = 0;
            bool sendback = data[i++];
            len--;
            
            // Load id
            SmGui::DrawListElem diffId;
            int count = SmGui::DrawList::loadItem(diffId, &data[i], len);
            if (count < 0) { sendError(ERROR_INVALID_ARGUMENT); return; }
            if (diffId.type != SmGui::DRAW_LIST_ELEM_TYPE_STRING) { sendError(ERROR_INVALID_ARGUMENT); return; } 
            i += count;
            len -= count;

            // Load value
            SmGui::DrawListElem diffValue;
            count = SmGui::DrawList::loadItem(diffValue, &data[i], len);
            if (count < 0) { sendError(ERROR_INVALID_ARGUMENT); return; }
            i += count;
            len -= count;

            // Render and send back
            if (sendback) {
                sendUI(COMMAND_UI_ACTION, diffId.str, diffValue);
            }
            else {
                renderUI(NULL, diffId.str, diffValue);
            }
        }
        else if (cmd == COMMAND_SET_EFFT_LOSS_RATE) {
            if (len == 8) {
                double *pdata = (double*)data;
                fftCompressor.lossRate = pdata[0];
            }
        }
        else if (cmd == COMMAND_START) {
            if (len >= 8) {
                int32_t *pdata = (int32_t*)data;
                int32_t magic = pdata[0];
                if (magic != SDRPP_BROWN_MAGIC) {      // brown
                    // do nothing
                } else {
                    memset(&startCommandArguments, 0, sizeof startCommandArguments);
                    memcpy(&startCommandArguments, pdata, len);
                }
            }
            bool startAllowed = true;
            if (!authSigningKey.empty()) {
                if (challenge.size() == 0) {
                    flog::info("ASSERTION FAILED: challenge not produced");
                }
                HMAC_SHA256_CTX ctx;
                hmac_sha256_init(&ctx, (uint8_t *)authSigningKey.data(), authSigningKey.size());
                hmac_sha256_update(&ctx, (uint8_t *)challenge.data(), challenge.size());
                uint8_t hmac[256 / 8];
                hmac_sha256_final(&ctx, hmac);
                if (memcmp(hmac, startCommandArguments.signedChallenge, sizeof hmac)) {
                    // different?
                    startAllowed = false;
                    maybeSendChallenge();    // send new challenge. Password incorrect.
                }
            }
            if (startAllowed) {
                sigpath::sourceManager.start();
                running = true;
                maybeSendTransmitterState();
            }
        }
        else if (cmd == COMMAND_SET_SAMPLERATE) {
            forcedSampleRate = *(int32_t *)data;
            updateResampler();
            sendSampleRate(forcedSampleRate != 0 ? forcedSampleRate: sampleRate);
        }
        else if (cmd == COMMAND_STOP) {
            sigpath::sourceManager.stop();
            running = false;
            maybeSendTransmitterState();
            maybeSendChallenge();
        }
        else if (cmd == COMMAND_SET_FREQUENCY && len == 8) {
            lastTunedFrequency = *(double*)data;
            flog::info("Setting device to frequency: {}", (double)lastTunedFrequency);
            sigpath::sourceManager.tune(*(double*)data);
            sendCommandAck(COMMAND_SET_FREQUENCY, 0);
        }
        else if (cmd == COMMAND_SET_SAMPLE_TYPE && len == 1) {
            dsp::compression::PCMType type = (dsp::compression::PCMType)*(uint8_t*)data;
            comp.setPCMType(type);
        }
        else if (cmd == COMMAND_SET_COMPRESSION && len == 1) {
            compression = *(uint8_t*)data;
        }
        else if (cmd == COMMAND_SET_FFTZSTD_COMPRESSION && len == 1) {
            fftCompressor.setEnabled(*(uint8_t*)data);
        }
        else if (cmd == COMMAND_SET_EFFT_MASKED_FREQUENCIES) {
            std::vector<int32_t> freqs(len / sizeof(int32_t));
            memcpy(freqs.data(), data, len);
            fftCompressor.setMaskedFrequencies(freqs);
        }
        else if (cmd == COMMAND_TRANSMIT_ACTION && sigpath::transmitter != nullptr) {
            std::string str = std::string((char*)r_cmd_data, r_pkt_hdr->size - sizeof(PacketHeader) - sizeof(CommandHeader));
            try {
                auto j = json::parse(str);
                if (j.contains("transmitStatus")) {
                    txPressed = j["transmitStatus"];
                    flog::info("client transmit status requested: {}", txPressed);
                }
                if (j.contains("transmitSoftwareGain")) {
                    sigpath::transmitter->setTransmitSoftwareGain(j["transmitSoftwareGain"]);
                }
                if (j.contains("transmitHardwareGain")) {
                    sigpath::transmitter->setTransmitHardwareGain(j["transmitHardwareGain"]);
                }
                if (j.contains("transmitFrequency")) {
                    int txfreq = j["transmitFrequency"];
                    flog::info("set tx frequency: {}", txfreq);
                    sigpath::transmitter->setTransmitFrequency(txfreq);
                }
                if (j.contains("paEnabled")) {
                    sigpath::transmitter->setPAEnabled(j["paEnabled"]);
                }
            } catch(std::exception &ex) {
                flog::info("json parse exception: {}", ex.what());
            }
        }
        else {
            flog::error("Invalid Command: {0} (len = {1})", (int)cmd, len);
            sendError(ERROR_INVALID_COMMAND);
        }
    }

    void drawMenu() {
        if (running) { SmGui::BeginDisabled(); }
        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Combo("##sdrpp_server_src_sel", &sourceId, sourceList.txt)) {
            sigpath::sourceManager.selectSource(sourceList[sourceId]);
            core::configManager.acquire();
            core::configManager.conf["source"] = sourceList.key(sourceId);
            core::configManager.release(true);
        }
        if (running) { SmGui::EndDisabled(); }

        sigpath::sourceManager.showSelectedMenu();
    }


    void renderUI(SmGui::DrawList* dl, std::string diffId, SmGui::DrawListElem diffValue) {
        // If we're recording and there's an action, render once with the action and record without

        if (dl && !diffId.empty()) {
            SmGui::setDiff(diffId, diffValue);
            drawMenu();

            SmGui::setDiff("", dummyElem);
            SmGui::startRecord(dl);
            drawMenu();
            SmGui::stopRecord();
        }
        else {
            SmGui::setDiff(diffId, diffValue);
            SmGui::startRecord(dl);
            drawMenu();
            SmGui::stopRecord();
        }
    }

    void sendUI(Command originCmd, std::string diffId, SmGui::DrawListElem diffValue) {
        // Render UI
        SmGui::DrawList dl;
        renderUI(&dl, diffId, diffValue);

        // Create response
        int size = dl.getSize();
        dl.store(s_cmd_data, size);

        // Send to network
        //sendCommandAck(originCmd, size);
        sendCommand(COMMAND_GET_UI, size);
    }

    void sendUnsolicitedUI() {
        SmGui::DrawList dl;
        renderUI(&dl, "", dummyElem);
        int size = dl.getSize();
        dl.store(s_cmd_data, size);
        sendCommand(COMMAND_GET_UI, size);
    }

    void sendError(Error err) {
        PacketHeader* hdr = (PacketHeader*)sbuf;
        s_pkt_data[0] = err;
        sendPacket(PACKET_TYPE_ERROR, 1);
    }

    void sendSampleRate(double sampleRate) {
        *(double*)s_cmd_data = sampleRate;
        sendCommand(COMMAND_SET_SAMPLERATE, sizeof(double));
        lastSampleRate = sampleRate;
    }

    void sendCenterFrequency(double freq) {
        *(double*)s_cmd_data = freq;
        sendCommand(COMMAND_SET_FREQUENCY, sizeof(double));
    }

    void setInputSampleRate(double samplerate) {
        sampleRate = samplerate;
        updateResampler();
        if (!client || !client->isOpen()) { return; }
        sendSampleRate(forcedSampleRate != 0 ? forcedSampleRate: sampleRate);
    }

    void sendPacket(PacketType type, int len) {
        s_pkt_hdr->type = type;
        s_pkt_hdr->size = sizeof(PacketHeader) + len;
        client->write(s_pkt_hdr->size, sbuf);
    }

    void sendCommand(Command cmd, int len) {
        s_cmd_hdr->cmd = cmd;
        sendPacket(PACKET_TYPE_COMMAND, sizeof(CommandHeader) + len);
    }

    void sendCommandAck(Command cmd, int len) {
        s_cmd_hdr->cmd = cmd;
        sendPacket(PACKET_TYPE_COMMAND_ACK, sizeof(CommandHeader) + len);
    }
}
