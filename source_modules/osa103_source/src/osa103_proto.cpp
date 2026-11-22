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
#include "osa103_proto.h"
#include <cmath>
#include <cstring>
#include <vector>

namespace osa103 {

namespace {

inline void put16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

inline void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

// The 256-entry table the DLL carries at 0x4E65EA: reflected 0x8005 (CRC-16/ARC).
struct CrcTable {
    uint16_t t[256];
    CrcTable() {
        for (int i = 0; i < 256; i++) {
            uint16_t c = (uint16_t)i;
            for (int b = 0; b < 8; b++) { c = (c & 1) ? (uint16_t)((c >> 1) ^ 0xA001) : (uint16_t)(c >> 1); }
            t[i] = c;
        }
    }
};

const CrcTable kCrc;

}  // namespace

uint16_t crc16(const uint8_t* data, size_t len, uint16_t init) {
    uint16_t crc = init;
    for (size_t i = 0; i < len; i++) { crc = (uint16_t)((crc >> 8) ^ kCrc.t[(crc ^ data[i]) & 0xFF]); }
    return crc;
}

size_t buildSync(const Params& p, uint8_t* out, size_t outCapacity) {
    size_t need = (size_t)p.syncRepeat * 3 + 1;
    if (outCapacity < need) { return 0; }
    size_t n = 0;
    for (int i = 0; i < p.syncRepeat; i++) {
        out[n++] = p.syncByte0;
        out[n++] = p.syncByte1;
        out[n++] = p.syncByte2;
    }
    out[n++] = p.syncTerminator;
    return n;
}

size_t buildFrame(const Params& p, uint8_t type, const uint8_t* payload, size_t payloadLen,
                  uint8_t* out, size_t outCapacity) {
    size_t syncLen = buildSync(p, out, outCapacity);
    if (!syncLen) { return 0; }

    size_t bodyLen = 1 + payloadLen;              // type + payload
    size_t crcLen = p.crcEnabled ? 2 : 0;
    size_t total = syncLen + 2 + bodyLen + crcLen;
    if (outCapacity < total) { return 0; }

    uint8_t* f = out + syncLen;
    put16(f, (uint16_t)bodyLen);
    f[2] = type;
    if (payloadLen) { memcpy(f + 3, payload, payloadLen); }
    if (p.crcEnabled) {
        uint16_t crc = crc16(f, 2 + bodyLen, p.crcInit);
        put16(f + 2 + bodyLen, crc);
    }
    return total;
}

size_t buildConfigImage(const Params& p, double dialHz, uint8_t* out, size_t outCapacity) {
    if (outCapacity < kConfigImageLen) { return 0; }

    // A caller-supplied image wins over everything the generator would do.
    if (!p.imageOverrideHex.empty()) {
        std::vector<uint8_t> raw;
        if (parseHexBytes(p.imageOverrideHex, raw) && raw.size() == kConfigImageLen) {
            memcpy(out, raw.data(), kConfigImageLen);
            return kConfigImageLen;
        }
    }

    memset(out, 0, kConfigImageLen);

    // NCO frequency for the receive side: the DLL derives it from the dial frequency
    // and the IF offset; the exact relationship is one of the open questions.
    double ncoHz = dialHz;
    switch (p.ncoMode) {
    case 0: ncoHz = dialHz; break;
    case 1: ncoHz = dialHz + p.ncoIfHz; break;
    case 2: ncoHz = dialHz - p.ncoIfHz; break;
    case 3: ncoHz = p.ncoIfHz; break;
    default: ncoHz = dialHz; break;
    }
    if (ncoHz < 0.0) { ncoHz = 0.0; }

    uint32_t nco = (uint32_t)(ncoWord(ncoHz, p.ncoScale) & 0xFFFFFFFF);
    int64_t loRaw = (int64_t)llround(dialHz * p.loScale);
    uint32_t lo = (uint32_t)(loRaw & 0xFFFFFFFF);
    uint8_t loByte4 = (uint8_t)((loRaw >> 32) & 0xFF);

    uint8_t* q = out;
    q[0] = p.tag3;
    q[1] = p.tag4;
    q[2] = p.tag5;
    q[3] = p.tag6;
    put32(q + 4, nco);                    // frame 7..10
    put32(q + 8, lo);                     // frame 11..14
    q[12] = loByte4;                      // frame 15
    q[13] = p.rxtxTag;                    // frame 16
    q[14] = p.dividerTag;                 // frame 17
    q[15] = p.txDivShift;                 // frame 18
    put16(q + 16, p.txDivQ);              // frame 19..20
    put16(q + 18, p.txDivR);              // frame 21..22
    q[20] = p.dividerTag2;                // frame 23
    q[21] = p.txDivShift2;                // frame 24
    put16(q + 22, p.txDivQ2);             // frame 25..26
    put16(q + 24, p.txDivR2);             // frame 27..28
    put16(q + 26, p.const6258);           // frame 29..30
    q[28] = p.tag31;                      // frame 31
    q[29] = p.tag32;                      // frame 32
    q[30] = p.tag33;                      // frame 33
    q[31] = p.tag34;                      // frame 34
    q[32] = p.flags;                      // frame 35
    // q[33..35] stay zero             // frame 36..38
    put16(q + 36, p.adcGainWord);         // frame 39..40
    // q[38..45] stay zero             // frame 41..48
    // Captured calibration block: six constant bytes at frames 49..54.
    q[46] = p.calA;                       // frame 49
    q[47] = p.calB;
    q[48] = p.calC;
    q[49] = p.calD;
    q[50] = p.calE;
    q[51] = p.calF;                       // frame 54
    // q[52..55] stay zero             // frame 55..58
    q[56] = p.tag55;                      // frame 59: constant 0x82
    q[57] = (uint8_t)(p.tag56 & 0xFF);    // frame 60: constant 0x01
    q[58] = (uint8_t)(p.tag56 >> 8);      // frame 61: constant 0xAD
    // q[59..60] stay zero             // frame 62..63
    q[61] = p.tag64;                      // frame 64
    q[62] = p.tag65;
    q[63] = p.tag66;
    q[64] = p.tag67;
    q[65] = p.tag68;
    q[66] = p.tag69;                      // frame 69
    q[67] = p.mode;                       // frame 70
    // Trailing NCO word: (dial + offset) * 2^31 / 2e8, captured with a fixed
    // -16 kHz offset in the vendor's initial frame.
    put32(q + 68, p.sendTxNco
                    ? (uint32_t)(ncoWord(dialHz + p.txNcoOffsetHz, p.txNcoScale) & 0xFFFFFFFF)
                    : 0u);
    q[72] = p.level;                      // frame 75
    return kConfigImageLen;
}

size_t buildConfigFrame(const Params& p, double dialHz, uint8_t* out, size_t outCapacity) {
    uint8_t image[kConfigImageLen];
    if (!buildConfigImage(p, dialHz, image, sizeof(image))) { return 0; }
    return buildFrame(p, 0x03, image, sizeof(image), out, outCapacity);
}

size_t buildType1Frame(const Params& p, uint16_t addr, uint16_t fillValue, uint8_t* out,
                       size_t outCapacity) {
    // 2 constant bytes + address + 512 x 12-bit words
    const size_t payloadLen = 4 + 512 * 2;
    std::vector<uint8_t> payload(payloadLen);
    payload[0] = 0x00;
    payload[1] = 0x04;
    put16(payload.data() + 2, addr);
    for (size_t i = 0; i < 512; i++) {
        payload[4 + i * 2] = (uint8_t)(fillValue & 0xFF);
        payload[4 + i * 2 + 1] = (uint8_t)((fillValue >> 8) & 0x0F);
    }
    return buildFrame(p, 0x01, payload.data(), payloadLen, out, outCapacity);
}

size_t buildTableFrame(const Params& p, int block, uint8_t* out, size_t outCapacity) {
    // The eight blocks the working vendor session uploads carry one continuous
    // ramp: blocks 0..3 run 0x0555..0x0D50, block 4 is all zeros, block 5 is 169
    // zeros then 0x0000..0x0156, blocks 6..7 run 0x0157..0x0554. Generated here,
    // so no vendor data is reproduced.
    static const uint16_t kStart[8] = {0x0555, 0x0754, 0x0953, 0x0B52, 0, 0, 0x0157, 0x0356};
    if (block < 0 || block >= 8) { return 0; }

    uint16_t addr = (uint16_t)(p.type1BaseAddr + (uint16_t)block * p.type1Stride);
    uint8_t payload[4 + 512 * 2];
    payload[0] = 0x00;
    payload[1] = 0x04;
    put16(payload + 2, addr);
    for (int i = 0; i < 512; i++) {
        uint16_t w = 0;
        if (block <= 3) { w = (uint16_t)(kStart[block] + i); }
        else if (block == 5) { w = (i < 169) ? 0 : (uint16_t)(i - 169); }
        else if (block >= 6) { w = (uint16_t)(kStart[block] + i); }
        put16(payload + 4 + i * 2, w);
    }
    return buildFrame(p, 0x01, payload, sizeof(payload), out, outCapacity);
}

int64_t ncoWord(double hz, double scale) { return (int64_t)llround(hz * scale); }

// ------------------------------------------------------------------- Parser

Parser::Parser(const Params& params) : p(params) { buf.reserve(1 << 16); }

void Parser::reset() {
    buf.clear();
    bytesDiscarded = 0;
    dataPackets = 0;
    infoPackets = 0;
    crcErrors = 0;
    lastFlags = 0;
}

void Parser::feed(const uint8_t* data, size_t len) {
    buf.insert(buf.end(), data, data + len);
    process();
}

void Parser::process() {
    const int patLen = p.syncRepeat * 3;
    const size_t syncLen = (size_t)patLen + 1;
    if (p.syncRepeat <= 0) { return; }

    while (true) {
        if (buf.size() < syncLen + 1) { return; }

        // Find the sync pattern: the 3-byte triple repeated, then a terminator.
        size_t start = (size_t)-1;
        for (size_t i = 0; i + syncLen <= buf.size(); i++) {
            bool match = true;
            for (int k = 0; k < p.syncRepeat && match; k++) {
                if (buf[i + k * 3 + 0] != p.syncByte0) { match = false; }
                if (buf[i + k * 3 + 1] != p.syncByte1) { match = false; }
                if (buf[i + k * 3 + 2] != p.syncByte2) { match = false; }
            }
            if (match && buf[i + patLen] == p.syncTerminator) {
                start = i;
                break;
            }
        }

        if (start == (size_t)-1) {
            // Nothing usable: keep only a tail that could still grow into a pattern.
            if (buf.size() > syncLen) {
                bytesDiscarded += buf.size() - syncLen;
                buf.erase(buf.begin(), buf.end() - syncLen);
            }
            return;
        }
        if (start > 0) {
            bytesDiscarded += start;
            buf.erase(buf.begin(), buf.begin() + start);
        }

        const size_t typeOff = p.rxHasLenField ? 3 : 1;
        if (buf.size() < syncLen + typeOff) { return; }

        const uint8_t type = buf[syncLen + typeOff - 1];
        const size_t crcLen = (p.crcEnabled && p.crcBytes > 0) ? (size_t)p.crcBytes : 0;
        size_t need = 0;
        if (p.rxHasLenField) {
            // Same header as the host frame: u16 len = 1 + payload length.
            uint16_t declared = (uint16_t)(buf[syncLen] | (buf[syncLen + 1] << 8));
            need = 4 + (size_t)declared;
        }
        else {
            need = 1 + (p.hasFlagsByte ? 1u : 0u) + (size_t)p.dataBytes + crcLen;
        }

        if (type == 0x5A) {
            // Fixed-size info packet. infoPacketBytes counts the bytes after
            // the sync prefix (5A + subtype + body + CRC).
            size_t packetLen = p.rxHasLenField ? need
                                               : (size_t)(p.infoPacketBytes > 0 ? p.infoPacketBytes : 1);
            if (buf.size() < syncLen + packetLen) { return; }
            int bodyLen = (int)(packetLen - typeOff - crcLen);
            if (bodyLen < 0) { bodyLen = 0; }
            infoPackets++;
            if (onInfo) { onInfo(buf.data() + syncLen + typeOff, bodyLen); }
            buf.erase(buf.begin(), buf.begin() + syncLen + packetLen);
            continue;
        }

        if (buf.size() < syncLen + need) { return; }

        const uint8_t* packet = buf.data() + syncLen;
        bool ok = true;
        if (crcLen >= 2) { ok = (crc16(packet, need, p.crcInit) == 0); }
        if (ok) {
            uint8_t flags = 0;
            const uint8_t* payload = packet + typeOff;
            int payloadLen = (int)(need - typeOff - crcLen);
            if (p.hasFlagsByte) {
                flags = payload[0];
                payload++;
                payloadLen--;
            }
            lastFlags = flags;
            dataPackets++;
            if (onData) { onData(payload, payloadLen, flags); }
        }
        else {
            crcErrors++;
        }
        buf.erase(buf.begin(), buf.begin() + syncLen + need);
    }
}

std::string hexDump(const uint8_t* data, size_t len, size_t maxBytes) {
    static const char* digits = "0123456789ABCDEF";
    std::string out;
    size_t n = len < maxBytes ? len : maxBytes;
    out.reserve(n * 3 + 8);
    for (size_t i = 0; i < n; i++) {
        if (i) { out += ' '; }
        out += digits[data[i] >> 4];
        out += digits[data[i] & 0x0F];
    }
    if (n < len) { out += " ..."; }
    return out;
}

bool parseHexBytes(const std::string& text, std::vector<uint8_t>& out) {
    out.clear();
    // Drop C-style prefixes so "0xAA 0x0F" parses the same as "AA 0F".
    std::string cleaned;
    cleaned.reserve(text.size());
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == '0' && i + 1 < text.size() && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
            i++;
            continue;
        }
        cleaned += text[i];
    }

    int hi = -1;
    for (char c : cleaned) {
        int v;
        if (c >= '0' && c <= '9') { v = c - '0'; }
        else if (c >= 'a' && c <= 'f') { v = c - 'a' + 10; }
        else if (c >= 'A' && c <= 'F') { v = c - 'A' + 10; }
        else { continue; }  // separators: space, comma, newlines
        if (hi < 0) { hi = v; }
        else {
            out.push_back((uint8_t)((hi << 4) | v));
            hi = -1;
        }
    }
    return hi < 0 && !out.empty();
}

}  // namespace osa103
