// Dawson CW Decoder - SDR++ Module
// Main module implementation with waterfall overlay

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <config.h>
#include <core.h>
#include <gui/style.h>
#include <gui/gui.h>
#include <gui/widgets/waterfall.h>
#include <signal_path/signal_path.h>
#include <module.h>
#include <dsp/stream.h>
#include <dsp/types.h>
#include <dsp/sink/handler_sink.h>
#include <dsp/channel/rx_vfo.h>
#include <dsp/demod/am.h>
#include <thread>
#include <atomic>
#include <cmath>
#include <limits>

#include "cw_iq_dsp_engine.h"

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "dawson_cw_decoder",
    /* Description:     */ "Dawson HamFist CW Decoder - Multi-channel CW with autocorrect",
    /* Author:          */ "Jonathan P Dawson (ported to SDR++)",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ -1
};

ConfigManager config;

// CW band segments
struct CWBandSegment {
    double start_hz;
    double end_hz;
    const char* name;
};

static const std::vector<CWBandSegment> CW_BANDS = {
    {3500000,  3570000,  "80m CW"},
    {7000000,  7060000,  "40m CW"},
    {10100000, 10150000, "30m CW"},
    {14000000, 14060000, "20m CW"},
    {18068000, 18110000, "17m CW"},
    {21000000, 21060000, "15m CW"},
    {24890000, 24950000, "12m CW"},
    {28000000, 28060000, "10m CW"},
    {50000000, 50100000, "6m CW"},
};

// Find the best CW band visible within the given frequency range.
// Prefers bands fully inside the view, closest to center.
// Falls back to partially visible bands if none are fully visible.
static const CWBandSegment* findBestCWBand(double center_freq, double bandwidth) {
    double view_low = center_freq - bandwidth / 2.0;
    double view_high = center_freq + bandwidth / 2.0;
    
    const CWBandSegment* best = nullptr;
    double best_dist = std::numeric_limits<double>::max();
    bool best_fully_visible = false;
    
    for (const auto& band : CW_BANDS) {
        // Check if band overlaps view
        if (band.start_hz >= view_high || band.end_hz <= view_low) continue;
        
        bool fully = band.start_hz >= view_low && band.end_hz <= view_high;
        double band_center = (band.start_hz + band.end_hz) / 2.0;
        double dist = std::abs(band_center - center_freq);
        
        // Prefer: fully visible > partially visible, then closest to center
        bool better = false;
        if (!best) {
            better = true;
        } else if (fully && !best_fully_visible) {
            better = true;
        } else if (!fully && best_fully_visible) {
            better = false;
        } else if (dist < best_dist) {
            better = true;
        }
        
        if (better) {
            best = &band;
            best_dist = dist;
            best_fully_visible = fully;
        }
    }
    return best;
}

static bool isInCWBand(double center_freq, double bandwidth) {
    return findBestCWBand(center_freq, bandwidth) != nullptr;
}

static const char* getCurrentCWBandName(double center_freq, double bandwidth) {
    auto* band = findBestCWBand(center_freq, bandwidth);
    return band ? band->name : nullptr;
}

static bool getCWBandRange(double center_freq, double bandwidth, double& start_hz, double& end_hz) {
    auto* band = findBestCWBand(center_freq, bandwidth);
    if (band) {
        start_hz = band->start_hz;
        end_hz = band->end_hz;
        return true;
    }
    return false;
}

// Module class
class DawsonCWDecoderModule : public ModuleManager::Instance {
public:
    DawsonCWDecoderModule(std::string name);
    ~DawsonCWDecoderModule();
    
    void postInit() override;
    void enable() override;
    void disable() override;
    bool isEnabled() override;
    std::string handleDebugCommand(const std::string& cmd, const std::string& args) override;
    
private:
    static void menuHandler(void* ctx);
    static void waterfallDrawHandler(ImGui::WaterFall::WaterfallDrawArgs args, void* ctx);
    
    void drawMenu();
    void drawOverlay(const ImGui::WaterFall::WaterfallDrawArgs& args);
    void updateFrequencyLock();
    void updateBindings();
    
    std::string name;
    bool enabled = false;
    
    // IQ DSP Engine - acts as a preprocessor (pass-through with monitoring)
    dawson_cw::IQCWDSPEngine iq_dsp_engine;
    dawson_cw::IQConfig iq_config;
    
