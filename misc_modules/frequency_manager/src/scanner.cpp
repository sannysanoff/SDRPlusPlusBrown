#include "scanner.h"
#include "frequency_manager.h"  // For FrequencyManagerModule definition
#include <utils/flog.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <signal_path/signal_path.h>

Scanner::Scanner(FrequencyManagerModule* module) : module(module) {
    snrMeterHandler.ctx = this;
    snrMeterHandler.handler = [](ImGui::SNRMeterExtPoint point, void* ctx) {
        ((Scanner*)ctx)->onSNRMeterExtPoint(point);
    };
    ImGui::onSNRMeterExtPoint.bindHandler(&snrMeterHandler);
    
    // Listen for play state changes
    playStateHandler.ctx = this;
    playStateHandler.handler = [](bool playing, void* ctx) {
        ((Scanner*)ctx)->onPlayStateChange(playing);
    };
    gui::mainWindow.onPlayStateChange.bindHandler(&playStateHandler);

    // Load scanner settings from config
    auto& config = getFrequencyManagerConfig();
    config.acquire();
    
    // Initialize with defaults if scanner section doesn't exist
    if (!config.conf.contains("scanner")) {
        config.conf["scanner"] = json::object();
        config.conf["scanner"]["scanIntervalMs"] = 100.0f;
        config.conf["scanner"]["listenTimeSec"] = 10.0f;
        config.conf["scanner"]["noiseFloor"] = +3.0f;
        config.conf["scanner"]["signalMarginDb"] = 4.0f;
        config.conf["scanner"]["squelchEnabled"] = false;
        config.conf["scanner"]["carrierHoldMode"] = false;
        config.release(true);
        config.acquire();
    }

    scanIntervalMs = config.conf["scanner"]["scanIntervalMs"];
    listenTimeSec = config.conf["scanner"]["listenTimeSec"];
    noiseFloor = config.conf["scanner"]["noiseFloor"];
    signalMarginDb = config.conf["scanner"]["signalMarginDb"];
    squelchEnabled = config.conf["scanner"]["squelchEnabled"];
    if (config.conf["scanner"].contains("carrierHoldMode")) {
        carrierHoldMode = config.conf["scanner"]["carrierHoldMode"];
    } else {
        carrierHoldMode = false;
        config.conf["scanner"]["carrierHoldMode"] = false;
    }
    config.release();
}

Scanner::~Scanner() {
    stop();
    ImGui::onSNRMeterExtPoint.unbindHandler(&snrMeterHandler);
    gui::mainWindow.onPlayStateChange.unbindHandler(&playStateHandler);
}

void Scanner::setBookmarks(const std::vector<std::string>& bookmarks, const std::map<std::string, FrequencyBookmark>& bookmarksMap) {
    // If bookmarks changed, stop scanning
    if (this->bookmarks != bookmarks && scanning) {
        stop();
    }
    
    this->bookmarks = bookmarks;
    this->bookmarksMap = bookmarksMap;
    
    // Update current bookmark if needed
    if (!bookmarks.empty() && currentStationIndex < bookmarks.size()) {
        currentStation = bookmarks[currentStationIndex];
        currentBookmark = bookmarksMap.at(currentStation);
    }
}

void Scanner::setCurrentFrequency(double freq) {
    // This will be populated later
}

void Scanner::setNoiseFloor(float noiseFloor) {
    this->noiseFloor = noiseFloor;
}


void Scanner::render() {
    if (ImGui::CollapsingHeader("Scanner", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Start/Stop buttons
        if (scanning) {
            if (ImGui::Button("Stop Scanning")) {
                stop();
            }
        } else {
            if (ImGui::Button("Start Scanning")) {
                start();
            }
        }

        // Scan interval input
        ImGui::LeftLabel("Scan Interval (ms)");
        ImGui::SetNextItemWidth(100);
        if (ImGui::InputFloat("##scanner_interval", &scanIntervalMs, 10.0f, 50.0f, "%.1f")) {
            auto& config = getFrequencyManagerConfig();
            config.acquire();
            config.conf["scanner"]["scanIntervalMs"] = scanIntervalMs;
            config.release(true);
        }

        // Listen time input
        ImGui::LeftLabel(carrierHoldMode ? "Hang Time (s)" : "Listen Time (s)");
        ImGui::SetNextItemWidth(100);
        if (ImGui::InputFloat("##scanner_listen", &listenTimeSec, 0.1f, 1.0f, "%.1f")) {
            auto& config = getFrequencyManagerConfig();
            config.acquire();
            config.conf["scanner"]["listenTimeSec"] = listenTimeSec;
            config.release(true);
        }

        // Current station display
        ImGui::Text("Current Station: %s", currentStation.c_str());

        // Noise floor controls
        ImGui::LeftLabel("Noise Floor (dB)");
        ImGui::SetNextItemWidth(100);
        if (ImGui::InputFloat("##scanner_noise_floor", &noiseFloor, 1.0f, 5.0f, "%.1f")) {
            noiseFloor = std::max<float>(0.0f, std::min<float>(40.0f, noiseFloor));
            auto& config = getFrequencyManagerConfig();
            config.acquire();
            config.conf["scanner"]["noiseFloor"] = noiseFloor;
            config.release(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Use Current")) {
            if (!signalHistory.empty()) {
                noiseFloor = signalHistorySum / signalHistory.size();
            }
        }

        // Signal detection margin
        ImGui::LeftLabel("Signal Margin (dB)");
        ImGui::SetNextItemWidth(100);
        if (ImGui::InputFloat("##scanner_signal_margin", &signalMarginDb, 0.1f, 1.0f, "%.1f")) {
            auto& config = getFrequencyManagerConfig();
            config.acquire();
            config.conf["scanner"]["signalMarginDb"] = signalMarginDb;
            config.release(true);
        }

        // Squelch checkbox
        if (ImGui::Checkbox("Squelch", &squelchEnabled)) {
            auto& config = getFrequencyManagerConfig();
            config.acquire();
            config.conf["scanner"]["squelchEnabled"] = squelchEnabled;
            config.release(true);
        }

        ImGui::SameLine();

        // Carrier Hold checkbox
        if (ImGui::Checkbox("Carrier Hold", &carrierHoldMode)) {
            auto& config = getFrequencyManagerConfig();
            config.acquire();
            config.conf["scanner"]["carrierHoldMode"] = carrierHoldMode;
            config.release(true);
        }

        // Current signal level
        if (scanning) {
            if (signalLevelCount > 0) {
                float avgSignalLevel = signalLevelSum / signalLevelCount;
                ImGui::Text("Current Signal: %.1f dB", avgSignalLevel);
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Status: %s", 
                    signalDetected ? "Listening..." : "Detecting...");
            } else {
                ImGui::Text("Current Signal: -- dB");
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Status: Detecting...");
            }
        } else {
            // When not scanning, use 500ms history
            if (!signalHistory.empty()) {
                float avgSignalLevel = signalHistorySum / signalHistory.size();
                ImGui::Text("Current Signal: %.1f dB", avgSignalLevel);
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Status: %s", 
                    (avgSignalLevel > (noiseFloor + signalMarginDb)) ? "Detected" : "None");
            } else {
                ImGui::Text("Current Signal: -- dB");
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Status: None");
            }
        }
    }
}

