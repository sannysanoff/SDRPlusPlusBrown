#pragma once

#include <dsp/types.h>
#include <complex>
#include <atomic>
#include <ctm.h>
#include <thread>
#include <core.h>
#include <atomic>
#include "utils/usleep.h"
#include "utils/proto/websock.h"

struct KiwiSDRClient {
    net::websock::WSClient wsClient;
    std::string hostPort;
    bool connected;
    char connectionStatus[100];
    int64_t lastPing;
    std::atomic<bool> running;
    std::vector<int64_t> times;

    std::function<void()> onConnected = []() {};
    std::function<void()> onDisconnected = []() {};
    std::vector<std::complex<float>> iqData;
    std::mutex iqDataLock;


    virtual ~KiwiSDRClient() {
        running = false;
        flog::info("KiwiSDRClient: destructor");
        running = false;
    }

    int IQDATA_FREQUENCY = 12000;
    int NETWORK_BUFFER_SECONDS = 2;
    int NETWORK_BUFFER_SIZE = NETWORK_BUFFER_SECONDS * IQDATA_FREQUENCY;

    void init(const std::string& hostport) {

        this->hostPort = hostport;
        strcpy(connectionStatus, "Not connected");
        static std::atomic_int outCount;

        outCount = 0;

        wsClient.onDisconnected = [&]() {
            connected = false;
            this->onDisconnected();
            snprintf(connectionStatus, sizeof connectionStatus, "Disconnected");
        };

        wsClient.onConnected = [&]() {
            // x.sendString("SET mod=usb low_cut=300 high_cut=2700 freq=14100.000");
            wsClient.sendString("SET auth t=kiwi p=#");
            wsClient.sendString("SET AR OK in=" + std::to_string(IQDATA_FREQUENCY) + " out=48000");
            //            x.sendString("SET mod=am low_cut=-4900 high_cut=4900 freq=119604.33");
            wsClient.sendString("SERVER DE CLIENT sdr++brown SND");
            wsClient.sendString("SET compression=0");
            wsClient.sendString("SET agc=0 hang=0 thresh=-100 slope=6 decay=1000 manGain=50");
            connected = true;
            if (this->onConnected) {
                onConnected();
            }
            strcpy(connectionStatus, "Connected, waiting data...");
            //            wsClient.sendString("SET mod=iq low_cut=-5000 high_cut=5000 freq=14074.000");
        };
        wsClient.onTextMessage = [&](const std::string& msg) {
            flog::info("TEXT: {}", msg);
        };

        wsClient.onBinaryMessage = [&](const std::string& msg) {
            std::string start = "???";
            if (msg.size() > 3) {
                start = msg.substr(0, 3);
            }
            if (start == "MSG") {
//                flog::info("=> BIN/MSG: {} text: {}", (int64_t)msg.size(), msg);
            }
            else if (start == "SND") {
                if ((outCount++) % 50 == 0) {
//                    flog::info("=> SND (each 50 packets)");
                }
                //                flog::info("{} Got sound: bytes={} )", (int64_t)currentTimeMillis(), (int64_t)msg.size());
                long long int ctm = currentTimeMillis();
                times.emplace_back(ctm);
                int lastSecondCount = 0;
                for (int q = times.size() - 1; q >= 0; q--) {
                    if (times[q] < ctm - 1000) {
                        break;
                    }
                    lastSecondCount++;
                }
                while (!times.empty() && times.front() < ctm - 2000) {
                    times.erase(times.begin());
                }
                snprintf(connectionStatus, sizeof connectionStatus, "Receiving. %d KB/sec (%d)", (lastSecondCount * ((int)msg.size())) / 1024, lastSecondCount);
                int IQ_HEADER_SIZE = 20;
                int REAL_HEADER_SIZE = 10;
                if (currentModulation == TUNE_REAL && msg.size() == 1024 + REAL_HEADER_SIZE) { // REAL data
                    auto scan = msg.data();
                    scan += REAL_HEADER_SIZE;

                    if (scan != msg.data() + REAL_HEADER_SIZE) {
                        flog::info("ERROR scan != msg.data() + REAL_HEADER_SIZE\n");
                        abort(); // homegrown assert.
                    }

                    int16_t* ptr = (int16_t*)scan;
                    snprintf(connectionStatus, sizeof connectionStatus, "Storing real..");
                    iqDataLock.lock();
                    for (int z = 0; z < 512; z++) {
                        int16_t* iqsample = &ptr[z];
                        char* fourbytes = (char*)iqsample;
                        std::swap(fourbytes[0], fourbytes[1]);
                        iqData.emplace_back(iqsample[0] / 32767.0, 0);
                    }
                    while (iqData.size() > NETWORK_BUFFER_SIZE * 1.5) {
                        iqData.erase(iqData.begin(), iqData.begin() + 200);
                    }
                    iqDataLock.unlock();
                    snprintf(connectionStatus, sizeof connectionStatus, "Cont Recv. %d KB/sec (%d)", (lastSecondCount * ((int)msg.size())) / 1024, lastSecondCount);
                }
                if (currentModulation == TUNE_IQ && msg[3] == 0x08 && msg.size() == 2048 + IQ_HEADER_SIZE) { // IQ data
                    auto scan = msg.data();
                    scan += 4;
                    auto sequence = *(int32_t*)scan;
                    scan += 4;
                    char one = *(char*)scan;
                    scan += 1;
                    auto info1 = *(int16_t*)scan;
                    scan += 2;
                    char zero = *(char*)scan;
                    scan += 1;
                    auto timestamp = *(int32_t*)scan;
                    scan += 4;
                    auto* arg2 = (int32_t*)scan;
                    scan += 4;

                    if (scan != msg.data() + IQ_HEADER_SIZE) {
                        flog::info("ERROR scan != msg.data() + IQ_HEADER_SIZE\n");
                        abort(); // homegrown assert.
                    }

                    int16_t* ptr = (int16_t*)scan;
                    iqDataLock.lock();
                    for (int z = 0; z < 512; z++) {
                        int16_t* iqsample = &ptr[2 * z];
                        char* fourbytes = (char*)iqsample;
                        std::swap(fourbytes[0], fourbytes[1]);
                        std::swap(fourbytes[2], fourbytes[3]);
                        iqData.emplace_back(iqsample[0] / 32767.0, iqsample[1] / 32767.0);
                    }
                    while (iqData.size() > NETWORK_BUFFER_SIZE * 1.5) {
                        iqData.erase(iqData.begin(), iqData.begin() + 200);
                    }
                    iqDataLock.unlock();
                    //                    flog::info("{} Got sound: bytes={} sequence={} info1={} timestamp={} , {} samples, buflen now = {} (erased {})", (int64_t)currentTimeMillis(), msg.size(), sequence, info1, timestamp, (msg.size() - HEADER_SIZE) / 4, buflen, erased);
                }
            }
            else {
                if (msg.size() >= 70) {
                    char buf[100];
                    for (int q = 3; q < 30; q++) {
                        snprintf(buf, sizeof buf,"%02x ", (unsigned char)msg[q]);
                        start += buf;
                    }
                    start += "... ";
                    for (int q = -20; q < 0; q++) {
                        snprintf(buf, sizeof buf,"%02x ", (unsigned char)msg[msg.size() - 1 + q]);
                        start += buf;
                    }
                }
//                flog::info("=> BIN: {} bytes: {}", (int64_t)msg.size(), start);
            }
        };
        lastPing = currentTimeMillis();
        wsClient.onEveryReceive = [&]() {
            if (currentTimeMillis() - lastPing > 4000) {
                wsClient.sendString("SET keepalive");
                lastPing = currentTimeMillis();
            }
        };
    }


