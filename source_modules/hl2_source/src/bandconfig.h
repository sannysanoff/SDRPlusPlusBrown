#pragma once

#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <config.h>


struct BandSpec {
    std::string label;
    int lowRange, highRange; // in hz
    int bits;
};

struct AllBands {
    std::vector<BandSpec> values;
};


bool bandsEditor(ConfigManager &config, bool isTx, int currentFreq);
void loadBandsConfig(ConfigManager &config);
void initBands();
std::pair<int, int> getBitsForBand(int frequency, bool tx);