    // Sample rate
    double sample_rate = 48000.0;
    
    // UI State
    bool auto_enable_in_cw_band = true;
    char status_text[128] = "Disabled";
    
    // Event handlers
    EventHandler<ImGui::WaterFall::WaterfallDrawArgs> waterfall_draw_handler;
    
    // Frequency tracking
    double current_center_freq = 0;
    bool on_cw_frequency = false;
    
    // Spectrum buffer for waterfall overlay (max FFT size)
    static constexpr int MAX_SPECTRUM_BINS = 16384;
    float spectrum_buffer[MAX_SPECTRUM_BINS];
};

DawsonCWDecoderModule::DawsonCWDecoderModule(std::string name)
    : name(name) {
    
    // Load config
    config.acquire();
    if (!config.conf.contains(name)) {
        config.conf[name] = json::object();
    }
    
    auto& cfg = config.conf[name];
    iq_config.max_channels = cfg.value("maxChannels", 100);
    iq_config.threshold_mult = cfg.value("thresholdMult", 9.0f);
    iq_config.min_snr_db = cfg.value("minSnrDb", 12.0f);
    iq_config.timeout_seconds = cfg.value("timeoutSeconds", 30);
    iq_config.show_partial = cfg.value("showPartial", true);
    iq_config.sample_rate = static_cast<float>(sample_rate);
    auto_enable_in_cw_band = cfg.value("autoEnable", true);
    enabled = cfg.value("enabled", false);
    
    iq_dsp_engine.set_config(iq_config);
    config.release();
    
    // Note: Preprocessor is added in updateBindings(), NOT here in constructor
    // This follows the noise_reduction_logmmse pattern
    
    // Register menu
    gui::menu.registerEntry(name, menuHandler, this, static_cast<ModuleManager::Instance*>(this));
    
    // Register waterfall overlay
    gui::waterfall.afterWaterfallDraw.bindHandler(&waterfall_draw_handler);
    waterfall_draw_handler.ctx = this;
    waterfall_draw_handler.handler = [](ImGui::WaterFall::WaterfallDrawArgs args, void* ctx) {
        ((DawsonCWDecoderModule*)ctx)->drawOverlay(args);
    };
}

DawsonCWDecoderModule::~DawsonCWDecoderModule() {
    disable();
    gui::menu.removeEntry(name);
    gui::waterfall.afterWaterfallDraw.unbindHandler(&waterfall_draw_handler);
}

void DawsonCWDecoderModule::postInit() {
    // Update frequency tracking and CW band limits
    updateFrequencyLock();
    
    // If auto-enable is off but we were previously enabled, restore state
    if (enabled && !auto_enable_in_cw_band) {
        enable();
    }
}

void DawsonCWDecoderModule::enable() {
    if (enabled) return;
    
    flog::info("Dawson CW Decoder: enabling...");
    
    // Get current sample rate from front-end (with fallback)
    try {
        sample_rate = sigpath::iqFrontEnd.getSampleRate();
    } catch (...) {
        sample_rate = 0;
    }
    
    if (sample_rate <= 0 || sample_rate > 10000000) {  // Sanity check
        sample_rate = 48000.0;
    }
    
    // Update config with actual sample rate
    iq_config.sample_rate = static_cast<float>(sample_rate);
    iq_dsp_engine.set_config(iq_config);
    
    // Set initial center frequency
    try {
        current_center_freq = gui::waterfall.getCenterFrequency();
    } catch (...) {
        current_center_freq = 0;
    }
    iq_dsp_engine.set_center_frequency(current_center_freq);
    
    enabled = true;
    printf("[CWDBG] Calling updateBindings...\n"); fflush(stdout);
    updateBindings();
    printf("[CWDBG] updateBindings done\n"); fflush(stdout);
    flog::info("Dawson CW Decoder enabled");
    
    printf("[CWDBG] About to get channel count...\n"); fflush(stdout);
    auto ch_count = iq_dsp_engine.get_active_channel_count();
    printf("[CWDBG] Got channel count: %zu\n", ch_count); fflush(stdout);
    snprintf(status_text, sizeof(status_text), "Active - %zu channels", ch_count);
    
    // Save enabled state
    config.acquire();
    config.conf[name]["enabled"] = enabled;
    config.release(true);
    printf("[CWDBG] enable() complete\n"); fflush(stdout);
}

