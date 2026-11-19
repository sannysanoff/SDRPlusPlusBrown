#pragma once

#include <array>
#include <condition_variable>
#include <mutex>
#include <set>
#include <thread>
#include <RtAudio.h>
#include <signal_path/signal_path.h>

// Control operations run on the sink/UI thread. The callback and worker never
// access a sink instance: removing the first user cannot invalidate capture.
class SharedMicrophone {
public:
    ~SharedMicrophone() { stop(); }

    bool enabled = false;
    unsigned int sampleRate = 48000;

    void acquire(void* user) {
        users.insert(user);
        if (enabled && !audio.isStreamRunning()) { start(); }
    }

    void release(void* user) {
        users.erase(user);
        if (users.empty()) { stop(); }
    }

    void configure(bool enable, unsigned int rate) {
        if (enabled == enable && sampleRate == rate && (!enable || audio.isStreamRunning())) { return; }
        stop();
        enabled = enable;
        sampleRate = rate;
        if (enabled && !users.empty()) { start(); }
    }

private:
    void start() {
        stop();
        try {
            if (!audio.getDeviceCount()) { return; }
            RtAudio::StreamParameters input;
            input.deviceId = audio.getDefaultInputDevice();
            input.nChannels = 1;
            if (!audio.getDeviceInfo(input.deviceId).inputChannels) { return; }
            unsigned int frames = (sampleRate * 20) / 1000;
            RtAudio::StreamOptions options;
            options.streamName = "SDR++ shared microphone";
#if RTAUDIO_VERSION_MAJOR >= 6
            if (audio.openStream(nullptr, &input, RTAUDIO_FLOAT32, sampleRate, &frames, callback, this, &options) != RTAUDIO_NO_ERROR) {
                throw std::runtime_error("Could not open shared microphone");
            }
#else
            audio.openStream(nullptr, &input, RTAUDIO_FLOAT32, sampleRate, &frames, callback, this, &options);
#endif
            // Never replace a different sink provider's microphone pipeline.
            if (sigpath::sinkManager.defaultInputAudio.hasInput()) {
                throw std::runtime_error("Microphone pipeline is already in use");
            }
            sigpath::sinkManager.defaultInputAudio.setInput(&stream);
            connected = true;
            sigpath::sinkManager.defaultInputAudio.start();
            {
                std::lock_guard<std::mutex> lock(mutex);
                stopping = false;
                pending = 0;
            }
            worker = std::thread([this] {
                for (;;) {
                    unsigned int count;
                    {
                        std::unique_lock<std::mutex> lock(mutex);
                        ready.wait(lock, [this] { return stopping || pending; });
                        if (stopping) { return; }
                        count = pending;
                        for (unsigned int i = 0; i < count; i++) {
                            stream.writeBuf[i].l = stream.writeBuf[i].r = buffer[i];
                        }
                        pending = 0;
                    }
                    if (!stream.swap(count)) { return; }
                }
            });
#if RTAUDIO_VERSION_MAJOR >= 6
            if (audio.startStream() != RTAUDIO_NO_ERROR) {
                throw std::runtime_error("Could not start shared microphone");
            }
#else
            audio.startStream();
#endif
        } catch (const std::exception& e) {
            flog::error("Shared microphone: {}", e.what());
            stop();
        } catch (...) {
            flog::error("Could not start shared microphone");
            stop();
        }
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        ready.notify_all();
        stream.stopWriter();
        // closeStream also stops capture; the callback never waits on DSP.
        if (audio.isStreamOpen()) { audio.closeStream(); }
        if (worker.joinable()) { worker.join(); }
        if (connected) {
            sigpath::sinkManager.defaultInputAudio.stop();
            sigpath::sinkManager.defaultInputAudio.setInput(nullptr);
            connected = false;
        }
        stream.flush();
        stream.clearWriteStop();
    }

    static int callback(void*, void* input, unsigned int frames, double, RtAudioStreamStatus, void* context) {
        auto* self = static_cast<SharedMicrophone*>(context);
        // Bounded handoff: a stalled DSP consumer must never block RtAudio.
        std::unique_lock<std::mutex> lock(self->mutex, std::try_to_lock);
        if (!lock.owns_lock() || self->stopping || !input || frames > self->buffer.size()) { return 0; }
        memcpy(self->buffer.data(), input, frames * sizeof(float));
        self->pending = frames;
        self->ready.notify_one();
        return 0;
    }

    std::set<void*> users;
    RtAudio audio;
    dsp::stream<dsp::stereo_t> stream = "brown_audio_sink.shared_microphone";
    bool connected = false;
    std::mutex mutex;
    std::condition_variable ready;
    bool stopping = true;
    unsigned int pending = 0;
    std::array<float, 16384> buffer;
    std::thread worker;
};
