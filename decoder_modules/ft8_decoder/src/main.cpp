#define IMGUI_DEFINE_MATH_OPERATORS
#define _USE_MATH_DEFINES
#include <imgui.h>
#include <imgui.h>
#include <config.h>
#include <core.h>
#include <gui/style.h>
#include <gui/gui.h>
#include <gui/widgets/waterfall.h>
#include <signal_path/signal_path.h>
#include <module.h>
#include <filesystem>
#include <dsp/stream.h>
#include <dsp/types.h>
#include <gui/widgets/folder_select.h>
#include <fstream>
#include <chrono>
#include <future>
#include <unordered_map>
#include <unordered_set>
#include "ft8_decoder.h"
#include "../../radio/src/demodulators/usb.h"
#include <utils/kmeans.h>
#include <utils/cty.h>
#include "module_interface.h"
#include "ft8_etc/gen_ft8.h"


using namespace utils;

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "ft8_decoder",
    /* Description:     */ "FT8 Decoder for SDR++",
    /* Author:          */ "FT8 fathers and then I added few lines",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ -1
};

ConfigManager config;

#define INPUT_SAMPLE_RATE 14400

enum DecodedMode {
    DM_FT8 = 1,
    DM_FT4 = 2
};

static ImVec2 baseTextSize; // 6 wide chars


struct CallHashCache {

    struct CallWithTS {
        std::string call;
        int hash10;
        int hash12;
        int hash22;
        long long lastseen;
    };

    std::vector<CallWithTS> calls;

    long long lastctm = 0;

    void addCall(std::string call, long long ctm) {     // call without < .. >
        if (call.length() < 3) {        // CQ, DE etc
            return;
        }
        call = trim(call);
        if (call.find(' ') != std::string::npos) {  // CQ DX etc
            return;
        }
        if (call.find('<') != std::string::npos) {  // <1:2983> or <...>
            return;
        }
        call = normcall(call);
        for (auto &c : calls) {
            if (c.call == call) {
                c.lastseen = ctm;
                return;
            }
        }
        calls.emplace_back(CallWithTS{call, ihashcall(call, 10), ihashcall(call, 12), ihashcall(call, 22), ctm});
        if (lastctm != ctm) {
            calls.erase(std::remove_if(calls.begin(), calls.end(), [ctm](const CallWithTS &c) { return ctm - c.lastseen > 180000; }), calls.end());
            lastctm = ctm;
        }
    }

    std::string findCall(const std::string &hashed, long long ctm) { // hashed: <1:2983> or <2:2983> or <0:3498>
        if (hashed.size() == 0) {
            return "";
        }
        if (hashed.size() < 3) {
            return hashed;
        }
        if (hashed.front() == '<' && hashed.back() == '>' && hashed[2] == ':' && hashed[1] >= '1' && hashed[1] <= '3') {
            int hash = std::stoi(hashed.substr(3, hashed.size()-4));
            int mode = hashed[1] - '0';
            for (auto &c : calls) {
                if (mode == 0 && c.hash10 == hash) {
                    c.lastseen = ctm;
                    return trim(c.call);
                }
                if (mode == 1 && c.hash12 == hash) {
                    c.lastseen = ctm;
                    return trim(c.call);
                }
                if (mode == 2 && c.hash22 == hash) {
                    c.lastseen = ctm;
                    return trim(c.call);
                }
            }
        }
        return hashed; // not found
    }

    std::string trim(std::string in) {
        while (in.size() > 0 && in[0] == ' ') {
            in.erase(0, 1);
        }
        while (in.size() > 0 && in[in.size()-1] == ' ') {
            in.erase(in.end() - 1);
        }
        return in;
    }

    std::string normcall(std::string call) {
        while(call.size() > 0 && call[0] == ' ')
            call.erase(0, 1);
        while(call.size() > 0 && call[call.size()-1] == ' ')
            call.erase(call.end() - 1);
        while(call.size() < 11)
            call += " ";
        return call;
    }


    // via https://github.com/rtmrtmrtmrtm/ft8mon/blob/master/unpack.cc
    int ihashcall(const std::string &call, int m)
    {
        const char *chars = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ/";

        unsigned long long x = 0;
        for(int i = 0; i < 11; i++){
            int c = call[i];
            const char *p = strchr(chars, c);
            assert(p);
            int j = p - chars;
            x = 38*x + j;
        }
        x = x * 47055833459LL;
        x = x >> (64 - m);

        return x;
    }
};

bool removeFiles = true;

struct DecodedResult {
    DecodedMode mode;
    long long decodeEndTimestamp;
    long long frequency;
    std::string decodedBlock;       // in ALL.TXT format
    std::string frequencyBand;       // in MHZ float string
    std::string shortString;
    // above is unique key

    DecodedResult(DecodedMode mode, long long int decodeEndTimestamp, long long int frequency, const std::string& shortString, const std::string& detailedString);

    bool operator == (const DecodedResult&another) const {
        return mode == another.mode && shortString == another.shortString;
    }

    std::string detailedString;
    double strength; // normalized 0..1.0
    double strengthRaw = 0;

    // below, zero means not layed out.
    double width = -1;
    double height = -1;
    double group = 0;
    double intensity = 0;
    double distance = 0;
    std::string qth = "";
    long long addedTime = 0;

    // this is to save between drawables re-creation
    ImVec2 current; // on the waterfall
    int stepsToGo;
    int totalSteps;

};


struct DrawableDecodedResult {
    virtual long long getDecodeEndTimestamp() = 0;
    virtual long long getFrequency() = 0;
    ImVec2 layout;
    const char *info;
    virtual void draw(const ImVec2& origin, ImGuiWindow* pWindow) = 0;
    static constexpr int TOTAL_STEPS = 4 * 60;
    virtual void planTo(ImVec2 n) {
        layout = n;
    }
};

struct FT8DrawableDecodedResult : DrawableDecodedResult {

    DecodedResult* result;
    FT8DrawableDecodedResult(DecodedResult* result);
    long long int getDecodeEndTimestamp() override;
    long long int getFrequency() override;
    void draw(const ImVec2& origin, ImGuiWindow* pWindow) override;
};

struct FT8DrawableDecodedDistRange : DrawableDecodedResult {
    std::string extraText;

    static std::unordered_map<std::string, ImVec2> lastCoords; // by title

    long long freq;
    explicit FT8DrawableDecodedDistRange(const std::string& extraText, long long freq);

    long long int getDecodeEndTimestamp() override;
    long long int getFrequency() override;
    void draw(const ImVec2& origin, ImGuiWindow* pWindow) override;
};

std::unordered_map<std::string, ImVec2> FT8DrawableDecodedDistRange::lastCoords;

struct KMeansDR {
    double distance;
    int group = 0;
    int drIndex;
    double _kmc;

    KMeansDR(double distance, int drIndex) : distance(distance), drIndex(drIndex), _kmc(log10(distance)) {}

    double kmeansDistanceTo(KMeansDR *another) const {
        return abs(kmeansCoord()-another->kmeansCoord());
    }

    double kmeansCoord() const {
        return _kmc;
    }

