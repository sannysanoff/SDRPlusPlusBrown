#pragma once
#include <imgui/imgui.h>
#include <fftw3.h>
#include <dsp/types.h>
#include <dsp/stream.h>
#include <signal_path/vfo_manager.h>
#include <string>
#include <utils/event.h>
#include <mutex>
#include <gui/tuner.h>
#include "../main_window.h"
#include <signal_path/sink.h>


SDRPP_EXPORT float trxAudioSampleRate;

struct StatusReporter {
    virtual std::string reportStatus() = 0;
};

struct TheEncoder {

    double somePosition = 0; // angle in radians
    double speed = 0;
    double currentValue = 0;
    double delayFactor = 0.96;

    double lastMouseAngle = nan("");
    bool enabled = true;
    int encoderId = -1;

    std::vector<double> fingerMovement;

    double draw(float currentValue);
};

struct QSOPanel;
struct ConfigPanel;
struct CWPanel;


struct MobileButton {
    std::string upperText;
    std::string buttonText;
    ImVec2 pressPoint;
    MobileButton(std::string upperText, std::string buttonText) {
        this->upperText = upperText;
        this->buttonText = buttonText;
        this->pressPoint = ImVec2(nan(""), nan(""));
    }
    double sizeFactor = 0.8;
    bool draw();
    bool currentlyPressed;
    long long currentlyPressedTime;
    long long firstPressedTime;

    bool isLongPress();
};

struct SubWaterfall;

struct MobileMainWindowPrivate;


class MobileMainWindow : public MainWindow {
public:
    TheEncoder encoder;
    TheEncoder smallEncoder;
    MobileButton autoWaterfall;
    MobileButton audioConfigToggle;
    MobileButton smallWheelFunction;
    MobileButton modeToggle;
    MobileButton submodeToggle;
    MobileButton qsoButton;
    MobileButton endQsoButton;
    MobileButton exitConfig;
    MobileButton txButton;
    MobileButton softTune;
    MobileButton lockFrequency;
    MobileButton dummy;
    MobileButton callCQ;
    std::shared_ptr<ConfigPanel> configPanel;
    std::shared_ptr<QSOPanel> qsoPanel;
    std::shared_ptr<CWPanel> cwPanel;
    int smallWheelFunctionN = 0;
    int encoderWidth = 150;
    float buttonsWidthScale = 1.0;

    std::string currentDXInfo = "";
    bool doQSOAudioRecording = true;
    std::vector<StatusReporter *> statusSeporters;

    std::shared_ptr<SubWaterfall> audioWaterfall;
    std::shared_ptr<MobileMainWindowPrivate> pvt;
    dsp::stream<dsp::stereo_t> *currentAudioStream = nullptr;
    int currentAudioStreamSampleRate = 0;
    std::string currentAudioStreamName = "";
    bool drawAudioWaterfall = false;
    EventHandler<ImGuiContext*> displayDrawHandler;
    int cwAudioFrequency = 600;
    int cwWPM = 18;
    int txOffset = 0;       // for digital modes / audio player
    int txFrequencyOverride = 0;       // for digital modes / audio player
    int txBandwidthOverride = 0;       // for digital modes / audio player
    std::string txSubmodeOverride;       // for digital modes / audio player
    bool audioIsIqData = false;     // for audio player, affects the formation of tx iq data

