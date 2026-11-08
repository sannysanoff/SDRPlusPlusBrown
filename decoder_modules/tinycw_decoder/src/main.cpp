// SPDX-License-Identifier: MIT
// Copyright (C) 2026 DeepFist integration for SDR++ Brown
//
// TinyCW neural CW decoder module.
//
// Decodes every keyed CW signal inside the CW segment of the band currently in
// view, not just the tuned VFO: binds the wide IQ stream, scans the segment,
// opens one narrow channel per signal and runs the FastCW single-band model.
//
// Decoded text is printed to stdout in 30 s batches (the GUI panel shows only
// the live transmission count and the current search range).

// Windows.h defines min/max as macros; the DSP code below uses std::min/std::max.
#ifndef NOMINMAX
#define NOMINMAX
#endif

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <config.h>
#include <gui/tuner.h>
#include <gui/brown/mobile_main_window.h>
#include <signal_path/vfo_manager.h>
#include <core.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <gui/widgets/waterfall.h>
#include <module.h>
#include <signal_path/signal_path.h>
#include <utils/flog.h>

#include "tinycw_adapt.h"
#include "tinycw_bands.h"
#include "tinycw_engine.h"
#include "tinycw_interface.h"
#include "tinycw_model.h"

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "tinycw_decoder",
    /* Description:     */ "TinyCW neural CW decoder (whole-band)",
    /* Author:          */ "SDR++ Brown + DeepFist",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

ConfigManager config;

namespace {
// Vertical space (px, before style scaling) reserved between the oscilloscope
// and the waterfall for the carrier/decode markers.
constexpr float kOverlayReservePx = 8.0f;
constexpr double kBatchSeconds = 30.0;
constexpr double kMinMaxWidthHz = 2000.0;
constexpr double kDefaultMaxWidthHz = 70000.0;   // widest HF CW segment
constexpr double kUserMaxWidthLimitHz = 80000.0;
} // namespace

class TinyCWDecoderModule : public ModuleManager::Instance, public TinyCWInterface {
public:
    TinyCWDecoderModule(std::string name) : engine_(&model_) {
        this->name = name;

        config.acquire();
        const bool freshSection = !config.conf.contains(name);
        if (freshSection) { config.conf[name] = json({}); }
        json& c = config.conf[name];
        if (c.contains("autoWidth")) { autoWidth_ = c["autoWidth"].get<bool>(); }
        if (c.contains("maxWidthHz")) { maxWidthHz_ = c["maxWidthHz"].get<double>(); }
        if (c.contains("paused")) { paused_ = c["paused"].get<bool>(); }
        if (c.contains("threads")) { threads_ = c["threads"].get<int>(); }
        if (c.contains("provisionalSec")) { provisionalSec_ = c["provisionalSec"].get<double>(); }
        if (c.contains("followVfo")) { followVfo_ = c["followVfo"].get<bool>(); }
        if (c.contains("focusLagSec")) { focusLagSec_ = c["focusLagSec"].get<double>(); }
        if (c.contains("decodesWindow")) { decodesWindow_ = c["decodesWindow"].get<bool>(); }
        if (c.contains("decodesPos") && c["decodesPos"].is_array() && c["decodesPos"].size() == 2) {
            decodesPos_ = ImVec2(c["decodesPos"][0].get<float>(), c["decodesPos"][1].get<float>());
        }
        if (c.contains("decodesSize") && c["decodesSize"].is_array() && c["decodesSize"].size() == 2) {
            decodesSize_ = ImVec2(c["decodesSize"][0].get<float>(), c["decodesSize"][1].get<float>());
        }
        if (c.contains("bandSettings") && c["bandSettings"].is_object()) {
            for (auto it = c["bandSettings"].begin(); it != c["bandSettings"].end(); ++it) {
                if (!it.value().is_object()) { continue; }
                if (!it.value().contains("lo") || !it.value().contains("hi")) { continue; }
                bandSettings_[it.key()] = std::make_pair(it.value()["lo"].get<double>(),
                                                         it.value()["hi"].get<double>());
            }
        }
        config.release(true);

        // A fresh root has no config file, and core::ConfigManager::load() only
        // writes `{}` in that case. Persist this instance's defaults now so the
        // file on disk is complete from the first run, not after the first
        // UI change.
        if (freshSection) { saveConfig(); }

        widthPolicy_.ceilingHz = (maxWidthHz_ > 0.0) ? maxWidthHz_ : kDefaultMaxWidthHz;
        widthPolicy_.autoMode = autoWidth_;
        widthPolicy_.reset(widthPolicy_.ceilingHz, autoWidth_);

        // FastCW weights are embedded: nothing to download.
        loadModel();

        gui::menu.registerEntry(name, menuHandler, this, this);
        waterfallListener_.handler = [](ImGui::WaterFall::WaterfallDrawArgs args, void* ctx) {
            ((TinyCWDecoderModule*)ctx)->drawOverlay(args);
        };
        waterfallListener_.ctx = this;
        gui::waterfall.afterWaterfallDraw.bindHandler(&waterfallListener_);


        // Reserve a 4 px strip at the bottom of the oscilloscope for the
        // carrier/decode markers so they never sit on the spectrum trace.
        reserveListener_.handler = [](int& height, void* ctx) {
            TinyCWDecoderModule* _this = (TinyCWDecoderModule*)ctx;
            if (_this->enabled && _this->model_.ready()) {
                height += (int)std::lround(kOverlayReservePx * style::uiScale);
            }
        };
        reserveListener_.ctx = this;
        gui::waterfall.onReserveOverlayHeight.bindHandler(&reserveListener_);
        gui::waterfall.refreshLayout();

        tickThread_ = std::thread(&TinyCWDecoderModule::tickLoop, this);
        enable();
    }