    void setKmeansCoord(double coord) {
        _kmc = coord;
    }

};

static const int VFO_SAMPLE_RATE = 24000;
static const int INVALID_OFFSET = 0x7FFFFFFF;
static const int USB_BANDWIDTH = 3000;

static int rangeContainsInclusive(double largeRangeStart, double largeRangeEnd, double smallRangeStart, double smallRangeEnd) {
    if (largeRangeStart <= smallRangeStart && largeRangeEnd >= smallRangeEnd) {
        return 1;
    }
    return 0;
}

static std::pair<int,int> calculateVFOCenterOffset(const std::vector<int> &frequencies, double centerFrequency, double ifBandwidth) {
    int rangeStart = centerFrequency - ifBandwidth/2;
    int rangeEnd = centerFrequency + ifBandwidth/2;
    for (auto q = std::begin(frequencies); q != std::end(frequencies); q++) {
        auto center = (*q + USB_BANDWIDTH);
        if (rangeContainsInclusive(rangeStart, rangeEnd, (double)(center - USB_BANDWIDTH), (double(center + USB_BANDWIDTH)))) {
            return std::make_pair(center - centerFrequency, center);
        }
    }
    return std::make_pair(INVALID_OFFSET, INVALID_OFFSET);
}




class FT8DecoderModule;
static std::vector<int> ft8Frequencies = { 1840000, 3573000, 5357000, 7074000, 10136000, 14074000, 18100000, 21074000, 24915000, 28074000, 50323000, 144174000, 222065000, 432065000  };

struct SingleDecoder {
    FT8DecoderModule *mod;
    std::vector<dsp::stereo_t> reader;
    double ADJUST_PERIOD = 0.1; // seconds before adjusting the vfo offset after user changed the center freq
    int beforeAdjust = (int)(VFO_SAMPLE_RATE * ADJUST_PERIOD);
    int previousCenterOffset = 0;
    bool onTheFrequency = false;
    dsp::stream<dsp::complex_t> iqdata = "singledecoder.iqdata";
    std::atomic_bool running;
    bool processingEnabled = true;
    FT8DecoderModule *mod2;
    char decodeError[1000] = {0};

    void handleData(int rd, dsp::stereo_t* inputData);

    virtual void unbind() {
        usbDemod->stop();
        ifChain.stop();
        vfo->stop();
        sigpath::iqFrontEnd.unbindIQStream(&iqdata);
    }

    virtual void bind() {
        sigpath::iqFrontEnd.bindIQStream(&iqdata);
        vfo->start();
        ifChain.start();
        usbDemod->start();

    }

    virtual DecodedMode getModeDM() = 0;

    virtual std::string getModeString() = 0;

    dsp::chain<dsp::complex_t> ifChain;
    dsp::channel::RxVFO* vfo;
    ConfigManager usbDemodConfig;

    double prevBlockNumber = 0;
    std::shared_ptr<std::vector<dsp::stereo_t>> fullBlock;
    std::mutex processingBlockMutex;
    double vfoOffset = 0.0;
    std::atomic_int blockProcessorsRunning = 0;
    std::atomic_int lastDecodeTime0 = 0;
    std::atomic_int lastDecodeTime = 0;
    std::atomic_int lastDecodeCount = 0;
    std::atomic_int totalCallsignsDisplayed = 0;
    std::shared_ptr<demod::USB> usbDemod;
    CallHashCache callHashCache;
    std::mutex callHashCacheMutex;
    EventHandler<double> iqSampleRateListener;
    EventHandler<bool> onPlayStateChange;


    virtual void init(const std::string &name);
    virtual void destroy();
    virtual double getBlockDuration() = 0;

    void handleIFData(const std::vector<dsp::stereo_t>& data);

    void startBlockProcessing(const std::shared_ptr<std::vector<dsp::stereo_t>>& block, int blockNumber, int originalOffset);

    virtual std::pair<int,int> calculateVFOCenterOffsetForMode(double centerFrequency, double ifBandwidth) = 0;

};

struct SingleFT8Decoder : SingleDecoder {


    double getBlockDuration() override {
        return 15;
    }

    std::string getModeString() override {
        return "ft8";
    }
    
    DecodedMode getModeDM() override {
        return DM_FT8;
    }
    

    std::pair<int,int> calculateVFOCenterOffsetForMode(double centerFrequency, double ifBandwidth) override {
        return calculateVFOCenterOffset(ft8Frequencies, centerFrequency, ifBandwidth);
    }
};

const std::vector<int> wsprFrequencies = { 136000, 474200, 1836600, 3568600, 7038600, 10138700, 14095600, 21094600, 24924600, 28124600, 50293000, 70091000, 144489000, 129650000, 181046000, 249246000, 281246000, 432300000 };

struct SingleFT4Decoder : SingleDecoder {

    SingleFT4Decoder() {
        flog::info("FT4 decoder created");
    }

    double getBlockDuration() override {
        return 15/2.0;
    }

    DecodedMode getModeDM() override {
        return DM_FT4;
    }


    std::string getModeString() override {
        return "ft4";
    }
    std::pair<int,int> calculateVFOCenterOffsetForMode(double centerFrequency, double ifBandwidth) override {
        static std::vector<int> frequencies = { 7047500, 10140000, 14080000, 18104000, 21140000, 28180000 };
        return calculateVFOCenterOffset(frequencies, centerFrequency, ifBandwidth);
    }


};

class FT8DecoderModule : public ModuleManager::Instance, public FT8ModuleInterface {

    SingleFT8Decoder ft8decoder;
    SingleFT4Decoder ft4decoder;

    std::vector<SingleDecoder *>allDecoders = { &ft8decoder, &ft4decoder };

    //    const int CAPTURE_SAMPLE_RATE = 12000;

public:

    std::vector<DecodedResult>  decodedResults;
    std::vector<std::shared_ptr<DrawableDecodedResult>>  decodedResultsDrawables;
    std::mutex decodedResultsLock;

    bool isDefaultCallsign(const std::string &callsign) override {
        return false;
    }

    int getCurrentFrequency(const std::string &mode) override {
        if (mode == "FT8") {
            auto [newOffset, centerOffset] = calculateVFOCenterOffset(ft8Frequencies, gui::waterfall.getCenterFrequency(), gui::waterfall.getBandwidth());
            if (centerOffset == INVALID_OFFSET) {
                return INVALID_OFFSET;
            }
            return centerOffset - USB_BANDWIDTH;
        } else if (mode == "WSPR") {
            auto [newOffset, centerOffset] = calculateVFOCenterOffset(wsprFrequencies, gui::waterfall.getCenterFrequency(), gui::waterfall.getBandwidth());
            if (centerOffset == INVALID_OFFSET) {
                return INVALID_OFFSET;
            }
            return centerOffset - USB_BANDWIDTH;
        } else if (mode == "FT4") {
            if (ft4decoder.previousCenterOffset == INVALID_OFFSET) {
                return INVALID_OFFSET;
            }
            return ft4decoder.previousCenterOffset-USB_BANDWIDTH;
        }
        throw std::runtime_error("Unknown mode");
    }

