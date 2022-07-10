//
// Created by san on 10/07/22.
//
#pragma once

#include <vector>

class BackgroundNoiseCaltulator {

    double lastNoise = ERASED_SAMPLE;
    static constexpr auto NBUCKETS = 1000;
    static constexpr auto SKIP_FRAMES = 10;
    std::vector<int> buckets;
    std::vector<float> logFrame;
    int frameCount = 0;

public:

    static constexpr auto ERASED_SAMPLE = 1e9f;

    void reset();

    float addFrame(const std::vector<float> &fftFrame);

};