    ~TinyCWDecoderModule() {
        disable();
        tickRunning_ = false;
        if (tickThread_.joinable()) { tickThread_.join(); }
        gui::waterfall.afterWaterfallDraw.unbindHandler(&waterfallListener_);
        gui::waterfall.onReserveOverlayHeight.unbindHandler(&reserveListener_);
        gui::waterfall.refreshLayout();
        gui::menu.removeEntry(name);
        saveConfig();
    }

    void postInit() override {}

    void enable() override {
        if (enabled) { return; }
        sigpath::iqFrontEnd.bindIQStream(&iqStream_);
        enabled = true;
        gui::waterfall.refreshLayout();
        if (model_.ready()) { startEngine(); }
        else { flog::warn("[tinycw] module enabled but model is not loaded"); }
    }

    void disable() override {
        if (!enabled) { return; }
        stopEngine();
        sigpath::iqFrontEnd.unbindIQStream(&iqStream_);
        enabled = false;
        gui::waterfall.refreshLayout();
    }

    bool isEnabled() override { return enabled; }

    void *getInterface(const char *ifaceName) override {
        if (ifaceName != nullptr && std::string(ifaceName) == "TinyCWInterface") {
            return dynamic_cast<TinyCWInterface*>(this);
        }
        return nullptr;
    }

    bool isReady() override { return model_.ready(); }
    int activeSignals() override { return engine_.activeSignals(); }
    int liveChannels() override { return engine_.liveChannels(); }
    int carrierMarkers() override { return engine_.carrierMarkers(); }
    int decodeMarkers() override { return engine_.decodeMarkers(); }
    int spotCount() override { return engine_.spotCount(); }
    long scanFrameCount() override { return engine_.scanFrameCount(); }
    double busyPercent() override { return engine_.busyPercent(); }

    std::vector<std::string> takeDecodedLines() override {
        std::lock_guard<std::mutex> lck(linesMtx_);
        std::vector<std::string> out = testLines_;
        testLines_.clear();
        return out;
    }

private:
    // ---------------------------------------------------------------- engine
    void startEngine() {
        if (!enabled || !model_.ready() || engine_.running()) { return; }
        const double rate = sigpath::iqFrontEnd.getEffectiveSamplerate();
        if (rate <= 0.0) { return; }
        engine_.setDecodeThreads(threads_);
        engine_.setProvisionalSec(provisionalSec_);
        engine_.setFocusLagSec(focusLagSec_);
        std::string err;
        if (!engine_.start(&iqStream_, rate, err)) {
            flog::error("[tinycw] engine start failed: {}", err);
            return;
        }
        engineRate_ = rate;
        engine_.setPaused(paused_);
        lastBatch_ = std::chrono::steady_clock::now();
    }

    void stopEngine() {
        engine_.stop();
        engineRate_ = 0.0;
    }

    // "Decodes" window: per-round text per channel, sorted by frequency.
    // Window tail kept provisional (re-decoded by the next window), seconds.
    double provisionalSec_ = 3.0;
    double focusLagSec_ = 2.0;
    // Separate decoder that follows the tuned VFO (CW only, batch 1, 1 s).
    bool followVfo_ = true;
    bool decodesWindow_ = false;
    ImVec2 decodesPos_{ 0, 0 };
    ImVec2 decodesSize_{ 520, 320 };
    std::vector<tinycw::Engine::DecodeLine> uiDecodes_;
    double uiDecodesAt_ = 0.0;

    // <root>/tinycw/fastcw.weights overrides the embedded weights.
    std::string fastcwWeightsPath() {
        const std::string p = std::string(core::getRoot()) + "/tinycw/fastcw.weights";
        FILE* f = std::fopen(p.c_str(), "rb");
        if (!f) { return ""; }
        std::fclose(f);
        return p;
    }

    bool loadModel() {
        std::string err;
        if (!model_.load(fastcwWeightsPath(), err)) {
            modelError_ = err;
            flog::error("[tinycw] cannot load model: {}", err);
            return false;
        }
        modelError_.clear();
        flog::info("[tinycw] model loaded: {}", model_.path());
        gui::waterfall.refreshLayout();
        return true;
    }

