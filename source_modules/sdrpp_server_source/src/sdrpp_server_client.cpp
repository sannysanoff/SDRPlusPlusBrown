#include "sdrpp_server_client.h"
#include <volk/volk.h>
#include <cstring>
#include <signal_path/signal_path.h>
#include <utils/flog.h>
#include <core.h>
#include "dsp/compression/experimental_fft_decompressor.h"
#include <gui/tuner.h>
#include "utils/pbkdf2_sha256.h"
#include <utils/wav.h>

using namespace std::chrono_literals;

namespace server {


    struct RemoteTransmitter : public Transmitter {
        Client* client;
        bool remoteTxPressed = false;
        long long remoteTxPressedWhen = 0;
        const int *rxPrebufferMillis = nullptr;     // only pointers
        const int *txPrebufferMillis = nullptr;

        RemoteTransmitter(Client* client, const std::string someState) : client(client) {
            parseState(someState);
        }

        void sendTransmitAction(const json &action) {
            auto str = action.dump();
            strcpy((char*)client->s_cmd_data, str.c_str());
            client->sendCommand(COMMAND_TRANSMIT_ACTION, str.length()+1); // including zero, for conv.
        }

        bool txStatus = false;

        void setTransmitStatus(bool status) override {
            auto rv = json({});
            rv["transmitStatus"] = status;
            sendTransmitAction(rv);
            txStatus = status;
        }

        dsp::stream<dsp::complex_t> *txstream;

        static const int  TX_SEND_BUFFER_SAMPLES = 2400;
        void setTransmitStream(dsp::stream<dsp::complex_t> *astream) override {
            std::thread([this, astream]() {
                SetThreadName("sdrpp_server_client.txstream");
                dsp::multirate::RationalResampler<dsp::complex_t> txStreamDownsampler;
                txStreamDownsampler.init(nullptr, 48000, TX_WIRE_SAMPLERATE);
                auto debug = true;
                std::vector<dsp::complex_t> buffer;
                int addedBlocks = 0;
                int readSamples = 0;
                int nreads = 0;
                wav::ComplexDumper txDumpHi(48000, "/tmp/ssc_audio_hf.raw");
                wav::ComplexDumper txDump(48000, "/tmp/ssc_audio_lf.raw");
                while (true) {
                    int rd = astream->read();
                    if (sigpath::transmitter == nullptr) {
                        break; // transmitter died (sdrpp server disconnected, for example)
                    }
                    if (rd < 0) {
                        printf("End iq stream for tx");
                        break;
                    }
                    readSamples += rd;
                    nreads++;
                    for (int q = 0; q < rd; q++) {
                        buffer.push_back(astream->readBuf[q]);
                        if (buffer.size() == TX_SEND_BUFFER_SAMPLES) {
                            txDumpHi.dump(buffer.data(), buffer.size());
                            auto compressedCnt = txStreamDownsampler.process(buffer.size(), buffer.data(), txStreamDownsampler.out.writeBuf);
                            flog::info("tx data sending out: resample from {} to {}", (int)buffer.size(), (int)compressedCnt);
                            buffer.resize(compressedCnt);
                            memcpy(buffer.data(), txStreamDownsampler.out.writeBuf, compressedCnt * sizeof(dsp::complex_t));
                            txDump.dump(buffer.data(), buffer.size());
                            sendBuffer(buffer);
                            buffer.clear();
                        }
                    }
                    astream->flush();
                }
                if (sigpath::transmitter != nullptr) {
                    if (buffer.size() > 0) {
                        sendBuffer(buffer);
                    }
                    buffer.clear(); // send zero-sized buffer to indicate end stream;
                    sendBuffer(buffer);
                }
            }).detach();
        }

        uint8_t txStreamBuffer[TX_SEND_BUFFER_SAMPLES * sizeof(dsp::complex_t) + 1024];

