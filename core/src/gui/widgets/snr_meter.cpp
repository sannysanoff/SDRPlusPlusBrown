#include <gui/widgets/volume_meter.h>
#include <algorithm>

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui/imgui_internal.h>
#include <vector>

namespace ImGui {

    static const int NLASTSNR = 1500;
    static std::vector<float> lastsnr;

    static ImVec2 postSnrLocation;

    static float ratio;


    void SNRMeter(float val, const ImVec2& size_arg = ImVec2(0, 0)) {
        ImGuiWindow* window = GetCurrentWindow();
        ImGuiStyle& style = GImGui->Style;

        ImVec2 min = window->DC.CursorPos;
        ImVec2 size = CalcItemSize(size_arg, CalcItemWidth(), 26);
        ImRect bb(min, min + size);

        ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);

        ItemSize(size, style.FramePadding.y);
        if (!ItemAdd(bb, 0)) {
            return;
        }

        val = std::clamp<float>(val, 0, 100);
        ratio = size.x / 90;
        float it = size.x / 9;
        char buf[32];

        float drawVal = (float) val * ratio;

        postSnrLocation = min + ImVec2(0, 50);

        lastsnr.insert(lastsnr.begin(), drawVal);
        if (lastsnr.size() > NLASTSNR)
            lastsnr.resize(NLASTSNR);

        window->DrawList->AddRectFilled(min + ImVec2(0, 1), min + ImVec2(roundf(drawVal), 10), IM_COL32(0, 136, 255, 255));
        window->DrawList->AddLine(min, min + ImVec2(0, 9), text);
        window->DrawList->AddLine(min + ImVec2(0, 9), min + ImVec2(size.x + 1, 9), text);

        for (int i = 0; i < 10; i++) {
            window->DrawList->AddLine(min + ImVec2(roundf((float)i * it), 9), min + ImVec2(roundf((float)i * it), 14), text);
            sprintf(buf, "%d", i * 10);
            ImVec2 sz = ImGui::CalcTextSize(buf);
            window->DrawList->AddText(min + ImVec2(roundf(((float)i * it) - (sz.x / 2.0)) + 1, 16), text, buf);
        }
    }

    static std::vector<float> sma(int smawindow, std::vector <float> &src) {
        float running = 0;
        std::vector<float> dest;
        for(int q=0; q<src.size(); q++) {
            running += src[q];
            float taveraged = 0;
            if (q >= smawindow) {
                running -= src[q-smawindow];
                taveraged = running/smawindow;
            } else {
                taveraged = running/q;
            }
            dest.emplace_back(taveraged);
        }
        return dest;
    }

    static std::vector<float> maxeach(int maxwindow, std::vector <float> &src) {
        float running = 0;
        std::vector<float> dest;
        for(int q=0; q<src.size(); q++) {
            running = std::max(src[q], running);
            if (q % maxwindow == maxwindow - 1 || q == src.size() - 1) {
                dest.emplace_back(running);
                running = 0;
            }
        }
        return dest;
    }

    float SNRMeterGetMaxInWindow(int nframes) {
        if (ratio == 0)
            return -1;
        if (nframes > lastsnr.size()) {
            return -1;
        }
        float mx = 0;
        for(int i=0; i<nframes; i++) {
            float v = lastsnr[i] / ratio;
            if (v > mx)
                mx = v;
        }
        return mx;
    }

    float SNRMeterGetMinInWindow(int nframes) {
        if (ratio == 0)
            return -1;
        if (nframes > lastsnr.size()) {
            return -1;
        }
        float mx = 0;
        for(int i=0; i<nframes; i++) {
            float v = lastsnr[i] / ratio;
            if (v < mx)
                mx = v;
        }
        return mx;
    }

    void SNRMeterAverages() {

        static std::vector<float> r;
        static int counter = 0;
        static const int winsize = 10;
        counter++;
        if (counter % winsize == winsize - 1) {
            r =  maxeach(winsize, lastsnr);
        }
//        maxeach(maxv, 6, smav);
//        maxv = lastsnr;
        ImGuiWindow* window = GetCurrentWindow();
        ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
        for(int q=1; q<r.size(); q++) {
            window->DrawList->AddLine(postSnrLocation + ImVec2(0 + r[q-1], q-1), postSnrLocation + ImVec2(0 + r[q], q), text);
        }
    }
}