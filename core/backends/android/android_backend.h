#pragma once
#include <vector>
#include <stdint.h>
#include <string>

namespace backend {
    struct DevVIDPID {
        uint16_t vid;
        uint16_t pid;
    };

    extern const std::vector<DevVIDPID> AIRSPY_VIDPIDS;
    extern const std::vector<DevVIDPID> AIRSPYHF_VIDPIDS;
    extern const std::vector<DevVIDPID> HACKRF_VIDPIDS;
    extern const std::vector<DevVIDPID> RTL_SDR_VIDPIDS;

    int getDeviceFD(int& vid, int& pid, const std::vector<DevVIDPID>& allowedVidPids);

    std::string httpGet(const std::string& url);

    std::string getAudioSinkIds();
    std::string getAudioSourceIds();
    std::string scanAudioDevices();
    std::string startBtSco();
    std::string stopBtSco();

    void doPermissionsDialogs();
    std::string getBatteryLevel();

}