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
#include "ftdi_d2xx.h"
#include <cstring>
#include <dlfcn.h>

namespace osa103 {

namespace {

// FTDI's own FT_DEVICE_LIST_INFO_NODE, redeclared so no FTDI headers are required.
// Layout: 4 x uint32 (Flags, Type, ID, LocId), SerialNumber[16], Description[64],
// then the handle pointer (8 bytes on 64-bit, 4 on 32-bit).
struct DeviceListNode {
    uint32_t flags;
    uint32_t type;
    uint32_t id;
    uint32_t locId;
    char serialNumber[16];
    char description[64];
    void* handle;
};

constexpr uint32_t FT_OPEN_BY_SERIAL_NUMBER = 1;

const char* const kLibraryNames[] = {
    "libftd2xx.dylib",
    "libftd2xx.1.dylib",
    "libftd2xx.so",
    "libftd2xx.so.1",
    "ftd2xx.dll",
    "/usr/local/lib/libftd2xx.dylib",
    "/usr/local/lib/libftd2xx.so",
    "/opt/homebrew/lib/libftd2xx.dylib",
    "/opt/homebrew/lib/libftd2xx.so",
    "/usr/local/lib/libftd2xx.1.4.35.dylib",
    "/Library/Application Support/FTDI/libftd2xx.dylib",
};

}  // namespace

FtdiD2xx::~FtdiD2xx() {
    close();
    if (lib) {
        dlclose(lib);
        lib = nullptr;
    }
}

std::string FtdiD2xx::statusString(uint32_t status) {
    switch (status) {
    case 0: return "FT_OK";
    case 1: return "FT_INVALID_HANDLE";
    case 2: return "FT_DEVICE_NOT_FOUND";
    case 3: return "FT_DEVICE_NOT_OPENED";
    case 4: return "FT_IO_ERROR";
    case 5: return "FT_INSUFFICIENT_RESOURCES";
    case 6: return "FT_INVALID_PARAMETER";
    case 7: return "FT_INVALID_BAUD_RATE";
    case 8: return "FT_DEVICE_NOT_OPENED_FOR_ERASE";
    case 9: return "FT_DEVICE_NOT_OPENED_FOR_WRITE";
    case 10: return "FT_FAILED_TO_WRITE_DEVICE";
    case 11: return "FT_EEPROM_READ_FAILED";
    case 12: return "FT_EEPROM_WRITE_FAILED";
    case 13: return "FT_EEPROM_ERASE_FAILED";
    case 14: return "FT_EEPROM_NOT_PRESENT";
    case 15: return "FT_EEPROM_NOT_PROGRAMMED";
    case 16: return "FT_INVALID_ARGS";
    case 17: return "FT_NOT_SUPPORTED";
    case 18: return "FT_OTHER_ERROR";
    case 19: return "FT_DEVICE_LIST_NOT_READY";
    default: break;
    }
    return "FT_STATUS(" + std::to_string(status) + ")";
}

bool FtdiD2xx::load(const std::string& explicitPath, std::string& err) {
    if (lib) { return true; }

    std::vector<std::string> candidates;
    if (!explicitPath.empty()) { candidates.push_back(explicitPath); }
    else {
        for (const char* name : kLibraryNames) { candidates.push_back(name); }
    }

    for (const std::string& candidate : candidates) {
        lib = dlopen(candidate.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (lib) {
            path = candidate;
            break;
        }
    }
    if (!lib) {
        err = "Could not load the FTDI D2XX library";
        if (!explicitPath.empty()) { err += " '" + explicitPath + "'"; }
        const char* dlerr = dlerror();
        if (dlerr) { err += std::string(": ") + dlerr; }
        return false;
    }

#define OSA103_RESOLVE(field, symbol)                                                 \
    do {                                                                              \
        field = reinterpret_cast<decltype(field)>(dlsym(lib, symbol));                \
        if (!field) {                                                                 \
            err = std::string("D2XX library is missing symbol ") + symbol;            \
            dlclose(lib);                                                             \
            lib = nullptr;                                                            \
            return false;                                                             \
        }                                                                             \
    } while (0)

    OSA103_RESOLVE(pCreateDeviceInfoList, "FT_CreateDeviceInfoList");
    OSA103_RESOLVE(pGetDeviceInfoList, "FT_GetDeviceInfoList");
    OSA103_RESOLVE(pOpen, "FT_Open");
    OSA103_RESOLVE(pOpenEx, "FT_OpenEx");
    OSA103_RESOLVE(pClose, "FT_Close");
    OSA103_RESOLVE(pResetDevice, "FT_ResetDevice");
    OSA103_RESOLVE(pSetBaudRate, "FT_SetBaudRate");
    OSA103_RESOLVE(pSetDataCharacteristics, "FT_SetDataCharacteristics");
    OSA103_RESOLVE(pSetTimeouts, "FT_SetTimeouts");
    OSA103_RESOLVE(pSetLatencyTimer, "FT_SetLatencyTimer");
    OSA103_RESOLVE(pSetBitMode, "FT_SetBitMode");
    OSA103_RESOLVE(pPurge, "FT_Purge");
    OSA103_RESOLVE(pStopInTask, "FT_StopInTask");
    OSA103_RESOLVE(pRestartInTask, "FT_RestartInTask");
    OSA103_RESOLVE(pGetQueueStatus, "FT_GetQueueStatus");
    OSA103_RESOLVE(pRead, "FT_Read");
    OSA103_RESOLVE(pWrite, "FT_Write");

#undef OSA103_RESOLVE
    return true;
}

bool FtdiD2xx::listDevices(std::vector<FtdiDeviceInfo>& out, std::string& err) {
    out.clear();
    if (!lib) { err = "D2XX library not loaded"; return false; }

    uint32_t count = 0;
    uint32_t st = pCreateDeviceInfoList(&count);
    if (st) { err = "FT_CreateDeviceInfoList: " + statusString(st); return false; }
    if (!count) { return true; }

    std::vector<DeviceListNode> nodes(count);
    st = pGetDeviceInfoList(nodes.data(), &count);
    if (st) { err = "FT_GetDeviceInfoList: " + statusString(st); return false; }

    for (uint32_t i = 0; i < count; i++) {
        FtdiDeviceInfo info;
        info.serial.assign(nodes[i].serialNumber, strnlen(nodes[i].serialNumber, 16));
        info.description.assign(nodes[i].description, strnlen(nodes[i].description, 64));
        info.id = nodes[i].id;
        info.type = nodes[i].type;
        out.push_back(info);
    }
    return true;
}

bool FtdiD2xx::openBySerial(const std::string& serial, std::string& err) {
    if (!lib) { err = "D2XX library not loaded"; return false; }
    close();
    uint32_t st = pOpenEx((void*)serial.c_str(), FT_OPEN_BY_SERIAL_NUMBER, &handle);
    if (st) {
        handle = nullptr;
        err = "FT_OpenEx('" + serial + "'): " + statusString(st);
        return false;
    }
    return true;
}

bool FtdiD2xx::openByIndex(int index, std::string& err) {
    if (!lib) { err = "D2XX library not loaded"; return false; }
    close();
    uint32_t st = pOpen(index, &handle);
    if (st) {
        handle = nullptr;
        err = "FT_Open(" + std::to_string(index) + "): " + statusString(st);
        return false;
    }
    return true;
}

void FtdiD2xx::close() {
    if (handle && pClose) { pClose(handle); }
    handle = nullptr;
}

uint32_t FtdiD2xx::resetDevice() { return handle ? pResetDevice(handle) : 1; }
uint32_t FtdiD2xx::setBaudRate(uint32_t baud) { return handle ? pSetBaudRate(handle, baud) : 1; }

uint32_t FtdiD2xx::setDataCharacteristics(uint8_t wordLength, uint8_t stopBits, uint8_t parity) {
    return handle ? pSetDataCharacteristics(handle, wordLength, stopBits, parity) : 1;
}

uint32_t FtdiD2xx::setTimeouts(uint32_t readMs, uint32_t writeMs) {
    return handle ? pSetTimeouts(handle, readMs, writeMs) : 1;
}

uint32_t FtdiD2xx::setLatencyTimer(uint8_t ms) { return handle ? pSetLatencyTimer(handle, ms) : 1; }

uint32_t FtdiD2xx::setBitMode(uint8_t mask, uint8_t mode) {
    return handle ? pSetBitMode(handle, mask, mode) : 1;
}

uint32_t FtdiD2xx::purge(uint32_t mask) { return handle ? pPurge(handle, mask) : 1; }
uint32_t FtdiD2xx::stopInTask() { return handle ? pStopInTask(handle) : 1; }
uint32_t FtdiD2xx::restartInTask() { return handle ? pRestartInTask(handle) : 1; }

uint32_t FtdiD2xx::queueStatus() {
    uint32_t queued = 0;
    if (handle) { pGetQueueStatus(handle, &queued); }
    return queued;
}

int FtdiD2xx::read(uint8_t* buf, uint32_t len) {
    if (!handle) { return -1; }
    uint32_t transferred = 0;
    uint32_t st = pRead(handle, buf, len, &transferred);
    if (st) { return -(int)st; }
    return (int)transferred;
}

int FtdiD2xx::write(const uint8_t* buf, uint32_t len) {
    if (!handle) { return -1; }
    uint32_t transferred = 0;
    uint32_t st = pWrite(handle, buf, len, &transferred);
    if (st) { return -(int)st; }
    return (int)transferred;
}

}  // namespace osa103
