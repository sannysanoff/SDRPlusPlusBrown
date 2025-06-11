#pragma once
#include <core.h>
#include <vector>
#include <mutex>
#include <gui/widgets/bandplan.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <utils/event.h>
#include <ctm.h>

#include <utils/opengl_include_code.h>

#define WATERFALL_RESOLUTION 1000

namespace ImGui {



    class WaterfallVFO {
    public:
        void setOffset(double offset);
        void setCenterOffset(double offset);
        void setBandwidth(double bw);
        void setReference(int ref);
        void setSnapInterval(double interval);
        void setNotchOffset(double offset);
        void setNotchVisible(bool visible);
        void updateDrawingVars(double viewBandwidth, float dataWidth, double viewOffset, ImVec2 widgetPos, int fftHeight); // NOTE: Datawidth double???
        void draw(ImGuiWindow* window, bool selected);

        enum {
            REF_LOWER,
            REF_CENTER,
            REF_UPPER,
            _REF_COUNT
        };

        double generalOffset;
        double centerOffset;
        double lowerOffset;
        double upperOffset;
        double bandwidth;
        double snapInterval = 5000;
        int reference = REF_CENTER;

        double notchOffset = 0;
        bool notchVisible = false;

        bool leftClamped;
        bool rightClamped;

        ImVec2 rectMin;
        ImVec2 rectMax;
        ImVec2 lineMin;
        ImVec2 lineMax;
        ImVec2 wfRectMin;
        ImVec2 wfRectMax;
        ImVec2 wfLineMin;
        ImVec2 wfLineMax;
        ImVec2 lbwSelMin;
        ImVec2 lbwSelMax;
        ImVec2 rbwSelMin;
        ImVec2 rbwSelMax;
        ImVec2 wfLbwSelMin;
        ImVec2 wfLbwSelMax;
        ImVec2 wfRbwSelMin;
        ImVec2 wfRbwSelMax;
        ImVec2 notchMin;
        ImVec2 notchMax;

        bool centerOffsetChanged = false;
        bool lowerOffsetChanged = false;
        bool upperOffsetChanged = false;
        bool redrawRequired = true;
        bool lineVisible = true;
        bool bandwidthChanged = false;

        double minBandwidth;
        double maxBandwidth;
        bool bandwidthLocked;

        ImU32 color = IM_COL32(255, 255, 255, 50);

        Event<double> onUserChangedBandwidth;
        Event<double> onUserChangedNotch;
        Event<int> onUserChangedDemodulator;
    };

    class WaterFall {
    public:
        WaterFall();
        virtual ~WaterFall();

        void init();

        void draw();
        float* getFFTBuffer();
        void pushFFT();

        void updatePallette(float colors[][3], int colorCount);
        void updatePalletteFromArray(float* colors, int colorCount);

        void setCenterFrequency(double freq);
        double getCenterFrequency();

        void setBandwidth(double bandWidth);
        double getBandwidth();

        void setUsableSpectrumRatio(double spectrumratio);
        double getUsableSpectrumRatio();

        void setViewBandwidth(double bandWidth);
        double getViewBandwidth();

        void setViewOffset(double offset);
        double getViewOffset();

        void setFFTMin(float min);
        float getFFTMin();

        void setFFTMax(float max);
        float getFFTMax();

        void setWaterfallMin(float min);
        float getWaterfallMin();

        void setWaterfallMax(float max);
        float getWaterfallMax();

        void setZoom(double zoomLevel);
        void setOffset(double zoomOffset);

        std::pair<int, int> autoRange();

        void selectFirstVFO();

        void showWaterfall();
        void hideWaterfall();

        void showBandplan();
        void hideBandplan();

        void setFFTHeight(int height);
        int getFFTHeight();

        void setRawFFTSize(int size);

        void setFullWaterfallUpdate(bool fullUpdate);

        void setBandPlanPos(int pos);

        void setFFTHold(bool hold);
        void setFFTHoldSpeed(float speed);

        void setFFTSmoothing(bool enabled);
        void setFFTSmoothingSpeed(float speed);

        void setSNRSmoothing(bool enabled);
        void setSNRSmoothingSpeed(float speed);

        float* acquireLatestFFT(int& width);
        void releaseLatestFFT();

        bool centerFreqMoved = false;
        bool vfoFreqChanged = false;
        bool bandplanEnabled = false;
        bandplan::BandPlan_t* bandplan = NULL;

        bool mouseInFFTResize = false;
        bool mouseInFreq = false;
        bool mouseInFFT = false;
        bool mouseInWaterfall = false;
        bool horizontalScaleVisible = true;

        float selectedVFOSNR = 0.0f;

        bool centerFrequencyLocked = false;

        std::map<std::string, WaterfallVFO*> vfos;
        std::string selectedVFO = "";
        bool selectedVFOChanged = false;
        bool quiet = false;

        struct FFTRedrawArgs {
            ImVec2 min;
            ImVec2 max;
            double lowFreq;
            double highFreq;
            double freqToPixelRatio;
            double pixelToFreqRatio;
            ImGuiWindow* window;
        };

        Event<FFTRedrawArgs> onFFTRedraw;

        struct WaterfallDrawArgs {
            ImGuiWindow* window;
            ImVec2 wfMin;
            ImVec2 wfMax;

        };