    // ------------------------------------------------------------ main tick
    void tickLoop() {
        SetThreadName("tinycw_module");
        int tick = 0;
        while (tickRunning_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
            if (!tickRunning_.load()) { break; }

            if (enabled && model_.ready()) {
                // Live thread-count changes from the menu.
                engine_.setDecodeThreads(threads_);
        engine_.setProvisionalSec(provisionalSec_);
        engine_.setFocusLagSec(focusLagSec_);
                // Restart the engine if the source sample rate changed.
                const double rate = sigpath::iqFrontEnd.getEffectiveSamplerate();
                if (engine_.running() && std::fabs(rate - engineRate_) > 1.0) {
                    stopEngine();
                    startEngine();
                }
                else if (!engine_.running()) {
                    startEngine();
                }
                syncRange();
                adaptWidth();
            }

            {
                // Focus decoder: follow the tuned VFO, CW only.
                const bool cw = (gui::mainWindow.getCurrentMode() == "CW");
                if (followVfo_ && cw && enabled) {
                    const double f = vfo0Freq();
                    if (f > 0.0) { engine_.setFocusFreq(f); }
                }
                else {
                    engine_.disableFocus();
                }
            }
            {
                auto lines = engine_.decodeLines(120.0);
                std::lock_guard<std::mutex> lck(uiMtx_);
                uiDecodes_ = std::move(lines);
                uiDecodesAt_ = std::chrono::duration<double>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                static double lastLog = 0.0;
                const double nowS = uiDecodesAt_;
                if (nowS - lastLog >= 5.0) {
                    lastLog = nowS;
                    char m[128];
                    snprintf(m, sizeof m, "ui rows %d, non-empty %d", (int)uiDecodes_.size(),
                             (int)std::count_if(uiDecodes_.begin(), uiDecodes_.end(),
                                                [](const tinycw::Engine::DecodeLine& l) {
                                                    return !l.text.empty();
                                                }));
                    flog::info("[tinycw] {}", m);
                }
            }
            if (++tick % 4 == 0) { maybePrintBatch(); }
        }
    }

    void syncRange() {
        const double center = gui::waterfall.getCenterFrequency();
        const double span = gui::waterfall.getBandwidth();
        if (span <= 0.0) { return; }

        const std::string band = currentBandName(center, span);
        double userLo = 0.0;
        double userHi = 0.0;
        auto it = bandSettings_.find(band);
        if (it != bandSettings_.end()) {
            userLo = it->second.first;
            userHi = it->second.second;
        }

        tinycw::RangeResolution res = tinycw::resolveRange(center, span, widthPolicy_.currentHz, userLo, userHi);
        const tinycw::RangeResolution full = tinycw::resolveRange(center, span, 0.0, userLo, userHi);
        {
            std::lock_guard<std::mutex> lck(uiMtx_);
            range_ = res;
            centerHz_ = center;
            spanHz_ = span;
        }
        if (!res.valid || !full.valid) { return; }
        // The wide channelizer covers the whole CW segment in view; the decode
        // range follows the adaptive width policy and only gates channels.
        engine_.setWideRange(center, full.loHz, full.hiHz);
        engine_.setRange(center, res.loHz, res.hiHz);
        engine_.setMaxChannels(maxChannels());
    }

    static std::string currentBandName(double center, double span) {
        const double lo = center - span * 0.5;
        const double hi = center + span * 0.5;
        for (const tinycw::CwBandDef& b : tinycw::cwBandTable()) {
            if (b.loHz < hi && b.hiHz > lo) { return b.name; }
        }
        return {};
    }

    int maxChannels() const {
        // Rough guard rail: one channel per ~400 Hz of the decoded range.
        const double width = (range_.hiHz > range_.loHz) ? (range_.hiHz - range_.loHz) : 0.0;
        int n = (int)std::lround(width / 250.0);
        return std::clamp(n, 8, 160);
    }

    // Adapt the decoded bandwidth to the measured CPU load, so the slider moves.
    void adaptWidth() {
        widthPolicy_.ceilingHz = (maxWidthHz_ > 0.0) ? maxWidthHz_ : kDefaultMaxWidthHz;
        widthPolicy_.autoMode = autoWidth_;
        widthPolicy_.update(engine_.cpuLoadPercent());
    }

    void maybePrintBatch() {
        auto now = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double>(now - lastBatch_).count();
        if (dt < kBatchSeconds) { return; }
        lastBatch_ = now;

        auto lines = engine_.takeBatch();
        if (enabled && model_.ready()) { printBatch(lines); }
    }

