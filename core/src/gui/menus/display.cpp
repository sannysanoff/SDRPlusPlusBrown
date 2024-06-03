
#include <gui/menus/display.h>
#include <imgui.h>
#include <gui/gui.h>
#include <core.h>
#include <gui/colormaps.h>
#include <gui/gui.h>
#include <gui/main_window.h>
#include <signal_path/signal_path.h>
#include <gui/style.h>
#include <utils/optionlist.h>
#include <algorithm>

namespace displaymenu {
    bool showWaterfall;
    bool showFFT = true;
    bool showFFTShadows = true;
    bool showMicHistogram = false;
    bool fullWaterfallUpdate = true;
    bool showBattery = true;
    bool showClock = true;
    std::string currentBatteryLevel = "?";
    int colorMapId = 0;
    std::vector<std::string> colorMapNames;
    std::string colorMapNamesTxt = "";
    std::string colorMapAuthor = "";
    int selectedWindow = 0;
    int fftRate = 20;
    int uiScaleId = 0;
    int transcieverLayoutId = 0;
    bool restartRequired = false;
    bool fftHold = false;
    int fftHoldSpeed = 60;
    bool fftSmoothing = false;
    int fftSmoothingSpeed = 100;
    bool phoneLayout = false;

    Event<ImGuiContext *> onDisplayDraw;

    TranscieverLayout transcieverLayout = TRAL_NONE;

#ifdef __ANDROID__
    float displayDensity = 1.0;  // 1.0 = 160 dpi. 3.5 = kinda high dpi etc. Coincides with good default scale
#endif
    bool snrSmoothing = false;
    int snrSmoothingSpeed = 20;

    OptionList<float, float> uiScales;

    const int FFTSizes[] = {
        524288,
        262144,
        131072,
        65536,
        32768,
        16384,
        8192,
        4096,
        2048,
        1024
    };

    const char* FFTSizesStr = "524288\0"
                              "262144\0"
                              "131072\0"
                              "65536\0"
                              "32768\0"
                              "16384\0"
                              "8192\0"
                              "4096\0"
                              "2048\0"
                              "1024\0"
#ifdef __APPLE__
                            "524288 ACCEL\0"
                            "262144 ACCEL\0"
                            "131072 ACCEL\0"
                            "65536 ACCEL\0"
                            "32768 ACCEL\0"
                            "16384 ACCEL\0"
                            "8192 ACCEL\0"
                            "4096 ACCEL\0"
                            "2048 ACCEL\0"
                            "1024 ACCEL\0"
#endif
        ;

    int fftSizeId = 0;

    const IQFrontEnd::FFTWindow fftWindowList[] = {
        IQFrontEnd::FFTWindow::RECTANGULAR,
        IQFrontEnd::FFTWindow::BLACKMAN,
        IQFrontEnd::FFTWindow::NUTTALL,
    };

    void updateFFTSpeeds() {
        gui::waterfall.setFFTHoldSpeed((float)fftHoldSpeed / ((float)fftRate * 10.0f));
        gui::waterfall.setFFTSmoothingSpeed(std::min<float>((float)fftSmoothingSpeed / (float)(fftRate * 10.0f), 1.0f));
        gui::waterfall.setSNRSmoothingSpeed(std::min<float>((float)snrSmoothingSpeed / (float)(fftRate * 10.0f), 1.0f));
    }

