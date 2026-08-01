#pragma once

#include <string>
#include "config.h"

struct FrequencyBookmark {
    double frequency;
    double bandwidth;
    int modeIndex;
    bool selected;
    std::string vfoName; // VFO/radio this bookmark belongs to; empty = legacy (apply to currently selected VFO)
};

struct WaterfallBookmark {
    std::string listName;
    std::string bookmarkName;
    std::string extraInfo;
    bool worked;
    FrequencyBookmark bookmark;
    long long notValidAfter;
};

struct TransientBookmarkManager {
    std::vector<WaterfallBookmark> transientBookmarks;

    virtual void refreshWaterfallBookmarks(bool lockConfig = true) = 0;
    virtual const char *getModesList() = 0;
};

void applyBookmark(FrequencyBookmark bm, std::string vfoName);
ConfigManager &getFrequencyManagerConfig();