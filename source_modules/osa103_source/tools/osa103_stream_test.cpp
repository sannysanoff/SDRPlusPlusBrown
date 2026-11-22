/*
 * OSA103 source module for SDR++.
 * Copyright (c) 2026 the OSA103 source module authors. Non-commercial / educational use only,
 * see LICENSE.md in this directory.
 *
 * Standalone, headless stream test for the OSA103 / OSA103 Mini.
 *
 * It is the same source the SDR++ module registers ("OSA103"), reduced to the parts that
 * matter for hardware bring-up, with no GUI and no sdrpp_core:
 *
 *   load D2XX -> open by serial -> 3 Mbaud 8N1 -> purge -> hello -> config frame
 *   -> background read thread -> Parser -> float I/Q at the declared sample rate
 *   -> stop (read thread joined, device closed)
 *
 * Everything it sends and parses comes from src/osa103_proto.h, exactly the code the
 * module uses, so a pass here means the module's transport and framing layers work
 * against this device.
 *
 * Build (from source_modules/osa103_source):
 *   g++ -std=c++17 -O2 -Isrc tools/osa103_stream_test.cpp src/osa103_proto.cpp \
 *       src/ftdi_d2xx.cpp -o osa103_stream_test.exe
 *
 * Usage:
 *   osa103_stream_test --list
 *   osa103_stream_test --serial AH1STN9C --freq 100e6 --seconds 5 --dump iq.raw
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ftdi_d2xx.h"
#include "osa103_proto.h"

namespace {

constexpr size_t kMaxTxFrame = 4096;

struct Options {
    bool list = false;
    bool sniff = false;
    bool noConfig = false;
    bool noHello = false;
    bool log = false;
    bool type1 = false;
    bool noTables = false;
    bool dumpConfig = false;
    bool rxLenField = false;
    bool noFlagsByte = false;
    bool resetOnOpen = false;
    std::string serial;
    std::string d2xxPath;
    std::string dumpPath;
    std::string dumpRxPath;
    std::string bitstreamPath;
    double freq = 100.0e6;
    double seconds = 5.0;
    double sampleRate = 65104.0;
};

void usage() {
    std::printf(
        "usage: osa103_stream_test [options]\n"
        "  --list                 enumerate D2XX devices and exit\n"
        "  --serial <sn>          device serial (default: first found)\n"
        "  --freq <hz>            dial frequency, default 100e6\n"
        "  --seconds <n>          stream duration, default 5 (0 = until Enter)\n"
        "  --rate <hz>            declared sample rate, default 65104\n"
        "  --dump <file>          write interleaved float32 I/Q\n"
        "  --dump-rx <file>       write the raw byte stream from the device\n"
        "  --d2xx <path>          explicit ftd2xx.dll path\n"
        "  --bitstream <file>     bit-bang this FPGA image before configuring\n"
        "  --sniff                never transmit, only listen\n"
        "  --no-hello             skip the A5 00 00 hello\n"
        "  --no-config            skip the type 0x03 configuration frame\n"
        "  --no-tables            skip the eight type 0x01 ramp blocks\n"
        "  --dump-config          print the config frame for --freq and exit\n"
        "  --log                  hex-dump TX/RX frames\n");
}

bool parseArgs(int argc, char** argv, Options& o, std::string& err) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&](const char* what) -> const char* {
            if (i + 1 >= argc) { err = std::string("missing value for ") + what; return nullptr; }
            return argv[++i];
        };
        if (a == "--list") { o.list = true; }
        else if (a == "--sniff") { o.sniff = true; }
        else if (a == "--no-config") { o.noConfig = true; }
        else if (a == "--no-hello") { o.noHello = true; }
        else if (a == "--log") { o.log = true; }
        else if (a == "--type1") { o.type1 = true; }
        else if (a == "--no-tables") { o.noTables = true; }
        else if (a == "--dump-config") { o.dumpConfig = true; }
        else if (a == "--rx-len") { o.rxLenField = true; }
        else if (a == "--no-flags") { o.noFlagsByte = true; }
        else if (a == "--reset") { o.resetOnOpen = true; }
        else if (a == "--help" || a == "-h") { usage(); std::exit(0); }
        else if (a == "--serial") { const char* v = next("--serial"); if (!v) { return false; } o.serial = v; }
        else if (a == "--d2xx") { const char* v = next("--d2xx"); if (!v) { return false; } o.d2xxPath = v; }
        else if (a == "--dump") { const char* v = next("--dump"); if (!v) { return false; } o.dumpPath = v; }
        else if (a == "--dump-rx") { const char* v = next("--dump-rx"); if (!v) { return false; } o.dumpRxPath = v; }
        else if (a == "--bitstream") { const char* v = next("--bitstream"); if (!v) { return false; } o.bitstreamPath = v; }
        else if (a == "--freq") { const char* v = next("--freq"); if (!v) { return false; } o.freq = strtod(v, nullptr); }
        else if (a == "--seconds") { const char* v = next("--seconds"); if (!v) { return false; } o.seconds = strtod(v, nullptr); }
        else if (a == "--rate") { const char* v = next("--rate"); if (!v) { return false; } o.sampleRate = strtod(v, nullptr); }
        else { err = "unknown option " + a; return false; }
    }
    return true;
}

/*
 * The source itself. Same lifecycle and same data path as Osa103SourceModule in
 * src/main.cpp: start() opens and configures, a worker thread reads and parses, the
 * data callback hands out float I/Q at sampleRate, stop() joins and closes.
 */
