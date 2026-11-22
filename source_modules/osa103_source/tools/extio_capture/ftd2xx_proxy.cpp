/*
 * Logging proxy for ftd2xx.dll, used to capture what the vendor's ExtIO_Osa.dll
 * actually sends to an OSA103. Forwards every call to the real D2XX library and
 * appends a trace to the file named by OSA_PROXY_LOG.
 *
 * Build (x64): g++ -shared -O2 -o ftd2xx.dll ftd2xx_proxy.cpp
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>

typedef unsigned long FT_STATUS;
typedef void* FT_HANDLE;

static HMODULE realLib = nullptr;
FILE* logFile = nullptr;
CRITICAL_SECTION logLock;

using Fn = void*;

static void trace(const char* fmt, ...);

Fn sym(const char* name) {
    trace("CALL %s", name);
    // Log where the call came from. Level 1 is the vendor's Delphi wrapper,
    // level 2 is the vendor code that called the wrapper - the interesting
    // one. Walk the ebp chain manually, validating every step with
    // VirtualQuery so a nonstandard frame can never crash the caller.
#if defined(_WIN32)
    {
        char* fp = (char*)__builtin_frame_address(0);
        for (int level = 1; level <= 3 && fp; level++) {
            MEMORY_BASIC_INFORMATION mbi;
            if (((DWORD_PTR)fp & 3) != 0) { break; }
            if (!VirtualQuery(fp, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) { break; }
            void** frame = (void**)fp;
            char* next = (char*)frame[0];
            void* ra = frame[1];
            if (!next || (DWORD_PTR)next <= (DWORD_PTR)fp) { break; }
            if (!VirtualQuery(next, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) { break; }
            MEMORY_BASIC_INFORMATION m2;
            if (ra && VirtualQuery(ra, &m2, sizeof(m2)) && m2.AllocationBase) {
                trace("STACK %s l%d base=%p rva=0x%08lX", name, level, m2.AllocationBase,
                      (unsigned long)((char*)ra - (char*)m2.AllocationBase));
            }
            fp = next;
        }
    }
#endif
    return realLib ? (Fn)GetProcAddress(realLib, name) : nullptr;
}

void trace(const char* fmt, ...) {
    if (!logFile) { return; }
    EnterCriticalSection(&logLock);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(logFile, fmt, ap);
    va_end(ap);
    fputc('\n', logFile);
    fflush(logFile);
    LeaveCriticalSection(&logLock);
}

std::string hex(const unsigned char* p, size_t n, size_t cap) {
    n = n < cap ? n : cap;
    static const char* d = "0123456789ABCDEF";
    std::string s;
    s.reserve(n * 3);
    for (size_t i = 0; i < n; i++) {
        s.push_back(d[p[i] >> 4]);
        s.push_back(d[p[i] & 15]);
        s.push_back(' ');
    }
    if (n < cap) { return s; }
    return s + "...";
}

// ---------------------------------------------------------------- exports

extern "C" {

FT_STATUS __attribute__((stdcall)) FT_CreateDeviceInfoList(DWORD* n) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(DWORD*))sym("FT_CreateDeviceInfoList");
    FT_STATUS st = f ? f(n) : 1;
    trace("FT_CreateDeviceInfoList -> %lu (count %lu)", st, n ? *n : 0UL);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_GetDeviceInfoList(void* list, DWORD* n) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(void*, DWORD*))sym("FT_GetDeviceInfoList");
    FT_STATUS st = f ? f(list, n) : 1;
    trace("FT_GetDeviceInfoList -> %lu (count %lu)", st, n ? *n : 0UL);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_GetDeviceInfoDetail(DWORD index, DWORD* flags, DWORD* type, DWORD* id, DWORD* locId,
                                 char* serial, char* desc, FT_HANDLE* h) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(DWORD, DWORD*, DWORD*, DWORD*, DWORD*, char*, char*, FT_HANDLE*))
        sym("FT_GetDeviceInfoDetail");
    FT_STATUS st = f ? f(index, flags, type, id, locId, serial, desc, h) : 1;
    trace("FT_GetDeviceInfoDetail(%lu) -> %lu serial='%s' desc='%s'", index, st,
          serial ? serial : "", desc ? desc : "");
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_GetDeviceInfo(FT_HANDLE h, DWORD* type, DWORD* id, DWORD* ver, char* serial,
                           char* desc, void* arg) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE, DWORD*, DWORD*, DWORD*, char*, char*, void*))
        sym("FT_GetDeviceInfo");
    FT_STATUS st = f ? f(h, type, id, ver, serial, desc, arg) : 1;
    trace("FT_GetDeviceInfo -> %lu", st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_GetVIDPID(FT_HANDLE h, DWORD* vid, DWORD* pid) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE, DWORD*, DWORD*))sym("FT_GetVIDPID");
    FT_STATUS st = f ? f(h, vid, pid) : 1;
    trace("FT_GetVIDPID -> %lu", st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_SetDivisor(FT_HANDLE h, WORD divisor) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE, WORD))sym("FT_SetDivisor");
    FT_STATUS st = f ? f(h, divisor) : 1;
    trace("FT_SetDivisor(%u) -> %lu", divisor, st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_GetLatencyTimer(FT_HANDLE h, BYTE* ms) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE, BYTE*))sym("FT_GetLatencyTimer");
    FT_STATUS st = f ? f(h, ms) : 1;
    trace("FT_GetLatencyTimer -> %lu", st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_EEPROM_Read(FT_HANDLE h, void* ee, DWORD size) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE, void*, DWORD))sym("FT_EEPROM_Read");
    FT_STATUS st = f ? f(h, ee, size) : 1;
    trace("FT_EEPROM_Read(%lu) -> %lu", size, st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_EEPROM_Write(FT_HANDLE h, void* ee, DWORD size) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE, void*, DWORD))sym("FT_EEPROM_Write");
    FT_STATUS st = f ? f(h, ee, size) : 1;
    trace("FT_EEPROM_Write(%lu) -> %lu", size, st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_Open(int index, FT_HANDLE* h) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(int, FT_HANDLE*))sym("FT_Open");
    FT_STATUS st = f ? f(index, h) : 1;
    trace("FT_Open(%d) -> %lu", index, st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_OpenEx(void* arg, DWORD flags, FT_HANDLE* h) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(void*, DWORD, FT_HANDLE*))sym("FT_OpenEx");
    FT_STATUS st = f ? f(arg, flags, h) : 1;
    trace("FT_OpenEx(%s, flags=%lu) -> %lu", arg && (flags & 1) ? (const char*)arg : "<ptr>",
          flags, st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_Close(FT_HANDLE h) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE))sym("FT_Close");
    FT_STATUS st = f ? f(h) : 1;
    trace("FT_Close -> %lu", st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_Read(FT_HANDLE h, void* buf, DWORD len, DWORD* got) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE, void*, DWORD, DWORD*))sym("FT_Read");
    FT_STATUS st = f ? f(h, buf, len, got) : 1;
    static int reads = 0;
    if (++reads < 40 || (got && *got)) {
        size_t cap = (got && *got && *got <= 256) ? 256 : 64;
        trace("FT_Read(len=%lu) -> %lu, got %lu: %s", len, st, got ? *got : 0UL,
              got && *got ? hex((unsigned char*)buf, *got, cap).c_str() : "");
    }
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_Write(FT_HANDLE h, void* buf, DWORD len, DWORD* sent) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE, void*, DWORD, DWORD*))sym("FT_Write");
    FT_STATUS st = f ? f(h, buf, len, sent) : 1;
    trace("FT_Write(len=%lu) -> %lu, sent %lu: %s", len, st, sent ? *sent : 0UL,
          hex((unsigned char*)buf, len, len <= 2100 ? 2100 : 160).c_str());
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_SetBaudRate(FT_HANDLE h, DWORD baud) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE, DWORD))sym("FT_SetBaudRate");
    FT_STATUS st = f ? f(h, baud) : 1;
    trace("FT_SetBaudRate(%lu) -> %lu", baud, st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_SetDataCharacteristics(FT_HANDLE h, BYTE bits, BYTE stop, BYTE parity) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE, BYTE, BYTE, BYTE))sym("FT_SetDataCharacteristics");
    FT_STATUS st = f ? f(h, bits, stop, parity) : 1;
    trace("FT_SetDataCharacteristics(%u,%u,%u) -> %lu", bits, stop, parity, st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_SetTimeouts(FT_HANDLE h, DWORD rd, DWORD wr) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE, DWORD, DWORD))sym("FT_SetTimeouts");
    FT_STATUS st = f ? f(h, rd, wr) : 1;
    trace("FT_SetTimeouts(%lu,%lu) -> %lu", rd, wr, st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_SetLatencyTimer(FT_HANDLE h, BYTE ms) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE, BYTE))sym("FT_SetLatencyTimer");
    FT_STATUS st = f ? f(h, ms) : 1;
    trace("FT_SetLatencyTimer(%u) -> %lu", ms, st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_SetBitMode(FT_HANDLE h, BYTE mask, BYTE mode) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE, BYTE, BYTE))sym("FT_SetBitMode");
    FT_STATUS st = f ? f(h, mask, mode) : 1;
    trace("FT_SetBitMode(mask=0x%02X, mode=%u) -> %lu", mask, mode, st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_GetBitMode(FT_HANDLE h, BYTE* mode) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE, BYTE*))sym("FT_GetBitMode");
    FT_STATUS st = f ? f(h, mode) : 1;
    trace("FT_GetBitMode -> %lu (0x%02X)", st, mode ? *mode : 0);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_Purge(FT_HANDLE h, DWORD mask) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE, DWORD))sym("FT_Purge");
    FT_STATUS st = f ? f(h, mask) : 1;
    trace("FT_Purge(%lu) -> %lu", mask, st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_ResetDevice(FT_HANDLE h) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE))sym("FT_ResetDevice");
    FT_STATUS st = f ? f(h) : 1;
    trace("FT_ResetDevice -> %lu", st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_ResetPort(FT_HANDLE h) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE))sym("FT_ResetPort");
    FT_STATUS st = f ? f(h) : 1;
    trace("FT_ResetPort -> %lu", st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_GetQueueStatus(FT_HANDLE h, DWORD* n) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE, DWORD*))sym("FT_GetQueueStatus");
    FT_STATUS st = f ? f(h, n) : 1;
    trace("FT_GetQueueStatus -> %lu (%lu queued)", st, n ? *n : 0UL);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_StopInTask(FT_HANDLE h) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE))sym("FT_StopInTask");
    FT_STATUS st = f ? f(h) : 1;
    trace("FT_StopInTask -> %lu", st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_RestartInTask(FT_HANDLE h) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE))sym("FT_RestartInTask");
    FT_STATUS st = f ? f(h) : 1;
    trace("FT_RestartInTask -> %lu", st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_SetUSBParameters(FT_HANDLE h, DWORD inSz, DWORD outSz) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE, DWORD, DWORD))sym("FT_SetUSBParameters");
    FT_STATUS st = f ? f(h, inSz, outSz) : 1;
    trace("FT_SetUSBParameters(%lu,%lu) -> %lu", inSz, outSz, st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_SetFlowControl(FT_HANDLE h, WORD mode, BYTE xon, BYTE xoff) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE, WORD, BYTE, BYTE))sym("FT_SetFlowControl");
    FT_STATUS st = f ? f(h, mode, xon, xoff) : 1;
    trace("FT_SetFlowControl(%u) -> %lu", mode, st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_SetDtr(FT_HANDLE h) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE))sym("FT_SetDtr");
    FT_STATUS st = f ? f(h) : 1;
    trace("FT_SetDtr -> %lu", st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_ClrDtr(FT_HANDLE h) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE))sym("FT_ClrDtr");
    FT_STATUS st = f ? f(h) : 1;
    trace("FT_ClrDtr -> %lu", st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_SetRts(FT_HANDLE h) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE))sym("FT_SetRts");
    FT_STATUS st = f ? f(h) : 1;
    trace("FT_SetRts -> %lu", st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_ClrRts(FT_HANDLE h) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE))sym("FT_ClrRts");
    FT_STATUS st = f ? f(h) : 1;
    trace("FT_ClrRts -> %lu", st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_GetLibraryVersion(DWORD* v) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(DWORD*))sym("FT_GetLibraryVersion");
    FT_STATUS st = f ? f(v) : 1;
    trace("FT_GetLibraryVersion -> %lu (0x%08lX)", st, v ? *v : 0UL);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_SetDeadmanTimeout(FT_HANDLE h, DWORD ms) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE, DWORD))sym("FT_SetDeadmanTimeout");
    FT_STATUS st = f ? f(h, ms) : 1;
    trace("FT_SetDeadmanTimeout(%lu) -> %lu", ms, st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_GetComPortNumber(FT_HANDLE h, LONG* n) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE, LONG*))sym("FT_GetComPortNumber");
    FT_STATUS st = f ? f(h, n) : 1;
    trace("FT_GetComPortNumber -> %lu (%ld)", st, n ? *n : 0L);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_SetChars(FT_HANDLE h, BYTE a, BYTE b, BYTE c, BYTE d) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE, BYTE, BYTE, BYTE, BYTE))sym("FT_SetChars");
    FT_STATUS st = f ? f(h, a, b, c, d) : 1;
    trace("FT_SetChars -> %lu", st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_GetStatus(FT_HANDLE h, DWORD* rx, DWORD* tx, DWORD* ev) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE, DWORD*, DWORD*, DWORD*))sym("FT_GetStatus");
    FT_STATUS st = f ? f(h, rx, tx, ev) : 1;
    trace("FT_GetStatus -> %lu", st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_SetEventNotification(FT_HANDLE h, DWORD mask, void* param) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE, DWORD, void*))sym("FT_SetEventNotification");
    FT_STATUS st = f ? f(h, mask, param) : 1;
    trace("FT_SetEventNotification(mask=0x%lX) -> %lu", mask, st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_ListDevices(void* a, void* b, DWORD flags) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(void*, void*, DWORD))sym("FT_ListDevices");
    FT_STATUS st = f ? f(a, b, flags) : 1;
    trace("FT_ListDevices(flags=0x%lX) -> %lu", flags, st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_GetModemStatus(FT_HANDLE h, DWORD* status) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE, DWORD*))sym("FT_GetModemStatus");
    FT_STATUS st = f ? f(h, status) : 1;
    trace("FT_GetModemStatus -> %lu (0x%lX)", st, status ? *status : 0UL);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_CyclePort(FT_HANDLE h) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE))sym("FT_CyclePort");
    FT_STATUS st = f ? f(h) : 1;
    trace("FT_CyclePort -> %lu", st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_SetBreakOn(FT_HANDLE h) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE))sym("FT_SetBreakOn");
    FT_STATUS st = f ? f(h) : 1;
    trace("FT_SetBreakOn -> %lu", st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_SetBreakOff(FT_HANDLE h) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE))sym("FT_SetBreakOff");
    FT_STATUS st = f ? f(h) : 1;
    trace("FT_SetBreakOff -> %lu", st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_SetWaitOnHandshake(FT_HANDLE h) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(FT_HANDLE))sym("FT_SetWaitOnHandshake");
    FT_STATUS st = f ? f(h) : 1;
    trace("FT_SetWaitOnHandshake -> %lu", st);
    return st;
}

FT_STATUS __attribute__((stdcall)) FT_Reload(WORD vid, WORD pid) {
    auto f = (FT_STATUS(__attribute__((stdcall)) *)(WORD, WORD))sym("FT_Reload");
    FT_STATUS st = f ? f(vid, pid) : 1;
    trace("FT_Reload(%u,%u) -> %lu", vid, pid, st);
    return st;
}

}  // extern "C"

// ---------------------------------------------------------------------------
// Complete export surface: everything the real ftd2xx.dll exports but that
// is not wrapped above is forwarded through bare "jmp" thunks. A jmp thunk
// has no frame and never touches the stack, so the real function's own
// "ret N" cleans the caller's stack correctly no matter how many arguments
// the caller pushed - functions whose exact signature is unknown are still
// forwarded safely. The target pointers are filled in DllMain once the real
// library is loaded. If the vendor resolves one of these by GetProcAddress,
// it now gets a valid function instead of NULL (which it treats as "driver
// unusable" / "Device not found").
#define THUNK(name)                                                             \
    extern "C" void* pFT_##name = nullptr;                                      \
    __asm__(".globl _FT_" #name "\n"                                            \
            "_FT_" #name ":\n"                                                  \
            " jmp *_pFT_" #name "\n");

THUNK(ComPortCancelIdle)
THUNK(ComPortIdle)
THUNK(EE_Program)
THUNK(EE_ProgramEx)
THUNK(EE_Read)
THUNK(EE_ReadConfig)
THUNK(EE_ReadECC)
THUNK(EE_ReadEx)
THUNK(EE_UARead)
THUNK(EE_UASize)
THUNK(EE_UAWrite)
THUNK(EE_WriteConfig)
THUNK(EEPROM_Program)
THUNK(EraseEE)
THUNK(GetDriverVersion)
THUNK(GetEventStatus)
THUNK(GetQueueStatusEx)
THUNK(IoCtl)
THUNK(ReadEE)
THUNK(Rescan)
THUNK(SetResetPipeRetryCount)
THUNK(SetWaitMask)
THUNK(VendorCmdGet)
THUNK(VendorCmdGetEx)
THUNK(VendorCmdSet)
THUNK(VendorCmdSetEx)
THUNK(W32_CancelIo)
THUNK(W32_ClearCommBreak)
THUNK(W32_ClearCommError)
THUNK(W32_CloseHandle)
THUNK(W32_CreateFile)
THUNK(W32_EscapeCommFunction)
THUNK(W32_GetCommMask)
THUNK(W32_GetCommModemStatus)
THUNK(W32_GetCommState)
THUNK(W32_GetCommTimeouts)
THUNK(W32_GetLastError)
THUNK(W32_GetOverlappedResult)
THUNK(W32_PurgeComm)
THUNK(W32_ReadFile)
THUNK(W32_SetCommBreak)
THUNK(W32_SetCommMask)
THUNK(W32_SetCommState)
THUNK(W32_SetCommTimeouts)
THUNK(W32_SetupComm)
THUNK(W32_WaitCommEvent)
THUNK(W32_WriteFile)
THUNK(WaitOnMask)
THUNK(WriteEE)

static void initThunks() {
    struct Pair {
        const char* name;
        void** ptr;
    };
    static const Pair kThunkTable[] = {
        {"FT_ComPortCancelIdle", &pFT_ComPortCancelIdle},
        {"FT_ComPortIdle", &pFT_ComPortIdle},
        {"FT_EE_Program", &pFT_EE_Program},
        {"FT_EE_ProgramEx", &pFT_EE_ProgramEx},
        {"FT_EE_Read", &pFT_EE_Read},
        {"FT_EE_ReadConfig", &pFT_EE_ReadConfig},
        {"FT_EE_ReadECC", &pFT_EE_ReadECC},
        {"FT_EE_ReadEx", &pFT_EE_ReadEx},
        {"FT_EE_UARead", &pFT_EE_UARead},
        {"FT_EE_UASize", &pFT_EE_UASize},
        {"FT_EE_UAWrite", &pFT_EE_UAWrite},
        {"FT_EE_WriteConfig", &pFT_EE_WriteConfig},
        {"FT_EEPROM_Program", &pFT_EEPROM_Program},
        {"FT_EraseEE", &pFT_EraseEE},
        {"FT_GetDriverVersion", &pFT_GetDriverVersion},
        {"FT_GetEventStatus", &pFT_GetEventStatus},
        {"FT_GetQueueStatusEx", &pFT_GetQueueStatusEx},
        {"FT_IoCtl", &pFT_IoCtl},
        {"FT_ReadEE", &pFT_ReadEE},
        {"FT_Rescan", &pFT_Rescan},
        {"FT_SetResetPipeRetryCount", &pFT_SetResetPipeRetryCount},
        {"FT_SetWaitMask", &pFT_SetWaitMask},
        {"FT_VendorCmdGet", &pFT_VendorCmdGet},
        {"FT_VendorCmdGetEx", &pFT_VendorCmdGetEx},
        {"FT_VendorCmdSet", &pFT_VendorCmdSet},
        {"FT_VendorCmdSetEx", &pFT_VendorCmdSetEx},
        {"FT_W32_CancelIo", &pFT_W32_CancelIo},
        {"FT_W32_ClearCommBreak", &pFT_W32_ClearCommBreak},
        {"FT_W32_ClearCommError", &pFT_W32_ClearCommError},
        {"FT_W32_CloseHandle", &pFT_W32_CloseHandle},
        {"FT_W32_CreateFile", &pFT_W32_CreateFile},
        {"FT_W32_EscapeCommFunction", &pFT_W32_EscapeCommFunction},
        {"FT_W32_GetCommMask", &pFT_W32_GetCommMask},
        {"FT_W32_GetCommModemStatus", &pFT_W32_GetCommModemStatus},
        {"FT_W32_GetCommState", &pFT_W32_GetCommState},
        {"FT_W32_GetCommTimeouts", &pFT_W32_GetCommTimeouts},
        {"FT_W32_GetLastError", &pFT_W32_GetLastError},
        {"FT_W32_GetOverlappedResult", &pFT_W32_GetOverlappedResult},
        {"FT_W32_PurgeComm", &pFT_W32_PurgeComm},
        {"FT_W32_ReadFile", &pFT_W32_ReadFile},
        {"FT_W32_SetCommBreak", &pFT_W32_SetCommBreak},
        {"FT_W32_SetCommMask", &pFT_W32_SetCommMask},
        {"FT_W32_SetCommState", &pFT_W32_SetCommState},
        {"FT_W32_SetCommTimeouts", &pFT_W32_SetCommTimeouts},
        {"FT_W32_SetupComm", &pFT_W32_SetupComm},
        {"FT_W32_WaitCommEvent", &pFT_W32_WaitCommEvent},
        {"FT_W32_WriteFile", &pFT_W32_WriteFile},
        {"FT_WaitOnMask", &pFT_WaitOnMask},
        {"FT_WriteEE", &pFT_WriteEE},
    };
    for (const Pair& t : kThunkTable) { *t.ptr = (void*)GetProcAddress(realLib, t.name); }
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        InitializeCriticalSection(&logLock);
        char path[MAX_PATH] = "";
        DWORD n = GetEnvironmentVariableA("OSA_PROXY_LOG", path, sizeof(path));
        if (!n || !path[0]) {
            char tmp[MAX_PATH] = "";
            if (GetEnvironmentVariableA("TEMP", tmp, sizeof(tmp)) && tmp[0]) {
                snprintf(path, sizeof(path), "%s\\osa_mitm_proxy.log", tmp);
            }
            else {
                snprintf(path, sizeof(path), "osa_mitm_proxy.log");
            }
        }
        logFile = fopen(path, "w");
        realLib = LoadLibraryA("C:\\Windows\\System32\\ftd2xx.dll");
        initThunks();
        trace("proxy attached, log=%s, real ftd2xx = %p", path, (void*)realLib);
    }
    else if (reason == DLL_PROCESS_DETACH) {
        trace("proxy detached");
        if (logFile) { fclose(logFile); logFile = nullptr; }
        DeleteCriticalSection(&logLock);
    }
    return TRUE;
}
