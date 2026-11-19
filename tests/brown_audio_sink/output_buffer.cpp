#include <cassert>
#include <chrono>
#include <iostream>
#include "../../../sink_modules/brown_audio_sink/src/output_buffer.h"

namespace flog {
    std::string __toString__(const char* value) { return value; }
    std::string __toString__(int value) { return std::to_string(value); }
    std::string formatString(const char* value, const std::vector<std::string>&) { return value; }
    void __log__(Type, const char*, const std::vector<std::string>&) {}
}

int main() {
    dsp::stream<dsp::stereo_t> input;
    AudioOutputBuffer buffer;
    buffer.init(&input);
    buffer.prepare(8);
    buffer.start();
    std::thread producer([&] {
        for (int batch = 0; batch < 100; batch++) {
            for (int i = 0; i < 37; i++) {
                float value = batch * 37 + i;
                input.writeBuf[i] = {value, -value};
            }
            assert(input.swap(37));
        }
    });
    int received = 0;
    dsp::stereo_t output[13];
    while (received < 3700) {
        auto count = buffer.consume(output, 13);
        for (size_t i = 0; i < count; i++, received++) {
            assert(output[i].l == received && output[i].r == -received);
        }
        for (size_t i = count; i < 13; i++) { assert(output[i].l == 0 && output[i].r == 0); }
        std::this_thread::yield();
    }
    producer.join();
    assert(buffer.writtenFrames.load() == 3700);
    assert(buffer.writeBlocks.load() == 100);
    assert(buffer.maxWriteFrames.load() == 37);

    // Preallocated capture records both real samples and zero-filled underrun frames.
    buffer.startCapture(10);
    for (int i = 0; i < 4; i++) { input.writeBuf[i] = {float(i + 1), float(-(i + 1))}; }
    assert(input.swap(4));
    while (buffer.available() < 4) { std::this_thread::yield(); }
    assert(buffer.consume(output, 6) == 4);
    assert(buffer.consume(output, 6) == 0);
    auto captured = buffer.copyCapture();
    assert(captured.size() == 10);
    for (int i = 0; i < 4; i++) { assert(captured[i].l == i + 1 && captured[i].r == -(i + 1)); }
    for (int i = 4; i < 10; i++) { assert(captured[i].l == 0 && captured[i].r == 0); }

    buffer.stop();
    buffer.stop();
    buffer.prepare(8);
    assert(buffer.consume(output, 13) == 0); // restart clears stale audio
    buffer.start();
    assert(input.swap(1000)); // larger than the ring; no hardware reader
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto before = std::chrono::steady_clock::now();
    buffer.stop();
    assert(std::chrono::steady_clock::now() - before < std::chrono::seconds(1));
    std::cout << "Output ordering, wraparound, underrun and stalled-consumer shutdown passed\n";
}