void DawsonCWDecoderModule::disable() {
    if (!enabled) return;
    
    flog::info("Dawson CW Decoder: disabling...");
    enabled = false;
    updateBindings();
    iq_dsp_engine.clear_all_channels();
    
    snprintf(status_text, sizeof(status_text), "Disabled");
    flog::info("Dawson CW Decoder disabled");
    
    // Save enabled state
    config.acquire();
    config.conf[name]["enabled"] = enabled;
    config.release(true);
}

void DawsonCWDecoderModule::updateBindings() {
    if (enabled && !iq_dsp_engine.is_enabled()) {
        flog::info("Dawson CW Decoder: adding preprocessor...");
        printf("[CWDBG] Adding preprocessor\n"); fflush(stdout);
        sigpath::iqFrontEnd.addPreprocessor(&iq_dsp_engine, false);
        printf("[CWDBG] Preprocessor added, enabling...\n"); fflush(stdout);
        iq_dsp_engine.set_enabled(true);
        sigpath::iqFrontEnd.togglePreprocessor(&iq_dsp_engine, true);
        printf("[CWDBG] Preprocessor enabled\n"); fflush(stdout);
        flog::info("Dawson CW Decoder: preprocessor added and enabled");
    }
    else if (iq_dsp_engine.is_enabled()) {
        flog::info("Dawson CW Decoder: removing preprocessor...");
        printf("[CWDBG] Removing preprocessor\n");
        iq_dsp_engine.set_enabled(false);
        sigpath::iqFrontEnd.togglePreprocessor(&iq_dsp_engine, false);
        sigpath::iqFrontEnd.removePreprocessor(&iq_dsp_engine);
        printf("[CWDBG] Preprocessor removed\n");
        flog::info("Dawson CW Decoder: preprocessor removed");
    }
}

bool DawsonCWDecoderModule::isEnabled() {
    return enabled;
}

std::string DawsonCWDecoderModule::handleDebugCommand(const std::string& cmd, const std::string& args) {
    flog::info("Dawson CW Decoder: handleDebugCommand called - cmd='{}', args='{}'", cmd, args);
    
    json response;
    
    if (cmd == "get_status") {
        response["enabled"] = enabled;
        response["active_channels"] = (int)iq_dsp_engine.get_active_channel_count();
        response["on_cw_frequency"] = on_cw_frequency;
        response["status_text"] = status_text;
        
        // Skip channel details to avoid thread safety issues
        response["channels"] = json::array();
        
    } else if (cmd == "set_max_channels") {
        if (!args.empty()) {
            iq_config.max_channels = std::stoi(args);
            iq_dsp_engine.set_config(iq_config);
            response["status"] = "ok";
            response["max_channels"] = iq_config.max_channels;
        } else {
            response["status"] = "error";
            response["error"] = "missing argument";
        }
        
    } else if (cmd == "set_threshold") {
        if (!args.empty()) {
            iq_config.threshold_mult = std::stof(args);
            iq_dsp_engine.set_config(iq_config);
            response["status"] = "ok";
            response["threshold"] = iq_config.threshold_mult;
        } else {
            response["status"] = "error";
            response["error"] = "missing argument";
        }
        
    } else if (cmd == "enable") {
        if (!enabled) enable();
        response["status"] = "ok";
        response["enabled"] = enabled;
        
    } else if (cmd == "disable") {
        if (enabled) disable();
        response["status"] = "ok";
        response["enabled"] = enabled;
        
    } else if (cmd == "reset") {
        iq_dsp_engine.reset();
        response["status"] = "ok";
        
    } else if (cmd == "get_config") {
        response["max_channels"] = iq_config.max_channels;
        response["threshold_mult"] = iq_config.threshold_mult;
        response["min_snr_db"] = iq_config.min_snr_db;
        response["timeout_seconds"] = iq_config.timeout_seconds;
        response["show_partial"] = iq_config.show_partial;
        
    } else if (cmd == "get_samples") {
        uint64_t samples = iq_dsp_engine.get_total_samples_processed();
        printf("[CWDBG] get_samples: total_samples=%llu\n", (unsigned long long)samples);
        response["total_samples"] = samples;
        response["status"] = "ok";
        
    } else {
        response["status"] = "error";
        response["error"] = "unknown command";
    }
    
    return response.dump();
}