    void init() {
        if (core::configManager.conf.contains("showFFT")) {
            showFFT = core::configManager.conf["showFFT"];
        }
        if (core::configManager.conf.contains("showFFTShadows")) {
            showFFTShadows = core::configManager.conf["showFFTShadows"];
        }
        if (core::configManager.conf.contains("showMicHistogram")) {
            showMicHistogram = core::configManager.conf["showMicHistogram"];
        }
        showWaterfall = core::configManager.conf["showWaterfall"];
        showWaterfall ? gui::waterfall.showWaterfall() : gui::waterfall.hideWaterfall();
        std::string colormapName = core::configManager.conf["colorMap"];
        if (colormaps::maps.find(colormapName) != colormaps::maps.end()) {
            colormaps::Map map = colormaps::maps[colormapName];
            gui::waterfall.updatePalletteFromArray(map.map, map.entryCount);
        }

        for (auto const& [name, map] : colormaps::maps) {
            colorMapNames.push_back(name);
            colorMapNamesTxt += name;
            colorMapNamesTxt += '\0';
            if (name == colormapName) {
                colorMapId = (colorMapNames.size() - 1);
                colorMapAuthor = map.author;
            }
        }

        fullWaterfallUpdate = core::configManager.conf["fullWaterfallUpdate"];
        gui::waterfall.setFullWaterfallUpdate(fullWaterfallUpdate);

        if (core::configManager.conf.contains("showBattery")) {
            showBattery = core::configManager.conf["showBattery"];
        }
        if (core::configManager.conf.contains("showClock")) {
            showClock = core::configManager.conf["showClock"];
        }

        fftSizeId = 4;
        int fftSize = core::configManager.conf["fftSize"];
        for (int i = 0; i < std::size(FFTSizes); i++) {
            if (fftSize == FFTSizes[i]) {
                fftSizeId = i;
                break;
            }
        }
        auto accel = core::configManager.conf.contains("fftAccel") && core::configManager.conf["fftAccel"];
        if (accel) {
            fftSizeId += std::size(FFTSizes);
        }

        enableAcceleratedFFT = accel;
        sigpath::iqFrontEnd.setFFTSize(FFTSizes[fftSizeId % std::size(FFTSizes)]);

        fftRate = core::configManager.conf["fftRate"];
        sigpath::iqFrontEnd.setFFTRate(fftRate);

        selectedWindow = std::clamp<int>((int)core::configManager.conf["fftWindow"], 0, (sizeof(fftWindowList) / sizeof(IQFrontEnd::FFTWindow)) - 1);
        sigpath::iqFrontEnd.setFFTWindow(fftWindowList[selectedWindow]);

        gui::menu.locked = core::configManager.conf["lockMenuOrder"];

        phoneLayout = core::configManager.conf["smallScreen"];
        transcieverLayout = core::configManager.conf["transcieverLayout"];

        fftHold = core::configManager.conf["fftHold"];
        fftHoldSpeed = core::configManager.conf["fftHoldSpeed"];
        gui::waterfall.setFFTHold(fftHold);
        fftSmoothing = core::configManager.conf["fftSmoothing"];
        fftSmoothingSpeed = core::configManager.conf["fftSmoothingSpeed"];
        gui::waterfall.setFFTSmoothing(fftSmoothing);
        snrSmoothing = core::configManager.conf["snrSmoothing"];
        snrSmoothingSpeed = core::configManager.conf["snrSmoothingSpeed"];
        gui::waterfall.setSNRSmoothing(snrSmoothing);
        updateFFTSpeeds();


        // Define and load UI scales

        std::vector<float> scales = {0.25f, 0.5f, 0.66f, 0.75f, 0.9f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f, 3.0f, 4.0f};
        bool hasNativeScale = false;
#ifdef __ANDROID__
        for (int i = 0; i < scales.size(); i++) {
            float scale = scales[i];
            if (scale == displayDensity) {
                hasNativeScale = true;
            }
        }
        if (!hasNativeScale) {
            uiScales.define(displayDensity, std::to_string((int)(displayDensity * 100)) + "% (native)", displayDensity);
        }
#endif
        for (int i = 0; i < scales.size(); i++) {
            float scale = scales[i];
            uiScales.define(scale, std::to_string((int)(scale * 100)) + "%", scale);
        }

        uiScaleId = uiScales.valueId(style::uiScale);
    }

    void setWaterfallShown(bool shown) {
        showWaterfall = shown;
        showWaterfall ? gui::waterfall.showWaterfall() : gui::waterfall.hideWaterfall();
        core::configManager.acquire();
        core::configManager.conf["showWaterfall"] = showWaterfall;
        core::configManager.release(true);
    }


