#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include "../../sink_modules/brown_audio_sink/src/coreaudio_period_watch.h"

#ifdef __APPLE__
#include <RtAudio.h>

struct SinkState {
    CoreAudioPeriodWatch* watch = nullptr;
    std::atomic<uint64_t> consumedFrames{0};
    std::atomic<uint64_t> callbacks{0};
};

static int primaryCallback(void* out, void*, unsigned int nFrames, double, RtAudioStreamStatus, void* userData) {
    auto* state = static_cast<SinkState*>(userData);
    unsigned int tracked = state->watch->frames();
    unsigned int effective = (tracked > 0 && tracked < nFrames) ? tracked : nFrames;
    std::memset(out, 0, size_t(nFrames) * 2 * sizeof(float));
    state->consumedFrames.fetch_add(effective, std::memory_order_relaxed);
    state->callbacks.fetch_add(1, std::memory_order_relaxed);
    return 0;
}

static AudioDeviceID defaultCoreAudioOutputDevice() {
    AudioDeviceID dev = kAudioObjectUnknown;
    UInt32 size = sizeof(dev);
    AudioObjectPropertyAddress addr = {
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &size, &dev) != noErr) {
        return kAudioObjectUnknown;
    }
    return dev;
}

int main() {
    RtAudio primary;
    unsigned int devId = primary.getDefaultOutputDevice();
    if (devId == 0) {
        std::cout << "No default CoreAudio output device; skipping live CoreAudio period watch test\n";
        return 0;
    }
    auto info = primary.getDeviceInfo(devId);
    if (info.outputChannels < 2) {
        std::cout << "Default CoreAudio output device has <2 channels; skipping live CoreAudio period watch test\n";
        return 0;
    }

    unsigned int sr = info.preferredSampleRate ? info.preferredSampleRate : 48000;
    unsigned int period = (sr * 20) / 1000;
    RtAudio::StreamParameters params;
    params.deviceId = devId;
    params.nChannels = 2;
    RtAudio::StreamOptions opts;

    CoreAudioPeriodWatch watch;
    SinkState state;
    state.watch = &watch;

    if (primary.openStream(&params, nullptr, RTAUDIO_FLOAT32, sr, &period, &primaryCallback, &state, &opts) != RTAUDIO_NO_ERROR) {
        std::cout << "Could not open primary RtAudio stream; skipping\n";
        return 0;
    }
    watch.start(info.name, info.isDefaultOutput, period);
    assert(watch.frames() > 0);
    if (primary.startStream() != RTAUDIO_NO_ERROR) {
        watch.stop();
        primary.closeStream();
        std::cout << "Could not start primary RtAudio stream; skipping\n";
        return 0;
    }

    // Simulate another in-process CoreAudio client shrinking kAudioDevicePropertyBufferFrameSize.
    AudioDeviceID caDev = defaultCoreAudioOutputDevice();
    if (caDev != kAudioObjectUnknown && period > 128) {
        UInt32 smaller = 128;
        AudioObjectPropertyAddress addr = {
            kAudioDevicePropertyBufferFrameSize,
            kAudioObjectPropertyScopeOutput,
            kAudioObjectPropertyElementMain
        };
        if (AudioObjectSetPropertyData(caDev, &addr, 0, nullptr, sizeof(smaller), &smaller) == noErr) {
            UInt32 actual = 0;
            UInt32 sz = sizeof(actual);
            if (AudioObjectGetPropertyData(caDev, &addr, 0, nullptr, &sz, &actual) == noErr && actual < period) {
                for (int i = 0; i < 50 && watch.frames() != actual; i++) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                assert(watch.frames() == actual);

                state.consumedFrames.store(0);
                state.callbacks.store(0);
                std::this_thread::sleep_for(std::chrono::milliseconds(400));
                uint64_t consumed = state.consumedFrames.load();
                uint64_t expected = uint64_t(sr) * 400 / 1000;
                // Without tracking, consumed would be ~(period / actual) * expected (~7.5x).
                assert(consumed > expected / 2 && consumed < expected * 2);
            }
        }
    }

    primary.closeStream();
    watch.stop();
    assert(watch.frames() == 0);
    std::cout << "CoreAudio period watch tracking passed\n";
    return 0;
}
#else
int main() {
    CoreAudioPeriodWatch watch;
    assert(watch.frames() == 0);
    watch.start("dummy", true, 960);
    assert(watch.frames() == 960);
    watch.stop();
    assert(watch.frames() == 0);
    std::cout << "CoreAudio period watch stub passed\n";
    return 0;
}
#endif