void DawsonCWDecoderModule::updateFrequencyLock() {
    double center = gui::waterfall.getCenterFrequency();
    double bw = gui::waterfall.getBandwidth();
    
    current_center_freq = center;
    on_cw_frequency = isInCWBand(center, bw);
    
    // Set CW band limits for peak filtering
    double band_start = 0, band_end = 0;
    if (getCWBandRange(center, bw, band_start, band_end)) {
        iq_dsp_engine.set_cw_band(static_cast<float>(band_start), static_cast<float>(band_end));
    } else {
        // Not in a known CW band - use the whole viewable range
        iq_dsp_engine.set_cw_band(
            static_cast<float>(center - bw / 2.0),
            static_cast<float>(center + bw / 2.0)
        );
    }
    
    // Update center frequency for absolute freq calculation
    iq_dsp_engine.set_center_frequency(center);
    
    if (on_cw_frequency) {
        auto band_name = getCurrentCWBandName(center, bw);
        if (band_name && auto_enable_in_cw_band && !enabled) {
            enable();
        }
    }
}

void DawsonCWDecoderModule::menuHandler(void* ctx) {
    ((DawsonCWDecoderModule*)ctx)->drawMenu();
}

void DawsonCWDecoderModule::drawMenu() {
    float menuWidth = ImGui::GetContentRegionAvail().x;
    
    // Status
    ImGui::Text("Status: %s", status_text);
    
    if (on_cw_frequency) {
        double bw = 0;
        try { bw = gui::waterfall.getBandwidth(); } catch (...) { bw = 0; }
        auto band_name = getCurrentCWBandName(current_center_freq, bw);
        if (band_name) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "[%s]", band_name);
        }
    }
    
    ImGui::Separator();
    
    // Enable/Disable
    bool was_enabled = enabled;
    if (ImGui::Checkbox(CONCAT("Enable Decoder##", name), &enabled)) {
        if (enabled && !was_enabled) {
            enable();
        } else if (!enabled && was_enabled) {
            disable();
        }
    }
    
    if (!enabled) {
        style::beginDisabled();
    }
    
    // Auto-enable in CW bands
    if (ImGui::Checkbox(CONCAT("Auto-enable in CW bands##", name), &auto_enable_in_cw_band)) {
        config.acquire();
        config.conf[name]["autoEnable"] = auto_enable_in_cw_band;
        config.release(true);
    }
    
    ImGui::Separator();
    
    // Configuration
    bool config_changed = false;
    
    // Max channels
    ImGui::LeftLabel("Max Channels");
    ImGui::FillWidth();
    if (ImGui::SliderInt(CONCAT("##max_channels_", name), &iq_config.max_channels, 1, 100)) {
        config_changed = true;
    }
    
    // Threshold
    ImGui::LeftLabel("Threshold");
    ImGui::FillWidth();
    if (ImGui::SliderFloat(CONCAT("##threshold_", name), &iq_config.threshold_mult, 1.0f, 20.0f, "%.1f")) {
        config_changed = true;
    }
    
    // Min SNR
    ImGui::LeftLabel("Min SNR (dB)");
    ImGui::FillWidth();
    if (ImGui::SliderFloat(CONCAT("##min_snr_", name), &iq_config.min_snr_db, 6.0f, 20.0f, "%.0f")) {
        config_changed = true;
    }
    
    // WPM range
    ImGui::LeftLabel("WPM Range");
    ImGui::FillWidth();
    if (ImGui::DragIntRange2(CONCAT("##wpm_range_", name), 
                              &iq_config.min_wpm, &iq_config.max_wpm, 
                              1, 5, 60, "Min: %d", "Max: %d")) {
        config_changed = true;
    }
    
    // Timeout
    ImGui::LeftLabel("Timeout (sec)");
    ImGui::FillWidth();
    if (ImGui::SliderInt(CONCAT("##timeout_", name), &iq_config.timeout_seconds, 5, 60)) {
        config_changed = true;
    }
    
    // Show partial
    if (ImGui::Checkbox(CONCAT("Show partial decodes##", name), &iq_config.show_partial)) {
        config_changed = true;
    }
    
    if (config_changed) {
        iq_dsp_engine.set_config(iq_config);
        
        config.acquire();
        config.conf[name]["maxChannels"] = iq_config.max_channels;
        config.conf[name]["thresholdMult"] = iq_config.threshold_mult;
        config.conf[name]["minSNR"] = iq_config.min_snr_db;
        config.conf[name]["minWPM"] = iq_config.min_wpm;
        config.conf[name]["maxWPM"] = iq_config.max_wpm;
        config.conf[name]["timeout"] = iq_config.timeout_seconds;
        config.conf[name]["showPartial"] = iq_config.show_partial;
        config.release(true);
    }
    
    ImGui::Separator();
    
    // Active channels info
    ImGui::Text("Active channels: %zu", iq_dsp_engine.get_active_channel_count());
    ImGui::Text("Samples processed: %llu", (unsigned long long)iq_dsp_engine.get_total_samples_processed());
    
    if (!enabled) {
        style::endDisabled();
    }
    
    // Reset button
    if (ImGui::Button(CONCAT("Reset##reset_", name), ImVec2(menuWidth, 0))) {
        iq_dsp_engine.reset();
    }
}

