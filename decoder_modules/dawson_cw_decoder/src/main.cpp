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

#include "cw_dsp_engine.h"

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

static bool isInCWBand(double center_freq, double bandwidth) {
    double view_low = center_freq - bandwidth / 2.0;
    double view_high = center_freq + bandwidth / 2.0;
    
    for (const auto& band : CW_BANDS) {
        if (view_high >= band.start_hz && view_low <= band.end_hz) {
            return true;
        }
    }
    return false;
}

static const char* getCurrentCWBandName(double center_freq) {
    for (const auto& band : CW_BANDS) {
        if (center_freq >= band.start_hz && center_freq <= band.end_hz) {
            return band.name;
        }
    }
    return nullptr;
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
    
private:
    static void menuHandler(void* ctx);
    static void waterfallDrawHandler(ImGui::WaterFall::WaterfallDrawArgs args, void* ctx);
    static void sampleRateChangeHandler(float sampleRate, void* ctx);
    static void audioHandler(dsp::stereo_t* data, int count, void* ctx);
    
    void drawMenu();
    void drawOverlay(const ImGui::WaterFall::WaterfallDrawArgs& args);
    void processAudio();
    void updateFrequencyLock();
    
    std::string name;
    bool enabled = false;
    
    // DSP Engine
    std::unique_ptr<dawson_cw::CWDSPEngine> dsp_engine;
    
    // Audio chain
    dsp::stream<dsp::stereo_t> audio_stream;
    dsp::sink::Handler<dsp::stereo_t> audio_handler;
    dawson_cw::Config decoder_config;
    
    // VFO for audio extraction
    VFOManager::VFO* vfo = nullptr;
    dsp::channel::RxVFO* rx_vfo = nullptr;
    dsp::chain<dsp::complex_t> if_chain;
    
    // Sample rate handling
    double audio_sample_rate = 48000.0;
    EventHandler<float> sr_change_handler;
    
    // UI State
    bool auto_enable_in_cw_band = true;
    char status_text[128] = "Disabled";
    
    // Event handlers
    EventHandler<ImGui::WaterFall::WaterfallDrawArgs> waterfall_draw_handler;
    
    // Frequency tracking
    double current_center_freq = 0;
    bool on_cw_frequency = false;
};

DawsonCWDecoderModule::DawsonCWDecoderModule(std::string name) 
    : name(name) {
    
    dsp_engine = std::make_unique<dawson_cw::CWDSPEngine>();
    
    // Load config
    config.acquire();
    if (!config.conf.contains(name)) {
        config.conf[name] = json::object();
    }
    
    auto& cfg = config.conf[name];
    decoder_config.max_channels = cfg.value("maxChannels", 20);
    decoder_config.threshold_mult = cfg.value("thresholdMult", 9.0f);
    decoder_config.min_snr_db = cfg.value("minSNR", 12.0f);
    decoder_config.min_wpm = cfg.value("minWPM", 10);
    decoder_config.max_wpm = cfg.value("maxWPM", 40);
    decoder_config.timeout_seconds = cfg.value("timeout", 30);
    decoder_config.show_partial = cfg.value("showPartial", true);
    auto_enable_in_cw_band = cfg.value("autoEnable", true);
    
    config.release(true);
    
    dsp_engine->set_config(decoder_config);
    
    // Initialize audio handler
    audio_handler.init(&audio_stream, audioHandler, this);
    
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
    // Check if we're on a CW frequency
    updateFrequencyLock();
}

void DawsonCWDecoderModule::enable() {
    if (enabled) return;
    
    double bw = gui::waterfall.getBandwidth();
    
    // Create VFO for audio extraction
    vfo = sigpath::vfoManager.createVFO(
        name, 
        ImGui::WaterfallVFO::REF_CENTER, 
        0, 
        12000,  // 12kHz bandwidth
        48000,  // Sample rate
        12000, 
        12000, 
        true
    );
    
    // Setup audio chain
    audio_stream.setBufferSize(48000);  // 1 second buffer
    
    // Start audio handler
    audio_handler.start();
    
    enabled = true;
    snprintf(status_text, sizeof(status_text), "Active - %d channels", 
             static_cast<int>(dsp_engine->get_active_channel_count()));
    
    flog::info("Dawson CW Decoder enabled");
}

void DawsonCWDecoderModule::disable() {
    if (!enabled) return;
    
    audio_handler.stop();
    
    if (vfo) {
        sigpath::vfoManager.deleteVFO(vfo);
        vfo = nullptr;
    }
    
    dsp_engine->clear_all_channels();
    
    enabled = false;
    snprintf(status_text, sizeof(status_text), "Disabled");
    
    flog::info("Dawson CW Decoder disabled");
}