class Osa103StreamSource {
public:
    osa103::Params params;
    std::string serial;
    std::string lastError;
    double sampleRate = 65104.0;
    double dialFreq = 100.0e6;

    std::atomic<uint64_t> bytesRead{0};
    std::atomic<uint64_t> samplesOut{0};
    std::atomic<uint64_t> txFrames{0};
    double peak = 0.0;
    std::ofstream dump;
    std::ofstream dumpRx;

    bool running() const { return run.load(); }

    bool start() {
        std::string err;
        if (!ftdi.load(params.d2xxLibraryPath, err)) { lastError = err; return false; }
        if (serial.empty()) {
            if (!pickFirstDevice()) { return false; }
        }
        if (!ftdi.openBySerial(serial, err)) { lastError = err; return false; }

        if (params.resetOnOpen) { ftdi.resetDevice(); }
        ftdi.setBaudRate(params.baudRate);
        ftdi.setDataCharacteristics(8, 0, 0);
        // Captured vendor sequence: RTS/CTS flow control plus RTS asserted before
        // the timeouts/latency are set.
        ftdi.setFlowControl(osa103::FtdiD2xx::FLOW_RTS_CTS, 0, 0);
        ftdi.setRts();
        ftdi.setTimeouts(params.readTimeoutMs, params.writeTimeoutMs);
        ftdi.setLatencyTimer(params.latencyMs);
        ftdi.purge(osa103::FtdiD2xx::PURGE_RX | osa103::FtdiD2xx::PURGE_TX);

        if (params.loadBitstream && !loadBitstream()) { return false; }

        setupParser();
        deviceReady = false;
        lastFlags = 0;

        if (!params.sniffOnly) {
            // Captured vendor sequence: stop the D2XX task, purge only the RX
            // side (the bit-bang leaves F6/F7 echo garbage in it), restart.
            ftdi.stopInTask();
            ftdi.purge(osa103::FtdiD2xx::PURGE_RX);
            ftdi.restartInTask();
        }

        if (params.sendHello && !params.sniffOnly && !sendHello()) { return false; }

        run = true;
        workerThread = std::thread(&Osa103StreamSource::worker, this);
        return true;
    }

    void stop() {
        if (!run.load()) { return; }
        run = false;
        if (workerThread.joinable()) { workerThread.join(); }
        ftdi.close();
        if (dump.is_open()) { dump.flush(); dump.close(); }
        if (dumpRx.is_open()) { dumpRx.flush(); dumpRx.close(); }
    }

    void tune(double hz) {
        dialFreq = hz;
        if (run.load() && params.resendConfigOnTune && !params.sniffOnly) { sendConfig(hz); }
    }

    void printStats() const {
        const osa103::Parser& p = const_cast<osa103::Parser&>(parser);
        std::printf(
            "  bytes read     : %llu\n"
            "  TX frames      : %llu\n"
            "  data packets   : %llu\n"
            "  info packets   : %llu\n"
            "  CRC errors     : %llu\n"
            "  discarded bytes: %llu\n"
            "  I/Q samples    : %llu\n"
            "  packet rate    : %.1f pkt/s\n"
            "  sample rate    : %.3f kS/s (declared %.0f)\n"
            "  peak |sample|  : %.1f\n",
            (unsigned long long)bytesRead.load(), (unsigned long long)txFrames.load(),
            (unsigned long long)p.dataPackets, (unsigned long long)p.infoPackets,
            (unsigned long long)p.crcErrors, (unsigned long long)p.bytesDiscarded,
            (unsigned long long)samplesOut.load(), (double)p.dataPackets / elapsedSeconds,
            (double)samplesOut.load() / (elapsedSeconds * 1000.0), sampleRate, peak);
    }