        void sendBuffer(std::vector<dsp::complex_t> buffer) {
            if (client == nullptr) return;
            if (client->isOpen()) {
                PacketHeader* s_pkt_hdr = (PacketHeader*)&txStreamBuffer[0];
                uint8_t* s_pkt_data = txStreamBuffer + sizeof(PacketHeader);
                s_pkt_hdr->type = PACKET_TYPE_TRANSMIT_DATA;            // transmitted as Int16 with float multiplier sent in front of data.
                int nsamples = buffer.size();
                s_pkt_hdr->size = sizeof(PacketHeader) + 2 * nsamples * sizeof(int16_t) + sizeof(float);
                float maxx = 0;
                for(int z=0; z<nsamples; z++) {
                    if (fabs(buffer[z].re) > maxx) {
                        maxx = fabs(buffer[z].re);
                    }
                    if (fabs(buffer[z].im) > maxx) {
                        maxx = fabs(buffer[z].im);
                    }
                }
                if (maxx == 0) {
                    maxx = 1; // all zeros
                }
                int16_t *iptr = (int16_t *)(s_pkt_data + sizeof(float));
                float *fptr = (float *)(s_pkt_data);
                *fptr = maxx; // store multiplier
                for(int z=0; z<nsamples; z++) {
                    iptr[2*z] = (int16_t)(buffer[z].re * 32767 / maxx);
                    iptr[2*z+1] = (int16_t)(buffer[z].im * 32767 / maxx);
                }
                //memcpy(s_pkt_data, buffer.data(), buffer.size() * sizeof(dsp::complex_t));
                client->sock->send(txStreamBuffer, s_pkt_hdr->size);
                client->bytes += s_pkt_hdr->size;

            }
        }

        unsigned char softwareGain = 0;
        unsigned char hardwareGain = 0;

        void setTransmitSoftwareGain(unsigned char gain) override {
            auto rv = json({});
            rv["transmitSoftwareGain"] = gain;
            sendTransmitAction(rv);
            softwareGain = gain;
        }
        void setTransmitHardwareGain(unsigned char gain) override {
            auto rv = json({});
            rv["transmitHardwareGain"] = gain;
            sendTransmitAction(rv);
            hardwareGain = gain;
        }
        unsigned char getTransmitHardwareGain() override {
            return hardwareGain;
        }
        void setTransmitFrequency(int freq) override {
            auto rv = json({});
            rv["transmitFrequency"] = freq;
            sendTransmitAction(rv);
        };

        bool prevPAEnabled = false;
        void setPAEnabled(bool enabled) override {
            if (enabled != prevPAEnabled) {
                auto rv = json({});
                rv["paEnabled"] = enabled;
                sendTransmitAction(rv);
                prevPAEnabled = enabled;
            }
        }

        int getTXStatus() override {
            bool actualRemoteTx = remoteTxPressed;
            long long int ctm = currentTimeMillis();
            if (txPrebufferMillis && rxPrebufferMillis && remoteTxPressedWhen > ctm - *rxPrebufferMillis - *txPrebufferMillis) {
                actualRemoteTx = !actualRemoteTx;       // comes in effect only after buffer time passes, so invert.
            }
            return txStatus || actualRemoteTx;
        }

        float transmitPower = 0;
        float reflectedPower = 0;
        float swr = 0;
        float getTransmitPower() override {
            return transmitPower;
        }
        float getReflectedPower() override {
            return reflectedPower;
        }
        float getTransmitSWR() override {
            return swr;
        }

        int normalZone = 5;
        int redZone = 10;
        int getNormalZone() override {
            return normalZone;
        }
        int getRedZone() override {
            return redZone;
        }

        std::string transmitterName = "not set";
        std::string &getTransmitterName() override {
            return transmitterName;
        }

        void parseState(const std::string &state) {
            try {
                auto s = json::parse(state);
                normalZone = s["normalZone"];
                redZone = s["redZone"];
                reflectedPower = s["reflectedPower"];
                transmitPower = s["transmitPower"];
                swr = s["swr"];
                hardwareGain = s["hardwareGain"];
                transmitterName = s["transmitterName"];
            } catch (std::exception &e) {
                return;
            }
        }

        virtual ~RemoteTransmitter() {
            // stream must be stopped outside prior to deletion.
//            if (txstream) {
//                txstream->stopReader(); // thread will terminate
//            }
        }



    };

