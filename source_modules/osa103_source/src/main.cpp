/*
 * OSA103 source module for SDR++.
 * Copyright (c) 2026 the OSA103 source module authors. Non-commercial / educational use only,
 * see LICENSE.md in this directory.
 *
 * Protocol reconstructed from the vendor's ExtIO_Osa.dll v3.09 (OSA103, osa103.ru)
 * by static analysis, for interoperability. No vendor code, listing or bitstream is
 * included. Provided as is, without warranty of any kind: it could not be tested
 * against hardware while it was written.
 */
#include <module.h>
#include <gui/gui.h>
#include <gui/smgui.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <utils/flog.h>
#include <utils/optionlist.h>
#include <dsp/stream.h>
#include <dsp/types.h>
#include <imgui.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ftdi_d2xx.h"
#include "osa103_proto.h"

SDRPP_MOD_INFO{
    /* Name:            */ "osa103_source",
    /* Description:     */ "OSA103 / OSA103 Mini source module (non-commercial)",
    /* Author:          */ "the OSA103 source module authors",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

ConfigManager config;

#define CONCAT(a, b) ((std::string(a) + b).c_str())
#define OSA103_MAX_TX_FRAME 4096

namespace {

// ImGui text buffers for the editable protocol knobs. The values themselves live in
// osa103::Params; these are only the editing surfaces.
struct TextBuffers {
    char flags[8] = "39";
    char mode[8] = "1D";
    char rxtxTag[8] = "80";
    char const6258[8] = "6258";
    char adcGain[8] = "8000";
    char adcOff1[8] = "0000";
    char adcOff2[8] = "0000";
    char ncoScale[32] = "21.47483648";
    char loScale[32] = "1.0";
    char imageOverride[512] = "";
    char bitstreamPath[512] = "";
    char d2xxPath[512] = "";
    char serial[64] = "";
};

void copyToBuf(char* buf, size_t size, const std::string& s) {
    if (!size) { return; }
    size_t n = s.size() < size - 1 ? s.size() : size - 1;
    memcpy(buf, s.data(), n);
    buf[n] = 0;
}

std::string bufToStr(const char* buf) { return std::string(buf ? buf : ""); }

unsigned parseHex(const char* text, unsigned fallback) {
    if (!text) { return fallback; }
    char* end = nullptr;
    unsigned long v = strtoul(text, &end, 16);
    if (end == text) { return fallback; }
    return (unsigned)v;
}

double parseDouble(const char* text, double fallback) {
    if (!text) { return fallback; }
    char* end = nullptr;
    double v = strtod(text, &end);
    if (end == text) { return fallback; }
    return v;
}

}  // namespace

class Osa103SourceModule : public ModuleManager::Instance {
public:
    Osa103SourceModule(std::string name) {
        this->name = name;

        handler.ctx = this;
        handler.selectHandler = menuSelected;
        handler.deselectHandler = menuDeselected;
        handler.menuHandler = menuHandler;
        handler.startHandler = start;
        handler.stopHandler = stop;
        handler.tuneHandler = tune;
        handler.stream = &stream;

        loadConfig();
        refresh();
        select(params.serial);

        sigpath::sourceManager.registerSource("OSA103", &handler);
    }

    ~Osa103SourceModule() {
        stop(this);
        sigpath::sourceManager.unregisterSource("OSA103");
    }

    void postInit() {}

    void enable() { enabled = true; }

    void disable() { enabled = false; }

    bool isEnabled() { return enabled; }

private:
    // ------------------------------------------------------------------ config
    void loadConfig() {
        config.acquire();

        auto getStr = [](const json& j, const char* key, const std::string& def) {
            if (j.contains(key) && j[key].is_string()) { return j[key].get<std::string>(); }
            return def;
        };
        auto getUint = [](const json& j, const char* key, uint32_t def) {
            if (j.contains(key) && j[key].is_number_unsigned()) { return (uint32_t)j[key]; }
            if (j.contains(key) && j[key].is_number_integer()) { return (uint32_t)(int32_t)j[key]; }
            return def;
        };
        auto getDouble = [](const json& j, const char* key, double def) {
            if (j.contains(key) && j[key].is_number()) { return j[key].get<double>(); }
            return def;
        };
        auto getBool = [](const json& j, const char* key, bool def) {
            if (j.contains(key) && j[key].is_boolean()) { return j[key].get<bool>(); }
            return def;
        };

        json& j = config.conf;
        params.d2xxLibraryPath = getStr(j, "d2xxLibraryPath", params.d2xxLibraryPath);
        params.serial = getStr(j, "serial", params.serial);
        params.bitstreamPath = getStr(j, "bitstreamPath", params.bitstreamPath);
        params.baudRate = getUint(j, "baudRate", params.baudRate);
        params.latencyMs = (uint8_t)getUint(j, "latencyMs", params.latencyMs);
        params.readTimeoutMs = getUint(j, "readTimeoutMs", params.readTimeoutMs);
        params.writeTimeoutMs = getUint(j, "writeTimeoutMs", params.writeTimeoutMs);
        params.crcEnabled = getBool(j, "crcEnabled", params.crcEnabled);
        params.syncRepeat = (int)getUint(j, "syncRepeat", params.syncRepeat);
        params.sendHello = getBool(j, "sendHello", params.sendHello);
        params.sendConfigOnStart = getBool(j, "sendConfigOnStart", params.sendConfigOnStart);
        params.resendConfigOnTune = getBool(j, "resendConfigOnTune", params.resendConfigOnTune);
        params.sendType1Table = getBool(j, "sendType1Table", params.sendType1Table);
        params.loadBitstream = getBool(j, "loadBitstream", params.loadBitstream);

        params.tag3 = (uint8_t)getUint(j, "tag3", params.tag3);
        params.tag4 = (uint8_t)getUint(j, "tag4", params.tag4);
        params.tag5 = (uint8_t)getUint(j, "tag5", params.tag5);
        params.tag6 = (uint8_t)getUint(j, "tag6", params.tag6);
        params.rxtxTag = (uint8_t)getUint(j, "rxtxTag", params.rxtxTag);
        params.const6258 = (uint16_t)getUint(j, "const6258", params.const6258);
        params.tag31 = (uint8_t)getUint(j, "tag31", params.tag31);
        params.tag32 = (uint8_t)getUint(j, "tag32", params.tag32);
        params.tag33 = (uint8_t)getUint(j, "tag33", params.tag33);
        params.tag34 = (uint8_t)getUint(j, "tag34", params.tag34);
        params.tag64 = (uint8_t)getUint(j, "tag64", params.tag64);
        params.tag65 = (uint8_t)getUint(j, "tag65", params.tag65);
        params.tag66 = (uint8_t)getUint(j, "tag66", params.tag66);
        params.tag67 = (uint8_t)getUint(j, "tag67", params.tag67);
        params.tag68 = (uint8_t)getUint(j, "tag68", params.tag68);
        params.tag69 = (uint8_t)getUint(j, "tag69", params.tag69);
        params.flags = (uint8_t)getUint(j, "flags", params.flags);
        params.mode = (uint8_t)getUint(j, "mode", params.mode);
        params.level = (uint8_t)getUint(j, "level", params.level);
        params.adcGainWord = (uint16_t)getUint(j, "adcGainWord", params.adcGainWord);
        params.adcOffWord = (uint16_t)getUint(j, "adcOffWord", params.adcOffWord);
        params.adcOffWord2 = (uint16_t)getUint(j, "adcOffWord2", params.adcOffWord2);
        params.ncoScale = getDouble(j, "ncoScale", params.ncoScale);
        params.ncoIfHz = getDouble(j, "ncoIfHz", params.ncoIfHz);
        params.ncoMode = (int)getUint(j, "ncoMode", params.ncoMode);
        params.loScale = getDouble(j, "loScale", params.loScale);
        params.imageOverrideHex = getStr(j, "imageOverrideHex", params.imageOverrideHex);
        params.dataBytes = (int)getUint(j, "dataBytes", params.dataBytes);
        params.crcBytes = (int)getUint(j, "crcBytes", params.crcBytes);
        params.hasFlagsByte = getBool(j, "hasFlagsByte", params.hasFlagsByte);
        params.rxHasLenField = getBool(j, "rxHasLenField", params.rxHasLenField);
        params.swapIQ = getBool(j, "swapIQ", params.swapIQ);
        params.invertQ = getBool(j, "invertQ", params.invertQ);
        params.sampleScale = getDouble(j, "sampleScale", params.sampleScale);
        params.muteOnFlagBit1 = getBool(j, "muteOnFlagBit1", params.muteOnFlagBit1);
        params.sniffOnly = getBool(j, "sniffOnly", params.sniffOnly);
        params.logFrames = getBool(j, "logFrames", params.logFrames);
        params.infoPacketBytes = (int)getUint(j, "infoPacketBytes", params.infoPacketBytes);

        // Transport/framing/image fields that are only reachable from the config file.
        params.resetOnOpen = (bool)getBool(j, "resetOnOpen", params.resetOnOpen);
        params.readChunk = (uint32_t)getUint(j, "readChunk", params.readChunk);
        params.crcInit = (uint16_t)getUint(j, "crcInit", params.crcInit);
        params.syncByte0 = (uint8_t)getUint(j, "syncByte0", params.syncByte0);
        params.syncByte1 = (uint8_t)getUint(j, "syncByte1", params.syncByte1);
        params.syncByte2 = (uint8_t)getUint(j, "syncByte2", params.syncByte2);
        params.syncTerminator = (uint8_t)getUint(j, "syncTerminator", params.syncTerminator);
        params.syncSeparateWrite = (bool)getBool(j, "syncSeparateWrite", params.syncSeparateWrite);
        params.hello0 = (uint8_t)getUint(j, "hello0", params.hello0);
        params.hello1 = (uint8_t)getUint(j, "hello1", params.hello1);
        params.hello2 = (uint8_t)getUint(j, "hello2", params.hello2);
        params.type1BaseAddr = (uint16_t)getUint(j, "type1BaseAddr", params.type1BaseAddr);
        params.type1Stride = (uint16_t)getUint(j, "type1Stride", params.type1Stride);
        params.type1Blocks = (int)getUint(j, "type1Blocks", params.type1Blocks);
        params.type1FillValue = (uint16_t)getUint(j, "type1FillValue", params.type1FillValue);
        params.dividerTag = (uint8_t)getUint(j, "dividerTag", params.dividerTag);
        params.dividerTag2 = (uint8_t)getUint(j, "dividerTag2", params.dividerTag2);
        params.txDivShift = (int)getUint(j, "txDivShift", params.txDivShift);
        params.txDivQ = (uint16_t)getUint(j, "txDivQ", params.txDivQ);
        params.txDivR = (uint16_t)getUint(j, "txDivR", params.txDivR);
        params.txDivShift2 = (int)getUint(j, "txDivShift2", params.txDivShift2);
        params.txDivQ2 = (uint16_t)getUint(j, "txDivQ2", params.txDivQ2);
        params.txDivR2 = (uint16_t)getUint(j, "txDivR2", params.txDivR2);
        params.calA = (uint8_t)getUint(j, "calA", params.calA);
        params.calB = (uint8_t)getUint(j, "calB", params.calB);
        params.calC = (uint8_t)getUint(j, "calC", params.calC);
        params.calD = (uint8_t)getUint(j, "calD", params.calD);
        params.calE = (uint8_t)getUint(j, "calE", params.calE);
        params.calF = (uint8_t)getUint(j, "calF", params.calF);
        params.calG = (uint8_t)getUint(j, "calG", params.calG);
        params.calH = (uint8_t)getUint(j, "calH", params.calH);
        params.sendTxNco = (bool)getBool(j, "sendTxNco", params.sendTxNco);
        params.bigEndianSamples = (bool)getBool(j, "bigEndianSamples", params.bigEndianSamples);
        params.logHexBytes = (int)getUint(j, "logHexBytes", params.logHexBytes);

        sampleRate = getDouble(j, "sampleRate", sampleRate);
        dialFreq = getDouble(j, "frequency", dialFreq);
        config.release();

        // Editing surfaces mirror the loaded values.
        snprintf(bufs.flags, sizeof(bufs.flags), "%02X", params.flags);
        snprintf(bufs.mode, sizeof(bufs.mode), "%02X", params.mode);
        snprintf(bufs.rxtxTag, sizeof(bufs.rxtxTag), "%02X", params.rxtxTag);
        snprintf(bufs.const6258, sizeof(bufs.const6258), "%04X", params.const6258);
        snprintf(bufs.adcGain, sizeof(bufs.adcGain), "%04X", params.adcGainWord);
        snprintf(bufs.adcOff1, sizeof(bufs.adcOff1), "%04X", params.adcOffWord);
        snprintf(bufs.adcOff2, sizeof(bufs.adcOff2), "%04X", params.adcOffWord2);
        snprintf(bufs.ncoScale, sizeof(bufs.ncoScale), "%.10g", params.ncoScale);
        snprintf(bufs.loScale, sizeof(bufs.loScale), "%.10g", params.loScale);
        copyToBuf(bufs.imageOverride, sizeof(bufs.imageOverride), params.imageOverrideHex);
        copyToBuf(bufs.bitstreamPath, sizeof(bufs.bitstreamPath), params.bitstreamPath);
        copyToBuf(bufs.d2xxPath, sizeof(bufs.d2xxPath), params.d2xxLibraryPath);
        copyToBuf(bufs.serial, sizeof(bufs.serial), params.serial);
    }

    // Every parameter, written into a json object. Used both by saveConfig() and by
    // the module's default config, so the file on disk always lists all knobs.
public:
    static void paramsToJson(json& j, const osa103::Params& params, double sr, double freq) {

        j["d2xxLibraryPath"] = params.d2xxLibraryPath;
        j["serial"] = params.serial;
        j["bitstreamPath"] = params.bitstreamPath;
        j["baudRate"] = params.baudRate;
        j["latencyMs"] = params.latencyMs;
        j["readTimeoutMs"] = params.readTimeoutMs;
        j["writeTimeoutMs"] = params.writeTimeoutMs;
        j["crcEnabled"] = params.crcEnabled;
        j["syncRepeat"] = params.syncRepeat;
        j["sendHello"] = params.sendHello;
        j["sendConfigOnStart"] = params.sendConfigOnStart;
        j["resendConfigOnTune"] = params.resendConfigOnTune;
        j["sendType1Table"] = params.sendType1Table;
        j["loadBitstream"] = params.loadBitstream;
        j["tag3"] = params.tag3;
        j["tag4"] = params.tag4;
        j["tag5"] = params.tag5;
        j["tag6"] = params.tag6;
        j["rxtxTag"] = params.rxtxTag;
        j["const6258"] = params.const6258;
        j["tag31"] = params.tag31;
        j["tag32"] = params.tag32;
        j["tag33"] = params.tag33;
        j["tag34"] = params.tag34;
        j["tag64"] = params.tag64;
        j["tag65"] = params.tag65;
        j["tag66"] = params.tag66;
        j["tag67"] = params.tag67;
        j["tag68"] = params.tag68;
        j["tag69"] = params.tag69;
        j["flags"] = params.flags;
        j["mode"] = params.mode;
        j["level"] = params.level;
        j["adcGainWord"] = params.adcGainWord;
        j["adcOffWord"] = params.adcOffWord;
        j["adcOffWord2"] = params.adcOffWord2;
        j["ncoScale"] = params.ncoScale;
        j["ncoIfHz"] = params.ncoIfHz;
        j["ncoMode"] = params.ncoMode;
        j["loScale"] = params.loScale;
        j["imageOverrideHex"] = params.imageOverrideHex;
        j["dataBytes"] = params.dataBytes;
        j["crcBytes"] = params.crcBytes;
        j["hasFlagsByte"] = params.hasFlagsByte;
        j["rxHasLenField"] = params.rxHasLenField;
        j["swapIQ"] = params.swapIQ;
        j["invertQ"] = params.invertQ;
        j["sampleScale"] = params.sampleScale;
        j["muteOnFlagBit1"] = params.muteOnFlagBit1;
        j["sniffOnly"] = params.sniffOnly;
        j["logFrames"] = params.logFrames;
        j["infoPacketBytes"] = params.infoPacketBytes;
        j["resetOnOpen"] = params.resetOnOpen;
        j["readChunk"] = params.readChunk;
        j["crcInit"] = params.crcInit;
        j["syncByte0"] = params.syncByte0;
        j["syncByte1"] = params.syncByte1;
        j["syncByte2"] = params.syncByte2;
        j["syncTerminator"] = params.syncTerminator;
        j["syncSeparateWrite"] = params.syncSeparateWrite;
        j["hello0"] = params.hello0;
        j["hello1"] = params.hello1;
        j["hello2"] = params.hello2;
        j["type1BaseAddr"] = params.type1BaseAddr;
        j["type1Stride"] = params.type1Stride;
        j["type1Blocks"] = params.type1Blocks;
        j["type1FillValue"] = params.type1FillValue;
        j["dividerTag"] = params.dividerTag;
        j["dividerTag2"] = params.dividerTag2;
        j["txDivShift"] = params.txDivShift;
        j["txDivQ"] = params.txDivQ;
        j["txDivR"] = params.txDivR;
        j["txDivShift2"] = params.txDivShift2;
        j["txDivQ2"] = params.txDivQ2;
        j["txDivR2"] = params.txDivR2;
        j["calA"] = params.calA;
        j["calB"] = params.calB;
        j["calC"] = params.calC;
        j["calD"] = params.calD;
        j["calE"] = params.calE;
        j["calF"] = params.calF;
        j["calG"] = params.calG;
        j["calH"] = params.calH;
        j["sendTxNco"] = params.sendTxNco;
        j["bigEndianSamples"] = params.bigEndianSamples;
        j["logHexBytes"] = params.logHexBytes;
        j["sampleRate"] = sr;
        j["frequency"] = freq;
            }

    void saveConfig() {
        config.acquire();
        paramsToJson(config.conf, params, sampleRate, dialFreq);
        config.release(true);
    }

private:

    // ----------------------------------------------------------------- devices
    void refresh() {
        devices.clear();
        std::string err;
        if (!ftdi.load(params.d2xxLibraryPath, err)) {
            lastError = err;
            flog::error("OSA103: {}", err);
            return;
        }
        flog::info("OSA103: using D2XX library '{}'", ftdi.libraryPath());

        std::vector<osa103::FtdiDeviceInfo> list;
        if (!ftdi.listDevices(list, err)) {
            lastError = err;
            flog::error("OSA103: {}", err);
            return;
        }
        for (size_t i = 0; i < list.size(); i++) {
            std::string label = list[i].description;
            if (label.empty()) { label = "FTDI device"; }
            label += " (" + list[i].serial + ")";
            devices.define(list[i].serial, label, list[i].serial);
        }
        lastError = devices.empty() ? "No FTDI devices found" : "";
    }

    void select(const std::string& serial) {
        if (devices.empty()) {
            selectedSerial.clear();
            return;
        }
        if (!devices.keyExists(serial)) {
            selectedSerial = devices.key(0);
        }
        else {
            selectedSerial = serial;
        }
        devId = devices.keyId(selectedSerial);
        params.serial = selectedSerial;
        core::setInputSampleRate(sampleRate);
    }

    // --------------------------------------------------------------------- I/O
    // Writes one already-built buffer (sync + frame) as its own FT_Write calls.
    bool writeTx(const uint8_t* data, size_t len, bool splitSync) {
        std::lock_guard<std::mutex> lck(ioMutex);
        if (!ftdi.isOpen()) { lastError = "Device is not open"; return false; }
        size_t offset = 0;
        if (splitSync && len > osa103::kSyncLen) {
            int written = ftdi.write(data, (uint32_t)osa103::kSyncLen);
            if (written < 0) {
                lastError = "FT_Write(sync): " + osa103::FtdiD2xx::statusString((uint32_t)(-written));
                return false;
            }
            offset = osa103::kSyncLen;
        }
        int written = ftdi.write(data + offset, (uint32_t)(len - offset));
        if (written < 0) {
            lastError = "FT_Write: " + osa103::FtdiD2xx::statusString((uint32_t)(-written));
            return false;
        }
        if (params.logFrames) {
            flog::info("OSA103 TX ({} bytes): {}", (uint64_t)len,
                       osa103::hexDump(data, len, params.logHexBytes));
        }
        txFrames++;
        return true;
    }

    bool sendHello() {
        uint8_t hello[3] = {params.hello0, params.hello1, params.hello2};
        std::lock_guard<std::mutex> lck(ioMutex);
        if (!ftdi.isOpen()) { return false; }
        int written = ftdi.write(hello, sizeof(hello));
        if (written < 0) {
            lastError = "FT_Write(hello): " + osa103::FtdiD2xx::statusString((uint32_t)(-written));
            return false;
        }
        flog::info("OSA103: hello sent ({:02X} {:02X} {:02X})", hello[0], hello[1], hello[2]);
        return true;
    }

    bool sendConfigSnapshot(double freq) {
        uint8_t frame[OSA103_MAX_TX_FRAME];
        size_t len = osa103::buildConfigFrame(params, freq, frame, sizeof(frame));
        if (!len) {
            lastError = "Failed to build the configuration frame";
            return false;
        }
        if (!writeTx(frame, len, params.syncSeparateWrite)) { return false; }

        if (params.sendType1Table) {
            // The eight hardware-validated ramp blocks: blocks 0..3 and 6..7
            // are ramps, block 4 is zeros, block 5 starts with 169 zeros.
            // Byte-exact against the vendor's startup sequence.
            uint8_t block[OSA103_MAX_TX_FRAME];
            for (int i = 0; i < params.type1Blocks; i++) {
                size_t n = osa103::buildTableFrame(params, i, block, sizeof(block));
                if (!n) { break; }
                if (!writeTx(block, n, params.syncSeparateWrite)) { return false; }
            }
        }
        flog::info("OSA103: configuration sent for {:.0f} Hz", freq);
        return true;
    }

    // Loads the FPGA bitstream the way ExtIO_Osa.dll does: async bit-bang, PROG_B on
    // D7, DIN on D0, CCLK on D2 (data bit, then clock low/high as two byte writes).
    bool loadBitstream() {
        if (params.bitstreamPath.empty()) {
            lastError = "No bitstream path configured";
            return false;
        }
        std::ifstream file(params.bitstreamPath, std::ios::binary);
        if (!file) {
            lastError = "Cannot open bitstream '" + params.bitstreamPath + "'";
            return false;
        }
        std::vector<uint8_t> bits((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (bits.empty()) {
            lastError = "Bitstream is empty";
            return false;
        }
        flog::info("OSA103: loading FPGA bitstream ({} bytes)", (uint64_t)bits.size());

        {
            std::lock_guard<std::mutex> lck(ioMutex);
            if (ftdi.setBitMode(0x85, 0x01)) {
                lastError = "FT_SetBitMode(bitbang) failed";
                return false;
            }
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
                    uint8_t bit = (byte >> b) & 1;
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

            // Back to the UART protocol.
            ftdi.setBitMode(0x05, 0x00);
            ftdi.purge(osa103::FtdiD2xx::PURGE_RX | osa103::FtdiD2xx::PURGE_TX);
            ftdi.setBaudRate(params.baudRate);
            ftdi.setDataCharacteristics(8, 0, 0);
            ftdi.setTimeouts(params.readTimeoutMs, params.writeTimeoutMs);
            ftdi.setLatencyTimer(params.latencyMs);
        }
        flog::info("OSA103: FPGA configured");
        return true;
    }

    // ------------------------------------------------------------------ receive
    void setupParser() {
        parser.setParams(params);
        parser.onInfo = [this](const uint8_t* payload, int len) { handleInfoPacket(payload, len); };
        parser.onData = [this](const uint8_t* payload, int len, uint8_t flags) {
            handleDataPayload(payload, len, flags);
        };
    }

    void handleInfoPacket(const uint8_t* packet, int len) {
        uint8_t subtype = packet[0];
        uint8_t ver1 = len > 1 ? packet[1] : 0;
        uint8_t ver2 = len > 2 ? packet[2] : 0;
        uint8_t devType = len > 4 ? packet[4] : 0;
        if (subtype == 0x02) {
            deviceReady = true;
            flog::info("OSA103: device ready, firmware {}.{:02d}, type 0x{:02X}", (unsigned)(ver1 + 300),
                       (unsigned)ver2, (unsigned)devType);
        }
        else if (subtype == 0x03) {
            flog::info("OSA103: configuration accepted");
        }
        else {
            flog::info("OSA103: info packet subtype 0x{:02X} ({} bytes)", (unsigned)subtype, len);
        }
        if (params.logFrames) {
            flog::info("OSA103 RX info: {}", osa103::hexDump(packet, len, params.logHexBytes));
        }
    }

    void handleDataPayload(const uint8_t* data, int len, uint8_t flags) {
        int pairs = len / 4;
        if (pairs <= 0) { return; }
        if (!run) { return; }
        if (pairs > STREAM_BUFFER_SIZE) { pairs = STREAM_BUFFER_SIZE; }

        bool mute = params.muteOnFlagBit1 && (flags & 0x02);
        dsp::complex_t* out = stream.writeBuf;
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
            out[i].re = fi;
            out[i].im = fq;
        }
        if (!stream.swap(pairs)) { run = false; }
    }

    void worker() {
        std::vector<uint8_t> chunk(params.readChunk > 0 ? params.readChunk : 65536);
        bool configSent = false;
        auto startTime = std::chrono::steady_clock::now();

        while (run) {
            int n = ftdi.read(chunk.data(), (uint32_t)chunk.size());
            if (n < 0) {
                uint32_t st = (uint32_t)(-n);
                // Frames are still parsed below; only a real error stops the loop.
                if (st != 4) {
                    lastError = "FT_Read: " + osa103::FtdiD2xx::statusString(st);
                    flog::error("OSA103: {}", lastError);
                    break;
                }
                n = 0;
            }
            if (n > 0) {
                bytesRead += n;
                if (params.logFrames) {
                    flog::info("OSA103 RX raw: {}", osa103::hexDump(chunk.data(), (size_t)n, params.logHexBytes));
                }
                parser.feed(chunk.data(), (size_t)n);
            }

            // Deferred first configuration: wait for the device-ready info packet, but
            // do not wait forever if the device does not answer.
            if (!configSent && params.sendConfigOnStart && !params.sniffOnly) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - startTime)
                                   .count();
                if (deviceReady || elapsed > 400) {
                    sendConfigSnapshot(dialFreq);
                    configSent = true;
                }
            }
        }
    }

    // ------------------------------------------------------------------- stats
    void resetStats() {
        bytesRead = 0;
        txFrames = 0;
        parser.reset();
    }

    // -------------------------------------------------------------------- menu
    static void menuSelected(void* ctx) {
        Osa103SourceModule* _this = (Osa103SourceModule*)ctx;
        core::setInputSampleRate(_this->sampleRate);
        flog::info("OSA103SourceModule '{0}': Menu Select!", _this->name);
    }

    static void menuDeselected(void* ctx) {
        Osa103SourceModule* _this = (Osa103SourceModule*)ctx;
        flog::info("OSA103SourceModule '{0}': Menu Deselect!", _this->name);
    }

    static void start(void* ctx) {
        Osa103SourceModule* _this = (Osa103SourceModule*)ctx;
        if (_this->running) { return; }

        std::string err;
        if (!_this->ftdi.load(_this->params.d2xxLibraryPath, err)) {
            _this->lastError = err;
            flog::error("OSA103: {}", err);
            return;
        }
        if (_this->selectedSerial.empty()) {
            _this->lastError = "No device selected";
            flog::error("OSA103: {}", _this->lastError);
            return;
        }
        if (!_this->ftdi.openBySerial(_this->selectedSerial, err)) {
            _this->lastError = err;
            flog::error("OSA103: {}", err);
            return;
        }
        if (_this->params.resetOnOpen) { _this->ftdi.resetDevice(); }
        _this->ftdi.setBaudRate(_this->params.baudRate);
        _this->ftdi.setDataCharacteristics(8, 0, 0);
        // Captured vendor sequence: RTS/CTS flow control with RTS asserted
        // before the timeouts and the latency timer are set.
        _this->ftdi.setFlowControl(osa103::FtdiD2xx::FLOW_RTS_CTS, 0, 0);
        _this->ftdi.setRts();
        _this->ftdi.setTimeouts(_this->params.readTimeoutMs, _this->params.writeTimeoutMs);
        _this->ftdi.setLatencyTimer(_this->params.latencyMs);
        _this->ftdi.purge(osa103::FtdiD2xx::PURGE_RX | osa103::FtdiD2xx::PURGE_TX);

        if (_this->params.loadBitstream && !_this->loadBitstream()) {
            flog::error("OSA103: {}", _this->lastError);
        }

        _this->setupParser();
        _this->resetStats();
        _this->deviceReady = false;
        _this->stream.clearWriteStop();

        if (!_this->params.sniffOnly) {
            // Captured vendor sequence: freeze the D2XX task, purge only the
            // RX side, restart. Without this the first packets still carry
            // leftovers and the FPGA stays silent.
            _this->ftdi.stopInTask();
            _this->ftdi.purge(osa103::FtdiD2xx::PURGE_RX);
            _this->ftdi.restartInTask();
        }

        if (_this->params.sendHello && !_this->params.sniffOnly) { _this->sendHello(); }
        if (_this->params.sniffOnly) {
            flog::warn("OSA103: sniff-only mode, nothing will be transmitted");
        }

        _this->run = true;
        _this->workerThread = std::thread(&Osa103SourceModule::worker, _this);
        _this->running = true;
        flog::info("OSA103SourceModule '{0}': Start!", _this->name);
    }

    static void stop(void* ctx) {
        Osa103SourceModule* _this = (Osa103SourceModule*)ctx;
        if (!_this->running) { return; }
        _this->run = false;
        _this->stream.stopWriter();
        if (_this->workerThread.joinable()) { _this->workerThread.join(); }
        _this->stream.clearWriteStop();
        _this->ftdi.close();
        _this->running = false;
        flog::info("OSA103SourceModule '{0}': Stop!", _this->name);
    }

    static void tune(double freq, void* ctx) {
        Osa103SourceModule* _this = (Osa103SourceModule*)ctx;
        _this->dialFreq = freq;
        if (_this->running && _this->params.resendConfigOnTune && !_this->params.sniffOnly) {
            _this->sendConfigSnapshot(freq);
        }
        flog::info("OSA103SourceModule '{0}': Tune: {1}!", _this->name, freq);
    }

    static void menuHandler(void* ctx) {
        Osa103SourceModule* _this = (Osa103SourceModule*)ctx;
        TextBuffers& b = _this->bufs;
        bool dirty = false;

        if (_this->running) { SmGui::BeginDisabled(); }

        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Combo(CONCAT("##_osa103_dev_", _this->name), &_this->devId, _this->devices.txt)) {
            _this->select(_this->devices.key(_this->devId));
            dirty = true;
        }

        SmGui::SameLine();
        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Button(CONCAT("Refresh##_osa103_refresh_", _this->name))) {
            _this->refresh();
            _this->select(_this->params.serial);
        }

        SmGui::LeftLabel("Sample rate");
        SmGui::FillWidth();
        int sr = (int)llround(_this->sampleRate);
        if (SmGui::InputInt(CONCAT("##_osa103_sr_", _this->name), &sr, 1000, 10000)) {
            if (sr > 0) {
                _this->sampleRate = (double)sr;
                core::setInputSampleRate(_this->sampleRate);
                dirty = true;
            }
        }

        if (_this->running) { SmGui::EndDisabled(); }

        if (SmGui::CollapsingHeader(CONCAT("Tuning##_osa103_tuning_", _this->name))) {
            static const char* ncoModes = "Dial frequency\0Dial + IF\0Dial - IF\0IF only\0";
            SmGui::LeftLabel("NCO frequency");
            SmGui::FillWidth();
            if (SmGui::Combo(CONCAT("##_osa103_ncomode_", _this->name), &_this->params.ncoMode, ncoModes)) {
                dirty = true;
            }

            SmGui::LeftLabel("IF (Hz)");
            SmGui::FillWidth();
            int ifHz = (int)llround(_this->params.ncoIfHz);
            if (SmGui::InputInt(CONCAT("##_osa103_if_", _this->name), &ifHz, 1000, 100000)) {
                _this->params.ncoIfHz = (double)ifHz;
                dirty = true;
            }

            SmGui::LeftLabel("NCO scale");
            SmGui::FillWidth();
            if (SmGui::InputText(CONCAT("##_osa103_ncoscale_", _this->name), b.ncoScale, sizeof(b.ncoScale))) {
                _this->params.ncoScale = parseDouble(b.ncoScale, _this->params.ncoScale);
                dirty = true;
            }

            SmGui::LeftLabel("LO scale");
            SmGui::FillWidth();
            if (SmGui::InputText(CONCAT("##_osa103_loscale_", _this->name), b.loScale, sizeof(b.loScale))) {
                _this->params.loScale = parseDouble(b.loScale, _this->params.loScale);
                dirty = true;
            }
        }

        if (SmGui::CollapsingHeader(CONCAT("Front end##_osa103_frontend_", _this->name))) {
            SmGui::LeftLabel("Flags byte");
            SmGui::FillWidth();
            if (SmGui::InputText(CONCAT("##_osa103_flags_", _this->name), b.flags, sizeof(b.flags))) {
                _this->params.flags = (uint8_t)parseHex(b.flags, _this->params.flags);
                dirty = true;
            }

            SmGui::LeftLabel("Mode byte");
            SmGui::FillWidth();
            if (SmGui::InputText(CONCAT("##_osa103_mode_", _this->name), b.mode, sizeof(b.mode))) {
                _this->params.mode = (uint8_t)parseHex(b.mode, _this->params.mode);
                dirty = true;
            }

            SmGui::LeftLabel("Rx/Tx tag");
            SmGui::FillWidth();
            if (SmGui::InputText(CONCAT("##_osa103_rxtx_", _this->name), b.rxtxTag, sizeof(b.rxtxTag))) {
                _this->params.rxtxTag = (uint8_t)parseHex(b.rxtxTag, _this->params.rxtxTag);
                dirty = true;
            }

            SmGui::LeftLabel("Clock const");
            SmGui::FillWidth();
            if (SmGui::InputText(CONCAT("##_osa103_c6258_", _this->name), b.const6258, sizeof(b.const6258))) {
                _this->params.const6258 = (uint16_t)parseHex(b.const6258, _this->params.const6258);
                dirty = true;
            }

            SmGui::LeftLabel("ADC gain word");
            SmGui::FillWidth();
            if (SmGui::InputText(CONCAT("##_osa103_gain_", _this->name), b.adcGain, sizeof(b.adcGain))) {
                _this->params.adcGainWord = (uint16_t)parseHex(b.adcGain, _this->params.adcGainWord);
                dirty = true;
            }

            SmGui::LeftLabel("ADC offset 1");
            SmGui::FillWidth();
            if (SmGui::InputText(CONCAT("##_osa103_off1_", _this->name), b.adcOff1, sizeof(b.adcOff1))) {
                _this->params.adcOffWord = (uint16_t)parseHex(b.adcOff1, _this->params.adcOffWord);
                dirty = true;
            }

            SmGui::LeftLabel("ADC offset 2");
            SmGui::FillWidth();
            if (SmGui::InputText(CONCAT("##_osa103_off2_", _this->name), b.adcOff2, sizeof(b.adcOff2))) {
                _this->params.adcOffWord2 = (uint16_t)parseHex(b.adcOff2, _this->params.adcOffWord2);
                dirty = true;
            }
        }

        if (SmGui::CollapsingHeader(CONCAT("Samples##_osa103_samples_", _this->name))) {
            if (SmGui::Checkbox(CONCAT("Swap I/Q##_osa103_swap_", _this->name), &_this->params.swapIQ)) { dirty = true; }
            if (SmGui::Checkbox(CONCAT("Invert Q##_osa103_invq_", _this->name), &_this->params.invertQ)) { dirty = true; }
            if (SmGui::Checkbox(CONCAT("Mute on Tx flag##_osa103_mute_", _this->name), &_this->params.muteOnFlagBit1)) { dirty = true; }

            SmGui::LeftLabel("Sample scale");
            SmGui::FillWidth();
            char scaleBuf[32];
            snprintf(scaleBuf, sizeof(scaleBuf), "%.6g", _this->params.sampleScale);
            if (SmGui::InputText(CONCAT("##_osa103_scale_", _this->name), scaleBuf, sizeof(scaleBuf))) {
                _this->params.sampleScale = parseDouble(scaleBuf, _this->params.sampleScale);
                dirty = true;
            }
        }

        if (SmGui::CollapsingHeader(CONCAT("Protocol##_osa103_proto_", _this->name))) {
            if (SmGui::Checkbox(CONCAT("CRC enabled##_osa103_crc_", _this->name), &_this->params.crcEnabled)) { dirty = true; }
            if (SmGui::Checkbox(CONCAT("Rx length field##_osa103_rxlen_", _this->name), &_this->params.rxHasLenField)) { dirty = true; }
            if (SmGui::Checkbox(CONCAT("Rx flags byte##_osa103_rxflags_", _this->name), &_this->params.hasFlagsByte)) { dirty = true; }

            SmGui::LeftLabel("Rx I/Q bytes");
            SmGui::FillWidth();
            if (SmGui::InputInt(CONCAT("##_osa103_rxbytes_", _this->name), &_this->params.dataBytes, 4, 64)) { dirty = true; }

            SmGui::LeftLabel("0x5A packet bytes");
            SmGui::FillWidth();
            if (SmGui::InputInt(CONCAT("##_osa103_rxinfo_", _this->name), &_this->params.infoPacketBytes, 1, 16)) { dirty = true; }
            if (SmGui::Checkbox(CONCAT("Send hello##_osa103_hello_", _this->name), &_this->params.sendHello)) { dirty = true; }
            if (SmGui::Checkbox(CONCAT("Config on start##_osa103_cfgstart_", _this->name), &_this->params.sendConfigOnStart)) { dirty = true; }
            if (SmGui::Checkbox(CONCAT("Config on tune##_osa103_cfgtune_", _this->name), &_this->params.resendConfigOnTune)) { dirty = true; }
            if (SmGui::Checkbox(CONCAT("Send type 1 table##_osa103_t1_", _this->name), &_this->params.sendType1Table)) { dirty = true; }
            if (SmGui::Checkbox(CONCAT("Load FPGA bitstream##_osa103_bitstream_", _this->name), &_this->params.loadBitstream)) { dirty = true; }
            if (SmGui::Checkbox(CONCAT("Sniff only##_osa103_sniff_", _this->name), &_this->params.sniffOnly)) { dirty = true; }
            if (SmGui::Checkbox(CONCAT("Log frames##_osa103_logf_", _this->name), &_this->params.logFrames)) { dirty = true; }

            SmGui::LeftLabel("Bitstream file");
            SmGui::FillWidth();
            if (SmGui::InputText(CONCAT("##_osa103_bfile_", _this->name), b.bitstreamPath, sizeof(b.bitstreamPath))) {
                _this->params.bitstreamPath = bufToStr(b.bitstreamPath);
                dirty = true;
            }

            SmGui::LeftLabel("Config image override");
            SmGui::FillWidth();
            if (SmGui::InputText(CONCAT("##_osa103_ovr_", _this->name), b.imageOverride, sizeof(b.imageOverride))) {
                _this->params.imageOverrideHex = bufToStr(b.imageOverride);
                dirty = true;
            }
        }

        if (_this->running) { SmGui::BeginDisabled(); }
        if (SmGui::CollapsingHeader(CONCAT("Advanced##_osa103_adv_", _this->name))) {
            SmGui::LeftLabel("D2XX library");
            SmGui::FillWidth();
            if (SmGui::InputText(CONCAT("##_osa103_d2xx_", _this->name), b.d2xxPath, sizeof(b.d2xxPath))) {
                _this->params.d2xxLibraryPath = bufToStr(b.d2xxPath);
                dirty = true;
            }
        }
        if (_this->running) { SmGui::EndDisabled(); }

        if (SmGui::Button(CONCAT("Send configuration##_osa103_send_", _this->name))) {
            if (_this->running) { _this->sendConfigSnapshot(_this->dialFreq); }
            else { flog::warn("OSA103: start the source first"); }
        }

        SmGui::SameLine();
        if (SmGui::Button(CONCAT("Reset stats##_osa103_rs_", _this->name))) { _this->resetStats(); }

        if (_this->ftdi.loaded()) {
            SmGui::Text(("D2XX: " + _this->ftdi.libraryPath()).c_str());
        }
        else {
            // Red, always visible while the FTDI library is missing: the module
            // loads ftd2xx.dll dynamically at runtime, so a missing driver is a
            // runtime condition, not a build-time one.
            SmGui::TextColored(ImVec4(1.0f, 0.25f, 0.25f, 1.0f),
                               "FTDI D2XX library not found - install the FTDI driver");
        }
        SmGui::Text(("read " + std::to_string(_this->bytesRead.load()) + " B, dropped " +
                     std::to_string((unsigned long long)_this->parser.bytesDiscarded) + " B").c_str());
        SmGui::Text(("packets " + std::to_string((unsigned long long)_this->parser.dataPackets) + ", info " +
                     std::to_string((unsigned long long)_this->parser.infoPackets) + ", TX " +
                     std::to_string(_this->txFrames.load()) + ", CRC err " +
                     std::to_string((unsigned long long)_this->parser.crcErrors)).c_str());
        SmGui::Text(("last flags 0x" + [&] {
                         char tmp[8];
                         snprintf(tmp, sizeof(tmp), "%02X", (unsigned)_this->parser.lastFlags);
                         return std::string(tmp);
                     }()).c_str());
        if (!_this->lastError.empty()) {
            SmGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), _this->lastError.c_str());
        }

        if (dirty) { _this->saveConfig(); }
    }

    std::string name;
    bool enabled = true;
    bool running = false;

    SourceManager::SourceHandler handler;
    dsp::stream<dsp::complex_t> stream;

    osa103::Params params;
    osa103::FtdiD2xx ftdi;
    TextBuffers bufs;

    OptionList<std::string, std::string> devices;
    int devId = 0;
    std::string selectedSerial;

    double sampleRate = 65104.0;
    double dialFreq = 100000000.0;

    std::thread workerThread;
    std::atomic<bool> run{false};
    std::mutex ioMutex;

    osa103::Parser parser{params};
    std::atomic<bool> deviceReady{false};

    std::atomic<uint64_t> bytesRead{0};
    std::atomic<uint64_t> txFrames{0};

    std::string lastError;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    osa103::Params defaults;
    Osa103SourceModule::paramsToJson(def, defaults, 65104.0, 100000000.0);
    config.setPath(core::args["root"].s() + "/osa103_source_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new Osa103SourceModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) { delete (Osa103SourceModule*)instance; }

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