bool DawsonCWDecoderModule::isEnabled() {
    return enabled;
}

void DawsonCWDecoderModule::updateFrequencyLock() {
    double center = gui::waterfall.getCenterFrequency();
    double bw = gui::waterfall.getBandwidth();
    
    current_center_freq = center;
    on_cw_frequency = isInCWBand(center, bw);
    
    if (on_cw_frequency) {
        auto band_name = getCurrentCWBandName(center);
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
        auto band_name = getCurrentCWBandName(current_center_freq);
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
    if (ImGui::SliderInt(CONCAT("##max_channels_", name), &decoder_config.max_channels, 1, 100)) {
        config_changed = true;
    }
    
    // Threshold
    ImGui::LeftLabel("Threshold");
    ImGui::FillWidth();
    if (ImGui::SliderFloat(CONCAT("##threshold_", name), &decoder_config.threshold_mult, 1.0f, 20.0f, "%.1f")) {
        config_changed = true;
    }
    
    // Min SNR
    ImGui::LeftLabel("Min SNR (dB)");
    ImGui::FillWidth();
    if (ImGui::SliderFloat(CONCAT("##min_snr_", name), &decoder_config.min_snr_db, 6.0f, 20.0f, "%.0f")) {
        config_changed = true;
    }
    
    // WPM range
    ImGui::LeftLabel("WPM Range");
    ImGui::FillWidth();
    if (ImGui::DragIntRange2(CONCAT("##wpm_range_", name), 
                              &decoder_config.min_wpm, &decoder_config.max_wpm, 
                              1, 5, 60, "Min: %d", "Max: %d")) {
        config_changed = true;
    }
    
    // Timeout
    ImGui::LeftLabel("Timeout (sec)");
    ImGui::FillWidth();
    if (ImGui::SliderInt(CONCAT("##timeout_", name), &decoder_config.timeout_seconds, 5, 60)) {
        config_changed = true;
    }
    
    // Show partial
    if (ImGui::Checkbox(CONCAT("Show partial decodes##", name), &decoder_config.show_partial)) {
        config_changed = true;
    }
    
    if (config_changed) {
        dsp_engine->set_config(decoder_config);
        
        config.acquire();
        config.conf[name]["maxChannels"] = decoder_config.max_channels;
        config.conf[name]["thresholdMult"] = decoder_config.threshold_mult;
        config.conf[name]["minSNR"] = decoder_config.min_snr_db;
        config.conf[name]["minWPM"] = decoder_config.min_wpm;
        config.conf[name]["maxWPM"] = decoder_config.max_wpm;
        config.conf[name]["timeout"] = decoder_config.timeout_seconds;
        config.conf[name]["showPartial"] = decoder_config.show_partial;
        config.release(true);
    }
    
    ImGui::Separator();
    
    // Active channels info
    ImGui::Text("Active channels: %zu", dsp_engine->get_active_channel_count());
    
    if (!enabled) {
        style::endDisabled();
    }
    
    // Reset button
    if (ImGui::Button(CONCAT("Reset##reset_", name), ImVec2(menuWidth, 0))) {
        dsp_engine->reset();
    }
}

void DawsonCWDecoderModule::audioHandler(dsp::stereo_t* data, int count, void* ctx) {
    auto* module = (DawsonCWDecoderModule*)ctx;
    
    // Convert stereo to mono int16 for the DSP engine
    // Take left channel and convert to 16-bit
    for (int i = 0; i < count; i++) {
        int16_t sample = static_cast<int16_t>(data[i].l * 32767.0f);
        module->dsp_engine->process_audio_sample(sample);
    }
}

void DawsonCWDecoderModule::sampleRateChangeHandler(float sampleRate, void* ctx) {
    auto* module = (DawsonCWDecoderModule*)ctx;
    module->audio_sample_rate = sampleRate;
}

void DawsonCWDecoderModule::drawOverlay(const ImGui::WaterFall::WaterfallDrawArgs& args) {
    if (!enabled) return;
    
    // Get active channels
    const auto& channels = dsp_engine->get_channels();
    if (channels.empty()) return;
    
    // Get waterfall dimensions
    float wf_width = args.wfMax.x - args.wfMin.x;
    float wf_height = args.wfMax.y - args.wfMin.y;
    
    // Center frequency and bandwidth
    double center_freq = gui::waterfall.getCenterFrequency();
    double bandwidth = gui::waterfall.getViewBandwidth();
    double low_freq = center_freq - bandwidth / 2.0;
    double high_freq = center_freq + bandwidth / 2.0;
    double freq_to_pixel = wf_width / bandwidth;
    
    // Current time for animation
    float time = ImGui::GetTime();
    
    ImGui::PushFont(style::baseFont);
    
    // Draw each channel
    for (const auto& channel : channels) {
        if (!channel->is_active) continue;
        
        // Get channel data
        float freq = channel->center_freq_hz + center_freq;  // Add VFO offset
        float snr = channel->snr;
        float wpm = channel->wpm;
        
        // Get text (thread-safe)
        std::string text;
        {
            std::lock_guard<std::mutex> lock(channel->text_mutex);
            text = channel->decoded_text;
        }
        
        if (text.empty() && !decoder_config.show_partial) continue;
        
        // Calculate X position on waterfall
        double freq_offset = freq - low_freq;
        float x_pos = static_cast<float>(freq_offset * freq_to_pixel);
        
        // Clamp to visible area
        if (x_pos < 0 || x_pos > wf_width) continue;
        
        // Format display: "14032.33 [text]"
        char display[128];
        char freq_str[16];
        snprintf(freq_str, sizeof(freq_str), "%.2f", freq / 1000.0);
        
        // Truncate or scroll text
        std::string display_text = text;
        const size_t max_display = 40;
        
        if (display_text.length() > max_display) {
            // Scrolling animation
            int scroll_pos = static_cast<int>(time * 2) % (static_cast<int>(display_text.length()) - max_display + 10);
            if (scroll_pos > static_cast<int>(display_text.length()) - max_display) {
                scroll_pos = 0;
            }
            display_text = display_text.substr(scroll_pos, max_display);
        }
        
        snprintf(display, sizeof(display), "%s %s", freq_str, display_text.c_str());
        
        // Calculate text size
        ImVec2 text_size = ImGui::CalcTextSize(display);
        
        // Y position - stack channels vertically, newer at bottom
        // Use channel ID to determine vertical offset
        float y_offset = 20.0f + (channel->id % 10) * (text_size.y + 2.0f);
        float y_pos = args.wfMin.y + y_offset;
        
        // Clamp Y to visible area
        if (y_pos + text_size.y > args.wfMax.y) {
            y_pos = args.wfMax.y - text_size.y - 5.0f;
        }
        
        ImVec2 pos = ImVec2(args.wfMin.x + x_pos - text_size.x / 2.0f, y_pos);
        
        // Ensure text stays within waterfall bounds horizontally
        if (pos.x < args.wfMin.x) pos.x = args.wfMin.x + 2;
        if (pos.x + text_size.x > args.wfMax.x) pos.x = args.wfMax.x - text_size.x - 2;
        
        // Colors based on SNR
        ImU32 text_color;
        if (snr > 20.0f) {
            text_color = IM_COL32(0, 255, 0, 255);  // Green (strong)
        } else if (snr > 15.0f) {
            text_color = IM_COL32(255, 255, 0, 255);  // Yellow (medium)
        } else {
            text_color = IM_COL32(255, 100, 100, 255);  // Red (weak)
        }
        
        ImU32 black = IM_COL32(0, 0, 0, 255);
        ImU32 bg_color = IM_COL32(0, 0, 0, 160);
        
        // Background
        args.window->DrawList->AddRectFilled(
            pos - ImVec2(2, 1),
            pos + text_size + ImVec2(2, 1),
            bg_color
        );
        
        // Text outline
        args.window->DrawList->AddText(pos + ImVec2(-1, -1), black, display);
        args.window->DrawList->AddText(pos + ImVec2(-1, 1), black, display);
        args.window->DrawList->AddText(pos + ImVec2(1, -1), black, display);
        args.window->DrawList->AddText(pos + ImVec2(1, 1), black, display);
        
        // Main text
        args.window->DrawList->AddText(pos, text_color, display);
        
        // SNR bar (small indicator below text)
        float bar_width = text_size.x;
        float bar_height = 3.0f;
        float snr_ratio = std::min(snr / 30.0f, 1.0f);
        
        ImVec2 bar_pos = pos + ImVec2(0, text_size.y + 1);
        args.window->DrawList->AddRectFilled(
            bar_pos,
            bar_pos + ImVec2(bar_width, bar_height),
            IM_COL32(50, 50, 50, 200)
        );
        args.window->DrawList->AddRectFilled(
            bar_pos,
            bar_pos + ImVec2(bar_width * snr_ratio, bar_height),
            text_color
        );
    }
    
    ImGui::PopFont();
    
    // Update status text periodically
    static int frame_count = 0;
    if (++frame_count % 60 == 0) {
        snprintf(status_text, sizeof(status_text), "Active - %zu channels",
                 dsp_engine->get_active_channel_count());
    }
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