    std::pair<std::vector<dsp::stereo_t>, std::string> encodeCQ_FT8(const std::string &callsign, const std::string &grid, int frequency) override {
        GenFt8 gen(false);
        std::vector<short> wave(48000*15*2, 0.0);
        auto msg = "CQ "+callsign+" "+grid;
        auto length = gen.genft8(msg, wave.data(), 48000, frequency);
        if (gen.s_unpack_msg != msg) {
            msg = "CQ "+callsign;
            length = gen.genft8(msg, wave.data(), 48000, frequency);
            if (gen.s_unpack_msg != msg) {
                throw std::runtime_error("Failed to generate CQ message");
            }
        }
        flog::info("Length={}", length);
        auto rv = std::vector<dsp::stereo_t>();
        for(int q=0; q<length; q++) {
            rv.emplace_back(dsp::stereo_t{wave[q]/32767.0f, wave[q]/32767.0f});
        }
        return std::make_pair(rv, *gen.s_unpack_msg.str);
    }

    void addDecodedResult(const DecodedResult& incoming) {
        std::lock_guard g(decodedResultsLock);
        if (this->enableAllTXT) {
            FILE *f = fopen(this->allTxtPath, "at");
            if (f) {
                char modeString[10];
                switch(incoming.mode) {
                case DM_FT8:
                    snprintf(modeString, sizeof modeString, "FT8");
                    break;
                case DM_FT4:
                    snprintf(modeString, sizeof modeString, "FT4");
                    break;
                default:
                    snprintf(modeString, sizeof modeString, "???");
                    break;
                }
                auto band = atof(incoming.frequencyBand.c_str());   // band in mhz
                fprintf(f, "%s%10s Rx %s%7d  0.0%5d %s\n",
                        incoming.decodedBlock.c_str(),
                        incoming.frequencyBand.c_str(),
                        modeString,
                        (int)incoming.strengthRaw,
                        (int)incoming.frequency + USB_BANDWIDTH/2,
                        incoming.detailedString.c_str());
                fclose(f);
                this->allTxtPathError = "";
            } else {
                this->allTxtPathError = "can't write file";
            }
        }
        for(auto & scan : decodedResults) {
            if(scan == incoming) {
                scan.detailedString = incoming.detailedString;
                scan.decodeEndTimestamp = incoming.decodeEndTimestamp;
                scan.strengthRaw = incoming.strengthRaw;
                scan.strength = incoming.strength;
                return;
            }
        }
        decodedResults.push_back(incoming);
        decodedResults.back().current.x = -1;
        decodedResults.back().current.y = -1;
        decodedResultsDrawables.clear();
    }

    void clearDecodedResults(DecodedMode mode) {
        std::lock_guard g(decodedResultsLock);
        decodedResults.erase(std::remove_if(decodedResults.begin(), decodedResults.end(), [mode](const DecodedResult& x) { return x.mode == mode; }), decodedResults.end());
        decodedResultsDrawables.clear();
    }


