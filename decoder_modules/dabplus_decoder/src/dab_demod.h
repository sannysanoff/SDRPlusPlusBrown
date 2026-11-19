#pragma once

// DAB/DAB+ as a mode of the existing radio module: this demodulator is handed
// to every RadioModuleInterface by the dabplus_decoder module, so the receiver
// runs on the radio's own VFO, IF chain and audio sinks instead of opening a
// second VFO of its own.
//
// The interface is the radio module's, so the two virtuals this demodulator adds
// to it (getExtraMenuWidth, handleDebugCommand) are declared there with defaults
// that leave every other demodulator untouched.

#include "../../radio/src/demod.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <complex>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <dsp/sink/handler_sink.h>
#include <dsp/multirate/rational_resampler.h>
#include <gui/widgets/constellation_diagram.h>
#include <utils/flog.h>

#include "dab_engine.h"

namespace demod {

    class DAB : public Demodulator {
    public:
        DAB() {}

        ~DAB() {
            stop();
        }

        void init(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, double audioSR) {
            this->name = name;
            this->config = config;
            audioSampleRate = audioSR;

            // Transmission mode lives with the radio's other per-demod settings.
            transmissionMode = 1;
            if (config != nullptr) {
                config->acquire();
                if (config->conf.contains(name) && config->conf[name].contains(getName())) {
                    transmissionMode = config->conf[name][getName()].value("mode", 1);
                }
                config->release(false);
            }
            if (transmissionMode < 1 || transmissionMode > 4) { transmissionMode = 1; }

            engine = std::make_unique<dabplus::Engine>(transmissionMode, 1);

            inputSink.init(input, inputHandler, this);
            rawStream.origin = "DAB.demod.raw";

            resamp.init(&rawStream, DEFAULT_ENGINE_RATE, audioSampleRate);

            audioRunning = true;
            audioThread = std::thread(&DAB::audioWorker, this);
        }

        void start() {
            inputSink.start();
            resamp.start();
            started = true;
        }

        void stop() {
            if (!started) { return; }
            started = false;

            audioRunning = false;
            rawStream.stopWriter();
            if (audioThread.joinable()) { audioThread.join(); }
            rawStream.clearWriteStop();

            inputSink.stop();
            resamp.stop();
        }

        void setInput(dsp::stream<dsp::complex_t>* input) {
            inputSink.setInput(input);
        }

        void setBandwidth(double bandwidth) {}

        void AFSampRateChanged(double newSR) {
            audioSampleRate = newSR;
            resamp.tempStop();
            resamp.setOutSamplerate(newSR);
            resamp.tempStart();
        }

        void setFrozen(bool frozen) {}

        const char* getName() { return "DAB"; }
        double getIFSampleRate() { return ENGINE_SAMPLE_RATE; }
        double getAFSampleRate() { return audioSampleRate; }
        double getDefaultBandwidth() { return DAB_BANDWIDTH; }
        double getMinBandwidth() { return DAB_BANDWIDTH; }
        double getMaxBandwidth() { return DAB_BANDWIDTH; }
        bool getBandwidthLocked() { return true; }
        double getDefaultSnapInterval() { return 16000.0; }
        int getVFOReference() { return ImGui::WaterfallVFO::REF_CENTER; }
        bool getDeempAllowed() { return false; }
        bool getPostProcEnabled() { return false; }
        int getDefaultDeemphasisMode() { return DEEMP_MODE_NONE; }
        bool getFMIFNRAllowed() { return false; }
        bool getNBAllowed() { return false; }
        dsp::stream<dsp::stereo_t>* getOutput() { return &resamp.out; }

        // A station list and a constellation need room beside the waterfall.
        float getExtraMenuWidth() { return 340.0f; }

        std::string handleDebugCommand(const std::string& cmd, const std::string& args) {
            if (cmd == "dab_status") { return statusJson(); }
            if (cmd == "dab_select") {
                int subchannel = -1;
                try { subchannel = std::stoi(args); }
                catch (...) { return "{\"error\":\"dab_select needs a subchannel id\"}"; }
                {
                    selectedSubchannel = subchannel;
                    userSelected = true;
                    std::lock_guard<std::mutex> lck(engineMtx);
                    if (engine) { engine->Select(subchannel); }
                }
                return "{\"status\":\"ok\",\"subchannel\":" + std::to_string(subchannel) + "}";
            }
            if (cmd == "dab_services") { return statusJson(); }
            return "{\"error\":\"dab: unknown command: " + cmd + "\"}";
        }

