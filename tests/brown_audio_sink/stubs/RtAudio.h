#pragma once
#include <string>
#include <stdexcept>
#ifndef RTAUDIO_VERSION_MAJOR
#define RTAUDIO_VERSION_MAJOR 6
#endif
constexpr int RTAUDIO_FLOAT32 = 1;
constexpr int RTAUDIO_NO_ERROR = 0;
using RtAudioStreamStatus = unsigned int;
class RtAudio {
public:
    struct StreamParameters { unsigned int deviceId = 0, nChannels = 0; };
    struct StreamOptions { std::string streamName; };
    struct DeviceInfo { unsigned int inputChannels = 1; };
    using Callback = int(*)(void*, void*, unsigned int, double, RtAudioStreamStatus, void*);
    inline static int opens = 0, closes = 0, devices = 1, fail = 0;
    inline static RtAudio* active = nullptr;
    bool isStreamOpen() { return opened; }
    bool isStreamRunning() { return running; }
    unsigned int getDeviceCount() { return devices; }
    unsigned int getDefaultInputDevice() { return 1; }
    DeviceInfo getDeviceInfo(unsigned int) { return {}; }
    int openStream(StreamParameters*, StreamParameters*, int, unsigned int, unsigned int*, Callback cb, void* ctx, StreamOptions*) {
        if (fail == 1) {
#if RTAUDIO_VERSION_MAJOR >= 6
            return 1;
#else
            throw std::runtime_error("open failed");
#endif
        }
        if (fail == 3) { throw std::runtime_error("open failed"); }
        callback = cb; context = ctx; opened = true; opens++; active = this; return 0;
    }
    int startStream() {
        if (fail == 2) {
#if RTAUDIO_VERSION_MAJOR >= 6
            return 1;
#else
            throw std::runtime_error("start failed");
#endif
        }
        running = true; return 0;
    }
    void closeStream() { if (opened) { closes++; } opened = running = false; active = nullptr; }
    void feed() { float samples[240] = {}; callback(nullptr, samples, 240, 0, 0, context); }
private:
    bool opened = false, running = false;
    Callback callback = nullptr;
    void* context = nullptr;
};