    double elapsedSeconds = 1.0;

private:
    bool pickFirstDevice() {
        std::vector<osa103::FtdiDeviceInfo> list;
        std::string err;
        if (!ftdi.listDevices(list, err)) { lastError = err; return false; }
        if (list.empty()) { lastError = "no D2XX device found"; return false; }
        serial = list[0].serial;
        if (serial.empty()) {
            lastError = "first device has an empty serial; use --serial";
            return false;
        }
        return true;
    }

    void setupParser() {
        parser.setParams(params);
        parser.onInfo = [this](const uint8_t* payload, int len) { handleInfo(payload, len); };
        parser.onData = [this](const uint8_t* payload, int len, uint8_t flags) {
            handleData(payload, len, flags);
        };
    }

    void handleInfo(const uint8_t* payload, int len) {
        uint8_t subtype = len > 0 ? payload[0] : 0;
        if (subtype == 0x02) {
            deviceReady = true;
            std::printf("  [info] device ready, firmware %u.%02u, type 0x%02X\n",
                        (unsigned)((len > 1 ? payload[1] : 0) + 300),
                        (unsigned)(len > 2 ? payload[2] : 0), (unsigned)(len > 4 ? payload[4] : 0));
        }
        else if (subtype == 0x03) {
            std::printf("  [info] configuration accepted\n");
        }
        else {
            std::printf("  [info] subtype 0x%02X, %d bytes\n", (unsigned)subtype, len);
        }
        if (params.logFrames) {
            std::printf("  RX info: %s\n", osa103::hexDump(payload, (size_t)len, params.logHexBytes).c_str());
        }
    }

    // Same conversion the module performs before it hands samples to the DSP chain.
    void handleData(const uint8_t* data, int len, uint8_t flags) {
        int pairs = len / 4;
        if (pairs <= 0) { return; }
        lastFlags = flags;
        bool mute = params.muteOnFlagBit1 && (flags & 0x02);

        static thread_local std::vector<float> out;
        out.resize((size_t)pairs * 2);
        for (int i = 0; i < pairs; i++) {
            const uint8_t* s = data + i * 4;
            int16_t iv, qv;
            if (params.bigEndianSamples) {
                iv = (int16_t)(((uint16_t)s[0] << 8) | s[1]);
                qv = (int16_t)(((uint16_t)s[2] << 8) | s[3]);
            }
            else {
                iv = (int16_t)(((uint16_t)s[1] << 8) | s[0]);
                qv = (int16_t)(((uint16_t)s[3] << 8) | s[2]);
            }
            float fi = (float)(iv * params.sampleScale);
            float fq = (float)(qv * params.sampleScale);
            if (params.swapIQ) { std::swap(fi, fq); }
            if (params.invertQ) { fq = -fq; }
            if (mute) { fi = 0.0f; fq = 0.0f; }
            out[(size_t)i * 2] = fi;
            out[(size_t)i * 2 + 1] = fq;

            float a = std::fabs(fi);
            float b = std::fabs(fq);
            float m = a > b ? a : b;
            if (m > peak) { peak = m; }
        }
        samplesOut += (uint64_t)pairs;
        if (dump.is_open()) {
            dump.write(reinterpret_cast<const char*>(out.data()), (std::streamsize)out.size() * 4);
        }
    }

    bool writeTx(const uint8_t* data, size_t len, bool splitSync) {
        std::lock_guard<std::mutex> lck(ioMutex);
        if (!ftdi.isOpen()) { lastError = "device is not open"; return false; }
        size_t offset = 0;
        if (splitSync && len > osa103::kSyncLen) {
            int n = ftdi.write(data, (uint32_t)osa103::kSyncLen);
            if (n < 0) {
                lastError = "FT_Write(sync): " + osa103::FtdiD2xx::statusString((uint32_t)(-n));
                return false;
            }
            offset = osa103::kSyncLen;
        }
        int n = ftdi.write(data + offset, (uint32_t)(len - offset));
        if (n < 0) {
            lastError = "FT_Write: " + osa103::FtdiD2xx::statusString((uint32_t)(-n));
            return false;
        }
        txFrames++;
        if (params.logFrames) {
            std::printf("  TX %zu bytes: %s\n", len, osa103::hexDump(data, len, params.logHexBytes).c_str());
        }
        return true;
    }

