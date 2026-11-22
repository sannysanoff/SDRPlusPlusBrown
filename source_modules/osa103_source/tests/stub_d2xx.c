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
// Fake libftd2xx used only to exercise the module's dlopen binding without hardware.
// Implements the FT_* entry points FtdiD2xx resolves and records what it is asked to
// do, so the caller can assert on it.
#include <stdint.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    uint32_t flags;
    uint32_t type;
    uint32_t id;
    uint32_t locId;
    char serialNumber[16];
    char description[64];
    void* handle;
} Node;

static int g_handle_token = 0x1234;
static int g_open = 0;
static char g_log[4096];
static uint8_t g_rx[64];
static uint32_t g_rx_len = 0;

static void logf_(const char* s) {
    size_t n = strlen(g_log);
    snprintf(g_log + n, sizeof(g_log) - n, "%s;", s);
}

const char* fakeD2xxLog(void) { return g_log; }
int fakeD2xxIsOpen(void) { return g_open; }
void fakeD2xxSetRx(const uint8_t* data, uint32_t len) {
    if (len > sizeof(g_rx)) { len = sizeof(g_rx); }
    memcpy(g_rx, data, len);
    g_rx_len = len;
}

uint32_t FT_CreateDeviceInfoList(uint32_t* count) {
    *count = 1;
    return 0;
}

uint32_t FT_GetDeviceInfoList(Node* nodes, uint32_t* count) {
    if (*count < 1) { return 6; }
    memset(nodes, 0, sizeof(Node));
    strcpy(nodes[0].serialNumber, "OSA103A");
    strcpy(nodes[0].description, "OSA103");
    nodes[0].id = 0x6001;
    nodes[0].type = 0;
    *count = 1;
    return 0;
}

uint32_t FT_Open(int index, void** handle) {
    (void)index;
    g_open = 1;
    *handle = &g_handle_token;
    return 0;
}

uint32_t FT_OpenEx(void* arg, uint32_t flags, void** handle) {
    logf_((const char*)arg);
    (void)flags;
    g_open = 1;
    *handle = &g_handle_token;
    return 0;
}

uint32_t FT_Close(void* handle) {
    (void)handle;
    g_open = 0;
    return 0;
}

uint32_t FT_ResetDevice(void* handle) {
    (void)handle;
    logf_("reset");
    return 0;
}

uint32_t FT_SetBaudRate(void* handle, uint32_t baud) {
    (void)handle;
    static char tmp[32];
    snprintf(tmp, sizeof(tmp), "baud=%u", baud);
    logf_(tmp);
    return 0;
}

uint32_t FT_SetDataCharacteristics(void* handle, uint8_t wl, uint8_t sb, uint8_t par) {
    (void)handle;
    static char tmp[32];
    snprintf(tmp, sizeof(tmp), "chars=%u%u%u", wl, sb, par);
    logf_(tmp);
    return 0;
}

uint32_t FT_SetFlowControl(void* handle, uint16_t flow, uint8_t xon, uint8_t xoff) {
    (void)handle;
    static char tmp[40];
    snprintf(tmp, sizeof(tmp), "flow=%04X/%02X/%02X", flow, xon, xoff);
    logf_(tmp);
    return 0;
}

uint32_t FT_SetRts(void* handle) {
    (void)handle;
    logf_("rts");
    return 0;
}

uint32_t FT_SetTimeouts(void* handle, uint32_t rd, uint32_t wr) {
    (void)handle;
    static char tmp[32];
    snprintf(tmp, sizeof(tmp), "timeouts=%u/%u", rd, wr);
    logf_(tmp);
    return 0;
}

uint32_t FT_SetLatencyTimer(void* handle, uint8_t ms) {
    (void)handle;
    static char tmp[32];
    snprintf(tmp, sizeof(tmp), "latency=%u", ms);
    logf_(tmp);
    return 0;
}

uint32_t FT_SetBitMode(void* handle, uint8_t mask, uint8_t mode) {
    (void)handle;
    static char tmp[32];
    snprintf(tmp, sizeof(tmp), "bitmode=%02X/%u", mask, mode);
    logf_(tmp);
    return 0;
}

uint32_t FT_Purge(void* handle, uint32_t mask) {
    (void)handle;
    (void)mask;
    logf_("purge");
    return 0;
}

uint32_t FT_StopInTask(void* handle) { (void)handle; return 0; }
uint32_t FT_RestartInTask(void* handle) { (void)handle; return 0; }

uint32_t FT_GetQueueStatus(void* handle, uint32_t* queued) {
    (void)handle;
    *queued = g_rx_len;
    return 0;
}

uint32_t FT_Read(void* handle, void* buf, uint32_t len, uint32_t* transferred) {
    (void)handle;
    uint32_t n = len < g_rx_len ? len : g_rx_len;
    memcpy(buf, g_rx, n);
    *transferred = n;
    return 0;
}

uint32_t FT_Write(void* handle, const void* buf, uint32_t len, uint32_t* transferred) {
    (void)handle;
    (void)buf;
    static char tmp[48];
    snprintf(tmp, sizeof(tmp), "write=%u", len);
    logf_(tmp);
    *transferred = len;
    return 0;
}
