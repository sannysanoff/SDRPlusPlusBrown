#pragma once
#include <dsp/routing/splitter.h>
namespace sigpath {
    inline struct TestSinkManager {
        dsp::routing::Splitter<dsp::stereo_t> defaultInputAudio{nullptr};
    } sinkManager;
}
