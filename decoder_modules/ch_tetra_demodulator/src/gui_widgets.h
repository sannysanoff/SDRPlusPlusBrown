#pragma once
#define GImGui (ImGui::GetCurrentContext())

#include <imgui/imgui.h>

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui/imgui_internal.h>

namespace ImGui {
    ImVec2 operator+(ImVec2 a, ImVec2 b) {
        return ImVec2(a.x + b.x, a.y + b.y);
    }

    void BoxIndicator(float menuWidth, ImU32 color, const ImVec2& size_arg = ImVec2(0, 0)) {
        ImGuiWindow* window = GetCurrentWindow();
        ImGuiStyle& style = GetStyle();

        ImVec2 min = window->DC.CursorPos;
        min.x = menuWidth - (GetFontSize());
        ImVec2 size = CalcItemSize(size_arg, (GetFontSize()), (GetFontSize()));
        ImRect bb(min, min+size);

        float lineHeight = size.y;

        ItemSize(size, style.FramePadding.y);
        if (!ItemAdd(bb, 0)) {
            return;
        }

        window->DrawList->AddRectFilled(min, min+size, color);
    }

    void SigQualityMeter(float avg, float val_min, float val_max, const ImVec2& size_arg = ImVec2(0, 0)) {
        ImGuiWindow* window = GetCurrentWindow();
        ImGuiStyle& style = GetStyle();

        avg = std::clamp<float>(avg, val_min, val_max);

        ImVec2 min = window->DC.CursorPos;
        ImVec2 size = CalcItemSize(size_arg, CalcItemWidth(), (GetFontSize() / 2) + style.FramePadding.y);
        ImRect bb(min, min + size);

        float lineHeight = size.y;

        ItemSize(size, style.FramePadding.y);
        if (!ItemAdd(bb, 0)) {
            return;
        }

        float badSig = roundf(0.3f * size.x);

        window->DrawList->AddRectFilled(min, min + ImVec2(badSig, lineHeight), IM_COL32(136, 9, 9, 255));
        window->DrawList->AddRectFilled(min + ImVec2(badSig, 0), min + ImVec2(size.x, lineHeight), IM_COL32(9, 136, 9, 255));

        float end = roundf(((avg - val_min) / (val_max - val_min)) * size.x);

        if (avg <= (val_min+(val_max-val_min)*0.3f)) {
            window->DrawList->AddRectFilled(min, min + ImVec2(end, lineHeight), IM_COL32(230, 5, 5, 255));
        }
        else {
            window->DrawList->AddRectFilled(min, min + ImVec2(badSig, lineHeight), IM_COL32(230, 5, 5, 255));
            window->DrawList->AddRectFilled(min + ImVec2(badSig, 0), min + ImVec2(end, lineHeight), IM_COL32(5, 230, 5, 255));
        }
    }
} 