// IQ processing is now handled by the preprocessor's run() method

void DawsonCWDecoderModule::drawOverlay(const ImGui::WaterFall::WaterfallDrawArgs& args) {
    if (!enabled) return;
    
    // Get active channels from IQ engine
    const auto& channels = iq_dsp_engine.get_channels();
    if (channels.empty()) return;
    
    // Get waterfall dimensions
    float wf_width = args.wfMax.x - args.wfMin.x;
    float wf_height = args.wfMax.y - args.wfMin.y;
    
    // Center frequency
    double center_freq = gui::waterfall.getCenterFrequency();
    
    // Create a sorted list of channels by frequency
    std::vector<const dawson_cw::IQChannelState*> sorted_channels;
    for (const auto& channel : channels) {
        if (channel->is_active) {
            sorted_channels.push_back(channel.get());
        }
    }
    std::sort(sorted_channels.begin(), sorted_channels.end(),
              [](const dawson_cw::IQChannelState* a, const dawson_cw::IQChannelState* b) {
                  return a->center_freq_hz < b->center_freq_hz;
              });
    
    // Draw channel list on the left side of waterfall
    float list_x = args.wfMin.x + 10;
    float list_y = args.wfMin.y + 10;
    float line_height = 14;
    int max_lines = static_cast<int>((wf_height - 20) / line_height);
    int display_count = std::min(static_cast<int>(sorted_channels.size()), max_lines);
    
    ImGui::PushFont(style::baseFont);
    
    // Draw background panel
    float panel_width = 280;
    float panel_height = display_count * line_height + 20;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(
        ImVec2(list_x - 5, list_y - 5),
        ImVec2(list_x + panel_width, list_y + panel_height),
        IM_COL32(0, 0, 0, 180)
    );
    
    // Draw header
    char header[64];
    snprintf(header, sizeof(header), "CW Channels: %zu", sorted_channels.size());
    draw_list->AddText(ImVec2(list_x, list_y), IM_COL32(255, 255, 255, 255), header);
    list_y += line_height + 2;
    
    // Draw each channel in sorted order
    for (int i = 0; i < display_count; i++) {
        const auto* channel = sorted_channels[i];
        
        // Get channel data
        float freq_abs = channel->center_freq_hz + center_freq;
        float snr = channel->snr;
        
        // Get text (thread-safe) - use const_cast for mutex
        std::string text;
        {
            std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(channel->text_mutex));
            text = channel->decoded_text;
        }
        
        // Truncate text to fit
        if (text.length() > 25) {
            text = text.substr(0, 22) + "...";
        }
        if (text.empty()) text = "...";
        
        // Color based on SNR (green = strong, yellow = medium, red = weak)
        ImU32 color;
        if (snr > 20) color = IM_COL32(100, 255, 100, 255);
        else if (snr > 12) color = IM_COL32(255, 255, 100, 255);
        else color = IM_COL32(255, 150, 100, 255);
        
        // Format: "14032.33 18dB [decoded text]"
        char line[128];
        snprintf(line, sizeof(line), "%.2f %4.0fdB %s", 
                 freq_abs / 1000.0, snr, text.c_str());
        
        draw_list->AddText(ImVec2(list_x, list_y), color, line);
        list_y += line_height;
    }
    
    ImGui::PopFont();
}

// Module entry points
MOD_EXPORT void _INIT_() {
    json def = json::object();
    config.setPath(std::string(core::getRoot()) + "/dawson_cw_decoder_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return static_cast<ModuleManager::Instance*>(new DawsonCWDecoderModule(name));
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (DawsonCWDecoderModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
