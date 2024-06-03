
#ifdef _WIN32
#define _WINSOCKAPI_ // stops windows.h including winsock.h
#endif
#define _USE_MATH_DEFINES

#include "gui/brown/kiwisdr_map.h"
#include "gui/brown/small_waterfall.h"

#include <imgui.h>
#include <cmath>
#include <module.h>
#include <gui/gui.h>
#include "utils/proto/reporter_services.h"
#include "utils/cty.h"
#include <core.h>
#include <signal_path/signal_path.h>
#include <config.h>
#include <ctm.h>
#include <gui/widgets/waterfall.h>
#include "../../decoder_modules/ft8_decoder/src/module_interface.h"
#include "utils/wav.h"
#include "gui/brown/imgui-notify/imgui_notify.h"
#include "dsp/taps/high_pass.h"

#define MAX_COMMAND_LENGTH 8192

SDRPP_MOD_INFO{
        /* Name:            */ "reports_monitor",
        /* Description:     */ "PSKreporter, WSPRnet, RBN",
        /* Author:          */ "San",
        /* Version:         */ 0, 1, 0,
        /* Max instances    */ -1
};

struct WSPREncoder {

    // via https://ganymedeham.blogspot.com/2015/04/arduino-wspr-symbol-generator.html  aka Ganymede M0IFA

    // WSPR_symbol_generator input coded, output on monitor
    // based on code from Martin Nawrath, Acedemy of Media Arts, Cologne

    const char SyncVec[162] = {
            1,1,0,0,0,0,0,0,1,0,0,0,1,1,1,0,0,0,1,0,0,1,0,1,1,1,1,0,0,0,0,0,0,0,1,0,0,1,0,1,0,0,0,0,0,0,1,0,
            1,1,0,0,1,1,0,1,0,0,0,1,1,0,1,0,0,0,0,1,1,0,1,0,1,0,1,0,1,0,0,1,0,0,1,0,1,1,0,0,0,1,1,0,1,0,1,0,
            0,0,1,0,0,0,0,0,1,0,0,1,0,0,1,1,1,0,1,1,0,0,1,1,0,1,0,0,0,1,1,1,0,0,0,0,0,1,0,1,0,0,1,1,0,0,0,0,
            0,0,0,1,1,0,1,0,1,1,0,0,0,1,1,0,0,0
    };

    unsigned long n1;    // encoded callsign
    unsigned long m1;    // encodes locator
    typedef unsigned char byte;

    byte c[11];                // encoded message
    byte sym[170];             // symbol table 162
    byte symt[170];            // symbol table temp

// put your data here
    char call[6];    // default values, 6 chars. 3rd numeric
    char locator[4];   // default value 4 chars
    byte power = 20;           // default value 2 numberic

    int ii,bb;



// normalize characters 0..9 A..Z Space in order 0..36
    char chr_normf(char bc )
    {
        char cc=36;

        if (bc >= '0' && bc <= '9') cc=bc-'0';
        if (bc >= 'A' && bc <= 'Z') cc=bc-'A'+10;
        if (bc == ' ' ) cc=36;

        return(cc);
    }

// encode call sign
    void encode_call()
    {
        unsigned long t1;

        n1=chr_normf(call[0]);
        n1=n1*36+chr_normf(call[1]);
        n1=n1*10+chr_normf(call[2]);
        n1=n1*27+chr_normf(call[3])-10;
        n1=n1*27+chr_normf(call[4])-10;
        n1=n1*27+chr_normf(call[5])-10;

        // merge coded callsign into message array c[]
        t1=n1;
        c[0]= t1 >> 20;
        t1=n1;
        c[1]= t1 >> 12;
        t1=n1;
        c[2]= t1 >> 4;
        t1=n1;
        c[3]= t1 << 4;
    }

// encode locator
    void encode_locator()
    {
        unsigned long t1;

        // coding of locator
        m1=179-10*(chr_normf(locator[0])-10)-chr_normf(locator[2]);
        m1=m1*180+10*(chr_normf(locator[1])-10)+chr_normf(locator[3]);
        m1=m1*128+power+64;

        // merge coded locator and power into message array c[]
        t1=m1;
        c[3]= c[3] + ( 0x0f & t1 >> 18);
        t1=m1;
        c[4]= t1 >> 10;
        t1=m1;
        c[5]= t1 >> 2;
        t1=m1;
        c[6]= t1 << 6;
    }

// calculate parity
    byte parity(unsigned long li)
    {
        byte po = 0;
        while(li != 0)
        {
            po++;
            li&= (li-1);
        }
        return (po & 1);
    }

    void encode_conv()
    {
        int bc=0;
        int cnt=0;
        int cc;
        unsigned long sh1=0;

        cc=c[0];

        for (int i=0; i < 81;i++)
        {
            if (i % 8 == 0 )
            {
                cc=c[bc];
                bc++;
            }
            if (cc & 0x80) sh1=sh1 | 1;

            symt[cnt++]=parity(sh1 & 0xF2D05351);
            symt[cnt++]=parity(sh1 & 0xE4613C47);

            cc=cc << 1;
            sh1=sh1 << 1;
        }
    }

