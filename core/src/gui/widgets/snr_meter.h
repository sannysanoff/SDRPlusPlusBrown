#pragma once
#include <imgui/imgui.h>

namespace ImGui {

    struct SNRMeterExtPoint {
        ImVec2 postSnrLocation; // where it ended drawing
        float lastDrawnValue;   // what it drew last time
    };

    extern Event<SNRMeterExtPoint> onSNRMeterExtPoint;

    void SNRMeter(float val, const ImVec2& size_arg = ImVec2(0, 0));
}