    void checkKeybinds() {
        if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
            setWaterfallShown(!showWaterfall);
        }
    }


    void draw(void* ctx) {
        float menuWidth = ImGui::GetContentRegionAvail().x;
        if (ImGui::Checkbox("Small screen / thick fingers##_sdrpp", &phoneLayout)) {
            core::configManager.acquire();
            core::configManager.conf["smallScreen"] = phoneLayout;
            core::configManager.release(true);
        }
        if (ImGui::RadioButton("Layout: default ##_sdrpp", transcieverLayout == TRAL_NONE)) {
            core::configManager.acquire();
            transcieverLayout = TRAL_NONE;
            core::configManager.conf["transcieverLayout"] = transcieverLayout;
            core::configManager.release(true);
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("SSB trx ##_sdrpp", transcieverLayout == TRAL_SSB_FIRST)) {
            core::configManager.acquire();
            transcieverLayout = TRAL_SSB_FIRST;
            core::configManager.conf["transcieverLayout"] = transcieverLayout;
            core::configManager.release(true);
        }

        if (ImGui::Checkbox("Waterfall##_sdrpp", &showWaterfall)) {
            setWaterfallShown(showWaterfall);
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("FFT##_sdrpp", &showFFT)) {
            core::configManager.acquire();
            core::configManager.conf["showFFT"] = showFFT;
            core::configManager.release(true);
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Shadow##_sdrpp", &showFFTShadows)) {
            core::configManager.acquire();
            core::configManager.conf["showFFTShadows"] = showFFTShadows;
            core::configManager.release(true);
        }

        if (ImGui::Checkbox("Full Waterfall Update##_sdrpp", &fullWaterfallUpdate)) {
            gui::waterfall.setFullWaterfallUpdate(fullWaterfallUpdate);
            core::configManager.acquire();
            core::configManager.conf["fullWaterfallUpdate"] = fullWaterfallUpdate;
            core::configManager.release(true);
        }
#ifdef __ANDROID__
        if (ImGui::Checkbox("Show Battery##_sdrpp", &showBattery)) {
            gui::waterfall.setFullWaterfallUpdate(fullWaterfallUpdate);
            core::configManager.acquire();
            core::configManager.conf["showBattery"] = showBattery;
            core::configManager.release(true);
        }
#endif
        if (ImGui::Checkbox("Show Clock##_sdrpp", &showClock)) {
            gui::waterfall.setFullWaterfallUpdate(fullWaterfallUpdate);
            core::configManager.acquire();
            core::configManager.conf["showClock"] = showClock;
            core::configManager.release(true);
        }

        if (ImGui::Checkbox("Lock Menu Order##_sdrpp", &gui::menu.locked)) {
            core::configManager.acquire();
            core::configManager.conf["lockMenuOrder"] = gui::menu.locked;
            core::configManager.release(true);
        }
        if (ImGui::Checkbox("FFT Hold##_sdrpp", &fftHold)) {
            gui::waterfall.setFFTHold(fftHold);
            core::configManager.acquire();
            core::configManager.conf["fftHold"] = fftHold;
            core::configManager.release(true);
        }
        ImGui::SameLine();
        ImGui::FillWidth();
        if (ImGui::InputInt("##sdrpp_fft_hold_speed", &fftHoldSpeed)) {
            updateFFTSpeeds();
            core::configManager.acquire();
            core::configManager.conf["fftHoldSpeed"] = fftHoldSpeed;
            core::configManager.release(true);
        }

        if (ImGui::Checkbox("FFT Smoothing##_sdrpp", &fftSmoothing)) {
            gui::waterfall.setFFTSmoothing(fftSmoothing);
            core::configManager.acquire();
            core::configManager.conf["fftSmoothing"] = fftSmoothing;
            core::configManager.release(true);
        }
        ImGui::SameLine();
        ImGui::FillWidth();
        if (ImGui::InputInt("##sdrpp_fft_smoothing_speed", &fftSmoothingSpeed)) {
            fftSmoothingSpeed = std::max<int>(fftSmoothingSpeed, 1);
            updateFFTSpeeds();
            core::configManager.acquire();
            core::configManager.conf["fftSmoothingSpeed"] = fftSmoothingSpeed;
            core::configManager.release(true);
        }

        if (ImGui::Checkbox("SNR Smoothing##_sdrpp", &snrSmoothing)) {
            gui::waterfall.setSNRSmoothing(snrSmoothing);
            core::configManager.acquire();
            core::configManager.conf["snrSmoothing"] = snrSmoothing;
            core::configManager.release(true);
        }
        ImGui::SameLine();
        ImGui::FillWidth();
        if (ImGui::InputInt("##sdrpp_snr_smoothing_speed", &snrSmoothingSpeed)) {
            snrSmoothingSpeed = std::max<int>(snrSmoothingSpeed, 1);
            updateFFTSpeeds();
            core::configManager.acquire();
            core::configManager.conf["snrSmoothingSpeed"] = snrSmoothingSpeed;
            core::configManager.release(true);
        }

        ImGui::LeftLabel("High-DPI Scaling");
        ImGui::FillWidth();
        if (ImGui::Combo("##sdrpp_ui_scale", &uiScaleId, uiScales.txt)) {
            core::configManager.acquire();
            core::configManager.conf["uiScale"] = uiScales[uiScaleId];
            core::configManager.release(true);
            restartRequired = true;
        }

        ImGui::LeftLabel("FFT Framerate");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputInt("##sdrpp_fft_rate", &fftRate, 1, 10)) {
            fftRate = std::max<int>(1, fftRate);
            sigpath::iqFrontEnd.setFFTRate(fftRate);
            updateFFTSpeeds();
            core::configManager.acquire();
            core::configManager.conf["fftRate"] = fftRate;
            core::configManager.release(true);
        }

        ImGui::LeftLabel("FFT Size");
        auto textSize8888  =  ImGui::CalcTextSize("88888888");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX() - textSize8888.x);
        if (ImGui::Combo("##sdrpp_fft_size", &fftSizeId, FFTSizesStr)) {
            enableAcceleratedFFT = fftSizeId >= std::size(FFTSizes);
            sigpath::iqFrontEnd.setFFTSize(FFTSizes[fftSizeId % std::size(FFTSizes)]);
            core::configManager.acquire();
            core::configManager.conf["fftSize"] = FFTSizes[fftSizeId % std::size(FFTSizes)];
            core::configManager.conf["fftAccel"] = fftSizeId >= std::size(FFTSizes);
            core::configManager.release(true);
        }

        {
            ImGui::SameLine();
            static auto lastFFTReportTime = currentTimeMillis();
            static long long lastFFTReport = 0;
            auto ctm = currentTimeMillis();
            if (ctm - lastFFTReportTime > 1000) {
                lastFFTReportTime += 1000;
                lastFFTReport = fftCumulativeTime;
                fftCumulativeTime = 0;
            }
            ImGui::Text("%lld ms/s", lastFFTReport);
        }

        ImGui::LeftLabel("FFT Window");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::Combo("##sdrpp_fft_window", &selectedWindow, "Rectangular\0Blackman\0Nuttall\0")) {
            sigpath::iqFrontEnd.setFFTWindow(fftWindowList[selectedWindow]);
            core::configManager.acquire();
            core::configManager.conf["fftWindow"] = selectedWindow;
            core::configManager.release(true);
        }

        if (colorMapNames.size() > 0) {
            ImGui::LeftLabel("Color Map");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::Combo("##_sdrpp_color_map_sel", &colorMapId, colorMapNamesTxt.c_str())) {
                colormaps::Map map = colormaps::maps[colorMapNames[colorMapId]];
                gui::waterfall.updatePalletteFromArray(map.map, map.entryCount);
                core::configManager.acquire();
                core::configManager.conf["colorMap"] = colorMapNames[colorMapId];
                core::configManager.release(true);
                colorMapAuthor = map.author;
            }
            ImGui::Text("Color map Author: %s", colorMapAuthor.c_str());
        }

        onDisplayDraw.emit(GImGui);

        if (restartRequired) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Restart required.");
        }
    }
}