    bool within(int a, int x1, int x2) {
        return a >= x1 && a <= x2;
    }


// interleave reorder the 162 data bits and and merge table with the sync vector
    void interleave_sync()
    {
        int ii,ij,b2,bis,ip;
        ip=0;

        for (ii=0;ii<=255;ii++)
        {
            bis=1;
            ij=0;

            for (b2=0;b2 < 8 ;b2++)
            {
                if (ii & bis) ij= ij | (0x80 >> b2);
                bis=bis << 1;
            }

            if (ij < 162 )
            {
                sym[ij]= SyncVec[ij] +2*symt[ip];
                ip++;
            }
        }
    }

// encode sequence
    void encode()
    {
        encode_call();
        encode_locator();
        encode_conv();
        interleave_sync();
    };

};

const double PI = 3.14159265358979323846;
#define SYMBOL_DURATION (8192.0/12000) // in seconds
#define TONE_SPACING 1.4648 // in Hz

std::vector<dsp::stereo_t> generate_tone(double freq, double duration, int SAMPLE_RATE, double &phi0) {
    int num_samples = static_cast<int>(SAMPLE_RATE * duration);
    std::vector<dsp::stereo_t> tone(num_samples);
    double phiOut = phi0;
    for (int i = 0; i < num_samples; ++i) {
        phiOut = phi0 + (2.0 * PI * i * freq) / SAMPLE_RATE;
        tone[i].l = static_cast<float>(sin(phiOut));
        tone[i].r = static_cast<float>(cos(phiOut));

    }
    phiOut = phi0 + (2.0 * PI * num_samples * freq) / SAMPLE_RATE;
    phi0 = phiOut;
    return tone;
}

std::vector<dsp::stereo_t> generate_wspr_message(const std::vector<int>& message, double base_frequency, int SAMPLE_RATE) {
    std::vector<dsp::stereo_t> audio_signal;
    double phi = 0;
    for (int i = 0; i < message.size(); ++i) {
        double freq = base_frequency + message[i] * TONE_SPACING;
        std::vector<dsp::stereo_t> tone = generate_tone(freq, SYMBOL_DURATION, SAMPLE_RATE, phi);
        audio_signal.insert(audio_signal.end(), tone.begin(), tone.end());
    }
    return audio_signal;
}

std::vector<int> convertToWSPRSymbols(std::string callsign, std::string loc, int powerDbm) {
    WSPREncoder wsprEncoder;
    if (callsign.size() < 3) {
        throw std::invalid_argument("callsign must be at least 3 characters long");
    }
    if (isdigit(callsign[2])) { // ok
        //
    } else {
        if (isdigit(callsign[1])) {
            callsign.insert(0, " ");
        }
    }
    if (callsign.length() > 6) {
        throw std::invalid_argument("long callsign cannot be sent in one go.");
    }
    while(callsign.size() < 6) {
        callsign.append(" ");
    }
    bool valid = callsign[0] == ' ' || isupper(callsign[0]) && isalnum(callsign[0]);
    valid &= isupper(callsign[1]) && isalpha(callsign[1]);
    valid &= isdigit(callsign[2]);
    valid &= isupper(callsign[3]) && isalnum(callsign[3]);
    valid &= isupper(callsign[4]) && isalpha(callsign[4]) || callsign[4] == ' ';
    valid &= isupper(callsign[5]) && isalpha(callsign[5]) || callsign[5] == ' ';
    if (!valid) {
        throw std::invalid_argument("callsign non-default, cannot send");
    }
    if (loc.length() > 4) {
        loc = loc.substr(0, 4);
    }
    if (powerDbm < 0 || powerDbm > 60) {
        throw std::invalid_argument("power must be between 0 and 60");
    }
    if (powerDbm % 10 != 0 && powerDbm % 10 != 3 && powerDbm % 10 != 7) {
        throw std::invalid_argument("power must end with 0,3,7");
    }
    if (!isdigit(loc[2]) || !isdigit(loc[3]) || !wsprEncoder.within(loc[0],'A','R') || !wsprEncoder.within(loc[1],'A','R')) {
        throw std::invalid_argument("locator is invalid (must be AA00-RR99)");
    }
    memcpy(wsprEncoder.call, callsign.c_str(), 6);
    memcpy(wsprEncoder.locator, loc.c_str(), 6);
    wsprEncoder.power = powerDbm;
    wsprEncoder.encode();
    std::vector<int> result;
    for(int i=0; i<162; i++) {
        result.emplace_back(wsprEncoder.sym[i]);
    }
    return result;
}