void Scanner::start() {
    if (bookmarks.empty()) {
        flog::warn("No bookmarks available for scanning");
        return;
    }

    scanning = true;
    currentStationIndex = 0;
    timeSinceLastSwitch = 0.0f;
    signalLevelSum = 0.0f;
    signalLevelCount = 0;
    signalDetected = false;
    currentStation = bookmarks[currentStationIndex];
    currentBookmark = bookmarksMap[currentStation];
    applyBookmark(currentBookmark, gui::waterfall.selectedVFO);
    flog::info("Scanner started");
}

void Scanner::stop() {
    if (!scanning) return;
    
    scanning = false;
    if (squelchEnabled) {
        sigpath::sinkManager.setAllMuted(false);
    }
    flog::info("Scanner stopped");
}

void Scanner::onSNRMeterExtPoint(ImGui::SNRMeterExtPoint point) {
    if (!scanning) return;
    
    // Convert drawn value back to dB
    float signalLevelDb = point.lastDrawnValue / (point.postSnrLocation.x / 90.0f);
    
    // For signal detection
    // Always track signal level when scanning
    if (scanning) {
        signalLevelSum += signalLevelDb;
        signalLevelCount++;
        
        // For signal history
        float currentTime = ImGui::GetTime() * 1000.0f; // Convert to milliseconds
        signalHistory.push_back({signalLevelDb, currentTime});
        signalHistorySum += signalLevelDb;
        
        // Remove old samples (older than 500ms)
        while (!signalHistory.empty() && (currentTime - signalHistory.front().time) > 500.0f) {
            signalHistorySum -= signalHistory.front().level;
            signalHistory.pop_front();
        }
    }
}

void Scanner::update(float deltaTime) {
    if (!scanning || bookmarks.empty()) return;

    // Convert deltaTime from seconds to milliseconds
    timeSinceLastSwitch += deltaTime * 1000.0f;

    // Handle squelch muting
    if (squelchEnabled && scanning) {
        sigpath::sinkManager.setAllMuted(!signalDetected);
    }

    if (signalDetected) {
        if (carrierHoldMode) {
            // Keep tracking level while "locked"
            if (!signalHistory.empty()) {
                float avgSignalLevel = signalHistorySum / signalHistory.size();
                if (avgSignalLevel > (noiseFloor + signalMarginDb)) {
                    timeSinceLastSwitch = 0.0f; // Carrier still present, reset hold
                }
            }
        }

        // If we've detected a signal and time is up, continue scanning
        if (timeSinceLastSwitch >= listenTimeSec * 1000.0f) {
            nextStation();
        }
        return;
    }

    // If we're scanning and time is up, check signal level
    if (timeSinceLastSwitch >= scanIntervalMs) {
        if (signalLevelCount > 0) {
            float avgSignalLevel = signalLevelSum / signalLevelCount;
            if (avgSignalLevel > (noiseFloor + signalMarginDb)) {
                // Signal detected, enter listening mode
                signalDetected = true;
                timeSinceLastSwitch = 0.0f;
                return;
            }
        }
        
        // No signal detected, move to next station
        nextStation();
    }
}

void Scanner::nextStation() {
    if (!scanning || bookmarks.empty()) return;

    // Reset signal accumulation for new station
    signalLevelSum = 0.0f;
    signalLevelCount = 0;
    signalDetected = false;
    timeSinceLastSwitch = 0.0f;

    // Move to next station
    size_t newIndex = (currentStationIndex + 1) % bookmarks.size();
    if (newIndex == currentStationIndex) return; // No change needed

    currentStationIndex = newIndex;
    currentStation = bookmarks[currentStationIndex];
    currentBookmark = bookmarksMap[currentStation];
    applyBookmark(currentBookmark, gui::waterfall.selectedVFO);
    flog::info("Scanner moved to next station: {}", currentStation);
}
void Scanner::onPlayStateChange(bool playing) {
    if (!playing && scanning) {
        stop();
    }
}