    void drawDecodedResults(const ImGui::WaterFall::WaterfallDrawArgs &args) {

//        auto ctm = currentTimeMillis();
        decodedResultsLock.lock();
//        auto wfHeight = args.wfMax.y - args.wfMin.y;
//        auto wfWidth = args.wfMax.x - args.wfMin.x;
//        double timePerLine = 1000.0 / sigpath::iqFrontEnd.getFFTRate();
        auto currentTime = sigpath::iqFrontEnd.getCurrentStreamTime();



        // delete obsolete ones, and detect relayout
        for(int i=0; i<decodedResults.size(); i++) {
            auto& result = decodedResults[i];
            auto resultTimeDelta = currentTime - result.decodeEndTimestamp;
            if (resultTimeDelta > (2*secondsToKeepResults)*1000 + 5000) { // this deletes anyway when new results came.
                decodedResults.erase(decodedResults.begin() + i);
                decodedResultsDrawables.clear();
                i--;
                continue;
            }
        }
        if (baseTextSize.y == 0) {
            ImGui::PushFont(style::baseFont);
            baseTextSize = ImGui::CalcTextSize("WW6WWW");
            ImGui::PopFont();
        }
        ImGui::PushFont(style::tinyFont);
        auto modeSize = ImGui::CalcTextSize("ft8");
        ImGui::PopFont();


        if (!decodedResults.empty() && decodedResultsDrawables.empty()) {

            for(int i=0; i<decodedResults.size(); i++) {                // this clear obsolete as soon as new batch arrives, not earlier (not in the middle of cycle).
                auto& result = decodedResults[i];
                auto resultTimeDelta = currentTime - result.decodeEndTimestamp;
                if (resultTimeDelta > secondsToKeepResults*1000 + 5000) {
                    decodedResults.erase(decodedResults.begin() + i);
                    i--;
                    continue;
                }
            }

            static int distances[] = {1000, 2000, 3000, 4000, 5000, 6000, 8000, 10000, 13000, 15000, 18000};
            static auto getGroup = [](int distance) -> int {
                for(int i=0; i<sizeof(distances)/sizeof(distances[0]); i++) {
                    if (distance < distances[i]) {
                        return i;
                    }
                }
                return sizeof(distances)/sizeof(distances[0])-1; // last one
            };
            for(auto & decodedResult : decodedResults) {
                decodedResult.group = getGroup(decodedResult.distance);
            }
            auto ngroups = sizeof(distances)/sizeof(distances[0]);
            const auto MAXGROUPS = ngroups;
            std::vector<int> groupDists;
            std::vector<bool> foundGroups;
            std::vector<int> groupSortable;
            groupDists.resize(ngroups);
            groupSortable.resize(ngroups);
            foundGroups.resize(ngroups, false);
            for(auto & decodedResult : decodedResults) {
                groupDists[decodedResult.group] = decodedResult.distance;
                foundGroups[decodedResult.group] = true;
            }

            for(int i=0; i<ngroups; i++) {
                groupSortable[i] = i;
            }
            std::sort(groupSortable.begin(), groupSortable.end(), [&groupDists](int a, int b) {
                return groupDists[a] > groupDists[b];
            });
            double scanY = 0;
            double scanX = 0;
            double maxColumnWidth = args.wfMax.x - args.wfMin.x - baseTextSize.x / 3 * 4; // "xxxxx" -> "12734 km"

            ft8decoder.totalCallsignsDisplayed = 0;
            ft4decoder.totalCallsignsDisplayed = 0;

            std::unordered_set<std::string> usedDistances;

            for(int i=0;i<ngroups; i++) {
                std::vector<DecodedResult*>insideGroup;
                for(auto & decodedResult : decodedResults) {
                    if (decodedResult.group == groupSortable[i]) {
                        insideGroup.push_back(&decodedResult);
                    }
                }
                std::sort(insideGroup.begin(), insideGroup.end(), [](DecodedResult *a, DecodedResult *b) {
                    if (a->mode == b->mode) {
                        return strcmp(a->shortString.c_str(), b->shortString.c_str()) < 0;
                    } else {
                        return (int)b->mode < (int)a->mode;
                    }
                });
                std::string lastAdded;
                double maxDistance = 0;
                for(auto & decodedResult : insideGroup) {
                    if (decodedResult->shortString != lastAdded) {
                        maxDistance = std::max<double>(maxDistance, decodedResult->distance);
                        lastAdded = decodedResult->shortString;
                        ImGui::PushFont(style::baseFont);
                        auto tisTextSize = ImGui::CalcTextSize(decodedResult->shortString.c_str());
                        ImGui::PopFont();
                        tisTextSize.x += modeSize.x;
                        if (scanX + tisTextSize.x > maxColumnWidth) {
                            scanX = 0;
                            scanY += baseTextSize.y;
                        }

                        auto fdr = std::make_shared<FT8DrawableDecodedResult>(decodedResult);
                        if (decodedResult->current.x < 0) { // never been yet, come from center
                            fdr->layout = (args.wfMax - args.wfMin) / 2;
                            decodedResult->current = fdr->layout;
                            int steps = fdr->TOTAL_STEPS;
                            if (decodedResult->distance < 5000) {
                                steps /= 2;
                            }
                            if (decodedResult->distance < 2500) {
                                steps /= 2;
                            }
                            if (decodedResult->distance < 1250) {
                                steps /= 2;
                            }
                            decodedResult->stepsToGo = steps;
                            decodedResult->totalSteps = steps;
                        }
                        fdr->planTo(ImVec2(scanX, scanY));
                        fdr->result->intensity = (1.0 / (MAXGROUPS - 1)) * i;
                        decodedResultsDrawables.emplace_back(fdr);
                        switch(decodedResult->mode) {
                        case DecodedMode::DM_FT8:
                                ft8decoder.totalCallsignsDisplayed++;
                                break;
                            case DecodedMode::DM_FT4:
                                ft4decoder.totalCallsignsDisplayed++;
                                break;
                        }
                        scanX += tisTextSize.x;
                    }

                }
                if (!insideGroup.empty()) {
                    auto freqq = (double)(*insideGroup.begin())->frequency;
                    auto label = "<= " + std::to_string((int)maxDistance) + " KM";
                    if (!getMyPos().isValid()) {
                        label = "=> setup your GRID SQUARE";
                    }
                    auto fdr2 = std::make_shared<FT8DrawableDecodedDistRange>(label, freqq);
                    auto &that = FT8DrawableDecodedDistRange::lastCoords[label];
                    fdr2->layout = that;
                    fdr2->planTo(ImVec2(scanX, scanY));

//                    flog::info("closing: {} {} {}", i, fdr2->layoutX, fdr2->layoutY);
                    decodedResultsDrawables.emplace_back(fdr2);
                    usedDistances.emplace(label);
                    scanX = 0;
                    scanY += baseTextSize.y + baseTextSize.y /2.5;
                }

            }

            for(auto it = FT8DrawableDecodedDistRange::lastCoords.begin(); it != FT8DrawableDecodedDistRange::lastCoords.end();it++) {
                if (usedDistances.find(it->first) == usedDistances.end()) {
                    it->second.x = args.wfMax.x - args.wfMin.x;
                    it->second.y = args.wfMax.y - args.wfMin.y;
                }
            }

            double maxy = 0;
            for (auto& result : decodedResultsDrawables) {
                //                mdf = std::min<long long>(result->getFrequency(), mdf);
                maxy = std::max<double>(maxy, result->layout.y);
            }
            for (auto& result : decodedResultsDrawables) {
                result->layout.y += (args.wfMax.y - args.wfMin.y) - maxy - baseTextSize.y;
            }

        }



        std::vector<ImRect> rects;

        // place new ones

        if (!decodedResultsDrawables.empty()) {
//            auto mdf = decodedResultsDrawables[0]->getFrequency();
            for (int i = 0; i < decodedResultsDrawables.size(); i++) {
                auto& result = decodedResultsDrawables[i];

//                auto df = mdf - gui::waterfall.getCenterFrequency();
//                auto dx = (wfWidth / 2) + df / (gui::waterfall.getViewBandwidth() / 2) * (wfWidth / 2); // in pixels, on waterfall

//                auto drawX = 0;
//                auto drawY = 20;
                result->draw(args.wfMin, args.window);
            }
        }

        decodedResultsLock.unlock();
    }

    EventHandler<ImGui::WaterFall::WaterfallDrawArgs> afterWaterfallDrawListener;
    EventHandler<ImGuiContext *> debugDrawHandler;


    void *getInterface(const char *name) override {
        if (!strcmp(name, "FT8ModuleInterface")) {
            return dynamic_cast<FT8ModuleInterface *>(this);
        }
        return Instance::getInterface(name);
    }

    FT8DecoderModule(std::string name) {
        this->name = name;
        ft8decoder.mod = ft8decoder.mod2 = this;
        ft4decoder.mod = ft4decoder.mod2 = this;
        gui::waterfall.afterWaterfallDraw.bindHandler(&afterWaterfallDrawListener);
        afterWaterfallDrawListener.ctx = this;
        afterWaterfallDrawListener.handler = [](ImGui::WaterFall::WaterfallDrawArgs args, void* ctx) {
            ((FT8DecoderModule*)ctx)->drawDecodedResults(args);
        };
        decodedResults.reserve(2000);  // to keep addresses constant.

        //        mshv_init();

        // Load config
        config.acquire();
        if (config.conf[name].find("allTxtPath") != config.conf[name].end()) {
            auto qq = config.conf[name]["allTxtPath"].get<std::string>();
            strcpy(allTxtPath, qq.data());
        } else {
            allTxtPath[0] = 0;
        }
        if (config.conf[name].find("secondsToKeepResults") != config.conf[name].end()) {
            secondsToKeepResults = config.conf[name]["secondsToKeepResults"];
        }
        if (config.conf[name].find("nthreads") != config.conf[name].end()) {
            nthreads = config.conf[name]["nthreads"];
        }
        if (config.conf[name].contains("processingEnabledFT8")) {
            ft8decoder.processingEnabled = config.conf[name]["processingEnabledFT8"].get<bool>();
        }
        if (config.conf[name].find("processingEnabledFT4") != config.conf[name].end()) {
            ft4decoder.processingEnabled = config.conf[name]["processingEnabledFT4"].get<bool>();
        }
        if (config.conf[name].find("enablePSKReporter") != config.conf[name].end()) {
            enablePSKReporter = config.conf[name]["enablePSKReporter"].get<bool>();
        }
        if (config.conf[name].find("enableALLTXT") != config.conf[name].end()) {
            enableAllTXT = config.conf[name]["enableALLTXT"].get<bool>();
        }
        config.release(true);

        gui::menu.registerEntry(name, menuHandler, this, this);

        std::for_each(allDecoders.begin(), allDecoders.end(), [&](SingleDecoder* d) {
            d->init(name);
        });

        gui::mainWindow.onDebugDraw.bindHandler(&debugDrawHandler);
        debugDrawHandler.ctx = this;
        debugDrawHandler.handler = [](ImGuiContext *gctx, void* ctx) {
            FT8DecoderModule* _this = (FT8DecoderModule*)ctx;
            _this->drawDebugMenu(gctx);
        };


        enable();
    }