    void printBatch(const std::vector<tinycw::BatchLine>& lines) {
        char ts[32];
        std::time_t t = std::time(nullptr);
        std::tm lt{};
#ifdef _WIN32
        localtime_s(&lt, &t);
#else
        localtime_r(&t, &lt);
#endif
        std::strftime(ts, sizeof ts, "%H:%M:%S", &lt);

        tinycw::RangeResolution r;
        {
            std::lock_guard<std::mutex> lck(uiMtx_);
            r = range_;
        }

        std::printf("===== TinyCW batch %s | band %s | %s | channels %d | spots %d/%ld frames | cpu %.0f%%/%d00%%\n",
                    ts,
                    r.bandName.empty() ? "-" : r.bandName.c_str(),
                    r.valid ? formatRange(r).c_str() : "no CW segment in view",
                    engine_.liveChannels(),
                    engine_.spotCount(),
                    engine_.scanFrameCount(),
                    engine_.busyPercent(), tinycw::Engine::hardwareThreads());
        {
            const auto st = engine_.takePerfStats();
            if (st.passes > 0) {
                std::printf("  perf: front-end %.2f ms/ch | ctc %.3f ms/ch | model %.1f ms/pass "
                            "for %.1f ch (%.3f ms/ch) | detect %.2f ms/tick (%.3f ms/ch)\n",
                            st.feMsPerChannel, st.ctcMsPerChannel, st.inferMsPerPass,
                            st.channelsPerPass, st.inferMsPerChannel,
                            st.detectMsPerTick, st.detectMsPerChannel);
                std::printf("  duty over %.1f s: detect %.1f%% of wall | model thread %.1f%% | "
                            "front-end %.1f%% cpu | passes %.2f/s | cpu total %.1f%% | open %.2f ms/tick\n",
                            st.windowSec, st.detectDutyPct, st.modelDutyPct, st.feCpuPct,
                            st.roundsPerSec, st.decodeDutyPct, st.openMsPerTick);
            }
        }
        for (const auto& line : lines) {
            std::printf("  %8.1f kHz | %s\n", line.freqHz / 1000.0, line.text.c_str());
        }
        std::fflush(stdout);

        // Also feed the standard logger so the decodes show up in the in-app
        // Log Console, not only on stdout.
        {
            char head[192];
            snprintf(head, sizeof head, "batch %s | %s | channels %d | decoded %d",
                     ts, r.valid ? formatRange(r).c_str() : "no CW segment in view",
                     engine_.liveChannels(), (int)lines.size());
            flog::info("[tinycw] {}", head);
            for (const auto& line : lines) {
                char l[512];
                snprintf(l, sizeof l, "%8.1f kHz | %s", line.freqHz / 1000.0, line.text.c_str());
                flog::info("[tinycw] {}", l);
            }
        }

        if (!lines.empty()) {
            std::lock_guard<std::mutex> lck(linesMtx_);
            for (const auto& line : lines) {
                char buf[512];
                snprintf(buf, sizeof buf, "%.1f|%s", line.freqHz / 1000.0, line.text.c_str());
                testLines_.emplace_back(buf);
            }
        }
    }

    static std::string formatRange(const tinycw::RangeResolution& r) {
        char buf[128];
        snprintf(buf, sizeof buf, "%.4f-%.4f MHz", r.loHz / 1e6, r.hiHz / 1e6);
        return buf;
    }

    // ------------------------------------------------------------ markers
    // Bright yellow = keyed CW carrier detected, cyan = real decode. Both are
    // held for EngineConfig::markerHoldSec after the last event.
    // One channel = one X. Two tiny dots stacked at the carrier frequency:
    // yellow on top = a channel was opened (keyed CW present), cyan below = that
    // channel actually produced text. Yellow without cyan is exactly "detected
    // but not decoded".
    void drawMarkerColumn(ImDrawList* dl, float x, float y, const tinycw::Marker& marker) {
        const float s = style::uiScale;
        const float r = 1.6f * s;
        const float dy = 2.5f * s;
        if (marker.carrier) {
            dl->AddCircleFilled(ImVec2(x, y - dy), r, IM_COL32(255, 205, 0, 235));
        }
        if (marker.decode) {
            dl->AddCircleFilled(ImVec2(x, y + dy), r, IM_COL32(0, 240, 255, 235));
        }
    }

    // Draws every active marker on one horizontal band [x0..x1] at baseline y.
    void drawMarkers(ImDrawList* dl, double lowFreq, double freqToPixelRatio,
                     float x0, float x1, float y) {
        if (!enabled || !model_.ready()) { return; }
        const std::vector<tinycw::Marker> markers = engine_.activeMarkers();
        for (const auto& m : markers) {
            const float x = x0 + (float)((m.freqHz - lowFreq) * freqToPixelRatio);
            if (x < x0 || x > x1) { continue; }
            drawMarkerColumn(dl, x, y, m);
        }
    }


    // ------------------------------------------------------------ overlay
    // Frequency of the first VFO in the waterfall, used for tuning and for the
    // highlight in the decodes window.
    static double vfo0Freq() {
        if (gui::waterfall.vfos.empty()) { return 0.0; }
        auto it = gui::waterfall.vfos.begin();
        if (it->second == nullptr) { return 0.0; }
        return gui::waterfall.getCenterFrequency() + it->second->centerOffset;
    }

    // Move VFO0 to an absolute frequency without retuning the source: with a
    // file source the center frequency is fixed, and tuner::tune() would shift
    // the whole view (and the file's center) instead of the VFO.
    static void tuneVfo0(double freqHz) {
        if (gui::waterfall.vfos.empty()) { return; }
        auto it = gui::waterfall.vfos.begin();
        if (it->second == nullptr) { return; }
        const double offset = freqHz - gui::waterfall.getCenterFrequency();
        it->second->setCenterOffset(offset);
        sigpath::vfoManager.setCenterOffset(it->first, offset);
    }

    static std::string vfo0Name() {
        if (gui::waterfall.vfos.empty()) { return std::string(); }
        return gui::waterfall.vfos.begin()->first;
    }

