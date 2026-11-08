// SPDX-License-Identifier: MIT
// Copyright (C) 2026 DeepFist integration for SDR++ Brown

#pragma once

#include <string>
#include <vector>

// Minimal interface exposed to the test framework.
struct TinyCWInterface {
    virtual bool isReady() = 0;
    virtual int activeSignals() = 0;
    virtual int liveChannels() = 0;
    virtual int carrierMarkers() = 0;
    virtual int decodeMarkers() = 0;
    virtual int spotCount() = 0;
    virtual long scanFrameCount() = 0;
    virtual double busyPercent() = 0;
    // Decoded lines since the previous call, formatted "freqKHz|text".
    virtual std::vector<std::string> takeDecodedLines() = 0;
};