    Client::Client(std::shared_ptr<net::Socket> sock, dsp::stream<dsp::complex_t>* out) {
        this->sock = sock;
        output = out;

        // Allocate buffers
        rbuffer = new uint8_t[SERVER_MAX_PACKET_SIZE];
        sbuffer = new uint8_t[SERVER_MAX_PACKET_SIZE];

        // Initialize headers
        r_pkt_hdr = (PacketHeader*)rbuffer;
        r_pkt_data = &rbuffer[sizeof(PacketHeader)];
        r_cmd_hdr = (CommandHeader*)r_pkt_data;
        r_cmd_data = &rbuffer[sizeof(PacketHeader) + sizeof(CommandHeader)];

        s_pkt_hdr = (PacketHeader*)sbuffer;
        s_pkt_data = &sbuffer[sizeof(PacketHeader)];
        s_cmd_hdr = (CommandHeader*)s_pkt_data;
        s_cmd_data = &sbuffer[sizeof(PacketHeader) + sizeof(CommandHeader)];

        // Initialize decompressor
        dctx = ZSTD_createDCtx();

        // Initialize DSP
        decompIn.setBufferSize(STREAM_BUFFER_SIZE*sizeof(dsp::complex_t) + 8);
        decompIn.clearWriteStop();
        decomp.init(&decompIn);
        fftDecompressor.init(&decomp.out);
        prebufferer.init(&fftDecompressor.out);
        link.init(&prebufferer.out, output);
        prebufferer.start();
        decomp.start();
        fftDecompressor.start();
        link.start();

        // Start worker thread
        workerThread = std::thread(&Client::worker, this);

        // Ask for a UI
        int res = getUI();
        if (res < 0) {
            // Close client
            close();

            // Throw error
            switch (res) {
            case CONN_ERR_TIMEOUT:
                throw std::runtime_error("Timed out");
            case CONN_ERR_BUSY:
                throw std::runtime_error("Server busy");
            default:
                throw std::runtime_error("Unknown error");
            }
        }
    }

    Client::~Client() {
        close();
        ZSTD_freeDCtx(dctx);
        delete[] rbuffer;
        delete[] sbuffer;
    }

    void Client::showMenu() {
        std::string diffId = "";
        SmGui::DrawListElem diffValue;
        bool syncRequired = false;
        {
            std::lock_guard<std::mutex> lck(dlMtx);
            dl.draw(diffId, diffValue, syncRequired);
        }

        if (!diffId.empty()) {
            // Save ID
            SmGui::DrawListElem elemId;
            elemId.type = SmGui::DRAW_LIST_ELEM_TYPE_STRING;
            elemId.str = diffId;

            // Encore packet
            int size = 0;
            s_cmd_data[size++] = syncRequired;
            size += SmGui::DrawList::storeItem(elemId, &s_cmd_data[size], SERVER_MAX_PACKET_SIZE - size);
            size += SmGui::DrawList::storeItem(diffValue, &s_cmd_data[size], SERVER_MAX_PACKET_SIZE - size);

            // Send
            if (syncRequired) {
                flog::warn("Action requires resync");
//                auto waiter = awaitCommandAck(COMMAND_UI_ACTION);
                sendCommand(COMMAND_UI_ACTION, size);
//                if (waiter->await(PROTOCOL_TIMEOUT_MS)) {
//                    std::lock_guard lck(dlMtx);
//                    dl.load(r_cmd_data, r_pkt_hdr->size - sizeof(PacketHeader) - sizeof(CommandHeader));
//                }
//                else {
//                    flog::error("Timeout out after asking for UI");
//                }
//                waiter->handled();
                flog::warn("Resync done");
            }
            else {
                flog::warn("Action does not require resync");
                sendCommand(COMMAND_UI_ACTION, size);
            }
        }


    }

    void Client::setFrequency(double freq) {
        if (!isOpen()) { return; }
        *(double*)s_cmd_data = freq;
        sendCommand(COMMAND_SET_FREQUENCY, sizeof(double));
//        auto waiter = awaitCommandAck(COMMAND_SET_FREQUENCY);
//        waiter->await(PROTOCOL_TIMEOUT_MS);
//        waiter->handled();
    }

    double Client::getSampleRate() {
        return currentSampleRate;
    }

    void Client::setSampleType(dsp::compression::PCMType type) {
        if (!isOpen()) { return; }
        s_cmd_data[0] = type;
        sendCommand(COMMAND_SET_SAMPLE_TYPE, 1);
    }