        Event<WaterfallDrawArgs> afterWaterfallDraw;

        struct InputHandlerArgs {
            ImVec2 fftRectMin;
            ImVec2 fftRectMax;
            ImVec2 freqScaleRectMin;
            ImVec2 freqScaleRectMax;
            ImVec2 waterfallRectMin;
            ImVec2 waterfallRectMax;
            double lowFreq;
            double highFreq;
            double freqToPixelRatio;
            double pixelToFreqRatio;
        };

        bool inputHandled = false;
        bool alwaysDrawLine = false;
        bool VFOMoveSingleClick = false;
        Event<InputHandlerArgs> onInputProcess;

        enum {
            REF_LOWER,
            REF_CENTER,
            REF_UPPER,
            _REF_COUNT
        };

        enum {
            BANDPLAN_POS_BOTTOM,
            BANDPLAN_POS_TOP,
            _BANDPLAN_POS_COUNT
        };

        ImVec2 fftAreaMin;
        ImVec2 fftAreaMax;
        ImVec2 freqAreaMin;
        ImVec2 freqAreaMax;
        ImVec2 wfMin;
        ImVec2 wfMax;
        int WATERFALL_NUMBER_OF_SECTIONS = 64;

        bool containsFrequency(double d);
        void updateWaterfallFb(const std::string &where = ""); // called from android, from outside
        ImVec2 widgetPos;
        ImVec2 widgetEndPos;


    private:
        void drawWaterfall();
        void drawFFT();
        void drawVFOs();
        void drawBandPlan();
        void processInputs();
        void onPositionChange();
        void onResize();
        void updateWaterfallTexture();

        enum {
            TEXTURE_SPECIFY_REQUIRED,
            TEXTURE_PIXELS_CHANGE_REQUIRED,
            TEXTURE_OK
        };

        void setTextureStatus(int index, int value);
        void updateWaterfallTexturesIfNeeded();
        void updateWaterfallTextureIfNeeded(int textureIndex, int startRowIndex);
        void specifyTexture(int textureIndex, const uint8_t* pixels) const;
        void changeTexturePixels(int textureIndex, const uint8_t* pixels) const;
        void drawWaterfallImages();

        void updateAllVFOs(bool checkRedrawRequired = false);
        bool calculateVFOSignalInfo(float* fftLine, WaterfallVFO* vfo, float& strength, float& snr);

        bool waterfallUpdate = false;

        uint32_t waterfallPallet[WATERFALL_RESOLUTION];

        ImVec2 widgetSize;

        ImVec2 lastWidgetPos;
        ImVec2 lastWidgetSize;

        ImGuiWindow* window;

        std::recursive_mutex buf_mtx;
        std::recursive_mutex latestFFTMtx;
        std::mutex texMtx;
        std::mutex smoothingBufMtx;

        float vRange;

        int maxVSteps;
        int maxHSteps;

        int dataWidth;           // Width of the FFT/oscilloscope and waterfall, taken from window size, in pixels
        int fftHeight;           // Height of the fft graph, taken from window size, in pixels
        int waterfallHeight = 0; // Height of the waterfall, taken from window size, in pixels

		double usableSpectrumRatio;
        double viewBandwidth;
        double viewOffset;

        double lowerFreq;
        double upperFreq;
        double range;

        float lastDrag;

        int vfoRef = REF_CENTER;

        // Absolute values
        double centerFreq;
        double wholeBandwidth;

        // Ranges
        float fftMin;
        float fftMax;
        float waterfallMin;
        float waterfallMax;

        //std::vector<std::vector<float>> rawFFTs;
        int rawFFTSize;
        float* rawFFTs = NULL;
        float* latestFFT = NULL;
        float* latestFFTHold = NULL;
        float* smoothingBuf = NULL;
        int currentFFTLine = 0;
        int fftLines = 0;

        uint32_t* waterfallFb;
        float* tempDataForUpdateWaterfallFb;

        int waterfallFbHeadRowIndex = 0;

        int waterfallMaxSectionHeight = 2;

        int waterfallHeadSectionIndex = 0;
        int waterfallHeadSectionHeight = 0;

        GLuint* waterfallTexturesIds;
        int* waterfallTexturesStatuses;

        bool draggingFW = false;
        int FFTAreaHeight;
        int newFFTAreaHeight;

        bool waterfallVisible = true;
        bool bandplanVisible = false;

        bool _fullUpdate = true;

        int bandPlanPos = BANDPLAN_POS_BOTTOM;

        bool fftHold = false;
        float fftHoldSpeed = 0.3f;

        bool fftSmoothing = false;
        float fftSmoothingAlpha = 0.5;
        float fftSmoothingBeta = 0.5;

        bool snrSmoothing = false;
        float snrSmoothingAlpha = 0.5;
        float snrSmoothingBeta = 0.5;

        // UI Select elements
        bool fftResizeSelect = false;
        bool freqScaleSelect = false;
        bool vfoSelect = false;
        bool vfoBorderSelect = false;
        WaterfallVFO* relatedVfo = NULL;
        ImVec2 mouseDownPos;

        ImVec2 lastMousePos;

        int rawFFTIndex(double frequency) const;
        void testAlloc(const std::string& where);
    };


};
