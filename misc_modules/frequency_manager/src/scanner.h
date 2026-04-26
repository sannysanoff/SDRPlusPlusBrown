#pragma once

#include <string>
#include <vector>
#include <deque>
#include <functional>
#include <imgui.h>
#include <module.h>
#include "frequency_manager.h"
#include <gui/widgets/snr_meter.h>

class FrequencyManagerModule; // Forward declaration

class Scanner {
public:
    Scanner(FrequencyManagerModule* module);
    ~Scanner();

    void onPlayStateChange(bool playing);

    void setBookmarks(const std::vector<std::string>& bookmarks, const std::map<std::string, FrequencyBookmark>& bookmarksMap);
    void setCurrentFrequency(double freq);
    void setNoiseFloor(float noiseFloor);

    void render();
    void update(float deltaTime);

    // Public accessors for debug commands
    bool isScanning() const { return scanning; }
    std::string getCurrentStation() const { return currentStation; }
    void startScanner() { start(); }
    void stopScanner() { stop(); }

private:
    FrequencyManagerModule* module;
    bool scanning = false;
    float scanIntervalMs = 100.0f; // Default 100ms between stations
    float listenTimeSec = 10.0f; // Default 10 seconds per station
    float elapsedTime = 0.0f;
    size_t currentStationIndex = 0;
    float noiseFloor = -120.0f; // Default noise floor
    float signalMarginDb = 6.0f; // Signal detection margin
    bool signalDetected = false;
    
    std::vector<std::string> bookmarks;
    std::map<std::string, FrequencyBookmark> bookmarksMap;
    std::string currentStation;
    FrequencyBookmark currentBookmark;
    float timeSinceLastSwitch = 0.0f;
    
    void start();
    void stop();
    void nextStation();
    void onSNRMeterExtPoint(ImGui::SNRMeterExtPoint point);
    EventHandler<ImGui::SNRMeterExtPoint> snrMeterHandler;
    EventHandler<bool> playStateHandler;
    float signalLevelSum = 0.0f;
    int signalLevelCount = 0;
    bool inSignalDetection = false;
    bool squelchEnabled = false;
    
    // For "Use Current" button
    struct SignalSample {
        float level;
        float time;
    };
    std::deque<SignalSample> signalHistory;
    float signalHistorySum = 0.0f;
};

