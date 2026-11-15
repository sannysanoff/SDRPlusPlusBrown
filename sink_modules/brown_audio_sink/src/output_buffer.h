#pragma once
#include <atomic>
#include <vector>
#include <chrono>
#include <dsp/sink.h>

// One DSP producer and one hardware consumer. Only prepare/reset may allocate;
// they run with both threads stopped. The hardware side never waits or locks.
class AudioOutputBuffer : public dsp::Sink<dsp::stereo_t> {
public:
    ~AudioOutputBuffer() { stop(); }

    void prepare(size_t frames) {
        size_t capacity = 1;
        while (capacity < frames) { capacity *= 2; }
        samples.resize(capacity);
        mask = capacity - 1;
        readPosition.store(0);
        writePosition.store(0);
    }

    void start() override {
        stopping.store(false);
        dsp::Sink<dsp::stereo_t>::start();
    }

    void stop() override {
        stopping.store(true);
        if (_block_init) { dsp::Sink<dsp::stereo_t>::stop(); }
    }

    size_t consume(dsp::stereo_t* output, size_t count) {
        size_t read = readPosition.load(std::memory_order_relaxed);
        size_t available = writePosition.load(std::memory_order_acquire) - read;
        size_t copied = (std::min)(count, available);
        for (size_t i = 0; i < copied; i++) { output[i] = samples[(read + i) & mask]; }
        readPosition.store(read + copied, std::memory_order_release);
        for (size_t i = copied; i < count; i++) { output[i] = {0, 0}; }
        return copied;
    }

    int run() override {
        int count = _in->read();
        if (count < 0) { return -1; }
        size_t offset = 0;
        while (offset < (size_t)count && !stopping.load()) {
            size_t write = writePosition.load(std::memory_order_relaxed);
            size_t free = samples.size() - (write - readPosition.load(std::memory_order_acquire));
            size_t copied = (std::min)(free, (size_t)count - offset);
            for (size_t i = 0; i < copied; i++) { samples[(write + i) & mask] = _in->readBuf[offset + i]; }
            writePosition.store(write + copied, std::memory_order_release);
            offset += copied;
            if (!copied) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        }
        _in->flush();
        return stopping.load() ? -1 : count;
    }

private:
    static_assert(std::atomic<size_t>::is_always_lock_free, "Audio indices must be lock-free");
    std::vector<dsp::stereo_t> samples;
    size_t mask = 0;
    std::atomic<size_t> readPosition{0};
    std::atomic<size_t> writePosition{0};
    std::atomic<bool> stopping{true};
};
