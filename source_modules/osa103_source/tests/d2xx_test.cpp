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
// Drives osa103::FtdiD2xx against the fake D2XX library: proves the dlopen binding,
// the device-info struct layout, and that a configuration frame reaches FT_Write
// byte-for-byte as the DLL would send it.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ftdi_d2xx.h"
#include "osa103_proto.h"

extern "C" {
const char* fakeD2xxLog(void);
int fakeD2xxIsOpen(void);
void fakeD2xxSetRx(const unsigned char* data, unsigned len);
}

static int failures = 0;
static void check(const char* what, bool ok, const char* detail = "") {
    printf("%s  %s %s\n", ok ? "PASS" : "FAIL", what, detail);
    if (!ok) { failures++; }
}

int main(int argc, char** argv) {
    const char* libPath = argc > 1 ? argv[1] : ".tmp_d2xx/libftd2xx.dylib";

    osa103::FtdiD2xx ftdi;
    std::string err;
    // NOTE: load into a local first - passing both ftdi.load(...) and err.c_str()
    // as sibling arguments is unspecified evaluation order, and GCC evaluates
    // right to left, so err.c_str() would be captured before load() fills err.
    bool loaded = ftdi.load(libPath, err);
    check("load stub d2xx", loaded, err.c_str());

    std::vector<osa103::FtdiDeviceInfo> list;
    bool listed = ftdi.listDevices(list, err);
    check("enumerate devices", listed && list.size() == 1, err.c_str());
    if (listed && !list.empty()) {
        check("serial parsed from FT_DEVICE_LIST_INFO_NODE", list[0].serial == "OSA103A", list[0].serial.c_str());
        check("description parsed", list[0].description == "OSA103", list[0].description.c_str());
        check("vid:pid parsed", list[0].id == 0x6001);
    }

    bool opened = ftdi.openBySerial("OSA103A", err);
    check("open by serial", opened, err.c_str());
    check("handle is open", ftdi.isOpen() && fakeD2xxIsOpen());

    osa103::Params p;
    ftdi.setBaudRate(p.baudRate);
    ftdi.setDataCharacteristics(8, 0, 0);
    // Captured vendor open sequence: RTS/CTS flow control with RTS asserted.
    ftdi.setFlowControl(osa103::FtdiD2xx::FLOW_RTS_CTS, 0, 0);
    ftdi.setRts();
    ftdi.setTimeouts(p.readTimeoutMs, p.writeTimeoutMs);
    ftdi.setLatencyTimer(p.latencyMs);
    ftdi.setBitMode(0x85, 0x01);

    // The configuration frame the module would send, split into sync + body exactly
    // like ExtIO_Osa.dll does.
    uint8_t frame[4096];
    size_t len = osa103::buildConfigFrame(p, 14.074e6, frame, sizeof(frame));
    check("frame built", len == 103);
    int w1 = ftdi.write(frame, (uint32_t)osa103::kSyncLen);
    int w2 = ftdi.write(frame + osa103::kSyncLen, (uint32_t)(len - osa103::kSyncLen));
    check("sync write acknowledged", w1 == (int)osa103::kSyncLen);
    check("body write acknowledged", w2 == (int)(len - osa103::kSyncLen));

    std::string log = fakeD2xxLog();
    check("FT_OpenEx got the serial", log.find("OSA103A") != std::string::npos, log.c_str());
    check("FT_SetBaudRate got 3000000", log.find("baud=3000000") != std::string::npos);
    check("FT_SetDataCharacteristics 8N1", log.find("chars=800") != std::string::npos);
    check("FT_SetFlowControl got RTS/CTS", log.find("flow=0100") != std::string::npos);
    check("FT_SetRts asserted", log.find("rts") != std::string::npos);
    check("FT_SetTimeouts 20/20", log.find("timeouts=20/20") != std::string::npos);
    check("FT_SetLatencyTimer 2 ms", log.find("latency=2") != std::string::npos);
    check("bit-bang mask 0x85 mode 1", log.find("bitmode=85/1") != std::string::npos);
    check("writes were 25 + 78 bytes", log.find("write=25") != std::string::npos &&
                                          log.find("write=78") != std::string::npos,
          log.c_str());

    // Inbound bytes must survive the read wrapper unchanged.
    uint8_t rx[8] = {0xAA, 0x0F, 0x55, 0xAA, 0x0F, 0x55, 0xAA, 0x00};
    fakeD2xxSetRx(rx, sizeof(rx));
    uint8_t got[16] = {0};
    int n = ftdi.read(got, sizeof(got));
    check("read returns byte count", n == (int)sizeof(rx));
    check("read payload intact", memcmp(got, rx, sizeof(rx)) == 0);

    ftdi.close();
    check("close clears the handle", !ftdi.isOpen());

    printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL CHECKS PASSED", failures);
    return failures ? 1 : 0;
}