    void drawDebugMenu(ImGuiContext *gctx) {
        ImGui::LeftLabel("Remove FT8 WAVs");
        ImGui::FillWidth();
        ImGui::Checkbox("##keep_ft8_wavs", &removeFiles);
        ImGui::Text("ft8en %d onfreq %d", allDecoders[0]->mod->enabled, allDecoders[0]->onTheFrequency);
    }



    ~FT8DecoderModule() {
        disable();
        gui::waterfall.afterWaterfallDraw.unbindHandler(&afterWaterfallDrawListener);
        std::for_each(allDecoders.begin(), allDecoders.end(), [&](SingleDecoder* d) {
            d->destroy();
        });

        gui::menu.removeEntry(name);
    }

    void postInit() override {}

    void enable() override  {
        if (!enabled) {
            std::for_each(allDecoders.begin(), allDecoders.end(), [](auto& d) { d->bind(); });
            enabled = true;
            flog::info("FT8 Decoder enabled");
        }
    }

    void disable() override {

        if (enabled) {
            std::for_each(allDecoders.begin(), allDecoders.end(), [](auto& d) { d->unbind(); });
            enabled = false;
            flog::info("FT8 Decoder disabled");
        }
    }

    bool isEnabled() override {
        return enabled;
    }

    static void menuHandler(void* ctx) {
        FT8DecoderModule* _this = (FT8DecoderModule*)ctx;
        ImGui::Text("My Loc: ");
        ImGui::SameLine();
        auto poss = _this->getMyPos();
        if (poss.isValid()) {
            ImGui::Text("%+02.5f %+02.5f", poss.lat, poss.lon);
        } else {
            ImGui::Text("Invalid");
        }
        ImGui::LeftLabel("Keep results (sec)");
        ImGui::FillWidth();
        if (ImGui::SliderInt("##ft8_keep_results_sec", &_this->secondsToKeepResults, 15, 300, "%d", 0)) {
            config.acquire();
            config.conf[_this->name]["secondsToKeepResults"] = _this->secondsToKeepResults;
            config.release(true);
        }
        //
        // FT8
        //
        ImGui::FillWidth();
        if (ImGui::SliderInt("##ft8_threads", &_this->nthreads, 1, 6, "%d threads decode", 0)) {
            config.acquire();
            config.conf[_this->name]["nthreads"] = _this->nthreads;
            config.release(true);
        }

        auto ft8processing = _this->ft8decoder.blockProcessorsRunning.load();
        if (ft8processing) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0, 1.0f));
        }
        ImGui::LeftLabel("Decode FT8");
        if (ft8processing) {
            ImGui::PopStyleColor();
        }
        if (ImGui::Checkbox(CONCAT("##_processing_enabled_ft8_", _this->name), &_this->ft8decoder.processingEnabled)) {
            config.acquire();
            config.conf[_this->name]["processingEnabledFT8"] = _this->ft8decoder.processingEnabled;
            config.release(true);
            if (!_this->ft8decoder.processingEnabled) {
                _this->clearDecodedResults(_this->ft8decoder.getModeDM());
            }
        }
        ImGui::SameLine();
        std::string stat;
        if (_this->ft8decoder.onTheFrequency) {
            stat += "[+]";
        } else {
            stat += "[-]";
        }
        ImGui::Text("%s Count: %d(%d) in %d..%d msec", stat.c_str(), _this->ft8decoder.lastDecodeCount.load(), _this->ft8decoder.totalCallsignsDisplayed.load(), _this->ft8decoder.lastDecodeTime0.load(), _this->ft8decoder.lastDecodeTime.load());
        if (_this->ft4decoder.decodeError[0]) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0, 0, 1.0f));
            ImGui::Text("Error: %s", _this->ft4decoder.decodeError);
            ImGui::PopStyleColor();
        }
        //
        // FT4
        //
        auto ft4processing = _this->ft4decoder.blockProcessorsRunning.load();
        if (ft4processing) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0, 1.0f));
        }
        ImGui::LeftLabel("Decode FT4");
        if (ft4processing) {
            ImGui::PopStyleColor();
        }
        ImGui::FillWidth();
        if (ImGui::Checkbox(CONCAT("##_processing_enabled_ft4_", _this->name), &_this->ft4decoder.processingEnabled)) {
            config.acquire();
            config.conf[_this->name]["processingEnabledFT4"] = _this->ft4decoder.processingEnabled;
            config.release(true);
            if (!_this->ft4decoder.processingEnabled) {
                _this->clearDecodedResults(_this->ft4decoder.getModeDM());
            }
        }
        ImGui::SameLine();
        stat = "";
        if (_this->ft8decoder.onTheFrequency) {
            stat += "[+]";
        } else {
            stat += "[-]";
        }
        ImGui::Text("%s Count: %d(%d) in %d..%d msec", stat.c_str(), _this->ft4decoder.lastDecodeCount.load(), _this->ft4decoder.totalCallsignsDisplayed.load(), _this->ft4decoder.lastDecodeTime0.load(), _this->ft4decoder.lastDecodeTime.load());
        if (_this->ft4decoder.decodeError[0]) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0, 0, 1.0f));
            ImGui::Text("Error: %s", _this->ft4decoder.decodeError);
            ImGui::PopStyleColor();
        }
        //
        // PSK Reporter
        //
        ImGui::LeftLabel("PSKReporter");
        ImGui::BeginDisabled();
        if (ImGui::Checkbox(CONCAT("##_enable_psk_reporter_", _this->name), &_this->enablePSKReporter)) {
            config.acquire();
            config.conf[_this->name]["enablePSKReporter"] = _this->enablePSKReporter;
            config.release(true);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Text("using callsign:");
        ImGui::SameLine();
        ImGui::FillWidth();
        if (sigpath::iqFrontEnd.operatorCallsign == "") {
            ImGui::Text("[set up in source menu]");
        } else {
            ImGui::Text("%s", sigpath::iqFrontEnd.operatorCallsign.c_str());
        }
        ImGui::LeftLabel("ALL.TXT log");
        if (ImGui::Checkbox(CONCAT("##_enable_alltxt_", _this->name), &_this->enableAllTXT)) {
            config.acquire();
            config.conf[_this->name]["enableALLTXT"] = _this->enableAllTXT;
            config.release(true);
        }
        ImGui::SameLine();
        ImGui::Text("filename:");
        ImGui::SameLine();
        ImGui::FillWidth();
        if (ImGui::InputText(CONCAT("##_alltxt_fname_", _this->name), _this->allTxtPath, sizeof(_this->allTxtPath))) {
            config.acquire();
            config.conf[_this->name]["allTxtPath"] = std::string(_this->allTxtPath);
            config.release(true);
        }
        if (!_this->allTxtPathError.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0, 0, 1.0f));
            ImGui::Text("Error: %s", _this->allTxtPathError.c_str());
            ImGui::PopStyleColor();
        }

    }

    std::string name;
    bool enabled = false;
    char myGrid[10];
    char myCallsign[13];
    bool enablePSKReporter = true;
    bool enableAllTXT = false;
    char allTxtPath[1024];
    std::string allTxtPathError;
    int secondsToKeepResults = 120;
    int nthreads = 1;

    std::string  lastLocation;
    LatLng _myPos = LatLng::invalid();

    LatLng getMyPos() {
        if (sigpath::iqFrontEnd.operatorLocation != lastLocation) {
            lastLocation = sigpath::iqFrontEnd.operatorLocation;
            _myPos = utils::gridToLatLng(lastLocation);
        }
        return _myPos;
    }



    //    dsp::buffer::Reshaper<float> reshape;
    //    dsp::sink::Handler<float> diagHandler;
    //    dsp::multirate::RationalResampler<dsp::stereo_t> resamp;
    //

    std::chrono::time_point<std::chrono::high_resolution_clock> lastUpdated;
};


