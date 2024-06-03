#pragma once
#include <utils/net.h>
#include <dsp/stream.h>
#include <dsp/types.h>
#include <atomic>
#include <queue>
#include <server_protocol.h>
#include <atomic>
#include <map>
#include <vector>
#include <dsp/compression/sample_stream_decompressor.h>
#include <dsp/sink.h>
#include <dsp/routing/stream_link.h>
#include <dsp/buffer/prebuffer.h>
#include <zstd.h>
#include "dsp/compression/experimental_fft_decompressor.h"

#define PROTOCOL_TIMEOUT_MS             10000

namespace server {

    enum CompressionType {
        CT_NONE,
        CT_LEGACY,
        CT_LOSSY
    };


    class PacketWaiter {

    public:
        bool await(int timeout) {
            std::unique_lock lck(readyMtx);
            return readyCnd.wait_for(lck, std::chrono::milliseconds(timeout), [=](){ return dataReady || canceled; }) && !canceled;
        }

        void handled() {
            {
                std::lock_guard lck(handledMtx);
                dataHandled = true;
            }
            handledCnd.notify_all();
        }

        void notify() {
            // Tell waiter that data is ready
            {
                std::lock_guard lck(readyMtx);
                dataReady = true;
            }
            readyCnd.notify_all();

            // Wait for waiter to handle the request
            {
                std::unique_lock lck(readyMtx);
                handledCnd.wait(lck, [=](){ return dataHandled; });
            }
        }

        void cancel() {
            canceled = true;
            notify();
        }

        void reset() {
            std::lock_guard lck1(readyMtx);
            std::lock_guard lck2(handledMtx);
            dataReady = false;
            dataHandled = false;
            canceled = false;
        }

    private:
        bool dataReady = false;
        bool dataHandled = false;
        bool canceled = 0;
        
        std::condition_variable readyCnd;
        std::condition_variable handledCnd;

        std::mutex readyMtx;
        std::mutex handledMtx;
    };

    enum ConnectionError {
        CONN_ERR_TIMEOUT    = -1,
        CONN_ERR_BUSY       = -2
    };

    struct RemoteTransmitter;

    class Client {
    public:
        Client(std::shared_ptr<net::Socket> sock, dsp::stream<dsp::complex_t>* out);
        ~Client();

        void showMenu();

        void setFrequency(double freq);
        double getSampleRate();
        
        void setSampleType(dsp::compression::PCMType type);
        void setCompressionType(CompressionType type);
        void setLossFactor(double mult);
        void setNoiseMultiplierDB(double mult);

        void start();
        void stop();

        void close();
        bool isOpen();

        void idle();

        int bytes = 0;
        bool serverBusy = false;

        int rxPrebufferMsec = 100;

        void setRxPrebufferMsec(int msec) {
            rxPrebufferMsec = msec;
            prebufferer.setPrebufferMsec(rxPrebufferMsec);
        }

        void setRxResample(int freq) {
            if (requestedSampleRate != freq) {
                requestedSampleRate = freq;
                *((int32_t*)&s_cmd_data[0]) = freq;
                sendCommand(COMMAND_SET_SAMPLERATE, sizeof(int32_t));
                prebufferer.clear();
            }
        }

        void setMaskedFrequencies(std::vector<int32_t> offsets) {
            if (maskedFrequencies != offsets) {
                maskedFrequencies = offsets;
                int bytesLen = offsets.size() * sizeof(offsets[0]);
                memcpy(&s_cmd_data[0], offsets.data(), bytesLen);
                sendCommand(COMMAND_SET_EFFT_MASKED_FREQUENCIES, bytesLen);
            }
        }

        int getBufferPercentFull() {
            return prebufferer.getPercentFull();
        }

        int getBufferTimeDelay() {
            return prebufferer.getTimeDelayInMillis();
        }

        friend struct RemoteTransmitter;
        long long secureChallengeReceived = 0LL;
        std::vector<uint8_t> hmacKeyToUse;
        std::vector<uint8_t> challenge;
        int transmitterSupported = -1; // unknown
        int txPrebufferMsec = 0;

    private:
        void worker();

        int getUI();

        void sendPacket(PacketType type, int len);
        void sendCommand(Command cmd, int len);
        void sendCommandAck(Command cmd, int len);

        PacketWaiter* awaitCommandAck(Command cmd);
        void commandAckHandled(PacketWaiter* waiter);
        std::map<PacketWaiter*, Command> commandAckWaiters;

        static void dHandler(dsp::complex_t *data, int count, void *ctx);

        std::shared_ptr<net::Socket> sock;

        dsp::stream<uint8_t> decompIn;
        dsp::compression::SampleStreamDecompressor decomp;
        dsp::compression::ExperimentalFFTDeCompressor fftDecompressor;
        dsp::buffer::Prebuffer<dsp::complex_t> prebufferer;
        dsp::routing::StreamLink<dsp::complex_t> link;
        dsp::stream<dsp::complex_t>* output;


        uint8_t* rbuffer = NULL;
        uint8_t* sbuffer = NULL;
        std::vector<int32_t> maskedFrequencies;

        PacketHeader* r_pkt_hdr = NULL;
        uint8_t* r_pkt_data = NULL;
        CommandHeader* r_cmd_hdr = NULL;
        uint8_t* r_cmd_data = NULL;

        PacketHeader* s_pkt_hdr = NULL;
        uint8_t* s_pkt_data = NULL;
        CommandHeader* s_cmd_hdr = NULL;
        uint8_t* s_cmd_data = NULL;

        SmGui::DrawList dl;
        std::mutex dlMtx;

        ZSTD_DCtx* dctx;

        std::thread workerThread;

        double currentSampleRate = 1000000.0;
        double requestedSampleRate = 0;
    };

    std::shared_ptr<Client> connect(std::string host, uint16_t port, dsp::stream<dsp::complex_t>* out);
}