    void Client::setCompressionType(CompressionType type) {
        if (!isOpen()) { return; }
        s_cmd_data[0] = type == CompressionType::CT_LEGACY ? 1 : 0;
        sendCommand(COMMAND_SET_COMPRESSION, 1);
        s_cmd_data[0] = type == CompressionType::CT_LOSSY ? 1 : 0;
        sendCommand(COMMAND_SET_FFTZSTD_COMPRESSION, 1);
    }

    void Client::setLossFactor(double mult) {
        if (!isOpen()) { return; }
        (*(double *)&s_cmd_data[0]) = mult;
        sendCommand(COMMAND_SET_EFFT_LOSS_RATE, 8);
    }

    void Client::setNoiseMultiplierDB(double multDB) {
        fftDecompressor.noiseMultiplierDB = multDB;
    }


    void Client::start() {
        if (!isOpen()) { return; }
        prebufferer.setPrebufferMsec(rxPrebufferMsec);
        prebufferer.setSampleRate(currentSampleRate);
        prebufferer.clear();
        fftDecompressor.noiseFigure.clear();
        int32_t *sr = (int32_t *)&s_cmd_data[0];
        *sr = requestedSampleRate;
        sendCommand(COMMAND_SET_SAMPLERATE, sizeof(int32_t));
        StartCommandArguments sca;
        sca.magic = SDRPP_BROWN_MAGIC;
        if (!challenge.empty()) {
            if (hmacKeyToUse.empty()) {
                flog::error("No HMAC key provided");
                return;
            }
            HMAC_SHA256_CTX ctx;
            hmac_sha256_init(&ctx, (uint8_t *)hmacKeyToUse.data(), hmacKeyToUse.size());
            hmac_sha256_update(&ctx, (uint8_t *)challenge.data(), challenge.size());
            uint8_t hmac[256 / 8];
            hmac_sha256_final(&ctx, hmac);
            memcpy(sca.signedChallenge, hmac, 256 / 8);
        }
        sca.magic = server::SDRPP_BROWN_MAGIC;
        sca.txPrebufferMsec = txPrebufferMsec;
        memcpy(s_cmd_data, &sca, sizeof(sca));
        sendCommand(COMMAND_START, sizeof(sca));
        getUI();
    }

    void Client::stop() {
        if (!isOpen()) { return; }
        sendCommand(COMMAND_STOP, 0);
        getUI();
        prebufferer.clear();
    }

    void Client::close() {
        // Stop worker
        decompIn.stopWriter();
        if (sock) { sock->close(); }
        if (workerThread.joinable()) { workerThread.join(); }
        decompIn.clearWriteStop();

        // Stop DSP
        decomp.stop();
        fftDecompressor.stop();
        prebufferer.stop();
        link.stop();
        if (sigpath::transmitter) {
            delete sigpath::transmitter;
            sigpath::transmitter = nullptr;
        }
    }

    bool Client::isOpen() {
        return sock && sock->isOpen();
    }

    long long lastReportedTime = 0;

    void updateStreamTime(Client *client) {
        long long currentTime = currentTimeMillis() - client->getBufferTimeDelay();
        if (currentTime > lastReportedTime) {
            sigpath::iqFrontEnd.setCurrentStreamTime(currentTime);
            lastReportedTime = currentTime;
        }

    }