    // Scrolling line under the clock: the newest characters of the channel whose
    // frequency is closest to the VFO. Only while the decoder is enabled and the
    // channel has text.
    // Draws text one byte at a time; brightness follows the vote confidence
    // (conf[i] in 0..1, nullptr = all 1): uncertain characters are darker.
    static float drawConfText(ImDrawList* dl, ImFont* font, float fsz, ImVec2 pos,
                              const std::string& s, const float* conf, ImVec4 base) {
        float x = pos.x;
        char one[2] = { 0, 0 };
        for (size_t i = 0; i < s.size(); i++) {
            one[0] = s[i];
            const float c = conf ? std::clamp(conf[i], 0.0f, 1.0f) : 1.0f;
            const float k = 0.25f + 0.75f * c;
            const ImVec4 col(base.x * k, base.y * k, base.z * k, base.w);
            dl->AddText(font, fsz, ImVec2(x, pos.y), ImGui::GetColorU32(col), one);
            x += font->CalcTextSizeA(fsz, FLT_MAX, 0.0f, one).x;
        }
        return x - pos.x;
    }

    void drawSelectedTicker(const ImGui::WaterFall::WaterfallDrawArgs& args) {
        if (args.window == nullptr) { return; }
        std::vector<tinycw::Engine::DecodeLine> lines;
        {
            std::lock_guard<std::mutex> lck(uiMtx_);
            lines = uiDecodes_;
        }
        if (lines.empty()) { return; }

        std::string text;
        std::vector<float> conf;
        if (engine_.focusActive()) { engine_.focusTextConf(text, conf); }
        if (text.empty()) {
            const double vfoHz = vfo0Freq();
            int nearest = -1;
            double bestDist = 1e18;
            for (size_t i = 0; i < lines.size(); i++) {
                const double d = std::fabs(lines[i].freqHz - vfoHz);
                if (d < bestDist) { bestDist = d; nearest = (int)i; }
            }
            if (nearest < 0) { return; }
            text = lines[(size_t)nearest].text;
            conf = lines[(size_t)nearest].conf;
        }
        if (text.empty()) { return; }
        if (conf.size() != text.size()) { conf.clear(); }
        if (text.size() > 40) {
            const size_t cut = text.size() - 40;
            text = text.substr(cut);
            if (!conf.empty()) { conf.erase(conf.begin(), conf.begin() + (long)cut); }
        }

        const float scale = style::uiScale;
        const float y = args.fftMin.y + 18.0f * scale;
        const float x = args.fftMin.x + 6.0f * scale;
        ImFont* font = ImGui::GetFont();
        const float fsz = ImGui::GetFontSize() * 0.95f;
        const ImVec2 tsz = font->CalcTextSizeA(fsz, FLT_MAX, 0.0f, text.c_str());
        ImDrawList* dl = args.window->DrawList;
        const ImVec2 bgMin(x - 3.0f * scale, y - 2.0f * scale);
        const ImVec2 bgMax(x + tsz.x + 3.0f * scale, y + tsz.y + 2.0f * scale);
        dl->AddRectFilled(bgMin, bgMax, IM_COL32(0, 0, 0, 140), 3.0f * scale);
        drawConfText(dl, font, fsz, ImVec2(x, y), text, conf.empty() ? nullptr : conf.data(),
                     ImVec4(1.0f, 214.0f / 255.0f, 38.0f / 255.0f, 1.0f));
    }

