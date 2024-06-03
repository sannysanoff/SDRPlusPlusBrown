#pragma once

#include "utils/event.h"
#include <imgui/imgui.h>

extern long long lastDrawTimeBackend;
extern long long lastDrawTime;
extern int glSleepTime;

namespace displaymenu {
    void init();
    void checkKeybinds();
    void draw(void* ctx);
    extern bool phoneLayout;
#ifdef __ANDROID__
    extern float displayDensity;
#endif
    extern Event<ImGuiContext *> onDisplayDraw;
    extern bool showBattery;
    extern bool showClock;
    extern bool showFFT;
    extern bool showFFTShadows;
    extern bool showMicHistogram;
    extern std::string currentBatteryLevel;

    extern enum TranscieverLayout {
        TRAL_NONE = 0,
        TRAL_SSB_FIRST = 1
    } transcieverLayout;

}