DecodedResult::DecodedResult(DecodedMode mode, long long int decodeEndTimestamp, long long int frequency, const std::string& shortString, const std::string& detailedString)
    : mode(mode), decodeEndTimestamp(decodeEndTimestamp), frequency(frequency), shortString(shortString), detailedString(detailedString) {

}
long long int FT8DrawableDecodedResult::getDecodeEndTimestamp() {
    return result->decodeEndTimestamp;
}
long long int FT8DrawableDecodedResult::getFrequency() {
    return result->frequency;
}

static int toColor(double coss) {
    if (coss < 0) {
        return 0;
    }
    return (int)(coss * 255);
}

void FT8DrawableDecodedResult::draw(const ImVec2& _origin, ImGuiWindow* window) {

    ImVec2 origin = _origin;

    auto lo = layout;
    if (this->result->stepsToGo <= 0) {
        // already at home
    } else {
        lo = this->result->current + (layout - this->result->current) * ((this->result->totalSteps - this->result->stepsToGo) / (float )this->result->totalSteps);
        this->result->stepsToGo--;
    }
    origin += lo;
    this->result->current = lo;


    double intensity = result->intensity;
    intensity = 0.1;  // rainbow could be tiring
    auto phase = intensity * 2 * M_PI;
    auto RR= toColor(cos((phase - M_PI / 4 - M_PI - M_PI / 4) / 2));
    auto GG= toColor(cos((phase - M_PI / 4 - M_PI / 2) / 2));
    auto BB = toColor(cos(phase - M_PI / 4));

    ImGui::PushFont(style::tinyFont);
    auto modeSize = ImGui::CalcTextSize("ft8");
    ImGui::PopFont();
    ImGui::PushFont(style::baseFont);
    const char* str = result->shortString.c_str();
    auto textSize = ImGui::CalcTextSize(str);
    ImGui::PopFont();

    ImU32 white = IM_COL32(255, 255, 255, 255);
    ImU32 black = IM_COL32(0, 0, 0, 255);

    //        const ImRect& drect = ImRect(layoutX, layoutY, layoutX + textSize.x, layoutY + textSize.y);
    ImGui::PushFont(style::tinyFont);
    switch(this->result->mode) {
    case DM_FT4:
        window->DrawList->AddRectFilled(origin, origin + ImVec2(modeSize.x, modeSize.y), IM_COL32(255, 0, 200, 255));
        window->DrawList->AddText(origin, white, "ft4");
        break;
    case DM_FT8:
        window->DrawList->AddRectFilled(origin, origin + ImVec2(modeSize.x, modeSize.y), IM_COL32(255, 0, 0, 255));
        window->DrawList->AddText(origin, white, "ft8");
        break;
    }
    ImGui::PopFont();

    origin.x += modeSize.x;

    auto maxCorner = origin + textSize - ImVec2(0, 2);
    window->DrawList->AddRectFilled(origin, maxCorner, IM_COL32(RR, GG, BB, 160));

    if (ImGui::IsMouseHoveringRect(origin, maxCorner) && !gui::mainWindow.showMenu) {
        char buf[128];
        ImGui::BeginTooltip();

        ImGui::Text("Distance: %0.0f km", result->distance);
        ImGui::Separator();
        ImGui::Text("Strength: %0.0f dB", result->strengthRaw);
        ImGui::Separator();
        ImGui::Text("QTH: %s", result->qth.c_str());
        ImGui::Separator();
        ImGui::Text("Message: %s", result->detailedString.c_str());

        ImGui::EndTooltip();
    }


    ImGui::PushFont(style::baseFont);
    window->DrawList->AddText(origin + ImVec2(-1, -1), black, str);
    window->DrawList->AddText(origin + ImVec2(-1, +1), black, str);
    window->DrawList->AddText(origin + ImVec2(+1, -1), black, str);
    window->DrawList->AddText(origin + ImVec2(+1, +1), black, str);
    window->DrawList->AddText(origin, white, str);
    ImGui::PopFont();
    float nsteps = 8.0;
    float h = 4.0;
    float yy = textSize.y - h;
    float xlimit = baseTextSize.x * 3 / 4;
    for(float x=0; x< xlimit; x+=xlimit/nsteps) {
        auto x0 = x;
        auto x1 = x + baseTextSize.x/nsteps;
        auto x0m = x0 + (0.8)*(x1-x0);
        bool green = (x / baseTextSize.x) < result->strength;
        if (!green) {
            break;
        }
        window->DrawList->AddRectFilled(ImVec2(x0, yy) + origin, ImVec2(x0m, yy+h) + origin, green ? IM_COL32(64, 255, 0, 255) : IM_COL32(180, 180, 180, 255));
//        window->DrawList->AddRectFilled(ImVec2(x0m, yy) + origin, ImVec2(x1, yy+h) + origin, IM_COL32(180, 180, 180, 255));
    }

}
FT8DrawableDecodedResult::FT8DrawableDecodedResult(DecodedResult* result) : result(result) {
    info = result->shortString.c_str();
}

FT8DrawableDecodedDistRange::FT8DrawableDecodedDistRange(const std::string& extraText, long long freq) : extraText(extraText), freq(freq) {
    info = "range";

}
long long int FT8DrawableDecodedDistRange::getDecodeEndTimestamp() {
    return 0;
}
long long int FT8DrawableDecodedDistRange::getFrequency() {
    return freq;
}
void FT8DrawableDecodedDistRange::draw(const ImVec2& _origin, ImGuiWindow* window) {
    ImVec2 origin = _origin;
    origin += layout;
    ImGui::PushFont(style::tinyFont);
    const char* str = this->extraText.c_str();
    auto strSize = ImGui::CalcTextSize(str);
    ImU32 white = IM_COL32(255, 255, 255, 255);
    window->DrawList->AddRectFilled(origin, origin + strSize, IM_COL32(0, 0, 0, 255));
    window->DrawList->AddText(origin, white, str);
    ImGui::PopFont();
}