    // One row per channel: text right-aligned next to the frequency column and
    // clipped from the left when the window is narrow. The row closest to the
    // VFO is highlighted; a click tunes the VFO to that frequency.
    void drawDecodesWindow() {
        if (!decodesWindow_) { return; }
        ImGui::SetNextWindowPos(decodesPos_, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(decodesSize_, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("TinyCW decodes", &decodesWindow_)) {
            ImGui::End();
            return;
        }
        decodesPos_ = ImGui::GetWindowPos();
        decodesSize_ = ImGui::GetWindowSize();

        std::vector<tinycw::Engine::DecodeLine> lines;
        {
            std::lock_guard<std::mutex> lck(uiMtx_);
            lines = uiDecodes_;
        }
        if (lines.empty()) {
            ImGui::TextUnformatted("no decodes yet");
            ImGui::End();
            return;
        }

        const double vfoHz = vfo0Freq();
        int nearest = -1;
        double bestDist = 1e18;
        for (size_t i = 0; i < lines.size(); i++) {
            const double d = std::fabs(lines[i].freqHz - vfoHz);
            if (d < bestDist) { bestDist = d; nearest = (int)i; }
        }

        const float avail = ImGui::GetContentRegionAvail().x;
        const float lineH = ImGui::GetTextLineHeight();
        const ImU32 hiColor = ImGui::GetColorU32(ImVec4(
            ImGui::GetStyleColorVec4(ImGuiCol_Header).x,
            ImGui::GetStyleColorVec4(ImGuiCol_Header).y,
            ImGui::GetStyleColorVec4(ImGuiCol_Header).z, 0.55f));

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 0.0f));
        for (size_t i = 0; i < lines.size(); i++) {
            const auto& l = lines[i];
            char freq[32];
            snprintf(freq, sizeof freq, "%9.1f kHz", l.freqHz / 1000.0);
            const float freqW = ImGui::CalcTextSize(freq).x;
            const float gap = ImGui::GetStyle().ItemSpacing.x;
            const float textRight = avail - freqW - gap;   // right edge of the text
            const float textRoom = std::max(0.0f, textRight);

            // The newest characters are drawn in yellow.
            std::string tail = l.fresh;
            std::string body = l.text;
            bool hasConf = l.conf.size() == l.text.size();
            size_t skip = 0;   // bytes of l.text cut from the left (for l.conf)
            if (tail.empty()) {
                // nothing new: the whole text is normal
            }
            else if (body.size() >= tail.size() &&
                     body.compare(body.size() - tail.size(), tail.size(), tail) == 0) {
                body.resize(body.size() - tail.size());
            }
            else {
                // the soup of characters no longer ends with the new ones
                body.clear();
                hasConf = false;
            }

            auto widthOf = [](const std::string& t) {
                return t.empty() ? 0.0f : ImGui::CalcTextSize(t.c_str()).x;
            };
            const size_t tailStart = hasConf ? l.text.size() - tail.size() : 0;   // index of tail in l.text
            size_t tailSkip = 0;
            while ((!body.empty() || !tail.empty()) && (widthOf(body) + widthOf(tail)) > textRoom) {
                if (!body.empty()) { body.erase(0, 1); skip++; }
                else { tail.erase(0, 1); tailSkip++; }
            }
            const float textW = widthOf(body) + widthOf(tail);

            const ImVec2 rowPos = ImGui::GetCursorScreenPos();
            ImGui::PushID((int)i);
            const bool clicked = ImGui::InvisibleButton("row", ImVec2(avail, lineH));
            if (clicked) {
                tuneVfo0(l.freqHz);
            }
            if (ImGui::IsItemHovered()) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); }

            ImDrawList* dl = ImGui::GetWindowDrawList();
            if ((int)i == nearest) {
                dl->AddRectFilled(rowPos, ImVec2(rowPos.x + avail, rowPos.y + lineH), hiColor);
            }
            static double lastDbg = 0.0;
            const double nowDbg = std::chrono::duration<double>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            if (nowDbg - lastDbg > 2.0) {
                lastDbg = nowDbg;
                char m[220];
                snprintf(m, sizeof m,
                         "draw: avail %.0f freqW %.0f textRight %.0f textW %.0f body %d tail %d text %d",
                         avail, freqW, textRight, textW, (int)body.size(), (int)tail.size(),
                         (int)l.text.size());
                flog::info("[tinycw] {}", m);
            }
            const float textX = rowPos.x + textRight - textW;
            ImFont* font = ImGui::GetFont();
            const float fsz = ImGui::GetFontSize();
            if (!body.empty()) {
                drawConfText(dl, font, fsz, ImVec2(textX, rowPos.y), body,
                             hasConf ? l.conf.data() + skip : nullptr,
                             ImGui::GetStyle().Colors[ImGuiCol_Text]);
            }
            if (!tail.empty()) {
                drawConfText(dl, font, fsz, ImVec2(textX + widthOf(body), rowPos.y), tail,
                             hasConf ? l.conf.data() + tailStart + tailSkip : nullptr,
                             ImVec4(1.0f, 0.85f, 0.25f, 1.0f));
            }
            dl->AddText(ImVec2(rowPos.x + avail - freqW, rowPos.y),
                        ImGui::GetColorU32(ImGuiCol_TextDisabled), freq);
            ImGui::PopID();
        }
        ImGui::PopStyleVar();
        ImGui::End();
    }

    void drawOverlay(ImGui::WaterFall::WaterfallDrawArgs args) {
        if (!enabled) { return; }
        drawDecodesWindow();
        drawSelectedTicker(args);
        tinycw::RangeResolution r;
        {
            std::lock_guard<std::mutex> lck(uiMtx_);
            r = range_;
        }
        if (!r.valid) { return; }

        char text[192];
        snprintf(text, sizeof text, "TinyCW: %d ch / %d dec | %.4f-%.4f MHz%s",
                 engine_.liveChannels(), engine_.decodeMarkers(),
                 r.loHz / 1e6, r.hiHz / 1e6,
                 model_.ready() ? "" : " | model missing");

        // Carrier/decode markers: fixed Y inside the band reserved above the
        // waterfall, at the exact carrier frequency.
        {
            const double viewBw = gui::waterfall.getViewBandwidth();
            const double viewOffset = gui::waterfall.getViewOffset();
            if (viewBw > 0.0) {
                const double lowFreq = gui::waterfall.getCenterFrequency() + viewOffset - viewBw * 0.5;
                const double ratio = (double)(args.wfMax.x - args.wfMin.x) / viewBw;
                const float y = args.wfMin.y - kOverlayReservePx * 0.5f * style::uiScale;
                drawMarkers(args.window->DrawList, lowFreq, ratio,
                            args.wfMin.x, args.wfMax.x, y);
            }
        }

        ImVec2 pos = args.wfMin;
        pos.x += 8.0f;
        pos.y += 8.0f;
        ImDrawList* dl = args.window->DrawList;
        const ImVec2 size = ImGui::CalcTextSize(text);
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x + 8.0f, pos.y + size.y + 4.0f),
                          IM_COL32(0, 0, 0, 160));
        dl->AddText(ImVec2(pos.x + 4.0f, pos.y + 2.0f), IM_COL32(255, 220, 80, 255), text);
    }

    // --------------------------------------------------------------- menu
    static void menuHandler(void* ctx) {
        ((TinyCWDecoderModule*)ctx)->drawMenu();
    }

    void drawMenu() {
        const float width = ImGui::GetContentRegionAvail().x;

        // --- model status block
        ImGui::TextUnformatted("Model");
        ImGui::SameLine();
        if (model_.ready()) {
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "%s", model_.path().c_str());
        }
        else {
            ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "not loaded");
            if (!modelError_.empty()) {
                ImGui::TextWrapped("load error: %s", modelError_.c_str());
            }
            ImGui::BeginDisabled();
        }

        // --- controls
        bool paused = paused_;
        if (ImGui::Checkbox(CONCAT("Pause##", name), &paused)) {
            paused_ = paused;
            engine_.setPaused(paused_);
            saveConfig();
        }

        const double busy = engine_.busyPercent();      // sum over workers
        const int usedThreads = std::max(1, engine_.decodeThreads());
        char cpuText[96];
        snprintf(cpuText, sizeof cpuText, "decode CPU: %.0f%% of %d00%% (%d thr)",
                 busy, usedThreads, usedThreads);
        ImGui::ProgressBar((float)std::clamp(busy / (100.0 * usedThreads), 0.0, 1.0),
                           ImVec2(width, 0), cpuText);

        int threads = (threads_ > 0) ? threads_ : tinycw::Engine::hardwareThreads();
        const int maxThreads = tinycw::Engine::hardwareThreads();
        ImGui::SetNextItemWidth(width);
        if (ImGui::SliderInt(CONCAT("##threads_", name), &threads, 1, maxThreads, "threads: %d")) {
            if (threads != threads_) {
                threads_ = threads;
                saveConfig();
                // Applied by the tick thread: the engine resizes its worker set
                // in place, the DSP graph and channels are untouched.
            }
        }

        {
            float prov = (float)provisionalSec_;
            ImGui::SetNextItemWidth(width);
            if (ImGui::SliderFloat(CONCAT("##prov_", name), &prov, 0.0f, 6.0f,
                                   "provisional tail: %.1f s")) {
                provisionalSec_ = prov;
                engine_.setProvisionalSec(provisionalSec_);
        engine_.setFocusLagSec(focusLagSec_);
                saveConfig();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
                ImGui::TextUnformatted("Provisional tail");
                ImGui::Separator();
                ImGui::TextUnformatted(
                    "Characters this close to the window end are decoded without "
                    "the audio that follows them, so their timing is not settled yet. "
                    "They are shown immediately and re-decoded by the next window, "
                    "which has the full context.");
                ImGui::Spacing();
                ImGui::TextUnformatted(
                    "0.06 s keeps only two output frames (30 ms each). "
                    "One CW character plus its trailing gap is 0.5-0.7 s at 18 WPM "
                    "and up to 1-2 s for a hand-sent fist, so 3 s covers one "
                    "character with margin.");
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }

        if (ImGui::Checkbox(CONCAT("Follow VFO decoder##follow_", name), &followVfo_)) {
            saveConfig();
            if (!followVfo_) { engine_.disableFocus(); }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
            ImGui::TextUnformatted("Follow VFO decoder");
            ImGui::Separator();
            ImGui::TextUnformatted(
                "Separate decoder on the tuned VFO only, CW mode only. Every second it "
                "runs one model pass with batch 1 over the last 24 s and averages the "
                "output frames of all overlapping windows before decoding. No squelch, "
                "no keying gate.");
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }

        if (followVfo_) {
            float lag = (float)focusLagSec_;
            ImGui::SetNextItemWidth(width);
            if (ImGui::SliderFloat(CONCAT("##focuslag_", name), &lag, 0.0f, 20.0f,
                                   "VFO decoder lag: %.0f s")) {
                focusLagSec_ = lag;
                engine_.setFocusLagSec(focusLagSec_);
                saveConfig();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
                ImGui::TextUnformatted("VFO decoder lag");
                ImGui::Separator();
                ImGui::TextUnformatted(
                    "The VFO decoder decodes a 12 s window every second. Each window "
                    "commits only the characters that have this much audio after "
                    "them, so the text runs this far behind live audio.");
                ImGui::Spacing();
                ImGui::TextUnformatted(
                    "2 s is enough right context for one character at normal "
                    "speeds. More lag gives slow or hand-sent CW more context.");
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }

        if (ImGui::Checkbox(CONCAT("Decodes window##decwin_", name), &decodesWindow_)) {
            saveConfig();
        }

        ImGui::TextUnformatted("Max decoded width (global)");
        float widthKHz = (float)(widthPolicy_.currentHz / 1000.0);
        ImGui::SetNextItemWidth(width);
        if (ImGui::SliderFloat(CONCAT("##maxwidth_", name), &widthKHz, (float)(kMinMaxWidthHz / 1000.0),
                               (float)(kUserMaxWidthLimitHz / 1000.0), "%.1f kHz")) {
            widthPolicy_.ceilingHz = std::clamp((double)widthKHz * 1000.0, kMinMaxWidthHz, kUserMaxWidthLimitHz);
            widthPolicy_.currentHz = widthPolicy_.ceilingHz;
            maxWidthHz_ = widthPolicy_.ceilingHz;
            widthPolicy_.autoMode = false;
            autoWidth_ = false;
            saveConfig();
        }
        bool autoWidth = autoWidth_;
        if (ImGui::Checkbox(CONCAT("Auto (CPU adaptive)##", name), &autoWidth)) {
            autoWidth_ = autoWidth;
            saveConfig();
        }
        if (autoWidth_) { ImGui::TextDisabled("slider follows the CPU load"); }

        ImGui::Separator();
        tinycw::RangeResolution r;
        double center = 0.0;
        double span = 0.0;
        {
            std::lock_guard<std::mutex> lck(uiMtx_);
            r = range_;
            center = centerHz_;
            span = spanHz_;
        }

        if (!r.valid) {
            ImGui::TextWrapped("No CW segment of a known band is in view (%.3f MHz, span %.1f kHz).",
                               center / 1e6, span / 1000.0);
        }
        else {
            ImGui::Text("Band: %s  (CW %0.4f-%0.4f MHz)", r.bandName.c_str(),
                        r.segLoHz / 1e6, r.segHiHz / 1e6);
            ImGui::Text("Decoding: %s", formatRange(r).c_str());
            if (r.segmentPartial) {
                ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.0f),
                                   "Only %.1f kHz of the CW segment is in the source span.",
                                   (r.spanHiHz < r.segHiHz ? r.spanHiHz : r.segHiHz) / 1000.0 -
                                   (r.spanLoHz > r.segLoHz ? r.spanLoHz : r.segLoHz) / 1000.0);
            }
            if (r.limitedByMaxWidth) {
                ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.0f),
                                   "limited by max width (%.1f kHz)", widthPolicy_.currentHz / 1000.0);
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Band range (from / to)");
            float lo = (float)(r.settingLoHz / 1000.0);
            float hi = (float)(r.settingHiHz / 1000.0);
            const float segLo = (float)(r.segLoHz / 1000.0);
            const float segHi = (float)(r.segHiHz / 1000.0);
            ImGui::SetNextItemWidth(width * 0.48f);
            if (ImGui::SliderFloat(CONCAT("##lo_", name), &lo, segLo, segHi, "%.1f")) {
                bandSettings_[r.bandName] = std::make_pair((double)lo * 1000.0, r.settingHiHz);
                saveConfig();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(width * 0.48f);
            if (ImGui::SliderFloat(CONCAT("##hi_", name), &hi, segLo, segHi, "%.1f")) {
                bandSettings_[r.bandName] = std::make_pair(r.settingLoHz, (double)hi * 1000.0);
                saveConfig();
            }
            if (ImGui::Button(CONCAT("Reset band range##", name), ImVec2(width, 0))) {
                bandSettings_.erase(r.bandName);
                saveConfig();
            }
        }

        ImGui::Separator();
        ImGui::Text("Signals: %d active / %d channels / %d decoded",
                    engine_.activeSignals(), engine_.liveChannels(), engine_.decodeMarkers());
        if (!model_.ready()) { ImGui::EndDisabled(); }
    }

    void saveConfig() {
        config.acquire();
        json& c = config.conf[name];
        c["autoWidth"] = autoWidth_;
        c["maxWidthHz"] = maxWidthHz_ > 0.0 ? maxWidthHz_ : kDefaultMaxWidthHz;
        c["paused"] = paused_;
        c["threads"] = threads_;
        c["provisionalSec"] = provisionalSec_;
        c["followVfo"] = followVfo_;
        c["focusLagSec"] = focusLagSec_;
        c["decodesWindow"] = decodesWindow_;
        c["decodesPos"] = json::array({ decodesPos_.x, decodesPos_.y });
        c["decodesSize"] = json::array({ decodesSize_.x, decodesSize_.y });
        json bands = json::object();
        for (const auto& [band, range] : bandSettings_) {
            bands[band] = { { "lo", range.first }, { "hi", range.second } };
        }
        c["bandSettings"] = bands;
        config.release(true);
    }

private:
    std::string name;

    dsp::stream<dsp::complex_t> iqStream_{ "tinycw.iq" };
    tinycw::Model model_;
    tinycw::Engine engine_;

    std::thread tickThread_;
    std::atomic_bool tickRunning_{ true };
    bool enabled = false;

    bool autoWidth_ = true;
    double maxWidthHz_ = kDefaultMaxWidthHz;
    tinycw::AdaptiveWidth widthPolicy_;
    bool paused_ = false;
    int threads_ = 0;            // 0 = auto
    double engineRate_ = 0.0;

    mutable std::mutex uiMtx_;
    tinycw::RangeResolution range_;
    double centerHz_ = 0.0;
    double spanHz_ = 0.0;

    std::map<std::string, std::pair<double, double>> bandSettings_;
    std::string modelError_;

    std::chrono::steady_clock::time_point lastBatch_ = std::chrono::steady_clock::now();

    std::mutex linesMtx_;
    std::vector<std::string> testLines_;

    EventHandler<ImGui::WaterFall::WaterfallDrawArgs> waterfallListener_;
    EventHandler<int&> reserveListener_;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    config.setPath(std::string(core::getRoot()) + "/tinycw_decoder_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new TinyCWDecoderModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (TinyCWDecoderModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}