        void showMenu() {
            refreshSnapshot();
            const dabplus::Snapshot& snap = snapshot;
            float width = ImGui::GetContentRegionAvail().x;

            ImGui::Text("Ensemble");
            ImGui::SameLine(width * 0.42f);
            ImGui::TextUnformatted(snap.ensemble.empty() ? "---" : snap.ensemble.c_str());

            ImGui::Text("Programme");
            ImGui::SameLine(width * 0.42f);
            ImGui::TextUnformatted(snap.dynamic_label.empty() ? "---" : snap.dynamic_label.c_str());

            ImGui::Text("Sync");
            ImGui::SameLine(width * 0.42f);
            ImGui::TextUnformatted(syncStateName(snap.sync_state));

            ImGui::Text("Audio");
            ImGui::SameLine(width * 0.42f);
            if (snap.audio_rate) {
                ImGui::Text("%u Hz %s", unsigned(snap.audio_rate), snap.audio_stereo ? "stereo" : "mono");
            }
            else {
                ImGui::TextUnformatted("no service");
            }

            ImGui::Spacing();

            ImGui::BeginGroup();
            ImGui::TextUnformatted("Services");
            if (snap.services.empty()) { ImGui::TextUnformatted("Searching for ensemble..."); }
            else {
                ImGui::BeginChild("dab_services_list", ImVec2(width * 0.55f, 170), true);
                for (const auto& service : snap.services) {
                    // An FIC entry without a label carries no programme name yet;
                    // listing it as "Subchannel N" only clutters the list.
                    if (service.label.empty()) { continue; }
                    const bool isSelected = (service.subchannel == selectedSubchannel);
                    std::string label = service.label;
                    label += "##" + std::to_string(service.subchannel);
                    if (ImGui::Selectable(label.c_str(), isSelected)) {
                        selectedSubchannel = service.subchannel;
                        userSelected = true;
                        std::lock_guard<std::mutex> lck(engineMtx);
                        if (engine) { engine->Select(service.subchannel); }
                    }
                }
                ImGui::EndChild();
            }
            ImGui::EndGroup();

            ImGui::SameLine();

            ImGui::BeginGroup();
            ImGui::TextUnformatted("Constellation");
            {
                size_t count = 0;
                {
                    std::lock_guard<std::mutex> lck(engineMtx);
                    if (engine) { count = engine->GetConstellation(constBuf.data(), constBuf.size()); }
                }
                dsp::complex_t* buf = constDiagram.acquireBuffer();
                for (size_t i = 0; i < 1024; i++) {
                    buf[i] = (i < count)
                        ? dsp::complex_t{ constBuf[i].real(), constBuf[i].imag() }
                        : dsp::complex_t{ 0.0f, 0.0f };
                }
                constDiagram.releaseBuffer();
                constDiagram.draw(ImVec2(150, 150));
            }
            ImGui::EndGroup();

            if (ImGui::TreeNode("Receiver settings##dab")) {
                if (ImGui::BeginCombo("Transmission mode##dab", transmissionModeName())) {
                    for (int mode = 1; mode <= 4; mode++) {
                        if (ImGui::Selectable(transmissionModeName(mode), mode == transmissionMode)) {
                            setTransmissionMode(mode);
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::TextUnformatted("Mode I covers Band III. Tune to the ensemble centre.");
                ImGui::TreePop();
            }

            // First audio service the FIC announces, unless a service was picked.
            autoSelectAudioService();
        }

    private:
        static constexpr double ENGINE_SAMPLE_RATE = 2048000.0;
        static constexpr double DAB_BANDWIDTH = 1600000.0;
        static constexpr double DEFAULT_ENGINE_RATE = 48000.0;

        static const char* syncStateName(int state) {
            switch (state) {
            case 0: return "Searching for null";
            case 1: return "Null and phase reference";
            case 2: return "Coarse frequency sync";
            case 3: return "Fine time sync";
            case 4: return "Reading symbols";
            default: return "Unknown";
            }
        }

        const char* transmissionModeName() { return transmissionModeName(transmissionMode); }

        static const char* transmissionModeName(int mode) {
            switch (mode) {
            case 1: return "I (Band III)";
            case 2: return "II";
            case 3: return "III";
            case 4: return "IV (L-band)";
            default: return "I (Band III)";
            }
        }

        void setTransmissionMode(int mode) {
            if (mode == transmissionMode) { return; }
            transmissionMode = mode;
            if (config != nullptr) {
                config->acquire();
                config->conf[name][getName()]["mode"] = mode;
                config->release(true);
            }
            // Rebuilding the engine is the only way to change the OFDM
            // parameters; it happens on the UI thread while no samples arrive.
            inputSink.stop();
            {
                std::lock_guard<std::mutex> lck(engineMtx);
                engine = std::make_unique<dabplus::Engine>(mode, 1);
            }
            selectedSubchannel = -1;
            userSelected = false;
            inputSink.start();
        }

        static void inputHandler(dsp::complex_t* data, int count, void* ctx) {
            DAB* _this = (DAB*)ctx;
            {
                std::lock_guard<std::mutex> lck(_this->engineMtx);
                if (!_this->engine) { return; }
                _this->engine->Process((const std::complex<float>*)data, size_t(count));
            }
            _this->paceInput(size_t(count));
        }

        // The engine decodes exactly as fast as samples arrive. From a live radio
        // that is the device rate; from a recording it is the file reader's speed,
        // many times faster, so the audio ring fills and drops its backlog - heard
        // as gaps and a programme running ahead of real time.
        //
        // Holding the whole chain back in one big wait after a burst is the wrong
        // shape: it stalls the IQ frontend too, so the waterfall moves in jerks.
        // Instead this paces the input to the rate of the signal itself, sleeping
        // a millisecond at a time, which keeps the chains flowing smoothly and
        // leaves almost nothing queued. A live receiver is already at that rate and
        // never waits here.
        void paceInput(size_t samples) {
            const auto now = std::chrono::steady_clock::now();
            if (pacedAt.time_since_epoch().count() == 0) {
                pacedAt = now;
                pacedSignal = 0.0;
            }
            pacedSignal += double(samples) / ENGINE_SAMPLE_RATE;
            const auto target = pacedAt + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(pacedSignal));

            // A stopped source, a seek or a long stall must not leave credit that
            // would let the next read run at full speed to catch up.
            if (now - target > std::chrono::milliseconds(100)) {
                pacedAt = now;
                pacedSignal = 0.0;
                return;
            }
            // Sleep right up to the deadline: leaving a millisecond unspent on
            // every block would let the input run a few percent fast, and the
            // audio ring would slowly fill. Chunked so stop() is never waited on.
            using Clock = std::chrono::steady_clock;
            const auto floorLeft = std::chrono::duration_cast<Clock::duration>(std::chrono::microseconds(100));
            const auto chunk = std::chrono::duration_cast<Clock::duration>(std::chrono::milliseconds(1));
            while (audioRunning.load()) {
                const auto left = target - Clock::now();
                if (left <= floorLeft) { break; }
                std::this_thread::sleep_for(left < chunk ? left : chunk);
            }
        }

        // The engine emits PCM in bursts at the service's own rate. This worker
        // is the audio clock: it pushes real-time paced blocks so the radio's AF
        // chain never stalls on a decoder hiccup.
        void audioWorker() {
            constexpr size_t BLOCK_FRAMES = 512;
            std::vector<int16_t> pcm(BLOCK_FRAMES * 2);
            auto next = std::chrono::steady_clock::now();
            uint32_t engineRate = uint32_t(DEFAULT_ENGINE_RATE);
            bool engineStereo = true;

            while (audioRunning) {
                size_t frames = 0;
                {
                    std::lock_guard<std::mutex> lck(engineMtx);
                    if (engine) { frames = engine->ReadAudio(pcm.data(), BLOCK_FRAMES, engineRate, engineStereo); }
                }
                if (engineRate == 0) { engineRate = uint32_t(DEFAULT_ENGINE_RATE); }
                if (engineRate != resampInRate) {
                    resamp.tempStop();
                    resamp.setInSamplerate(double(engineRate));
                    resamp.tempStart();
                    resampInRate = engineRate;
                }

                if (frames == 0) {
                    // Nothing decoded yet (or the source is between frames). Waiting
                    // is right: padding the stream with zeros here would inject
                    // silence between the decoder's own bursts and be heard as a
                    // quiet crackle.
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    continue;
                }

                const size_t channels = engineStereo ? 2 : 1;
                dsp::stereo_t* buf = rawStream.writeBuf;
                float blockPeak = 0.0f;
                for (size_t i = 0; i < frames; i++) {
                    const float l = float(pcm[i * channels]) * (1.0f / 32768.0f);
                    const float r = float(pcm[i * channels + (engineStereo ? 1 : 0)]) * (1.0f / 32768.0f);
                    buf[i] = { l, r };
                    blockPeak = (std::max)(blockPeak, (std::max)(std::fabs(l), std::fabs(r)));
                }
                audioPeak.store((std::max)(blockPeak, audioPeak.load() * 0.85f));
                audioFrames += frames;
                if (!rawStream.swap(int(frames))) { break; }

                next += std::chrono::microseconds(int64_t(1000000.0 * double(frames) / double(engineRate)));
                const auto now = std::chrono::steady_clock::now();
                if (next > now) { std::this_thread::sleep_until(next); }
                else if ((now - next) > std::chrono::milliseconds(250)) { next = now; }
            }
        }

        void refreshSnapshot() {
            const auto now = std::chrono::steady_clock::now();
            if (now - lastSnapshot < std::chrono::milliseconds(200)) { return; }
            lastSnapshot = now;
            {
                std::lock_guard<std::mutex> lck(engineMtx);
                if (engine) { snapshot = engine->GetSnapshot(); }
            }
            // Runs off the snapshot rather than the menu, so a headless radio
            // (debug commands, tests) gets audio the same way the GUI does.
            autoSelectAudioService();
        }

        // The engine only runs the subchannels it was asked to run, so pick the
        // first announced service unless the user already chose one. Entries
        // without a label have no programme name yet, so they are skipped.
        void autoSelectAudioService() {
            if (userSelected || snapshot.services.empty()) { return; }
            const dabplus::Service* pick = nullptr;
            for (const auto& service : snapshot.services) {
                if (!service.label.empty()) { pick = &service; break; }
            }
            if (pick == nullptr) { pick = &snapshot.services.front(); }
            if (pick->subchannel == selectedSubchannel) { return; }
            selectedSubchannel = pick->subchannel;
            std::lock_guard<std::mutex> lck(engineMtx);
            if (engine) { engine->Select(pick->subchannel); }
        }

        std::string statusJson() {
            refreshSnapshot();
            const dabplus::Snapshot& snap = snapshot;
            std::string out = "{";
            out += "\"demod\":\"DAB\",\"mode\":" + std::to_string(transmissionMode);
            out += ",\"sync_state\":" + std::to_string(snap.sync_state);
            out += ",\"frames\":" + std::to_string(snap.frames);
            out += ",\"desyncs\":" + std::to_string(snap.desyncs);
            out += ",\"ensemble\":\"" + escape(snap.ensemble) + "\"";
            out += ",\"ensemble_id\":" + std::to_string(snap.ensemble_id);
            out += ",\"ecc\":" + std::to_string(snap.ecc);
            out += ",\"dynamic_label\":\"" + escape(snap.dynamic_label) + "\"";
            out += ",\"selected\":" + std::to_string(snap.selected);
            out += ",\"audio_rate\":" + std::to_string(unsigned(snap.audio_rate));
            out += ",\"audio_stereo\":" + std::string(snap.audio_stereo ? "true" : "false");
            char level[64];
            snprintf(level, sizeof(level), "%.4f", double(audioPeak.load()));
            out += ",\"audio_peak\":" + std::string(level);
            out += ",\"audio_frames\":" + std::to_string(audioFrames.load());
            out += ",\"audio_overflows\":" + std::to_string(snap.audio_overflows);
            out += ",\"constellation_points\":" + std::to_string(constellationCount());
            out += ",\"services\":[";
            for (size_t i = 0; i < snap.services.size(); i++) {
                const auto& service = snap.services[i];
                if (i) { out += ","; }
                out += "{\"subchannel\":" + std::to_string(service.subchannel);
                out += ",\"type\":" + std::to_string(service.type);
                out += ",\"label\":\"" + escape(service.label) + "\"}";
            }
            out += "]}";
            return out;
        }

        size_t constellationCount() {
            std::lock_guard<std::mutex> lck(engineMtx);
            if (!engine) { return 0; }
            return engine->GetConstellation(constBuf.data(), constBuf.size());
        }

        static std::string escape(const std::string& text) {
            std::string out;
            for (char c : text) {
                if (c == '"' || c == '\\') { out += '\\'; out += c; }
                else if ((unsigned char)c < 0x20) { out += ' '; }
                else { out += c; }
            }
            return out;
        }

        std::string name = "Radio";
        ConfigManager* config = nullptr;
        int transmissionMode = 1;
        double audioSampleRate = 48000.0;

        // Engine and its input
        std::mutex engineMtx;
        std::unique_ptr<dabplus::Engine> engine;
        dsp::sink::Handler<dsp::complex_t> inputSink;
        bool started = false;

        // Audio: engine rate stream -> resampler -> the radio's AF chain
        dsp::stream<dsp::stereo_t> rawStream;
        dsp::multirate::RationalResampler<dsp::stereo_t> resamp;
        uint32_t resampInRate = uint32_t(DEFAULT_ENGINE_RATE);
        std::atomic<bool> audioRunning{false};
        // Anchor for pacing the input to the signal's own rate (see paceInput).
        std::chrono::steady_clock::time_point pacedAt;
        double pacedSignal = 0.0;
        std::atomic<float> audioPeak{0.0f};
        std::atomic<uint64_t> audioFrames{0};
        std::thread audioThread;

        // UI
        dabplus::Snapshot snapshot;
        std::chrono::steady_clock::time_point lastSnapshot;
        int selectedSubchannel = -1;
        bool userSelected = false;
        ImGui::ConstellationDiagram constDiagram;
        std::vector<std::complex<float>> constBuf = std::vector<std::complex<float>>(1024);
    };

}