    bool sendHello() {
        uint8_t hello[3] = {params.hello0, params.hello1, params.hello2};
        std::lock_guard<std::mutex> lck(ioMutex);
        if (!ftdi.isOpen()) { return false; }
        int n = ftdi.write(hello, sizeof(hello));
        if (n < 0) {
            lastError = "FT_Write(hello): " + osa103::FtdiD2xx::statusString((uint32_t)(-n));
            return false;
        }
        txFrames++;
        if (params.logFrames) { std::printf("  TX hello: A5 00 00\n"); }
        return true;
    }

    bool sendConfig(double freq) {
        uint8_t frame[kMaxTxFrame];
        size_t len = osa103::buildConfigFrame(params, freq, frame, sizeof(frame));
        if (!len) { lastError = "failed to build the configuration frame"; return false; }
        if (!writeTx(frame, len, params.syncSeparateWrite)) { return false; }

        if (params.sendType1Table) {
            uint8_t block[kMaxTxFrame];
            for (int i = 0; i < params.type1Blocks; i++) {
                size_t n = osa103::buildTableFrame(params, i, block, sizeof(block));
                if (!n) { break; }
                if (!writeTx(block, n, params.syncSeparateWrite)) { return false; }
            }
        }
        return true;
    }

    // FPGA bit-bang loader, identical to the module's.
    bool loadBitstream() {
        if (params.bitstreamPath.empty()) { lastError = "no bitstream path"; return false; }
        std::ifstream file(params.bitstreamPath, std::ios::binary);
        if (!file) { lastError = "cannot open bitstream " + params.bitstreamPath; return false; }
        std::vector<uint8_t> bits((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (bits.empty()) { lastError = "bitstream is empty"; return false; }
        std::printf("  loading FPGA bitstream, %zu bytes\n", bits.size());

        std::lock_guard<std::mutex> lck(ioMutex);
        if (ftdi.setBitMode(0x85, 0x01)) { lastError = "FT_SetBitMode(bitbang) failed"; return false; }
        uint8_t low = 0x00;
        ftdi.write(&low, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        uint8_t high = 0x80;
        ftdi.write(&high, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        std::vector<uint8_t> chunk;
        chunk.reserve(4096);
        for (uint8_t byte : bits) {
            for (int b = 7; b >= 0; b--) {
                uint8_t bit = (uint8_t)((byte >> b) & 1);
                chunk.push_back((uint8_t)(0x80 | bit));
                chunk.push_back((uint8_t)(0x84 | bit));
                if (chunk.size() >= 4096) {
                    ftdi.write(chunk.data(), (uint32_t)chunk.size());
                    chunk.clear();
                }
            }
        }
        for (int i = 0; i < 64; i++) {
            chunk.push_back(0x85);
            chunk.push_back(0x81);
        }
        if (!chunk.empty()) { ftdi.write(chunk.data(), (uint32_t)chunk.size()); }

        // Captured vendor exit sequence: (0x05, mode 1) first, then (0x05, mode 0)
        // to hand the pins back to the UART.
        ftdi.setBitMode(0x05, 0x01);
        ftdi.setBitMode(0x05, 0x00);
        ftdi.setBaudRate(params.baudRate);
        ftdi.setDataCharacteristics(8, 0, 0);
        ftdi.setFlowControl(osa103::FtdiD2xx::FLOW_RTS_CTS, 0, 0);
        ftdi.setRts();
        ftdi.setTimeouts(params.readTimeoutMs, params.writeTimeoutMs);
        ftdi.setLatencyTimer(params.latencyMs);
        return true;
    }

    void worker() {
        std::vector<uint8_t> chunk((size_t)(params.readChunk > 0 ? params.readChunk : 65536));
        bool configSent = noConfigOnStart;
        auto startTime = std::chrono::steady_clock::now();

        while (run.load()) {
            int n = ftdi.read(chunk.data(), (uint32_t)chunk.size());
            if (n < 0) {
                uint32_t st = (uint32_t)(-n);
                if (st != 4) {
                    lastError = "FT_Read: " + osa103::FtdiD2xx::statusString(st);
                    std::printf("  [error] %s\n", lastError.c_str());
                    break;
                }
                n = 0;
            }
            if (n > 0) {
                bytesRead += (uint64_t)n;
                if (dumpRx.is_open()) { dumpRx.write(reinterpret_cast<const char*>(chunk.data()), n); }
                if (params.logFrames) {
                    std::printf("  RX raw: %s\n", osa103::hexDump(chunk.data(), (size_t)n, params.logHexBytes).c_str());
                }
                parser.feed(chunk.data(), (size_t)n);
            }

            // Deferred first configuration: the module waits for the device-ready info
            // packet but gives up after 400 ms so a silent device still gets configured.
            if (!configSent && !params.sniffOnly) {
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - startTime)
                              .count();
                if (deviceReady.load() || ms > 400) {
                    std::string err;
                    if (sendConfig(dialFreq)) { configSent = true; }
                    else {
                        lastError = err;
                        std::printf("  [error] configuring: %s\n", lastError.c_str());
                        break;
                    }
                }
            }
        }
    }

public:
    // Set by the CLI; kept public so the harness can force "no config on start".
    bool noConfigOnStart = false;

private:
    osa103::FtdiD2xx ftdi;
    osa103::Parser parser{params};
    std::thread workerThread;
    std::atomic<bool> run{false};
    std::atomic<bool> deviceReady{false};
    std::mutex ioMutex;
    uint8_t lastFlags = 0;
};

}  // namespace

int main(int argc, char** argv) {
    Options o;
    std::string err;
    if (!parseArgs(argc, argv, o, err)) {
        std::printf("error: %s\n\n", err.c_str());
        usage();
        return 2;
    }

    if (o.list) {
        osa103::FtdiD2xx ftdi;
        if (!ftdi.load(o.d2xxPath, err)) { std::printf("error: %s\n", err.c_str()); return 1; }
        std::printf("D2XX library: %s\n", ftdi.libraryPath().c_str());
        std::vector<osa103::FtdiDeviceInfo> devices;
        if (!ftdi.listDevices(devices, err)) { std::printf("error: %s\n", err.c_str()); return 1; }
        std::printf("devices: %zu\n", devices.size());
        for (size_t i = 0; i < devices.size(); i++) {
            std::printf("  [%zu] serial=%-16s desc=%-24s id=0x%04X type=%u\n", i,
                        devices[i].serial.c_str(), devices[i].description.c_str(), devices[i].id,
                        devices[i].type);
        }
        return devices.empty() ? 3 : 0;
    }

    if (o.dumpConfig) {
        // Byte-exact verification aid: print the sync + type 0x03 frame the
        // builder now produces for --freq so it can be diffed against a captured
        // vendor frame.
        osa103::Params p;
        uint8_t frame[4096];
        size_t len = osa103::buildConfigFrame(p, o.freq, frame, sizeof(frame));
        if (!len) { std::printf("error: cannot build the frame\n"); return 1; }
        std::printf("config frame for %.3f MHz (%zu bytes):\n", o.freq / 1.0e6, len);
        std::printf("%s\n", osa103::hexDump(frame, len, len).c_str());
        return 0;
    }

    Osa103StreamSource src;
    src.params.d2xxLibraryPath = o.d2xxPath;
    src.params.serial = o.serial;
    src.params.bitstreamPath = o.bitstreamPath;
    src.params.loadBitstream = !o.bitstreamPath.empty();
    src.params.sniffOnly = o.sniff;
    src.params.sendType1Table = !o.noTables;
    src.params.rxHasLenField = o.rxLenField;
    src.params.resetOnOpen = o.resetOnOpen;
    src.params.sendHello = !o.noHello;
    src.params.logFrames = o.log;
    src.noConfigOnStart = o.noConfig;
    src.serial = o.serial;
    src.sampleRate = o.sampleRate;
    src.dialFreq = o.freq;

    if (!o.dumpPath.empty()) {
        src.dump.open(o.dumpPath, std::ios::binary | std::ios::trunc);
        if (!src.dump) { std::printf("error: cannot open %s\n", o.dumpPath.c_str()); return 1; }
    }
    if (!o.dumpRxPath.empty()) {
        src.dumpRx.open(o.dumpRxPath, std::ios::binary | std::ios::trunc);
        if (!src.dumpRx) { std::printf("error: cannot open %s\n", o.dumpRxPath.c_str()); return 1; }
    }

    std::printf("declared sample rate: %.0f Hz, dial %.0f Hz, %s\n", src.sampleRate, src.dialFreq,
                o.sniff ? "sniff-only" : "transmitting");

    if (!src.start()) {
        std::printf("start failed: %s\n", src.lastError.c_str());
        return 1;
    }
    std::printf("started on serial '%s'\n", src.serial.c_str());

    auto t0 = std::chrono::steady_clock::now();
    if (o.seconds <= 0.0) {
        std::printf("streaming; press Enter to stop\n");
        (void)getchar();
    }
    else {
        std::this_thread::sleep_for(std::chrono::milliseconds((long long)(o.seconds * 1000.0)));
    }
    src.elapsedSeconds = std::max(
        0.001, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());

    src.stop();
    std::printf("stopped\n\nstats:\n");
    src.printStats();
    return src.bytesRead.load() ? 0 : 4;
}
