#pragma once
#include <atomic>
#include <vector>
#include <chrono>
#include <mutex>
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

    // Start collecting exactly what the callback hands to the device.
    // Preallocates on the caller thread so the audio callback never allocates.
    void startCapture(size_t frames) {
        std::lock_guard<std::mutex> lock(captureMutex);
        capture.resize(frames);
        captureWritten = 0;
        captureLimit = frames;
        capturing.store(frames > 0, std::memory_order_release);
    }

    std::vector<dsp::stereo_t> copyCapture() {
        std::lock_guard<std::mutex> lock(captureMutex);
        return std::vector<dsp::stereo_t>(capture.begin(), capture.begin() + captureWritten);
    }

    std::atomic<uint64_t> writtenFrames{0};
    std::atomic<uint64_t> writeBlocks{0};
    std::atomic<unsigned> maxWriteFrames{0};

    // Frames waiting to be played, for diagnosis.
    size_t available() const {
        return writePosition.load(std::memory_order_acquire) - readPosition.load(std::memory_order_acquire);
    }

    size_t capacity() const { return samples.size(); }

    size_t consume(dsp::stereo_t* output, size_t count) {
        size_t read = readPosition.load(std::memory_order_relaxed);
        size_t available = writePosition.load(std::memory_order_acquire) - read;
        size_t copied = (std::min)(count, available);
        for (size_t i = 0; i < copied; i++) { output[i] = samples[(read + i) & mask]; }
        readPosition.store(read + copied, std::memory_order_release);
        for (size_t i = copied; i < count; i++) { output[i] = {0, 0}; }
        if (capturing.load(std::memory_order_acquire)) {
            std::unique_lock<std::mutex> lock(captureMutex, std::try_to_lock);
            if (lock.owns_lock() && capturing.load(std::memory_order_relaxed)) {
                size_t toSave = (std::min)(count, captureLimit - captureWritten);
                for (size_t i = 0; i < toSave; i++) { capture[captureWritten + i] = output[i]; }
                captureWritten += toSave;
                if (captureWritten >= captureLimit) { capturing.store(false, std::memory_order_release); }
            }
        }
        return copied;
    }

    int run() override {
        int count = _in->read();
        if (count < 0) { return -1; }
        writeBlocks.fetch_add(1, std::memory_order_relaxed);
        unsigned prevMax = maxWriteFrames.load(std::memory_order_relaxed);
        while ((unsigned)count > prevMax && !maxWriteFrames.compare_exchange_weak(prevMax, (unsigned)count, std::memory_order_relaxed)) {}
        size_t offset = 0;
        while (offset < (size_t)count && !stopping.load()) {
            size_t write = writePosition.load(std::memory_order_relaxed);
            size_t free = samples.size() - (write - readPosition.load(std::memory_order_acquire));
            size_t copied = (std::min)(free, (size_t)count - offset);
            for (size_t i = 0; i < copied; i++) { samples[(write + i) & mask] = _in->readBuf[offset + i]; }
            writePosition.store(write + copied, std::memory_order_release);
            writtenFrames.fetch_add(copied, std::memory_order_relaxed);
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
    std::mutex captureMutex;
    std::vector<dsp::stereo_t> capture;
    std::atomic<bool> capturing{false};
    size_t captureWritten = 0;
    size_t captureLimit = 0;
};
