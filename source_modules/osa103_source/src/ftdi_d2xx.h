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
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace osa103 {

struct FtdiDeviceInfo {
    std::string serial;
    std::string description;
    uint32_t id = 0;
    uint32_t type = 0;
};

/*
Minimal runtime binding to FTDI's D2XX library (ftd2xx.dll / libftd2xx.dylib /
libftd2xx.so). Nothing is linked at build time and no FTDI headers are needed: the
library is loaded at run time (dlopen on macOS and Linux, LoadLibrary on Windows) and the
handful of entry points the OSA103 needs are resolved by name. This is deliberately the same
mechanism ExtIO_Osa.dll uses, so the module works with whatever D2XX build the user already has
installed.

Return codes are FT_STATUS values (0 = FT_OK). 32-bit integer arguments are
zero-extended by the caller, which satisfies the LP64 `unsigned long` parameters in
FTDI's Linux/macOS headers.
*/
class FtdiD2xx {
public:
    FtdiD2xx() = default;
    ~FtdiD2xx();

    FtdiD2xx(const FtdiD2xx&) = delete;
    FtdiD2xx& operator=(const FtdiD2xx&) = delete;

    // explicitPath may be empty, in which case the standard library names are tried
    // in the platform's library search path.
    bool load(const std::string& explicitPath, std::string& err);
    bool loaded() const { return lib != nullptr; }
    const std::string& libraryPath() const { return path; }

    bool listDevices(std::vector<FtdiDeviceInfo>& out, std::string& err);
    bool openBySerial(const std::string& serial, std::string& err);
    bool openByIndex(int index, std::string& err);
    bool isOpen() const { return handle != nullptr; }
    void close();

    uint32_t resetDevice();
    uint32_t setBaudRate(uint32_t baud);
    uint32_t setDataCharacteristics(uint8_t wordLength, uint8_t stopBits, uint8_t parity);
    uint32_t setFlowControl(uint16_t flowControl, uint8_t xon, uint8_t xoff);
    uint32_t setRts();
    uint32_t setTimeouts(uint32_t readMs, uint32_t writeMs);
    uint32_t setLatencyTimer(uint8_t ms);
    uint32_t setBitMode(uint8_t mask, uint8_t mode);
    uint32_t purge(uint32_t mask);
    uint32_t stopInTask();
    uint32_t restartInTask();
    uint32_t queueStatus();

    // Return the number of bytes transferred, or a negative FT_STATUS on error.
    int read(uint8_t* buf, uint32_t len);
    int write(const uint8_t* buf, uint32_t len);

    static std::string statusString(uint32_t status);

    static constexpr uint32_t PURGE_RX = 1;
    static constexpr uint32_t PURGE_TX = 2;
    static constexpr uint16_t FLOW_RTS_CTS = 0x0100;

private:
    void* lib = nullptr;
    std::string path;
    void* handle = nullptr;

// D2XX entry points are __stdcall on 32-bit Windows; on x64 Windows and on
// Linux/macOS there is a single calling convention. Modeling this matters: a
// 32-bit host that calls the real (stdcall) exports through cdecl pointers
// corrupts the stack on every call, which is exactly how the first 32-bit
// stream-test build died at its third D2XX call.
#if defined(_WIN32) && !defined(_WIN64)
#define OSA103_FTAPI __attribute__((stdcall))
#else
#define OSA103_FTAPI
#endif

    uint32_t (*pCreateDeviceInfoList)(uint32_t*) = nullptr;
    OSA103_FTAPI uint32_t (*pGetDeviceInfoList)(void*, uint32_t*) = nullptr;
    OSA103_FTAPI uint32_t (*pOpen)(int, void**) = nullptr;
    OSA103_FTAPI uint32_t (*pOpenEx)(void*, uint32_t, void**) = nullptr;
    OSA103_FTAPI uint32_t (*pClose)(void*) = nullptr;
    OSA103_FTAPI uint32_t (*pResetDevice)(void*) = nullptr;
    OSA103_FTAPI uint32_t (*pSetBaudRate)(void*, uint32_t) = nullptr;
    OSA103_FTAPI uint32_t (*pSetDataCharacteristics)(void*, uint8_t, uint8_t, uint8_t) = nullptr;
    OSA103_FTAPI uint32_t (*pSetFlowControl)(void*, uint16_t, uint8_t, uint8_t) = nullptr;
    OSA103_FTAPI uint32_t (*pSetRts)(void*) = nullptr;
    OSA103_FTAPI uint32_t (*pSetTimeouts)(void*, uint32_t, uint32_t) = nullptr;
    OSA103_FTAPI uint32_t (*pSetLatencyTimer)(void*, uint8_t) = nullptr;
    OSA103_FTAPI uint32_t (*pSetBitMode)(void*, uint8_t, uint8_t) = nullptr;
    OSA103_FTAPI uint32_t (*pPurge)(void*, uint32_t) = nullptr;
    OSA103_FTAPI uint32_t (*pStopInTask)(void*) = nullptr;
    OSA103_FTAPI uint32_t (*pRestartInTask)(void*) = nullptr;
    OSA103_FTAPI uint32_t (*pGetQueueStatus)(void*, uint32_t*) = nullptr;
    OSA103_FTAPI uint32_t (*pRead)(void*, void*, uint32_t, uint32_t*) = nullptr;
    OSA103_FTAPI uint32_t (*pWrite)(void*, const void*, uint32_t, uint32_t*) = nullptr;
};

}  // namespace osa103
