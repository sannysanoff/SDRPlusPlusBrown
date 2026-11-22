/*
 * OSA103 source module for SDR++.
 * Copyright (c) 2026 the OSA103 source module authors. Non-commercial / educational use only,
 * see LICENSE.md in this directory.
 *
 * Protocol reconstructed from the vendor's ExtIO_Osa.dll v3.09 (OSA103, osa103.ru)
 * for interoperability. No vendor code, listing or bitstream is included.
 * Hardware-validated against a live OSA103 (FT232R) through an FT_Write/FT_Read
 * MITM capture of the vendor's ExtIO session; see README.md for the full story.
 */
// Standalone check of the protocol layer. The encoder assertions were first
// derived by static analysis and are now pinned to hardware truth: the config
// frame regression compares byte-for-byte against a captured vendor frame, and
// the parser model matches the device's real inbound packets (data = sync +
// type + 2048 bytes of int16 LE I/Q + CRC; info = sync + 5A + 148 + CRC).
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "osa103_proto.h"
#include "vendor_config_9108500.h"

static int failures = 0;

static void check(const char* what, bool ok) {
    printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) { failures++; }
}

static void checkEq(const char* what, long long got, long long want) {
    bool ok = (got == want);
    printf("%s  %s (got %lld, want %lld)\n", ok ? "PASS" : "FAIL", what, got, want);
    if (!ok) { failures++; }
}


// Builds a device->host packet: sync prefix, [optional u16 len], type, payload, CRC.
// With rxHasLenField the len field is 1 + payload length, exactly like the host frame.
static std::vector<uint8_t> devicePacket(const osa103::Params& p, uint8_t type,
                                         const std::vector<uint8_t>& payload, bool withLen) {
    std::vector<uint8_t> out;
    uint8_t sync[32];
    size_t syncLen = osa103::buildSync(p, sync, sizeof(sync));
    out.insert(out.end(), sync, sync + syncLen);

    size_t bodyLen = 1 + payload.size();
    uint8_t head[2];
    if (withLen) {
        head[0] = (uint8_t)(bodyLen & 0xFF);
        head[1] = (uint8_t)((bodyLen >> 8) & 0xFF);
        out.insert(out.end(), head, head + 2);
    }
    out.push_back(type);
    out.insert(out.end(), payload.begin(), payload.end());

    // CRC covers everything after the sync prefix, up to and including the payload.
    size_t start = syncLen;
    uint16_t crc = osa103::crc16(out.data() + start, out.size() - start, p.crcInit);
    out.push_back((uint8_t)(crc & 0xFF));
    out.push_back((uint8_t)(crc >> 8));
    return out;
}