    void Client::worker() {
        while (true) {

            idle();

            // Receive header
            if (sock->recv(rbuffer, sizeof(PacketHeader), true) <= 0) {
                break;
            }

            // Receive remaining data
            if (sock->recv(&rbuffer[sizeof(PacketHeader)], r_pkt_hdr->size - sizeof(PacketHeader), true, PROTOCOL_TIMEOUT_MS) <= 0) {
                break;
            }



            // Increment data counter
            bytes += r_pkt_hdr->size;

            // Decode packet
            if (r_pkt_hdr->type == PACKET_TYPE_COMMAND) {
                // TODO: Move to command handler
                if (r_cmd_hdr->cmd == COMMAND_SET_SAMPLERATE && r_pkt_hdr->size == sizeof(PacketHeader) + sizeof(CommandHeader) + sizeof(double)) {
                    currentSampleRate = *(double*)r_cmd_data;
                    core::setInputSampleRate(currentSampleRate);
                    prebufferer.setSampleRate(currentSampleRate);
                } else if (r_cmd_hdr->cmd == COMMAND_GET_UI) {
                    // unsolicited UI
                    std::lock_guard lck(dlMtx);
                    dl.load(r_cmd_data, r_pkt_hdr->size - sizeof(PacketHeader) - sizeof(CommandHeader));
                } else if (r_cmd_hdr->cmd == COMMAND_SECURE_CHALLENGE) {
                    secureChallengeReceived = currentTimeMillis();
                    challenge.resize(256 / 8);
                    memcpy(challenge.data(), r_cmd_data, challenge.size());
                } else if (r_cmd_hdr->cmd == COMMAND_SET_FREQUENCY) {
                    double freq = *(double *) r_cmd_data;
                    tuner::tune(tuner::TUNER_MODE_IQ_ONLY, "", freq);
                } else if (r_cmd_hdr->cmd == COMMAND_TRANSMIT_ACTION) {
                    // state from server
                    std::string str = std::string((char*)r_cmd_data, r_pkt_hdr->size - sizeof(PacketHeader) - sizeof(CommandHeader));
                    try {
                        auto j = json::parse(str);
                        if (j.contains("transmitStatus")) {
                            bool remoteTx = j["transmitStatus"];
                            auto rt = ((RemoteTransmitter *)sigpath::transmitter);
                            rt->remoteTxPressed = remoteTx;
                            rt->remoteTxPressedWhen = currentTimeMillis();
                            flog::info("From remote: tx = {}", remoteTx);
                        }
                    } catch(std::exception &ex) {
                        flog::info("json parse exception: {}", ex.what());
                    }

                } else if (r_cmd_hdr->cmd == COMMAND_EFFT_NOISE_FIGURE) {
                    int nbytes = r_pkt_hdr->size - sizeof(PacketHeader) - sizeof(CommandHeader);
                    int nelems = nbytes / sizeof(float);
                    std::vector<float >figure(nelems);
                    memcpy(figure.data(), r_cmd_data, nbytes);
                    fftDecompressor.setNoiseFigure(figure);
                } else if (r_cmd_hdr->cmd == COMMAND_SET_TRANSMITTER_SUPPORTED) {
                    transmitterSupported = 1;
                    if (!sigpath::transmitter) {
                        std::string str = std::string((char*)r_cmd_data, r_pkt_hdr->size - sizeof(PacketHeader) - sizeof(CommandHeader));
                        auto rt = new RemoteTransmitter(this, str);
                        rt->rxPrebufferMillis = &prebufferer.prebufferMsec;
                        rt->txPrebufferMillis = &txPrebufferMsec;
                        sigpath::transmitter = rt;
                    }
                } else if (r_cmd_hdr->cmd == COMMAND_SET_TRANSMITTER_NOT_SUPPORTED) {
                    transmitterSupported = 0;
                    if (sigpath::transmitter) {
                        delete sigpath::transmitter;
                        sigpath::transmitter = nullptr;
                    }
                }
                else if (r_cmd_hdr->cmd == COMMAND_DISCONNECT) {
                    flog::error("Asked to disconnect by the server");
                    serverBusy = true;

                    // Cancel waiters
                    std::vector<PacketWaiter*> toBeRemoved;
                    for (auto& [waiter, cmd] : commandAckWaiters) {
                        waiter->cancel();
                        toBeRemoved.push_back(waiter);
                    }

                    // Remove handled waiters
                    for (auto& waiter : toBeRemoved) {
                        commandAckWaiters.erase(waiter);
                        delete waiter;
                    }
                }
            }
            else if (r_pkt_hdr->type == PACKET_TYPE_COMMAND_ACK) {
                // Notify waiters
                std::vector<PacketWaiter*> toBeRemoved;
                for (auto& [waiter, cmd] : commandAckWaiters) {
                    if (cmd != r_cmd_hdr->cmd) { continue; }
                    waiter->notify();
                    toBeRemoved.push_back(waiter);
                }

                // Remove handled waiters
                for (auto& waiter : toBeRemoved) {
                    commandAckWaiters.erase(waiter);
                    delete waiter;
                }
            }
            else if (r_pkt_hdr->type == PACKET_TYPE_BASEBAND) {
                fftDecompressor.setEnabled(false);
                memcpy(decompIn.writeBuf, &rbuffer[sizeof(PacketHeader)], r_pkt_hdr->size - sizeof(PacketHeader));
                if (!decompIn.swap(r_pkt_hdr->size - sizeof(PacketHeader))) { break; }
                updateStreamTime(this);
            }
            else if (r_pkt_hdr->type == PACKET_TYPE_BASEBAND_EXPERIMENTAL_FFT) {
                fftDecompressor.setEnabled(true);
                size_t outCount = ZSTD_decompressDCtx(dctx, decompIn.writeBuf, STREAM_BUFFER_SIZE*sizeof(dsp::complex_t)+8, r_pkt_data, r_pkt_hdr->size - sizeof(PacketHeader));
                if (!decompIn.swap(outCount)) { break; }
                updateStreamTime(this);
            }
            else if (r_pkt_hdr->type == PACKET_TYPE_BASEBAND_COMPRESSED) {
                fftDecompressor.setEnabled(false);
                size_t outCount = ZSTD_decompressDCtx(dctx, decompIn.writeBuf, STREAM_BUFFER_SIZE*sizeof(dsp::complex_t)+8, r_pkt_data, r_pkt_hdr->size - sizeof(PacketHeader));
                if (outCount) {
                    if (!decompIn.swap(outCount)) { break; }
                };
                updateStreamTime(this);
            }
            else if (r_pkt_hdr->type == PACKET_TYPE_ERROR) {
                flog::error("SDR++ Server Error: {0}", rbuffer[sizeof(PacketHeader)]);
            }
            else {
                flog::error("Invalid packet type: {0}", r_pkt_hdr->type);
            }

        }
    }