    enum Modulation {
        TUNE_IQ = 1,
        TUNE_REAL = 2,           // only real data, -3 .. +3 kHz
    };

    Modulation currentModulation;

    void tune(double freq, Modulation mod) {
        char buf[1024];
        switch (mod) {
        case Modulation::TUNE_IQ:
            currentModulation = mod;
            snprintf(buf, sizeof buf, "SET mod=iq low_cut=-7000 high_cut=7000 freq=%0.3f", freq / 1000.0);
            break;
        case Modulation::TUNE_REAL:
            currentModulation = mod;
            snprintf(buf, sizeof(buf), "SET mod=usb low_cut=0 high_cut=8000 freq=%0.3f", (freq - 3000) / 1000.0);
            break;
        }
        wsClient.sendString(buf);
    }

    void stop() {
        strcpy(connectionStatus, "Disconnecting..");
        wsClient.stopSocket();
        strcpy(connectionStatus, "Disconnecting2..");
        while (running) {
            usleep(100000);
        }
        strcpy(connectionStatus, "Disconnected.");
    }

    void start() {
        strcpy(connectionStatus, "Connecting..");
        running = true;
        std::thread looper([&]() {
            SetThreadName("kiwisdr.wscli");
            flog::info("calling x.connectAndReceiveLoop..");
            try {
                std::string hostName;
                int port;
                std::size_t colonPosition = hostPort.find(":");
                if (colonPosition != std::string::npos) {
                    hostName = hostPort.substr(0, colonPosition);
                    port = std::stoi(hostPort.substr(colonPosition + 1));
                }
                else {
                    hostName = hostPort;
                    port = 0;
                }
                wsClient.connectAndReceiveLoop(hostName, port, "/kiwi/" + std::to_string(currentTimeMillis()) + "/SND");
                flog::info("x.connectAndReceiveLoop exited.");
                strcpy(connectionStatus, "Disconnected");
                running = false;
            }
            catch (const std::runtime_error& e) {
                flog::error("KiwiSDRSourceModule: Exception: {}", e.what());
                strcpy(connectionStatus, "Error: ");
                strcat(connectionStatus, e.what());
                running = false;
            }
        });
        looper.detach();
    }
};