void SingleDecoder::handleData(int rd, dsp::stereo_t* inputData) {
    beforeAdjust -= rd;
    if (beforeAdjust <= 0) {
        beforeAdjust = (int)(VFO_SAMPLE_RATE * ADJUST_PERIOD);
        auto [newOffset, centerOffset] = calculateVFOCenterOffsetForMode(gui::waterfall.getCenterFrequency(), gui::waterfall.getBandwidth());
        if (centerOffset != previousCenterOffset) {
            mod->clearDecodedResults(getModeDM());
            previousCenterOffset = centerOffset;
        }
        if (newOffset == INVALID_OFFSET) {
            if (onTheFrequency) {
                flog::info("ON FREQUENCY: FALSE ({}) {} {} -> {}", getModeString(), gui::waterfall.getCenterFrequency(), gui::waterfall.getBandwidth(), newOffset);
                onTheFrequency = false;
            }
        } else { // not invalid
            if (!onTheFrequency) {
                flog::info("ON FREQUENCY: TRUE ({}) {} {} -> {}", getModeString(), gui::waterfall.getCenterFrequency(), gui::waterfall.getBandwidth(), newOffset);
                onTheFrequency = true;
            }
            if (newOffset == (int)vfoOffset) {
                // do nothing
            }
            else {
                vfoOffset = newOffset;
                flog::info("FT8 vfo ({}): center offset {}, bandwidth: {}", getModeString(), vfoOffset, USB_BANDWIDTH);
                vfo->setOffset(vfoOffset - USB_BANDWIDTH / 2);
            }
        }
    }

    if (mod->enabled && onTheFrequency) {
        reader.resize(rd);
        std::copy(usbDemod->getOutput()->readBuf, usbDemod->getOutput()->readBuf + rd, reader.begin());
        handleIFData(reader);
    }
}
void SingleDecoder::startBlockProcessing(const std::shared_ptr<std::vector<dsp::stereo_t>>& block, int blockNumber, int originalOffset) {
    std::thread processor([=]() {
        SetThreadName(getModeString()+"_startBlockProcessing");
        std::time_t bst = (std::time_t)(blockNumber * getBlockDuration());
//        flog::info("Start processing block ({}), size={}, block time: {}", this->getModeString(), (int64_t)block->size(), std::asctime(std::gmtime(&bst)));
        blockProcessorsRunning.fetch_add(1);
        auto started = currentTimeMillis();
        std::unique_ptr<int, std::function<void(int*)>> myPtr(new int, [&](int* p) {
            delete p;
            auto prev = blockProcessorsRunning.fetch_add(-1);
//            flog::info("blockProcessorsRunning ({}) released after {} msec, prev={}", getModeString(), (int64_t)(currentTimeMillis() - started), prev);
        });
        int count = 0;
        long long time0 = 0;
        auto poss = mod->getMyPos();
        auto handler = [&](int mode, std::vector<std::string> result, std::atomic<const char *> &progress) {
            progress ="in-handler";
            if (result.size() == 2 && result[0] == "ERROR") {
                strcpy(decodeError, result[1].c_str());
            } else if (result.size() == 1 && result[0] == "DECODE_EOF") {
                //
            } else {
                strcpy(decodeError , "");
                if (time0 == 0) {
                    time0 = currentTimeMillis();
                }
                auto message = result[4];
                auto pipe = message.find('|');
                std::string callsigns;
                std::string callsign;
                if (pipe != std::string::npos) {
                    progress = "pipe1";
                    callsigns = message.substr(pipe + 1);
                    message = message.substr(0, pipe);
                    std::vector<std::string> callsignsV;
                    progress = "pipe2";
                    splitStringV(callsigns, ";", callsignsV);
                    progress = "pipe3";
                    if (callsignsV.size() > 1) {
                        progress = "pipe4";
                        callsign = callsignsV[1];
                        callHashCacheMutex.lock();
                        progress = "pipe5";
                        callHashCache.addCall(callsignsV[0], bst * 1000);
                        callHashCache.addCall(callsignsV[1], bst * 1000);
                        callHashCacheMutex.unlock();
                        progress = "pipe6";
                    }
                    if (!callsign.empty() && callsign[0] == '<') {
                        progress = "pipe7";
                        callHashCacheMutex.lock();
                        auto ncallsign = callHashCache.findCall(callsign, bst * 1000);
                        progress = "pipe8";
//                        flog::info("Found call: {} -> {}", callsign, ncallsign);
                        callsign = ncallsign;
                        callHashCacheMutex.unlock();
                        progress = "pipe9";
                    }
                }
                else {
                    progress = "pre-extract";
                    callsign = extractCallsignFromFT8(message);
                    progress = "post-extract";
                }
                count++;
                if (callsign.empty() || callsign.find('<') != std::string::npos) { // ignore <..> callsigns
                    return;
                }
                double distance = 0;
                CTY::Callsign cs;
                if (callsign.empty()) {
                    callsign = "?? " + message;
                }
                else {
                    progress = "pre-find";
                    cs = globalCty.findCallsign(callsign);
                    progress = "post-find";
                    if (poss.isValid()) {
                        auto bd = bearingDistance(poss, cs.ll);
                        distance = bd.distance;
                    }
                }
                progress = "pre-mkdecoded-result";
                auto frequencyInBand = atoi(result[3].c_str());

                if (message.find(callsign) == std::string::npos) {
                    // inject callsign into message
                    std::string newmsg;
                    std::vector<std::string> splitMessage;
                    splitStringV(message, " ", splitMessage);
                    for(auto &s : splitMessage) {
                        if (s[0] == '<' && s[s.size()-1] == '>') {
                            s = callsign;   // inject
                        }
                        newmsg += s + " ";
                    }
                    if (newmsg.size() > 0) {
                        newmsg.resize(newmsg.size() - 1);
                    }
                    message = newmsg;
                }
                DecodedResult decodedResult(getModeDM(), (long long)(blockNumber * getBlockDuration() * 1000 + getBlockDuration()), frequencyInBand, callsign, message);
                decodedResult.distance = (int)distance;
                auto strength = atof(result[1].c_str());
                strength = (strength + 24) / (24 + 24);
                if (strength < 0.0) strength = 0.0;
                if (strength > 1.0) strength = 1.0;
                decodedResult.strength = strength;
                decodedResult.strengthRaw = atof(result[1].c_str());
                decodedResult.intensity = 0;

                time_t blocktimeUnix = blockNumber * getBlockDuration();
                tm* ltm = std::gmtime(&blocktimeUnix);

                char buf[100];
                snprintf(buf, sizeof buf, "%02d%02d%02d_%02d%02d%02d", ltm->tm_year % 100, ltm->tm_mon + 1, ltm->tm_mday, ltm->tm_hour, ltm->tm_min, ltm->tm_sec);
                decodedResult.decodedBlock = buf;
                snprintf(buf, sizeof buf,  "%0.3f", (previousCenterOffset - USB_BANDWIDTH) / 1000000.0);
                decodedResult.frequencyBand = buf;

                // (random() % 100) / 100.0;
                if (!cs.dxccname.empty()) {
                    decodedResult.qth = cs.dxccname;
                }
                progress = "pre-add-result";
                mod->addDecodedResult(decodedResult);
                progress = "post-add-result";
            }
            return;
        };
        std::atomic<const char *> progress;
        progress = "Starting";
        std::thread t0([&]() {
            SetThreadName(getModeString()+"_callDecode");
            auto start = currentTimeMillis();
            dsp::ft8::decodeFT8(mod->nthreads, getModeString(), VFO_SAMPLE_RATE, block->data(), block->size(), handler, progress, removeFiles);
            auto end = currentTimeMillis();
            if (noisy_ft8) {
                flog::info("FT8 decoding ({}) took {} ms", this->getModeString(), (int64_t) (end - start));
            }
            lastDecodeCount = (int)count;
            lastDecodeTime = (int)(end - start);
            if (time0 == 0) {
                lastDecodeTime0 = 0;
            } else {
                lastDecodeTime0 = (int)(time0 - start);
            }
        });

        auto future = std::async(std::launch::async, &std::thread::join, &t0);
        int count0=0;
        while(true) {
            if (future.wait_for(std::chrono::seconds(1)) == std::future_status::timeout) {
//                flog::info("outside progress({}: decoding ({}) : {}", count0, this->getModeString(), progress.load());
                count0++;
            } else {
                break;
            }
        }

//        flog::info("blockProcessorsRunning ({}) gracefully completed {}", getModeString(), blockNumber);

    });
    processor.detach();
}

