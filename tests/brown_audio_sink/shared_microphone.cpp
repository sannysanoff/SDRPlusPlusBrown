#include <cassert>
#include <chrono>
#include <iostream>
#include "../../../sink_modules/brown_audio_sink/src/shared_microphone.h"

namespace flog {
    std::string __toString__(const char* value) { return value; }
    std::string __toString__(int value) { return std::to_string(value); }
    std::string formatString(const char* value, const std::vector<std::string>&) { return value; }
    void __log__(Type, const char*, const std::vector<std::string>&) {}
}

int main() {
    SharedMicrophone mic;
    int first, second;
    mic.acquire(&first); // no primary has enabled capture yet
    assert(!RtAudio::active);
    mic.configure(true, 48000);
    mic.acquire(&first);
    assert(RtAudio::opens == 1);
    assert(sigpath::sinkManager.defaultInputAudio.hasInput());
    mic.acquire(&first); // duplicate start must not add another reference
    mic.acquire(&second);
    assert(RtAudio::opens == 1);
    mic.release(&first); // first instance may now be destroyed
    assert(RtAudio::closes == 0);
    RtAudio::active->feed();

    // Stall the actual DSP splitter on an output with no reader. Capture must
    // remain bounded, and shutdown must release the blocked forwarding worker.
    dsp::stream<dsp::stereo_t> stalled;
    sigpath::sinkManager.defaultInputAudio.bindStream(&stalled);
    for (int i = 0; i < 100 && !stalled.isDataReady(); i++) {
        RtAudio::active->feed();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(stalled.isDataReady());
    auto began = std::chrono::steady_clock::now();
    for (int i = 0; i < 10000; i++) { RtAudio::active->feed(); }
    mic.release(&second);
    assert(std::chrono::steady_clock::now() - began < std::chrono::seconds(2));
    assert(!sigpath::sinkManager.defaultInputAudio.hasInput());
    sigpath::sinkManager.defaultInputAudio.unbindStream(&stalled);
    assert(RtAudio::opens == RtAudio::closes);
    mic.release(&first); // repeated cleanup is harmless

    for (int failure : {1, 2, 3}) {
        RtAudio::fail = failure;
        mic.acquire(&first);
        assert(!sigpath::sinkManager.defaultInputAudio.hasInput());
        assert(RtAudio::opens == RtAudio::closes);
        RtAudio::fail = 0;
        mic.acquire(&second); // retry after failure, without losing first user
        assert(sigpath::sinkManager.defaultInputAudio.hasInput());
        mic.release(&second);
        assert(RtAudio::active);
        mic.release(&first);
    }
    RtAudio::devices = 0;
    mic.acquire(&first);
    assert(!RtAudio::active);
    RtAudio::devices = 1;
    mic.configure(true, 48000); // recover missing device
    assert(RtAudio::active);
    mic.configure(false, 48000); // toggle while users remain
    assert(!RtAudio::active);
    mic.acquire(&second);
    mic.configure(true, 44100);
    assert(RtAudio::active);
    mic.release(&first);
    assert(RtAudio::active);
    mic.release(&second);

    // Do not steal another provider's input or disconnect it on failure.
    dsp::stream<dsp::stereo_t> other;
    sigpath::sinkManager.defaultInputAudio.setInput(&other);
    mic.acquire(&first);
    assert(!RtAudio::active);
    assert(sigpath::sinkManager.defaultInputAudio.hasInput());
    mic.release(&first);
    assert(sigpath::sinkManager.defaultInputAudio.hasInput());
    sigpath::sinkManager.defaultInputAudio.setInput(nullptr);
    assert(RtAudio::opens == RtAudio::closes);
    {
        SharedMicrophone scoped;
        scoped.configure(true, 48000);
        scoped.acquire(&first);
        assert(RtAudio::active);
    }
    assert(!RtAudio::active);
    assert(!sigpath::sinkManager.defaultInputAudio.hasInput());
    std::cout << "Shared microphone lifecycle passed\n";
}