int main() {
    osa103::Params p;
    uint8_t buf[8192];

    // 1. CRC known-answer test. The DLL drives the reflected 0xA001 table with an
    //    init of 0xFFFF, i.e. the CRC-16/MODBUS parameters, whose check value is 0x4B37.
    const uint8_t kat[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    checkEq("crc16(123456789) == 0x4B37", osa103::crc16(kat, sizeof(kat)), 0x4B37);

    // 2. Sync prefix is 8 x AA 0F 55 then 00.
    size_t syncLen = osa103::buildSync(p, buf, sizeof(buf));
    checkEq("sync length", (long long)syncLen, 25);
    check("sync pattern", buf[0] == 0xAA && buf[1] == 0x0F && buf[2] == 0x55 && buf[24] == 0x00);

    // 3. Type 0x03 configuration frame: 78 bytes total, len field 74, payload 73.
    size_t cfgLen = osa103::buildConfigFrame(p, 100e6, buf, sizeof(buf));
    // 25 bytes of sync prefix + 78-byte frame (2 len + 1 type + 73 payload + 2 CRC).
    checkEq("config buffer length", (long long)cfgLen, 103);
    checkEq("config frame length", (long long)(cfgLen - syncLen), 78);
    const uint8_t* f = buf + syncLen;
    checkEq("config len field", f[0] | (f[1] << 8), 74);
    checkEq("config frame type", f[2], 0x03);
    checkEq("config CRC residue over whole frame", osa103::crc16(f, 78), 0x0);
    checkEq("config flags byte", f[3 + 32], 0x39);   // preamp | 50R | dither | 0x10
    checkEq("config mode byte", f[3 + 67], 0x1D);    // vendor session value
    // 100 MHz with the observed NCO scale lands on 2^31 exactly.
    const uint8_t* img = f + 3;
    uint32_t ncoWord = (uint32_t)img[4] | ((uint32_t)img[5] << 8) | ((uint32_t)img[6] << 16) |
                       ((uint32_t)img[7] << 24);
    checkEq("config NCO word (100 MHz)", (long long)ncoWord, 0x80000000LL);
    uint32_t loWord = (uint32_t)img[8] | ((uint32_t)img[9] << 8) | ((uint32_t)img[10] << 16) |
                      ((uint32_t)img[11] << 24);
    checkEq("config LO field (100 MHz)", (long long)loWord, 100000000LL);

    // 4. Type 0x01 block write: 1033 bytes total, len field 1029, 512 12-bit words.
    size_t t1Len = osa103::buildType1Frame(p, 0xC000, 0x0555, buf, sizeof(buf));
    checkEq("type1 buffer length", (long long)t1Len, 1058);
    checkEq("type1 frame length", (long long)(t1Len - syncLen), 1033);
    const uint8_t* g = buf + syncLen;
    checkEq("type1 len field", g[0] | (g[1] << 8), 1029);
    checkEq("type1 frame type", g[2], 0x01);
    checkEq("type1 CRC residue over whole frame", osa103::crc16(g, 1033), 0x0);
    checkEq("type1 address field", g[5] | (g[6] << 8), 0xC000);
    checkEq("type1 first word", g[7] | ((g[8] & 0x0F) << 8), 0x0555);
    checkEq("type1 last word offset", 4 + 2 + 511 * 2, 1028);

    // 4b. The eight ramp blocks the vendor session uploads after every config
    //     frame. Hardware-validated: the stream only runs when these are sent.
    {
        static const uint16_t kStart[8] = {0x0555, 0x0754, 0x0953, 0x0B52, 0, 0, 0x0157, 0x0356};
        for (int b = 0; b < 8; b++) {
            char label[64];
            size_t n = osa103::buildTableFrame(p, b, buf, sizeof(buf));
            snprintf(label, sizeof(label), "table block %d length", b);
            checkEq(label, (long long)n, 1058);
            snprintf(label, sizeof(label), "table block %d CRC residue", b);
            check(label, n == 1058 && osa103::crc16(buf + syncLen, 1033) == 0);
            const uint8_t* t = buf + syncLen;
            snprintf(label, sizeof(label), "table block %d address", b);
            checkEq(label, t[5] | (t[6] << 8), 0xC000 + b * 0x0400);
            snprintf(label, sizeof(label), "table block %d first word", b);
            uint16_t first = t[7] | ((t[8] & 0x0F) << 8);
            checkEq(label, first, kStart[b]);
            snprintf(label, sizeof(label), "table block %d last word", b);
            uint16_t last = t[7 + 511 * 2] | ((t[8 + 511 * 2] & 0x0F) << 8);
            uint16_t wantLast = (b <= 3 || b >= 6) ? (uint16_t)(kStart[b] + 511)
                                                  : (uint16_t)(b == 5 ? 511 - 169 : 0);
            checkEq(label, last, wantLast);
        }
        // Block 4 is all zeros; block 5 carries 169 zero words first.
        osa103::buildTableFrame(p, 4, buf, sizeof(buf));
        bool zero4 = true;
        for (int i = 0; i < 512; i++) {
            if (buf[syncLen + 7 + i * 2] | (buf[syncLen + 8 + i * 2] & 0x0F)) { zero4 = false; }
        }
        check("table block 4 all zeros", zero4);
        osa103::buildTableFrame(p, 5, buf, sizeof(buf));
        uint16_t w169 = buf[syncLen + 7 + 169 * 2] | ((buf[syncLen + 8 + 169 * 2] & 0x0F) << 8);
        checkEq("table block 5 word 169", w169, 0);
    }

    // 4c. Regression against a real captured vendor frame: ExtIO_Osa.dll v3.09,
    //     dial 9.1085 MHz, the first FT_Write(78) after FTDI bring-up. The
    //     builder reproduces it byte for byte, CRC included.
    {
        size_t vLen = osa103::buildConfigFrame(p, 9108500.0, buf, sizeof(buf));
        checkEq("vendor frame: our total length", (long long)vLen, 103);
        checkEq("vendor frame: captured array size", (long long)sizeof(kVendorCfg9108500), 78);
        bool same = (vLen == 103) && memcmp(buf + syncLen, kVendorCfg9108500, 78) == 0;
        check("vendor config frame byte-exact (78/78)", same);
        if (!same && vLen == 103) {
            printf("  ours  : %s\n  vendor: %s\n",
                   osa103::hexDump(buf + syncLen, 78, 78).c_str(),
                   osa103::hexDump(kVendorCfg9108500, sizeof(kVendorCfg9108500), 78).c_str());
        }
    }

    // 5. NCO maths and hex helpers.
    checkEq("ncoWord(65104 Hz)", (long long)osa103::ncoWord(65104.0, p.ncoScale), 1398098);
    std::vector<uint8_t> parsed;
    check("parseHexBytes spaced", osa103::parseHexBytes("AA 0F 55", parsed) && parsed.size() == 3 &&
                                      parsed[0] == 0xAA && parsed[2] == 0x55);
    check("parseHexBytes 0x-prefixed", osa103::parseHexBytes("0xaa,0x0f", parsed) && parsed.size() == 2 &&
                                           parsed[1] == 0x0F);
    checkEq("hexDump length", (long long)osa103::hexDump(buf, 4, 96).size(), 11);

    // 6. The image override must be used verbatim.
    osa103::Params q;
    q.imageOverrideHex = "";
    for (int i = 0; i < 73; i++) { q.imageOverrideHex += "5A"; }
    uint8_t overrideImage[73];
    osa103::buildConfigImage(q, 100e6, overrideImage, sizeof(overrideImage));
    check("image override used verbatim", overrideImage[0] == 0x5A && overrideImage[72] == 0x5A);


    // 7. Receive path: feed synthetic device streams through the parser in awkward
    //    chunk sizes, in both inbound layouts (with and without a length field).
    for (int variant = 0; variant < 2; variant++) {
        const bool withLen = (variant == 1);
        osa103::Params rp;
        rp.rxHasLenField = withLen;
        // Hardware inbound format: after the sync prefix a 0x5A packet is
        // 5A + 148 bytes (subtype + 147 body) + CRC = 151; a 0x00 packet is
        // type + 2048 bytes of int16 LE I/Q + CRC with no flags byte.
        rp.infoPacketBytes = 151;
        osa103::Parser parser(rp);
        int dataCalls = 0, infoCalls = 0, lastPairs = 0;
        uint8_t lastFlagByte = 0xFF;
        int16_t firstI = 0, firstQ = 0, lastI = 0, lastQ = 0;
        parser.onData = [&](const uint8_t* d, int len, uint8_t flags) {
            dataCalls++;
            lastPairs = len / 4;
            lastFlagByte = flags;
            firstI = (int16_t)(((uint16_t)d[1] << 8) | d[0]);
            firstQ = (int16_t)(((uint16_t)d[3] << 8) | d[2]);
            const uint8_t* s = d + (len / 4 - 1) * 4;
            lastI = (int16_t)(((uint16_t)s[1] << 8) | s[0]);
            lastQ = (int16_t)(((uint16_t)s[3] << 8) | s[2]);
        };
        parser.onInfo = [&](const uint8_t* d, int len) {
            infoCalls++;
            (void)d;
            (void)len;
        };

        std::vector<uint8_t> payload;
        for (int i = 0; i < 512; i++) {  // 512 I/Q pairs, no flags byte
            int16_t iv = (int16_t)(i - 256);
            int16_t qv = (int16_t)(256 - i);
            payload.push_back((uint8_t)(iv & 0xFF));
            payload.push_back((uint8_t)((iv >> 8) & 0xFF));
            payload.push_back((uint8_t)(qv & 0xFF));
            payload.push_back((uint8_t)((qv >> 8) & 0xFF));
        }
        std::vector<uint8_t> dataPkt = devicePacket(rp, 0x00, payload, withLen);

        std::vector<uint8_t> info(148, 0);  // subtype + 147 body bytes
        info[0] = 0x02;  // device ready
        info[1] = 0x05;
        info[2] = 0x03;
        std::vector<uint8_t> infoPkt = devicePacket(rp, 0x5A, info, withLen);

        std::vector<uint8_t> streamBytes;
        streamBytes.insert(streamBytes.end(), dataPkt.begin(), dataPkt.end());
        streamBytes.insert(streamBytes.end(), infoPkt.begin(), infoPkt.end());
        for (int i = 0; i < 40; i++) { streamBytes.push_back(0x11); }  // garbage
        streamBytes.insert(streamBytes.end(), dataPkt.begin(), dataPkt.end());

        std::string tag = withLen ? "[len-field] " : "[bare] ";
        size_t pos = 0;
        for (size_t step : {(size_t)7, (size_t)13, (size_t)1}) {
            parser.feed(streamBytes.data() + pos, step);
            pos += step;
        }
        parser.feed(streamBytes.data() + pos, streamBytes.size() - pos);

        checkEq((tag + "data packets").c_str(), (long long)parser.dataPackets, 2);
        checkEq((tag + "info packets").c_str(), (long long)parser.infoPackets, 1);
        checkEq((tag + "data callbacks").c_str(), dataCalls, 2);
        checkEq((tag + "info callbacks").c_str(), infoCalls, 1);
        checkEq((tag + "CRC errors").c_str(), (long long)parser.crcErrors, 0);
        checkEq((tag + "samples per packet").c_str(), lastPairs, 512);
        checkEq((tag + "dropped bytes").c_str(), (long long)parser.bytesDiscarded, 40);
        checkEq((tag + "flag byte").c_str(), lastFlagByte, 0x00);
        checkEq((tag + "first I").c_str(), firstI, -256);
        checkEq((tag + "first Q").c_str(), firstQ, 256);
        checkEq((tag + "last I").c_str(), lastI, 255);
        checkEq((tag + "last Q").c_str(), lastQ, -255);

        // A corrupted CRC must be rejected and counted, not delivered.
        std::vector<uint8_t> bad = dataPkt;
        bad.back() ^= 0xFF;
        int before = dataCalls;
        parser.feed(bad.data(), bad.size());
        checkEq((tag + "corrupt packet rejected").c_str(), dataCalls, before);
        checkEq((tag + "corrupt packet counted").c_str(), (long long)parser.crcErrors, 1);

        // Disabling the CRC check must let the same packet through.
        rp.crcEnabled = false;
        parser.setParams(rp);
        parser.feed(bad.data(), bad.size());
        checkEq((tag + "CRC check disabled accepts packet").c_str(), dataCalls, before + 1);
    }

    printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL CHECKS PASSED", failures);
    return failures ? 1 : 0;
}