void SingleDecoder::init(const std::string &name) {
    vfo = new dsp::channel::RxVFO(&iqdata, sigpath::iqFrontEnd.getEffectiveSamplerate(), VFO_SAMPLE_RATE, USB_BANDWIDTH, vfoOffset);

    sigpath::iqFrontEnd.onEffectiveSampleRateChange.bindHandler(&iqSampleRateListener);
    iqSampleRateListener.ctx = this;
    iqSampleRateListener.handler = [](double newSampleRate, void* ctx) {
//        flog::info("FT8 decoder: effective sample rate changed to {}", newSampleRate);
        ((SingleDecoder*)ctx)->vfo->setInSamplerate(newSampleRate);
    };

    usbDemod = std::make_shared<demod::USB>();
    usbDemodConfig.acquire();
    usbDemodConfig.disableAutoSave();
    usbDemodConfig.conf[name]["USB"]["agcAttack"] = 0.0;
    usbDemodConfig.conf[name]["USB"]["agcDecay"] = 0.0;
    usbDemodConfig.release(true);
    usbDemod->init(name, &usbDemodConfig, ifChain.out, USB_BANDWIDTH, VFO_SAMPLE_RATE);
    //        usbDemod->setFrozen(true);
    ifChain.setInput(&vfo->out, [&](auto ifchainOut) {
        usbDemod->setInput(ifchainOut);
//        flog::info("ifchain change out");
        // next
    });

    gui::mainWindow.onPlayStateChange.bindHandler(&onPlayStateChange);
    onPlayStateChange.ctx = this;
    onPlayStateChange.handler = [](bool playing, void* ctx) {
        SingleDecoder* thiz = (SingleDecoder*)ctx;
        thiz->mod->clearDecodedResults(thiz->getModeDM());
    };
    running = true;



    std::thread reader([thiz=this]() {
        SetThreadName(thiz->getModeString()+"_ssb_reader");

//        auto _this = this;
        while (thiz->running.load()) {
            int rd = thiz->usbDemod->getOutput()->read();
            if (rd < 0) {
                break;
            }
            if (thiz->mod != thiz->mod2) {
                abort();
            }
            if (thiz->processingEnabled) {
                thiz->handleData(rd, thiz->usbDemod->getOutput()->readBuf);
            }
            thiz->usbDemod->getOutput()->flush();
        }
    });
    reader.detach();
}

void SingleDecoder::destroy() {
    sigpath::iqFrontEnd.onEffectiveSampleRateChange.unbindHandler(&iqSampleRateListener);
    gui::mainWindow.onPlayStateChange.unbindHandler(&onPlayStateChange);
    ifChain.out->stopReader();
    running = false;
}


MOD_EXPORT void _INIT_() {
    // Create default recording directory
    json def = json({});
    config.setPath(std::string(core::getRoot()) + "/ft8_decoder_config.json");
    config.load(def);
    config.enableAutoSave();
    mshv_init();

}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new FT8DecoderModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (FT8DecoderModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}


void SingleDecoder::handleIFData(const std::vector<dsp::stereo_t>& data) {
    long long int curtime = sigpath::iqFrontEnd.getCurrentStreamTime() ;
    double blockNumber = floor((curtime / 1000.0) / getBlockDuration());
    if (blockNumber != prevBlockNumber) {
        if (noisy_ft8) {
            flog::info("handleIFdata new block ({}) : {} ", getModeString(), blockNumber);
        }
        if (fullBlock) {
            bool shouldStartProcessing = true;
            std::shared_ptr<std::vector<dsp::stereo_t>> processingBlock;
            processingBlock = fullBlock;
            int currentlyRunning = blockProcessorsRunning.load();
            if (currentlyRunning > 0) {
                shouldStartProcessing = false;
                flog::info("blockProcessorsRunning ({}) not starting: {}", getModeString(), currentlyRunning);

            }
            if (shouldStartProcessing) {
                // no processing is done.
                if (processingBlock->size() / VFO_SAMPLE_RATE > getBlockDuration()+1 || processingBlock->size() / VFO_SAMPLE_RATE <= getBlockDuration()-2) {
                    flog::info("Block size ({}) is not matching: {}, curtime={}",
                               getModeString(),
                               (int64_t)(processingBlock->size() / VFO_SAMPLE_RATE),
                               (int64_t)curtime);
                    processingBlock.reset(); // clear for new one
                } else {
                    // block size is ok.
                    startBlockProcessing(processingBlock, prevBlockNumber, vfoOffset + gui::waterfall.getCenterFrequency());
                }
            }
            fullBlock.reset();
        }
        prevBlockNumber = blockNumber;
    }
    if (!fullBlock) {
        fullBlock = std::make_shared<std::vector<dsp::stereo_t>>();
        fullBlock->reserve((getBlockDuration() + 1) * VFO_SAMPLE_RATE);
    }
    fullBlock->insert(std::end(*fullBlock), std::begin(data), std::end(data));
    //        flog::info("{} Got {} samples: {}", blockNumber, data.size(), data[0].l);
}


#ifdef NEW_BUILTIN_MODE

extern void doDecode(const char *mode, const char *path, int threads, std::function<void(int mode, std::vector<std::string> result)> callback);
namespace dsp {
    namespace ft8 {
        void invokeDecoder(int nthreads, const std::string &mode, const std::string &wavPath, const std::string &outPath,
                           const std::string &errPath,
                           std::function<void(int mode, std::vector<std::string> result, std::atomic<const char *> &progress)> callback1,
                           std::atomic<const char *> &progress)
        {
            ::doDecode(mode.c_str(), wavPath.c_str(), nthreads, [&](int mode2, std::vector<std::string> result) {
                callback1(mode2, result, progress);
            });
        }

    }
}


#endif