    enum {
        VIEW_DEFAULT = 1,
        VIEW_QSO = 2,
        VIEW_CONFIG = 3
    } qsoMode = VIEW_DEFAULT, prevMode = VIEW_CONFIG;       // different ui
    bool shouldInitialize = true;
    bool txStateByButton = false; // true on short press of tx
    std::string currentDX; // callsign of current dx
    std::vector<std::string> modes = { "SSB", "CW", "FM", "AM", "DIGI" };
    std::map<std::string, std::vector<std::string>> subModes = { { "SSB", { "LSB", "USB" } }, { "FM", { "WFM", "NFM" } }, { "AM", { "AM", "DSB" } }, { "CW", { "CW" /*, "CWU", "CWL"*/ } }, { "DIGI", { "FT8", "FT4", "OLIVIA", "PSK31", "SSTV" } } };
    std::vector<std::string> bands = { "MW", "LW", "160M", "80M", "60M", "40M", "30M", "20M", "17M", "15M", "12M", "10M", "2M" };
    std::map<std::string, std::pair<int, int>> bandsLimits = {
        {"MW", {527000, 160000}},
        {"LW", {148000, 283000}},
        {"160M", {1800000, 2000000}},
        {"80M", {3500,3800}},
        {"60M",{5351000, 5367000}},
        {"40M", {7000000, 7200000}},
        {"30M",{10100000, 10150000}},
        {"20M",{14000000, 14350000}},
        {"17M",{18068000, 18168000}},
        {"15M",{21000000, 21350000}},
        {"12M",{24890000, 24990000}},
        {"10M",{28000000, 29000000}},
        {"2M",{144000000, 146000000}}
    };
    std::vector<std::string> ssbBandwidths = { "2.0", "2.2", "2.5", "2.7", "2.8", "3.0", "3.2", "3.5" };
    std::vector<std::string> cwBandwidths = { "50", "100", "150", "200", "300" };
    std::vector<std::string> fmBandwidths = { "6", "12" };
    std::vector<std::string> amBandwidths = { "6", "7", "8", "9", "10", "11", "12" };
    std::map<std::string, float> frequencyDefaults = {
        { "MW", 630 },
        { "LW", 125 },
        { "160M", 1888 },
        { "160M_CW", 1800 },
        { "160M_FT8", 1840 },
        { "160M_PSK31", 1838 },
        { "160M_OLIVIA", 1808.75 },
        { "160M_SSTV", 1890 },
        { "80M", 3650 },
        { "80M_CW", 3600 },
        { "80M_FT8", 3573 },
        { "80M_FT4", 3568 },
        { "80M_PSK31", 3580 },
        { "80M_OLIVIA", 3577.75 },
        { "80M_SSTV", 3730 },
        { "60M", 5357 },
        { "60M_OLIVIA", 5366.5 },
        { "40M", 7100 },
        { "40M_CW", 7000 },
        { "40M_FT8", 7074 },
        { "40M_FT4", 7047 },
        { "40M_PSK31", 7040 },
        { "40M_OLIVIA", 7043.25 },
        { "40M_SSTV", 7033 },
        { "30M", 10100 },
        { "30M_CW", 10100 },
        { "30M_FT8", 10136 },
        { "30M_FT4", 10140 },
        { "30M_PSK31", 10142 },
        { "30M_SSTV", 10132 },
        { "30M_OLIVIA", 10142.25 },
        { "20M", 14200 },
        { "20M_CW", 14000 },
        { "20M_FT8", 14074 },
        { "20M_PSK31", 14070 },
        { "20M_FT4", 14080 },
        { "20M_OLIVIA", 14076.4 },
        { "20M_SSTV", 14230 },
        { "17M", 18120 },
        { "17M_FT8", 18100 },
        { "17M_FT4", 18104 },
        { "17M_CW", 18068 },
        { "17M_PSK31", 18097 },
        { "17M_OLIVIA", 18103.4 },
        { "15M", 21250 },
        { "15M_CW", 21000 },
        { "15M_FT8", 21074 },
        { "15M_PSK31", 21080 },
        { "15M_FT4", 21140 },
        { "15M_OLIVIA", 21087.25 },
        { "15M_SSTV", 21340 },
        { "12M", 24960 },
        { "12M_FT8", 24915 },
        { "12M_FT4", 24919 },
        { "12M_PSK31", 24920 },
        { "12M_CW", 24890 },
        { "12M_OLIVIA", 24922.25 },
        { "12M_SSTV", 24975 },
        { "11M", 27500 },
        { "11M_FM", 27100 },
        { "11M_SSB", 27185 },
        { "10M", 28530 },
        { "10M_CW", 28000 },
        { "10M_FT8", 28074 },
        { "10M_FT4", 28180 },
        { "10M_PSK31", 28120 },
        { "10M_OLIVIA", 28076.75 },
        { "10M_SSTV", 28680 },
        { "2M", 145000 },
        { "2M_FM", 145100 },
        { "2M_SSB", 144100 },
        { "2M_FT8", 144174 },
        { "2M_FT4", 144120 },
        { "2M_PSK31", 144144 },
        { "2M_OLIVIA", 144136.25 },
        { "2M_SSTV", 144550 }
    };

    MobileMainWindow();


    void draw() override;
    void init() override;
    void end() override;
    std::string getCurrentMode();
    void setCurrentMode(std::string);
    std::string getCurrentBand();
    void selectCurrentBand(const std::string &band, int leavingFrequency);
    std::string getCurrentModeAttr(const std::string& key);
    void setCurrentModeAttr(const std::string& key, std::string val);
    void updateFrequencyAfterChange();
    void updateSubmodeAfterChange();
    const std::string &getBand(int frequency);
    void leaveBandOrMode(int leavingFrequency);
    void selectSSBModeForBand(const std::string& band);
    void updateAudioWaterfallPipeline();
    void updateDXInfo();

    void setBothGains(unsigned char gain);


    ImVec2 logbookPopupPosition = ImVec2(0, 0);
    ImVec2 scanSWRPopupPosition = ImVec2(0, 0);
    void logbookEntryPopup(int frequency);
    void logbookDetailsPopup();
    void scanSWRPopup(ImGui::WaterfallVFO *vfo);

    void maybeTransmit(std::shared_ptr<std::vector<dsp::stereo_t>> sharedPtr, std::function<void()> txStart, std::function<void()> txEnd);

    bool canTransmit();

    int getLowPass();
    void setLowPass(int lowPass);

    void setCurrentModeBySubmode(std::string submode);

    void updateModeFromRadio(int radioDemodId);

    bool stopTx();

    void registerStatusReporter(StatusReporter *rep) {
        statusSeporters.emplace_back(rep);
    }
    void unregisterStatusReporter(StatusReporter *rep) {
        auto iter = std::remove(statusSeporters.begin(), statusSeporters.end(), rep);
        statusSeporters.erase(iter, statusSeporters.end());
    }

    bool getIqDataInAudio();
    void setIqDataInAudio(bool);

    void maybeAddBookmark(std::string dx, double frequency, std::string submode, double bandwidth, bool worked);

    std::string maybeFindBookmark(double d, std::string basicString, double d1);
    void softTunePressed(ImGui::WaterfallVFO *vfo);
};
