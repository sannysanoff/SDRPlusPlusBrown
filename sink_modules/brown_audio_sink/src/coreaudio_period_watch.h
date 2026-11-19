#pragma once

#include <atomic>
#include <string>

#ifdef __APPLE__
#include <Block.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>
#include <cstring>
#include <vector>
#endif

// RtAudio's CoreAudio backend caches stream_.bufferSize at openStream() time
// and passes that stale frame count to the user callback even if another client
// changes kAudioDevicePropertyBufferFrameSize on the shared HAL device, while
// only copying mDataByteSize bytes to CoreAudio (dropping the remaining frames).
// Watch the HAL device's actual buffer frame size so the callback consumes the
// exact number of frames CoreAudio will actually play.
class CoreAudioPeriodWatch {
public:
    ~CoreAudioPeriodWatch() { stop(); }

    void start(const std::string& rtDeviceName, bool isDefaultOutput, unsigned int negotiatedFrames) {
        stop();
        currentFrames.store(negotiatedFrames, std::memory_order_relaxed);
#ifdef __APPLE__
        deviceId = findCoreAudioDevice(rtDeviceName, isDefaultOutput);
        if (deviceId == kAudioObjectUnknown) { return; }

        unsigned int actual = queryBufferFrames(deviceId);
        if (actual > 0) {
            currentFrames.store(actual, std::memory_order_relaxed);
        }

        queue = dispatch_queue_create("sdrpp.brown_audio_sink.period_watch", DISPATCH_QUEUE_SERIAL);
        if (!queue) {
            deviceId = kAudioObjectUnknown;
            return;
        }

        AudioDeviceID watchedDev = deviceId;
        std::atomic<unsigned int>* target = &currentFrames;
        AudioObjectPropertyListenerBlock block = ^(UInt32, const AudioObjectPropertyAddress*) {
            unsigned int updated = queryBufferFrames(watchedDev);
            if (updated > 0) {
                target->store(updated, std::memory_order_relaxed);
            }
        };
        listener = Block_copy(block);

        AudioObjectPropertyAddress addr = {
            kAudioDevicePropertyBufferFrameSize,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        if (AudioObjectAddPropertyListenerBlock(deviceId, &addr, queue, listener) != noErr) {
            Block_release(listener);
            listener = nullptr;
            dispatch_release(queue);
            queue = nullptr;
            deviceId = kAudioObjectUnknown;
            return;
        }
        listening = true;
#else
        (void)rtDeviceName;
        (void)isDefaultOutput;
#endif
    }

    void stop() {
#ifdef __APPLE__
        if (listening && deviceId != kAudioObjectUnknown && queue && listener) {
            AudioObjectPropertyAddress addr = {
                kAudioDevicePropertyBufferFrameSize,
                kAudioObjectPropertyScopeGlobal,
                kAudioObjectPropertyElementMain
            };
            AudioObjectRemovePropertyListenerBlock(deviceId, &addr, queue, listener);
            // Drain any in-flight notification before releasing resources or destroying this object.
            dispatch_sync(queue, ^{});
        }
        listening = false;
        if (listener) {
            Block_release(listener);
            listener = nullptr;
        }
        if (queue) {
            dispatch_release(queue);
            queue = nullptr;
        }
        deviceId = kAudioObjectUnknown;
#endif
        currentFrames.store(0, std::memory_order_relaxed);
    }

    unsigned int frames() const {
        return currentFrames.load(std::memory_order_relaxed);
    }

private:
#ifdef __APPLE__
    static std::string cfStringToStd(CFStringRef ref) {
        if (!ref) { return {}; }
        CFIndex len = CFStringGetLength(ref);
        CFIndex maxSize = CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8) + 1;
        std::string out(size_t(maxSize), '\0');
        if (!CFStringGetCString(ref, out.data(), maxSize, kCFStringEncodingUTF8)) { return {}; }
        out.resize(std::strlen(out.c_str()));
        return out;
    }

    static AudioDeviceID findCoreAudioDevice(const std::string& rtName, bool isDefaultOutput) {
        AudioObjectPropertyAddress devListAddr = {
            kAudioHardwarePropertyDevices,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        UInt32 size = 0;
        if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &devListAddr, 0, nullptr, &size) == noErr && size > 0) {
            std::vector<AudioDeviceID> devs(size / sizeof(AudioDeviceID));
            if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &devListAddr, 0, nullptr, &size, devs.data()) == noErr) {
                for (AudioDeviceID id : devs) {
                    CFStringRef cfName = nullptr;
                    CFStringRef cfMfr = nullptr;
                    UInt32 propSize = sizeof(CFStringRef);
                    AudioObjectPropertyAddress nameAddr = {
                        kAudioObjectPropertyName,
                        kAudioObjectPropertyScopeGlobal,
                        kAudioObjectPropertyElementMain
                    };
                    AudioObjectPropertyAddress mfrAddr = {
                        kAudioObjectPropertyManufacturer,
                        kAudioObjectPropertyScopeGlobal,
                        kAudioObjectPropertyElementMain
                    };
                    AudioObjectGetPropertyData(id, &nameAddr, 0, nullptr, &propSize, &cfName);
                    propSize = sizeof(CFStringRef);
                    AudioObjectGetPropertyData(id, &mfrAddr, 0, nullptr, &propSize, &cfMfr);
                    std::string name = cfStringToStd(cfName);
                    std::string mfr = cfStringToStd(cfMfr);
                    if (cfName) { CFRelease(cfName); }
                    if (cfMfr) { CFRelease(cfMfr); }
                    std::string full = mfr.empty() ? name : (mfr + ": " + name);
                    if (full == rtName || name == rtName) { return id; }
                }
            }
        }
        if (isDefaultOutput) {
            AudioDeviceID defDev = kAudioObjectUnknown;
            UInt32 defSize = sizeof(defDev);
            AudioObjectPropertyAddress defAddr = {
                kAudioHardwarePropertyDefaultOutputDevice,
                kAudioObjectPropertyScopeGlobal,
                kAudioObjectPropertyElementMain
            };
            if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &defAddr, 0, nullptr, &defSize, &defDev) == noErr) {
                return defDev;
            }
        }
        return kAudioObjectUnknown;
    }

    static unsigned int queryBufferFrames(AudioDeviceID dev) {
        UInt32 val = 0;
        UInt32 size = sizeof(val);
        AudioObjectPropertyAddress addr = {
            kAudioDevicePropertyBufferFrameSize,
            kAudioObjectPropertyScopeOutput,
            kAudioObjectPropertyElementMain
        };
        if (AudioObjectGetPropertyData(dev, &addr, 0, nullptr, &size, &val) != noErr) { return 0; }
        return val;
    }

    AudioDeviceID deviceId = kAudioObjectUnknown;
    dispatch_queue_t queue = nullptr;
    AudioObjectPropertyListenerBlock listener = nullptr;
    bool listening = false;
#endif
    std::atomic<unsigned int> currentFrames{0};
};
