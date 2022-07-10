//
// Created by san on 10/07/22.
//
#pragma once

#include <vector>

class BackgroundNoiseCaltulator {
public:

    static constexpr auto ERASED_SAMPLE = 1e9f;

    void reset();

    void addFrame(const std::vector<float> &fftFrame);

};