    int Client::getUI() {
        if (!isOpen()) { return -1; }
//        auto waiter = awaitCommandAck(COMMAND_GET_UI);
        sendCommand(COMMAND_GET_UI, 0);
//        if (waiter->await(PROTOCOL_TIMEOUT_MS)) {
//            std::lock_guard lck(dlMtx);
//            dl.load(r_cmd_data, r_pkt_hdr->size - sizeof(PacketHeader) - sizeof(CommandHeader));
//        }
//        else {
//            if (!serverBusy) { flog::error("Timeout out after asking for UI"); };
//            waiter->handled();
//            return serverBusy ? CONN_ERR_BUSY : CONN_ERR_TIMEOUT;
//        }
//        waiter->handled();
        return 0;
    }

    void Client::sendPacket(PacketType type, int len) {
        s_pkt_hdr->type = type;
        s_pkt_hdr->size = sizeof(PacketHeader) + len;
        sock->send(sbuffer, s_pkt_hdr->size);
        bytes += s_pkt_hdr->size;

    }

    void Client::sendCommand(Command cmd, int len) {
        s_cmd_hdr->cmd = cmd;
        sendPacket(PACKET_TYPE_COMMAND, sizeof(CommandHeader) + len);
    }

    void Client::sendCommandAck(Command cmd, int len) {
        s_cmd_hdr->cmd = cmd;
        sendPacket(PACKET_TYPE_COMMAND_ACK, sizeof(CommandHeader) + len);
    }

    PacketWaiter* Client::awaitCommandAck(Command cmd) {
        PacketWaiter* waiter = new PacketWaiter;
        commandAckWaiters[waiter] = cmd;
        return waiter;
    }

    void Client::dHandler(dsp::complex_t *data, int count, void *ctx) {
        Client* _this = (Client*)ctx;
        memcpy(_this->output->writeBuf, data, count * sizeof(dsp::complex_t));
        _this->output->swap(count);
    }

    std::shared_ptr<Client> connect(std::string host, uint16_t port, dsp::stream<dsp::complex_t>* out) {
        return std::make_shared<Client>(net::connect(host, port), out);
    }

    int prevTxStatus = 0;
    void Client::idle() {
        auto rt = ((RemoteTransmitter *)sigpath::transmitter);
        if (rt) {
            int nowStatus = rt->getTXStatus();
            if (nowStatus != prevTxStatus) {
                flog::info("Emit TX status: {}", nowStatus);
                sigpath::txState.emit(nowStatus);
                prevTxStatus = nowStatus;
            }
        }
    }


}
