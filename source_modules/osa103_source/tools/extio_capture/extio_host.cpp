/*
 * Headless ExtIO host: drives the vendor's ExtIO_Osa.dll (x64) through the standard
 * ExtIO entry points so the ftd2xx proxy can record what it sends to the device.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

typedef int (*ExtioCallback)(int cnt, int status, float iqOffs, short* iqData);

static volatile long long dataBytes = 0;
static volatile long long callbacks = 0;
static volatile long long lastStatus = 0;
static volatile long long firstDataMs = -1;
static DWORD startTick = 0;

static int callback(int cnt, int status, float iqOffs, short* iqData) {
    if (status & 1) {
        if (firstDataMs < 0) { firstDataMs = (long long)(GetTickCount() - startTick); }
        dataBytes += (long long)cnt * 4;
        callbacks++;
    }
    lastStatus = status;
    return 0;
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    const char* dllPath = argc > 1 ? argv[1] : "ExtIO_Osa.dll";
    double seconds = argc > 2 ? atof(argv[2]) : 6.0;
    double freq = argc > 3 ? atof(argv[3]) : 100e6;

    SetDllDirectoryA(".");
    HMODULE h = LoadLibraryA(dllPath);
    if (!h) { std::printf("LoadLibrary(%s) failed: %lu\n", dllPath, GetLastError()); return 1; }

    auto InitHW = (bool(*)(char*, char*, int*))GetProcAddress(h, "InitHW");
    auto OpenHW = (bool(*)())GetProcAddress(h, "OpenHW");
    auto StartHW64 = (void(*)(long long))GetProcAddress(h, "StartHW64");
    auto StartHW = (void(*)(long))GetProcAddress(h, "StartHW");
    auto StopHW = (void(*)())GetProcAddress(h, "StopHW");
    auto CloseHW = (void(*)())GetProcAddress(h, "CloseHW");
    auto SetCallback = (void(*)(ExtioCallback, void*))GetProcAddress(h, "SetCallback");
    auto GetHWSR = (int(*)())GetProcAddress(h, "GetHWSR");
    auto SetHWLO64 = (int(*)(long long))GetProcAddress(h, "SetHWLO64");
    auto ShowGUI = (void(*)(bool))GetProcAddress(h, "ShowGUI");
    auto HideGUI = (void(*)(bool))GetProcAddress(h, "HideGUI");
    auto GetStatus = (int(*)())GetProcAddress(h, "GetStatus");

    std::printf("exports: InitHW=%p OpenHW=%p StartHW64=%p StopHW=%p SetCallback=%p\n",
                (void*)InitHW, (void*)OpenHW, (void*)StartHW64, (void*)StopHW, (void*)SetCallback);
    if (!InitHW || !OpenHW) { std::printf("missing essential exports\n"); return 1; }

    char name[64] = {0}, model[64] = {0};
    int type = 0;
    bool ok = InitHW(name, model, &type);
    std::printf("InitHW -> %d, name='%s' model='%s' type=%d\n", (int)ok, name, model, type);
    if (!ok) { return 2; }

    ok = OpenHW();
    std::printf("OpenHW -> %d\n", (int)ok);
    if (!ok) { return 3; }

    {
        HMODULE m = GetModuleHandleA("ftd2xx.dll");
        char p[MAX_PATH] = {0};
        if (m) { GetModuleFileNameA(m, p, sizeof(p)); }
        std::printf("ftd2xx module: %s\n", m ? p : "<not loaded>");
    }

    if (GetHWSR) { std::printf("GetHWSR -> %d\n", GetHWSR()); }
    if (SetCallback) { SetCallback(callback, nullptr); }

    if (SetHWLO64) { std::printf("SetHWLO64(%.0f) -> %d\n", freq, SetHWLO64((long long)freq)); }

    startTick = GetTickCount();
    if (StartHW64) { StartHW64((long long)freq); std::printf("StartHW64(%.0f)\n", freq); }
    else if (StartHW) { StartHW((long)freq); std::printf("StartHW(%.0f)\n", freq); }
    else { std::printf("no StartHW export\n"); return 4; }

    if (ShowGUI) { ShowGUI(true); std::printf("ShowGUI(true)\n"); }

    // Pump messages: the vendor DLL is a VCL component that drives hardware from
    // window timers, so without a pump it stays inert.
    DWORD endTick = GetTickCount() + (DWORD)(seconds * 1000.0);
    int pumped = 0;
    while ((long)(endTick - GetTickCount()) > 0) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
            pumped++;
        }
        Sleep(5);
    }
    std::printf("pumped %d messages\n", pumped);
    if (GetStatus) { std::printf("GetStatus -> %d\n", GetStatus()); }
    if (StopHW) { StopHW(); std::printf("StopHW\n"); }
    if (CloseHW) { CloseHW(); std::printf("CloseHW\n"); }

    std::printf("callbacks=%lld dataBytes=%lld firstDataMs=%lld lastStatus=%lld\n", callbacks,
                dataBytes, firstDataMs, lastStatus);
    FreeLibrary(h);
    return 0;
}