std::string convertToMorseCode(std::string input) {
    // Define Morse Code map
    std::unordered_map<char, std::string> morse {
            {'A', ".-"}, {'B', "-..."}, {'C', "-.-."}, {'D', "-.."},
            {'E', "."}, {'F', "..-."}, {'G', "--."}, {'H', "...."},
            {'I', ".."}, {'J', ".---"}, {'K', "-.-"}, {'L', ".-.."},
            {'M', "--"}, {'N', "-."}, {'O', "---"}, {'P', ".--."},
            {'Q', "--.-"}, {'R', ".-."}, {'S', "..."}, {'T', "-"},
            {'U', "..-"}, {'V', "...-"}, {'W', ".--"}, {'X', "-..-"},
            {'Y', "-.--"}, {'Z', "--.."},

            {'1', ".----"}, {'2', "..---"}, {'3', "...--"}, {'4', "....-"},
            {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."},
            {'9', "----."}, {'0', "-----"},

            {',', "--..--"},  // Comma
            {'.', ".-.-.-"},  // Period
            {'?', "..--.."},  // Question mark
            {'!', "-.-.--"},  // Exclamation mark
            {'-', "-....-"},  // Hyphen
            {'/', "-..-."},   // Slash
            {'@', ".--.-."},  // At symbol
            {'(', "-.--."},   // Open parenthesis
            {')', "-.--.-"},  // Close parenthesis
            {'\'', ".----."},  // Apostrophe
            {'\"', ".-..-."},  // Quotation mark
            {':', "---..."},  // Colon
            {';', "-.-.-."},  // Semicolon
            {'=', "-...-"},  // Equal sign
            {'+', ".-.-."},  // Plus sign
            {'_', "..--.-"},  // Underscore
            {'$', "...-..-"},  // Dollar sign
            {'&', ".-..."}  // Ampersand
    };

    std::string output;
    for (const auto &ch : input) {
        if (ch == ' ') {
            output += "   ";  // Three spaces for word separator
        } else {
            char upperChar = toupper(ch);  // Convert to uppercase
            if(morse.count(upperChar) > 0) {  // If the character is in the map
                output += morse[upperChar];
                output += " ";  // One space for letter separator
            }
        }
    }
    
    return output;
}


void doLowPassComplex(int freq, int transition, float sampleRate, dsp::complex_t *pComplex, unsigned long size) {
    auto halfBandPassTaps = dsp::taps::lowPass0<dsp::complex_t>(freq, transition, sampleRate);
    dsp::filter::FIR<dsp::complex_t, dsp::complex_t> lpf(nullptr, halfBandPassTaps);
    for(int q=0; q<size; q+=STREAM_BUFFER_SIZE) {
        int sz = STREAM_BUFFER_SIZE;
        if (q + sz > size) {
            sz = size - q;
        }
        lpf.process(sz, pComplex, pComplex);
        pComplex+=sz;
    }
    dsp::taps::free(halfBandPassTaps);
}

void doHighPassComplex(int freq, int transition, float sampleRate, dsp::complex_t *pComplex, unsigned long size) {
    auto halfBandPassTaps = dsp::taps::highPass0<dsp::complex_t>(freq, transition, sampleRate);
    dsp::filter::FIR<dsp::complex_t, dsp::complex_t> lpf(nullptr, halfBandPassTaps);
    for(int q=0; q<size; q+=STREAM_BUFFER_SIZE) {
        int sz = STREAM_BUFFER_SIZE;
        if (q + sz > size) {
            sz = size - q;
        }
        lpf.process(sz, pComplex, pComplex);
        pComplex+=sz;
    }
    dsp::taps::free(halfBandPassTaps);
}


std::vector<dsp::stereo_t> generateMorseAudio(int sampleRate, int freq, int ditLengthMs, const std::string &morse) {
    std::vector<dsp::stereo_t> samples;
    std::vector<float> envelope;
    const float ditDuration = ditLengthMs / 1000.0f;
    const float envelopeDuration = ditDuration / 10.0f;
    const int ditSamples = static_cast<int>(ditDuration * sampleRate);
    const int envelopeSamples = static_cast<int>(envelopeDuration * sampleRate);
    const float pi = std::acos(-1);


    // generates /--\

    for (int i=0; i < envelopeSamples; i++) {
        envelope.emplace_back(0.5 * (1 - cos(2 * M_PI * i / (envelopeSamples - 1))));
    }

    
    for (char c : morse) {
        if (c == '.' || c == '-') {
            int totalSamples = (c == '.' ? ditSamples : 3 * ditSamples);
            for (int i = 0; i < totalSamples; i++) {
                float env = 1.0f;
                if (i < envelopeSamples/2) {
                    env = envelope[i];
                } else if (i >= totalSamples - envelopeSamples/2) // End of pulse
                {
                    env = envelope[envelopeSamples/2 + i-(totalSamples - envelopeSamples/2)];
                }
                float sampleI = env * sin(2.0f * pi * freq * i / sampleRate);
                float sampleQ = env * cos(2.0f * pi * freq * i / sampleRate);
                samples.push_back(dsp::stereo_t{sampleI * 0.8f, sampleQ * 0.8f});
            }
            for (int i = 0; i < ditSamples; i++) {
                samples.push_back(dsp::stereo_t{0, 0});
            }
        } else if (c == ' ') {
            for (int i = 0; i < 3 * ditSamples; i++) {
                samples.push_back(dsp::stereo_t{0, 0});
            }
        }
    }
    doLowPassComplex(freq, 400, trxAudioSampleRate, (dsp::complex_t *)samples.data(), samples.size());
    doHighPassComplex(freq, 400, trxAudioSampleRate, (dsp::complex_t *)samples.data(), samples.size());
    return samples;
}

ConfigManager config;

class ReportsMonitorModule;

ReportsMonitorModule *modul;

class ReportsMonitorModule : public ModuleManager::Instance, public StatusReporter {

public:

    std::string monitoringCallsign;
    static ReportsMonitorModule *instance;

    ReportsMonitorModule(std::string name, std::string root) {
        this->name = name;
        modul = this;
        gui::menu.registerEntry(name, _menuHandler, this, NULL);
        gui::mainWindow.registerStatusReporter(this);
    }

    ~ReportsMonitorModule() {
        gui::mainWindow.unregisterStatusReporter(this);
        gui::menu.removeEntry(name);
    }

    void addReport(const net::Report &report) {
        reportsMutex.lock();
        bool found = false;
        for(int q=0; q<reports.size(); q++) {
            auto &rep = reports[q];
            if (rep.mode == report.mode && rep.reporterCallsign == report.reporterCallsign && rep.reportedCallsign == report.reportedCallsign && rep.timestamp == report.timestamp) {
                found = true;
                break;
            }
        }
        if (!found) {
            auto nr = report;
            nr.createdTimestamp = currentTimeMillis();
            if (nr.receiverLocator != "") {
                auto ll = utils::gridToLatLng(nr.receiverLocator);
                auto ll2 = utils::gridToLatLng(sigpath::iqFrontEnd.operatorLocation);
                if (ll.isValid() && ll2.isValid()) {
                    auto bd = utils::bearingDistance(ll, ll2);
                    nr.distance = bd.distance;
                } else {
                    nr.distance = -1;
                }
            } else {
                nr.distance = -1;
            }
            reports.insert(reports.begin(), nr);
        }


        reportsMutex.unlock();

    }

    std::string reportStatus() override {
        std::lock_guard g(reportsMutex);
        char buf[100];
        snprintf(buf, sizeof buf, "RPTS: %03zu", reports.size());
        return buf;
    }

    struct ReportingService {
        net::ReportingSource source;
        bool running = false;
        long long started = 0;
        std::shared_ptr<std::thread> thr;
        std::string status = "not monitoring";
        bool stopping = false;
        bool usePull = false;
        std::mutex mtx;
        int displayCount;

        auto callbackReceiver() {
            return [&](const ::net::Report &report) {
                if (!running) {
                    return;
                }
                if (report.errorStatus != "") {
                    setStatus(report.errorStatus);
                    return;
                }
                modul->addReport(report);
            };
        }

        void toggleWithButton() {
            if (stopping) {
                // do nothing
            } else {
                if (running) {
                    stop();
                } else {
                    start();
                }
            }
        }

        void setStatus(std::string status) {
            mtx.lock();
            this->status = status;
            mtx.unlock();
        }

        void stop() {
            if (!stopping) {
                if (running) {
                    setStatus("Stopping.");
                    stopping = true;
                }
                running = false;
                started = 0;
            }
        }

        void start() {
            if (!running) {
                running = true;
                started = currentTimeMillis();
                if (!thr) {
                    thr = std::make_shared<std::thread>([&]() {
                        bool doStatus = true;
                        while (running) {
                            switch(source) {
                                case net::RS_WSPRNET_PULL:
                                    SetThreadName("WSPRNET_pull");
                                    if (!net::getReportsFromWSPR_pull(sigpath::iqFrontEnd.operatorCallsign, callbackReceiver(), running)) {
                                        doStatus = false; // already reported
                                    }
                                    break;
                                case net::RS_PSKREPORTER_PULL:
                                    SetThreadName("PSKrep_pull");
                                    if (!net::getReportsFromPSKR_pull(sigpath::iqFrontEnd.operatorCallsign, callbackReceiver(), running)) {
                                        doStatus = false; // already reported
                                    }
                                    break;
                                case net::RS_PSKREPORTER:
                                    SetThreadName("PSKrep_mq");
                                    net::getReportsFromPSKR(sigpath::iqFrontEnd.operatorCallsign, callbackReceiver(), running);
                                    break;
                                case net::RS_RBN_PULL:
                                    SetThreadName("RBN_pull");
                                    if (!net::getReportsFromRBN_pull(sigpath::iqFrontEnd.operatorCallsign, callbackReceiver(), running)) {
                                        doStatus = false;
                                    }
                                    break;
                                case net::RS_RBN:
                                    SetThreadName("RBN_telnet");
                                    net::getReportsFromRBN(sigpath::iqFrontEnd.operatorCallsign, sigpath::iqFrontEnd.operatorCallsign, callbackReceiver(), running);
                                    break;
                            }
                        }
                        thr->detach();
                        thr.reset();
                        stopping = false;
                        if (doStatus) {
                            setStatus("Stopped.");
                        }
                    });
                }
            }
        }

        void maintainThread(int receiveDuration) {
            long long int ctm = currentTimeMillis();
            if (started != 0 && ctm - currentTimeMillis() > receiveDuration * 1000) {
                stop();
            }
        }

    };

    int receiveDuration = 180;       // seconds
    int aggregateDuration = 600;
    bool visible = true;

    std::vector<net::Report> reports;
    std::mutex reportsMutex;

    ReportingService wspr;
    ReportingService rbn;
    ReportingService rbn_pull;
    ReportingService pskr;
    ReportingService pskr_pull;

    std::vector<ReportingService *> allServices= {&wspr, &rbn, &rbn_pull, &pskr, &pskr_pull};

    void postInit() override {
        config.acquire();
        if (config.conf.contains("receiveDuration")) {
            receiveDuration = config.conf["receiveDuration"];
        }
        if (config.conf.contains("aggregateDuration")) {
            aggregateDuration = config.conf["aggregateDuration"];
        }
        config.release(false);
        rbn.source = net::RS_RBN;
        pskr.source = net::RS_PSKREPORTER;
        wspr.source = net::RS_WSPRNET_PULL;
        rbn_pull.source = net::RS_RBN_PULL;
        pskr_pull.source = net::RS_PSKREPORTER_PULL;
        pskr_pull.usePull = true;
        rbn_pull.usePull = true;
    }

    void enable() override  {
        enabled = true;
    }

    void disable() override {
        enabled = false;
        rbn.stop();
        pskr.stop();
        wspr.stop();
    }

    bool isEnabled() override {
        return enabled;
    }

    void maintainThreads() {
        pskr.maintainThread(receiveDuration);
        rbn.maintainThread(receiveDuration);
        wspr.maintainThread(receiveDuration);
    }


    const char *SELF_REPORTS_POPUP = "Self-Reports";

private:

    void drawReportsPopup() {
        ImVec2 ds = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowSize(ds * 0.9);
        if (ImGui::BeginPopupModal(SELF_REPORTS_POPUP, NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar)) {
            // Get window size
            ImVec2 window_size = ImGui::GetWindowSize();
            // Set the table size to be 90% of window size
            ImVec2 table_size = ds * 0.8;

            // Start the table
            if (ImGui::BeginTable("ReportsTable", 10, ImGuiTableFlags_ScrollY, table_size)) {
                ImGui::TableSetupColumn("Timestamp");
                ImGui::TableSetupColumn("Type");
                ImGui::TableSetupColumn("Mode");
                ImGui::TableSetupColumn("DE (spotter)");
                ImGui::TableSetupColumn("Distance");
                ImGui::TableSetupColumn("DX (spotted)");
                ImGui::TableSetupColumn("Decibel");
                ImGui::TableSetupColumn("Frequency");
                ImGui::TableSetupColumn("Mode Parameters");

                // Header row
                ImGui::TableHeadersRow();

                // Iterate over each report and create a row for it
                reportsMutex.lock();
                for (const auto &report: reports) {
                    int col = 0;
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(col++);
                    ImGui::Text("%s", report.timestamp.c_str());
                    ImGui::TableSetColumnIndex(col++);
                    ImGui::Text("%s", to_string(report.reportingSource).c_str());
                    ImGui::TableSetColumnIndex(col++);
                    ImGui::Text("%s", report.mode.c_str());
                    ImGui::TableSetColumnIndex(col++);
                    ImGui::Text("%s", report.reporterCallsign.c_str());
                    ImGui::TableSetColumnIndex(col++);
                    char buf[100];
                    if (report.distance > 0) {
                        static int charMaxWidth = -1;
                        if (charMaxWidth < 0) {
                            snprintf(buf, sizeof buf, "88888 km");
                            charMaxWidth = ImGui::CalcTextSize(buf).x;
                        }
                        snprintf(buf, sizeof buf, "%d km", report.distance);
                        float textWidth = ImGui::CalcTextSize(buf).x;
                        ImGui::Dummy(ImVec2(charMaxWidth - textWidth, 0));
                        ImGui::SameLine();
                        ImGui::Text("%s", buf);
                    } else {
                        ImGui::Text("");
                    }
                    ImGui::TableSetColumnIndex(col++);
                    ImGui::Text("%s", report.reportedCallsign.c_str());
                    ImGui::TableSetColumnIndex(col++);

                    if (report.decibel > 0) {
                        snprintf(buf, sizeof buf, "%d", (int)report.decibel);
                        float textWidth = ImGui::CalcTextSize(buf).x;
                        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x - textWidth, 0));
                        ImGui::SameLine();
                        ImGui::Text("%s", buf);
                    } else {
                        ImGui::Text("");
                    }

                    ImGui::TableSetColumnIndex(col++);
                    ImGui::Text("%0.2f", report.frequency);
                    ImGui::TableSetColumnIndex(col++);
                    ImGui::Text("%s", report.modeParameters.c_str());
                }
                reportsMutex.unlock();
                // End the table
                ImGui::EndTable();
            }

            if (ImGui::Button("Close")) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    bool doTXButtonAtFrequency(const std::string &text, int frequency) {
        static const int INVALID_OFFSET = 0x7FFFFFFF;
        char buf[1000];
        std::vector<std::string> x;
        splitStringV(text, "\n", x);
        if (frequency == INVALID_OFFSET) {
            ImGui::BeginDisabled(true);
            x[0] += " ! not on freq";
            auto rv = doFingerButton(joinStringV("\n", x));
            ImGui::EndDisabled();
            return rv;
        } else {
            snprintf(buf, sizeof buf, "@ %0.4f MHz", frequency/1e6);
            x[0] += buf;
            auto rv = doFingerButton(joinStringV("\n", x));
            return rv;
        }
    }

    bool doServiceButton(const char *lbl, ReportingService &service) {
        bool pushed = false;
        if (service.stopping) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0, 1.0f));
            pushed = true;
        } else if (service.running) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0, 1.0f));
            pushed = true;
        }
        auto rv = ImGui::Button(lbl);
        if (pushed) {
            ImGui::PopStyleColor();
        }
        if (rv) {
            service.toggleWithButton();
        }
        return rv;
    }

    void menuHandler() {
        maintainThreads();
        ImGui::Text("Operator Callsign: %s", sigpath::iqFrontEnd.operatorCallsign.c_str());

        ImGui::LeftLabel("Receive:");
        ImGui::SameLine();
        if (ImGui::SliderInt("##_websdr_recvduration_", &receiveDuration, 15, 600, "%d sec")) {
            config.acquire();
            config.conf["receiveDuration"] = receiveDuration;
            config.release(true);

        }
        ImGui::LeftLabel("Aggregate:");
        ImGui::SameLine();
        if (ImGui::SliderInt("##_websdr_aggduration_", &aggregateDuration, 15, 600, "%d sec")) {
            config.acquire();
            config.conf["aggregateDuration"] = aggregateDuration;
            config.release(true);

        }
        ImGui::LeftLabel("Enabled");
        if (ImGui::Checkbox("##_websdr_visible_", &visible)) {
            config.acquire();
            config.conf["visible"] = visible;
            config.release(true);
            reports.clear();
        }
        bool noCallsign = sigpath::iqFrontEnd.operatorCallsign.empty();
        if (noCallsign) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0, 0, 1.0f));
            ImGui::TextUnformatted("Callsign is not set");
            ImGui::PopStyleColor();

        }
        ImGui::BeginDisabled(noCallsign);
        char boo[200];
        for(auto &s: allServices) {
            snprintf(boo, sizeof boo, "%s (%d): %s", net::to_string(s->source).c_str(), s->displayCount, s->status.c_str());
            doServiceButton(boo, *s);
            s->displayCount = 0;
        }
        ImGui::EndDisabled();
        if (reports.size() > 0) {
            ImGui::SameLine();
            if (doFingerButton("View..")) {
                ImGui::OpenPopup(SELF_REPORTS_POPUP);
            }
        }
        reportsMutex.lock();
        long long int ctm = currentTimeMillis();
        for (auto &r:  reports) {
            if (ctm - r.createdTimestamp > aggregateDuration * 1000) {
                continue;
            }
            for(auto &s: allServices) {
                if (r.reportingSource == s->source) {
                    s->displayCount++;
                }
            }
        }
        while(reports.size() > 0 && ctm - reports.back().createdTimestamp > aggregateDuration * 1000) {
            reports.resize(reports.size()-1);
        };
        reportsMutex.unlock();

        auto disabled = noCallsign || !sigpath::transmitter || sigpath::transmitter->getTXStatus() || !gui::mainWindow.canTransmit();
        ImGui::BeginDisabled(disabled);
        char buf[1024];
        snprintf(buf, sizeof buf, "CW TX:\nCQ CQ DE %s %s K", sigpath::iqFrontEnd.operatorCallsign.c_str(), sigpath::iqFrontEnd.operatorCallsign.c_str());
        if (doTXButtonAtFrequency(buf, gui::freqSelect.frequency)) {
            transmitCW(gui::freqSelect.frequency);
        }

        FT8ModuleInterface *ft8 = nullptr;
        for(auto x: core::moduleManager.instances) {
            Instance *pInstance = x.second.instance;
            ft8 = (FT8ModuleInterface *)pInstance->getInterface("FT8ModuleInterface");
            if (ft8) {
                break;
            }
        }
        if (ft8) {
            int freq = ft8->getCurrentFrequency("WSPR");
            snprintf(buf, sizeof buf, "WSPR TX:\n5W power, %s", sigpath::iqFrontEnd.operatorLocation.substr(0, 4).c_str());
            if (doTXButtonAtFrequency("WSPR TX:\n5W power", freq)) {
                transmitWSPR(freq);
            }
            snprintf(buf, sizeof buf, "FT8 TX:\nCQ %s %s", sigpath::iqFrontEnd.operatorCallsign.c_str(), sigpath::iqFrontEnd.operatorLocation.substr(0, 4).c_str());
            freq = ft8->getCurrentFrequency("FT8");
            if (doTXButtonAtFrequency(buf, freq)) {
                transmitFT8(freq);
            }
        } else {
            ImGui::Text("Load FT8 module to tx FT8/WSPR");
        }
        ImGui::EndDisabled();

        drawScheduledDialog();
        drawReportsPopup();
    }

    std::shared_ptr<std::vector<dsp::stereo_t>> saudio = std::make_shared<std::vector<dsp::stereo_t>>();
    enum ScheduledDialogState {
        SCHEDULED_NO_SCHEDULE,
        SCHEDULED_SCHEDULED_WAITING,
        SCHEDULED_WAIT_TX_BEGIN,
        SCHEDULED_WAIT_TX_BEGIN_TO_CANCEL,
        SCHEDULED_WAIT_TX_END,
    } scheduledDialogState = SCHEDULED_NO_SCHEDULE;

    std::function<void()> startScheduledTransmit;
    long long scheduledTransmitTime = 0;
    const char *SCHEDULED_POPUP = "Reports Monitor - Scheduled Transmit";

    void drawScheduledDialog() {
        ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.125f);

        if (ImGui::BeginPopupModal(SCHEDULED_POPUP, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGuiContext* g = ImGui::GetCurrentContext();
            g->DimBgRatio = 0.0f; // Set the alpha value to zero
            long long deltaTime = 0;
            switch(scheduledDialogState) {
                default:
                    break;
                case SCHEDULED_NO_SCHEDULE:
                    ImGui::CloseCurrentPopup();
                    break;
                case SCHEDULED_SCHEDULED_WAITING:
                    deltaTime = (scheduledTransmitTime - currentTimeMillis()) / 1000;
                    if (deltaTime < 0) {
                        scheduledDialogState = SCHEDULED_WAIT_TX_BEGIN;
                        startScheduledTransmit();
                    } else {
                        ImGui::Text("Waiting before transmit: %lld sec", deltaTime);
                        if (doFingerButton("Cancel")) {
                            scheduledDialogState = SCHEDULED_NO_SCHEDULE;
                        }
                    }
                    break;
                case SCHEDULED_WAIT_TX_BEGIN_TO_CANCEL:
                    ImGui::Text("Cancelling scheduled TX...");
                    if (doFingerButton("Abort")) {
                        scheduledDialogState = SCHEDULED_NO_SCHEDULE;
                    } else {
                        if (sigpath::transmitter && sigpath::transmitter->getTXStatus()) {
                            scheduledDialogState = SCHEDULED_NO_SCHEDULE;
                            gui::mainWindow.stopTx();
                        }
                    }
                    break;
                case SCHEDULED_WAIT_TX_BEGIN:
                    ImGui::Text("Will tx, waiting tx to start now...");
                    if (doFingerButton("Cancel")) {
                        scheduledDialogState = SCHEDULED_WAIT_TX_BEGIN_TO_CANCEL;
                    } else {
                        if (sigpath::transmitter && sigpath::transmitter->getTXStatus()) {
                            scheduledDialogState = SCHEDULED_WAIT_TX_END;
                        }
                    }
                case SCHEDULED_WAIT_TX_END:
                    deltaTime = (currentTimeMillis() - scheduledTransmitTime) / 1000;
                    long long totalTime = (saudio->size() / 48000) + 1;
                    ImGui::Text("Waiting tx to finish... %lld / %lld sec", deltaTime, totalTime);
                    if (doFingerButton("Cancel")) {
                        scheduledDialogState = SCHEDULED_NO_SCHEDULE;
                        gui::mainWindow.stopTx();
                    }
                    if (sigpath::transmitter && !sigpath::transmitter->getTXStatus()) {
                        scheduledDialogState = SCHEDULED_NO_SCHEDULE;
                    }
                    break;
            }

            ImGui::Text("TX  PA:");
            ImGui::SameLine();
            int hwgain = sigpath::transmitter->getTransmitHardwareGain();
            if (ImGui::SliderInt("##_radio_tx_gain_", &hwgain, 0, 255)) {
                gui::mainWindow.setBothGains(hwgain);
            }

            ImGui::EndPopup();
        }
    }
    void runScheduledTransmit(long long scheduledTime, std::function<void()> start) {
        startScheduledTransmit = start;
        scheduledDialogState = SCHEDULED_SCHEDULED_WAITING;
        scheduledTransmitTime = scheduledTime;
        ImGui::OpenPopup(SCHEDULED_POPUP);
    }

    void transmitCW(int frequency) {
        char buf[1024];
        snprintf(buf, sizeof buf, "CQ CQ DE %s %s K", sigpath::iqFrontEnd.operatorCallsign.c_str(), sigpath::iqFrontEnd.operatorCallsign.c_str());
        const std::string morse = " " +convertToMorseCode(buf)+ " " ;

        auto ditDuration = 60 * 1000 / (50 * gui::mainWindow.cwWPM);
        auto ctm = currentTimeMillis();
        auto audio = generateMorseAudio(trxAudioSampleRate, gui::mainWindow.cwAudioFrequency, ditDuration, morse);
        flog::info("CW generation time: {}", (int64_t)(currentTimeMillis()-ctm));
        saudio->clear();
        saudio->insert(saudio->end(), audio.begin(), audio.end());

        runScheduledTransmit(currentTimeMillis(), [this, morse, frequency]() {
            auto savediq = gui::mainWindow.getIqDataInAudio();
            gui::mainWindow.maybeTransmit(saudio,
                                          [=]() {
                                              gui::mainWindow.txSubmodeOverride = "CW";
                                              gui::mainWindow.txOffset = -gui::mainWindow.cwAudioFrequency;
                                              gui::mainWindow.txFrequencyOverride = frequency;
                                              gui::mainWindow.setIqDataInAudio(true);
                                          },
                                          [=]() {
                                              gui::mainWindow.txSubmodeOverride = "";
                                              gui::mainWindow.txFrequencyOverride = 0;
                                              gui::mainWindow.txOffset = 0;
                                              gui::mainWindow.setIqDataInAudio(savediq);
                                          });
            flog::info("Done {}", morse);
        });
    }
    
    void transmitWSPR(int frequency) {
        try {
            auto audioFreq = 1505;
            auto ctm = currentTimeMillis();
            auto symbols = convertToWSPRSymbols(sigpath::iqFrontEnd.operatorCallsign, sigpath::iqFrontEnd.operatorLocation, 37); // 37 dbm=5W
            auto audio = generate_wspr_message(symbols, audioFreq, gui::mainWindow.currentAudioStreamSampleRate);
            flog::info("WSPR: generated tx wav in {} msec", (int64_t)(currentTimeMillis()-ctm));
//            auto halfBandPassTaps = dsp::taps::lowPass0<dsp::complex_t>(audioFreq+200, 500, trxAudioSampleRate);
//            dsp::filter::FIR<dsp::stereo_t, dsp::complex_t> lpf(nullptr, halfBandPassTaps);
//            int nparts = 9;
//            auto singlePart = audio.size()/nparts;
//            for(int q=0; q<nparts; q++) {
//                lpf.process(singlePart, audio.data()+singlePart * q, audio.data()+singlePart * q);
//            }
//            flog::info("WSPR: lowpass filter time: {}", (int64_t)(currentTimeMillis() - ctm));
            saudio->clear();
            saudio->insert(saudio->end(), audio.begin(), audio.end());

            auto scheduleStart = (currentTimeMillis() / 1000 / 120 + 1) * 120 * 1000 + 1000;
            runScheduledTransmit(scheduleStart, [&, frequency]() {
                auto saved = gui::mainWindow.getCurrentModeAttr("submode");
                auto savediq = gui::mainWindow.getIqDataInAudio();
                gui::mainWindow.maybeTransmit(saudio,
                                              [=]() {
                                                  gui::mainWindow.txSubmodeOverride = "USB";
                                                  gui::mainWindow.setIqDataInAudio(true);
                                                  gui::mainWindow.txFrequencyOverride = frequency;
                                              },
                                              [=]() {
                                                  gui::mainWindow.txSubmodeOverride = "";
                                                  gui::mainWindow.setCurrentModeBySubmode(saved);
                                                  gui::mainWindow.setIqDataInAudio(savediq);
                                                  gui::mainWindow.txFrequencyOverride = 0;
                                              });
            });


        } catch (std::exception &e) {
            ImGui::InsertNotification({ ImGuiToastType_Error, 5000, (std::string("WSPR:") + e.what()).c_str() });
            return;
        }

    }

    void transmitFT8(int frequency) {
        FT8ModuleInterface *ft8 = nullptr;
        for(auto x: core::moduleManager.instances) {
            ft8 = (FT8ModuleInterface *)x.second.instance->getInterface("FT8ModuleInterface");
            if (ft8) {
                break;
            }
        }
        if (ft8) {
            auto ctm = currentTimeMillis();
            auto [rv, msg] = ft8->encodeCQ_FT8(sigpath::iqFrontEnd.operatorCallsign,sigpath::iqFrontEnd.operatorLocation, 1000);
            flog::info("FT8: generated tx wav in {} msec", (int64_t)(currentTimeMillis()-ctm));
            saudio->clear();
            saudio->insert(saudio->end(), rv.begin(), rv.begin() + 13 * gui::mainWindow.currentAudioStreamSampleRate); // 13 seconds only
            flog::info("FT8: Sound length produced: {}", (int64_t)rv.size());
            auto scheduleStart = (currentTimeMillis() / 1000 / 15 + 1) * 15 * 1000;
            runScheduledTransmit(scheduleStart, [&, frequency]() {
                gui::mainWindow.maybeTransmit(saudio,
                                              [=]() {
                                                  gui::mainWindow.txSubmodeOverride = "USB";
                                                  gui::mainWindow.txFrequencyOverride = frequency;
                                                  gui::mainWindow.txBandwidthOverride = 1500;
                                              },
                                              [=]() {
                                                  gui::mainWindow.txSubmodeOverride = "";
                                                  gui::mainWindow.txFrequencyOverride = 0;
                                                  gui::mainWindow.txBandwidthOverride = 0;
                                              });
                flog::info("Done FT8");
            });
        }

    }

    static void _menuHandler(void *ctx) {
        ((ReportsMonitorModule *) ctx)->menuHandler();
    }

    std::string name;
    bool enabled = true;
};


MOD_EXPORT void _INIT_() {
    config.setPath(std::string(core::getRoot()) + "/repors_monitor.json");
    config.load(json::object());
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance *_CREATE_INSTANCE_(std::string name) {
    return new ReportsMonitorModule(name, std::string(core::getRoot()));
}

MOD_EXPORT void _DELETE_INSTANCE_(void *instance) {
    delete (ReportsMonitorModule *) instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}

