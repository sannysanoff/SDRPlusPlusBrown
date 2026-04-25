#pragma once
#include <imgui.h>
#include <module.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <signal_path/signal_path.h>
#include <config.h>
#include <dsp/chain.h>
#include <dsp/types.h>
#include <dsp/noise_reduction/noise_blanker.h>
#include <dsp/noise_reduction/fm_if.h>
#include <dsp/noise_reduction/power_squelch.h>
#include <dsp/noise_reduction/ctcss_squelch.h>
#include <dsp/multirate/rational_resampler.h>
#include <dsp/filter/deephasis.h>
#include <core.h>
#include <stdint.h>
#include <utils/optionlist.h>
#include <cmath>
#include <fftw3.h>
#include "radio_interface.h"
#include "demod.h"
#include "radio_module_interface.h"

extern ConfigManager config;

#define CONCAT(a, b) ((std::string(a) + b).c_str())

extern std::map<DeemphasisMode, double> deempTaus;

extern std::map<IFNRPreset, double> ifnrTaps;


class RadioModule : public ModuleManager::Instance, public RadioModuleInterface  {
public:


    RadioModule(std::string name) : RadioModuleInterface() {
        this->name = name;

        // Initialize option lists
        deempModes.define("None", DEEMP_MODE_NONE);
        deempModes.define("22us", DEEMP_MODE_22US);
        deempModes.define("50us", DEEMP_MODE_50US);
        deempModes.define("75us", DEEMP_MODE_75US);

        ifnrPresets.define("NOAA APT", IFNR_PRESET_NOAA_APT);
        ifnrPresets.define("Voice", IFNR_PRESET_VOICE);
        ifnrPresets.define("Narrow Band", IFNR_PRESET_NARROW_BAND);

        squelchModes.define("off", "Off", SQUELCH_MODE_OFF);
        squelchModes.define("power", "Power", SQUELCH_MODE_POWER);
        //squelchModes.define("snr", "SNR", SQUELCH_MODE_SNR);
        squelchModes.define("ctcss_mute", "CTCSS (Mute)", SQUELCH_MODE_CTCSS_MUTE);
        squelchModes.define("ctcss_decode", "CTCSS (Decode Only)", SQUELCH_MODE_CTCSS_DECODE);
        //squelchModes.define("dcs_mute", "DCS (Mute)", SQUELCH_MODE_DCS_MUTE);
        //squelchModes.define("dcs_decode", "DCS (Decode Only)", SQUELCH_MODE_DCS_DECODE);

        for (int i = 0; i < dsp::noise_reduction::_CTCSS_TONE_COUNT; i++) {
            float tone = dsp::noise_reduction::CTCSS_TONES[i];
            char buf[64];
            sprintf(buf, "%.1fHz", tone);
            ctcssTones.define((int)round(tone) * 10, buf, (dsp::noise_reduction::CTCSSTone)i);
        }
        ctcssTones.define(-1, "Any", dsp::noise_reduction::CTCSS_TONE_ANY);

        // Initialize the config if it doesn't exist
        bool created = false;
        config.acquire();
        if (!config.conf.contains(name)) {
            config.conf[name]["selectedDemodId"] = 1;
            created = true;
        }
        selectedDemodID = config.conf[name]["selectedDemodId"];
        config.release(created);

        // Initialize the VFO
        vfo = sigpath::vfoManager.createVFO(name, ImGui::WaterfallVFO::REF_CENTER, 0, 200000, 200000, 50000, 200000, false);
        onUserChangedBandwidthHandler.handler = vfoUserChangedBandwidthHandler;
        onUserChangedBandwidthHandler.ctx = this;
        vfo->wtfVFO->onUserChangedBandwidth.bindHandler(&onUserChangedBandwidthHandler);

        onUserChangedDemodulatorHandler.handler = vfoUserChangedDemodulatorHandler;
        onUserChangedDemodulatorHandler.ctx = this;
        vfo->wtfVFO->onUserChangedDemodulator.bindHandler(&onUserChangedDemodulatorHandler);

        // Initialize IF DSP chain
        ifChainOutputChanged.ctx = this;
        ifChainOutputChanged.handler = ifChainOutputChangeHandler;

        // Insert spectrum capture splitter before IF chain
        ifSplitter.init(vfo->output);
        ifSplitter.bindStream(&ifChainInputStream);
        ifSplitter.setHook([this](dsp::complex_t* data, int count) {
            std::lock_guard<std::mutex> lock(spectrumMtx);
            int toCopy = std::min(count, SPECTRUM_BUF_SIZE);
            if (spectrumBufPos + toCopy > SPECTRUM_BUF_SIZE) {
                int first = SPECTRUM_BUF_SIZE - spectrumBufPos;
                memcpy(&spectrumBuf[spectrumBufPos], data, first * sizeof(dsp::complex_t));
                memcpy(spectrumBuf, &data[first], (toCopy - first) * sizeof(dsp::complex_t));
            } else {
                memcpy(&spectrumBuf[spectrumBufPos], data, toCopy * sizeof(dsp::complex_t));
            }
            spectrumBufPos = (spectrumBufPos + toCopy) % SPECTRUM_BUF_SIZE;
        });
        ifChain.init(&ifChainInputStream);

        nb.init(NULL, 500.0 / 24000.0, 10.0);
        fmnr.init(NULL, 32);
        powerSquelch.init(NULL, MIN_SQUELCH);

        ifChain.addBlock(&nb, false);
        ifChain.addBlock(&powerSquelch, false);
        ifChain.addBlock(&fmnr, false);

        // Initialize audio DSP chain
        afChain.init(&dummyAudioStream);

        ctcss.init(NULL, 50000.0);
        resamp.init(NULL, 250000.0, 48000.0);
        hpTaps = dsp::taps::highPass(300.0, 100.0, 48000.0);
        hpf.init(NULL, hpTaps);
        deemp.init(NULL, 50e-6, 48000.0);

        afChain.addBlock(&ctcss, false);
        afChain.addBlock(&resamp, true);
        afChain.addBlock(&hpf, false);
        afChain.addBlock(&deemp, false);

        // Initialize the sink
        srChangeHandler.ctx = this;
        srChangeHandler.handler = sampleRateChangeHandler;

        // multi-sinks setup
        afsplitter.init(afChain.out);
        afsplitter.origin = "RadioModule.afsplitter";
        streams.emplace_back(std::make_shared<SinkManager::Stream>());
        streamNames.emplace_back(name);
        afsplitter.bindStream(streams.back()->getInput());
        afsplitter.setHook([=](dsp::stereo_t *ptr, int len) {
            auto data = std::make_shared<std::vector<dsp::stereo_t>>(ptr, ptr + len);
            auto event = std::make_shared<SinkManager::StreamHook>(name, SinkManager::StreamHook::SOURCE_DEMOD_OUTPUT, 0, audioSampleRate, data, std::shared_ptr<std::vector<dsp::complex_t>>());
            sigpath::sinkManager.onStream.emit(event);
        });
        streams.back()->init(&srChangeHandler, audioSampleRate);
        sigpath::sinkManager.registerStream(name, streams.back().get());

        sigpath::sinkManager.onAddSubstream.bindHandler(&onAddSubstreamHandler);
        sigpath::sinkManager.onRemoveSubstream.bindHandler(&onRemoveSubstreamHandler);
        onAddSubstreamHandler.handler = &addSubstreamHandler;
        onAddSubstreamHandler.ctx = this;
        onRemoveSubstreamHandler.handler = &removeSubstreamHandler;
        onRemoveSubstreamHandler.ctx = this;

        // read config for substreams
        for(int i=1; i<10; i++) {
            auto secondaryName = SinkManager::makeSecondaryStreamName(name, i);
            if (sigpath::sinkManager.configContains(secondaryName)) {
                addSecondaryStream(secondaryName);
            }
        }

        // Start IF chain
        ifSplitter.start();
        ifChain.start();

        // Start AF chain
        afChain.start();

        afsplitter.start();

        for (auto& s : streams) {
            s->start();
        }

        // Register the menu
        gui::menu.registerEntry(name, menuHandler, this, this);

        // Register the module interface
        core::modComManager.registerInterface("radio", name, moduleInterfaceHandler, this);

        txHandler.ctx = this;
        txHandler.handler = [](bool txActive, void *ctx){
            auto _this = (RadioModule*)ctx;
            _this->selectedDemod->setFrozen(txActive);
        };
        sigpath::txState.bindHandler(&txHandler);
    }

    // Automation channel — invoked by debug HTTP server
    std::string handleDebugCommand(const std::string& cmd, const std::string& args) override {
        if (cmd == "set_demod" || cmd == "set_demodulator") {
            // Try matching by name first (accept "FM", "NFM", "WFM", "AM", etc.)
            std::string modeName = args;
            for (auto& mode : radioModes) {
                if (mode.first == modeName) {
                    flog::info("Radio[{}]: selecting demod '{}' (ID={})", name, mode.first, mode.second);
                    selectDemodByID((DemodID)mode.second);
                    return "{\"status\": \"ok\", \"demod\": \"" + mode.first + "\", \"id\": " + std::to_string(mode.second) + "}";
                }
            }
            try {
                int id = std::stoi(args);
                flog::info("Radio[{}]: selecting demod by ID={}", name, id);
                selectDemodByID((DemodID)id);
                // Get the name back
                for (auto& mode : radioModes) {
                    if (mode.second == id) {
                        return "{\"status\": \"ok\", \"demod\": \"" + mode.first + "\", \"id\": " + args + "}";
                    }
                }
                return "{\"status\": \"ok\", \"id\": " + args + "}";
            } catch (...) {
                return "{\"error\": \"unknown demod '" + args + "'\"}";
            }
        }
        if (cmd == "get_demod") {
            for (auto& mode : radioModes) {
                if (mode.second == selectedDemodID) {
                    return "{\"demod\": \"" + mode.first + "\", \"id\": " + std::to_string(selectedDemodID) + "}";
                }
            }
            return "{\"demod\": \"unknown\", \"id\": " + std::to_string(selectedDemodID) + "}";
        }
        if (cmd == "set_freq") {
            try {
                double freq = std::stod(args);
                sigpath::sourceManager.tune(freq);
                return "{\"status\": \"ok\", \"frequency\": " + std::to_string(freq) + "}";
            } catch (...) {
                return "{\"error\": \"invalid frequency: '" + args + "'\"}";
            }
        }
        if (cmd == "get_spectrum") {
            // Parse: bandwidth_hz,num_buckets (optional)
            int numBuckets = 256;
            try {
                auto commaPos = args.find(',');
                if (commaPos != std::string::npos) {
                    numBuckets = std::stoi(args.substr(commaPos + 1));
                }
            } catch (...) {}
            if (numBuckets < 8) numBuckets = 8;
            if (numBuckets > 2048) numBuckets = 2048;

            // Capture a snapshot of the spectrum buffer
            std::vector<dsp::complex_t> snap;
            {
                std::lock_guard<std::mutex> lock(spectrumMtx);
                snap.resize(SPECTRUM_BUF_SIZE);
                memcpy(snap.data(), spectrumBuf, SPECTRUM_BUF_SIZE * sizeof(dsp::complex_t));
            }

            // Compute FFT on the captured data
            int fftSize = SPECTRUM_BUF_SIZE;
            std::vector<float> window(fftSize);
            for (int i = 0; i < fftSize; i++) {
                window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (fftSize - 1))); // Hann
            }

            // FFT via simple DFT for buckets (faster: average FFT bins into buckets)
            // Use FFTW if available, otherwise do a simple calculation
            // For bucket averaging: compute FFT, then average into buckets
            float* fftIn = (float*)fftwf_malloc(sizeof(fftwf_complex) * fftSize);
            fftwf_complex* fftOut = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * fftSize);
            auto plan = fftwf_plan_dft_1d(fftSize, (fftwf_complex*)fftIn, fftOut, FFTW_FORWARD, FFTW_ESTIMATE);

            for (int i = 0; i < fftSize; i++) {
                fftIn[2*i] = snap[i].re * window[i];
                fftIn[2*i+1] = snap[i].im * window[i];
            }
            fftwf_execute(plan);

            // Power spectrum
            std::vector<float> power(fftSize);
            float maxPower = 1e-30f;
            for (int i = 0; i < fftSize; i++) {
                float re = fftOut[i][0];
                float im = fftOut[i][1];
                power[i] = re*re + im*im;
                if (power[i] > maxPower) maxPower = power[i];
            }

            // Average into buckets
            int binsPerBucket = fftSize / numBuckets;
            std::string json = "{\"spectrum\": [";
            for (int b = 0; b < numBuckets; b++) {
                float sum = 0;
                for (int j = 0; j < binsPerBucket; j++) {
                    sum += power[b * binsPerBucket + j];
                }
                float avg = sum / binsPerBucket;
                float db = 10.0f * log10f(avg / maxPower + 1e-10f);
                if (b > 0) json += ", ";
                json += std::to_string(db);
            }
            json += "], \"num_buckets\": " + std::to_string(numBuckets);
            json += ", \"fft_size\": " + std::to_string(fftSize);
            json += ", \"max_bin\": " + std::to_string(maxPower);
            json += "}";

            fftwf_destroy_plan(plan);
            fftwf_free(fftIn);
            fftwf_free(fftOut);

            return json;
        }
        return "{\"error\": \"unknown command: " + cmd + "\"}";
    }




    void *getInterface(const char *name) override {
        if (!strcmp(name,"RadioModule")) {
            return (RadioModule*)this;
        }
        if (!strcmp(name,"RadioModuleInterface")) {
            return (RadioModuleInterface*)this;
        }
        return nullptr;
    }

    ~RadioModule() {
        sigpath::txState.unbindHandler(&txHandler);
        core::modComManager.unregisterInterface(name);
        gui::menu.removeEntry(name);
        afsplitter.stop();
        for (auto& s : streams) {
            s->stop();
        }
        if (enabled) {
            disable();
        }

        for (int i = 0; i < streams.size(); i++) {
            sigpath::sinkManager.unregisterStream(SinkManager::makeSecondaryStreamName(name, i));
        }
        sigpath::sinkManager.onAddSubstream.unbindHandler(&onAddSubstreamHandler);
        sigpath::sinkManager.onRemoveSubstream.unbindHandler(&onRemoveSubstreamHandler);
    }

    std::shared_ptr<SinkManager::Stream> addSecondaryStream(std::string secondaryName = "") {
        if (secondaryName.empty()) {
            secondaryName = SinkManager::makeSecondaryStreamName(name, streams.size());
        }
        streams.emplace_back(std::make_shared<SinkManager::Stream>());
        afsplitter.bindStream(streams.back()->getInput());
        streams.back()->init(&srChangeHandler, (float)audioSampleRate);
        sigpath::sinkManager.registerStream(secondaryName, &*streams.back());
        streamNames.emplace_back(secondaryName);

        return streams.back();
    }

    static void addSubstreamHandler(std::string name, void* ctx) {
        RadioModule* _this = (RadioModule*)ctx;
        if (name == _this->name) {
            auto stream = _this->addSecondaryStream();
            if (_this->enabled) {
                stream->start();
            }
        }
    }

    static void removeSubstreamHandler(std::string name, void* ctx) {
        RadioModule* _this = (RadioModule*)ctx;
        auto pos = std::find(_this->streamNames.begin(), _this->streamNames.end(), name);
        if (pos == _this->streamNames.end() || pos == _this->streamNames.begin()) // disable first stream removal as well
            return;
        auto index = pos - _this->streamNames.begin();
        auto stream = _this->streams[index];
        _this->afsplitter.unbindStream(stream->getInput());
        _this->streams.erase(_this->streams.begin()+index);
        _this->streamNames.erase(_this->streamNames.begin()+index);
        sigpath::sinkManager.unregisterStream(name);
        core::configManager.acquire();
        auto &streamz = core::configManager.conf["streams"];
        auto iter = streamz.find(name);
        if (iter != streamz.end()) {
            streamz.erase(iter);
        }
        core::configManager.release(true);
    }

    void postInit() {}

    void enable() {
        enabled = true;
        if (!vfo) {
            vfo = sigpath::vfoManager.createVFO(name, ImGui::WaterfallVFO::REF_CENTER, 0, 200000, 200000, 50000, 200000, false);
            vfo->wtfVFO->onUserChangedBandwidth.bindHandler(&onUserChangedBandwidthHandler);
            vfo->wtfVFO->onUserChangedDemodulator.bindHandler(&onUserChangedDemodulatorHandler);
        }
        ifSplitter.setInput(vfo->output);
        ifSplitter.start();
        ifChain.start();
        selectDemodByID((DemodID)selectedDemodID);
        afChain.start();
        afsplitter.start();
        for (auto& s : streams) {
            s->start();
        }
    }

    void disable() {
        enabled = false;
        for (auto& s : streams) {
            s->stop();
        }
        afsplitter.stop();
        ifSplitter.stop();
        ifChain.stop();
        if (selectedDemod) { selectedDemod->stop(); }
        afChain.stop();
        if (vfo) {
            sigpath::vfoManager.deleteVFO(vfo);
            vfo->wtfVFO->onUserChangedDemodulator.unbindHandler(&onUserChangedDemodulatorHandler);
        }
        vfo = NULL;
    }

    bool isEnabled() {
        return enabled;
    }

    std::string name;

    enum DemodID {
        RADIO_DEMOD_NFM,
        RADIO_DEMOD_WFM,
        RADIO_DEMOD_AM,
        RADIO_DEMOD_DSB,
        RADIO_DEMOD_USB,
        RADIO_DEMOD_CW,
        RADIO_DEMOD_LSB,
        RADIO_DEMOD_RAW,
        _RADIO_DEMOD_COUNT,
    };

private:
    static void menuHandler(void* ctx) {
        RadioModule* _this = (RadioModule*)ctx;

        if (!_this->enabled) { style::beginDisabled(); }

        float menuWidth = ImGui::GetContentRegionAvail().x;
        ImGui::BeginGroup();

        ImGui::Columns(4, CONCAT("RadioModeColumns##_", _this->name), false);
        if (ImGui::RadioButton(CONCAT("NFM##_", _this->name), _this->selectedDemodID == 0) && _this->selectedDemodID != 0) {
            _this->selectDemodByID(RADIO_DEMOD_NFM);
        }
        if (ImGui::RadioButton(CONCAT("WFM##_", _this->name), _this->selectedDemodID == 1) && _this->selectedDemodID != 1) {
            _this->selectDemodByID(RADIO_DEMOD_WFM);
        }
        ImGui::NextColumn();
        if (ImGui::RadioButton(CONCAT("AM##_", _this->name), _this->selectedDemodID == 2) && _this->selectedDemodID != 2) {
            _this->selectDemodByID(RADIO_DEMOD_AM);
        }
        if (ImGui::RadioButton(CONCAT("DSB##_", _this->name), _this->selectedDemodID == 3) && _this->selectedDemodID != 3) {
            _this->selectDemodByID(RADIO_DEMOD_DSB);
        }
        ImGui::NextColumn();
        if (ImGui::RadioButton(CONCAT("USB##_", _this->name), _this->selectedDemodID == 4) && _this->selectedDemodID != 4) {
            _this->selectDemodByID(RADIO_DEMOD_USB);
        }
        if (ImGui::RadioButton(CONCAT("CW##_", _this->name), _this->selectedDemodID == 5) && _this->selectedDemodID != 5) {
            _this->selectDemodByID(RADIO_DEMOD_CW);
        };
        ImGui::NextColumn();
        if (ImGui::RadioButton(CONCAT("LSB##_", _this->name), _this->selectedDemodID == 6) && _this->selectedDemodID != 6) {
            _this->selectDemodByID(RADIO_DEMOD_LSB);
        }
        if (ImGui::RadioButton(CONCAT("RAW##_", _this->name), _this->selectedDemodID == 7) && _this->selectedDemodID != 7) {
            _this->selectDemodByID(RADIO_DEMOD_RAW);
        };
        ImGui::Columns(1, CONCAT("EndRadioModeColumns##_", _this->name), false);

        ImGui::EndGroup();

        if (!_this->bandwidthLocked) {
            ImGui::LeftLabel("Bandwidth");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::InputFloat(("##_radio_bw_" + _this->name).c_str(), &_this->bandwidth, 1, 100, "%.0f")) {
                _this->bandwidth = std::clamp<float>(_this->bandwidth, _this->minBandwidth, _this->maxBandwidth);
                _this->setBandwidth(_this->bandwidth);
            }
        }

        // VFO snap interval
        ImGui::LeftLabel("Snap Interval");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputInt(("##_radio_snap_" + _this->name).c_str(), &_this->snapInterval, 1, 100)) {
            if (_this->snapInterval < 1) { _this->snapInterval = 1; }
            _this->vfo->setSnapInterval(_this->snapInterval);
            config.acquire();
            config.conf[_this->name][_this->selectedDemod->getName()]["snapInterval"] = _this->snapInterval;
            config.release(true);
        }

        // Deemphasis mode
        if (_this->deempAllowed) {
            ImGui::LeftLabel("De-emphasis");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::Combo(("##_radio_wfm_deemp_" + _this->name).c_str(), &_this->deempId, _this->deempModes.txt)) {
                _this->setDeemphasisMode(_this->deempModes[_this->deempId]);
            }
        }

        // Squelch
        if (_this->squelchAllowed) {
            ImGui::LeftLabel("Squelch Mode");
            ImGui::FillWidth();
            if (ImGui::Combo(("##_radio_sqelch_mode_" + _this->name).c_str(), &_this->squelchModeId, _this->squelchModes.txt)) {
                _this->setSquelchMode(_this->squelchModes[_this->squelchModeId]);
            }
            switch (_this->squelchModes[_this->squelchModeId]) {
            case SQUELCH_MODE_POWER:
                ImGui::LeftLabel("Squelch Level");
                ImGui::FillWidth();
                if (ImGui::SliderFloat(("##_radio_sqelch_lvl_" + _this->name).c_str(), &_this->squelchLevel, _this->MIN_SQUELCH, _this->MAX_SQUELCH, "%.3fdB")) {
                    _this->setSquelchLevel(_this->squelchLevel);
                }
                break;

            case SQUELCH_MODE_CTCSS_MUTE:
                if (_this->squelchModes[_this->squelchModeId] == SQUELCH_MODE_CTCSS_MUTE) {
                    ImGui::LeftLabel("CTCSS Tone");
                    ImGui::FillWidth();
                    if (ImGui::Combo(("##_radio_ctcss_tone_" + _this->name).c_str(), &_this->ctcssToneId, _this->ctcssTones.txt)) {
                        _this->setCTCSSTone(_this->ctcssTones[_this->ctcssToneId]);
                    }
                }
            }
        }

        // Noise blanker
        if (_this->nbAllowed) {
            if (ImGui::Checkbox(("Noise blanker (W.I.P.)##_radio_nb_ena_" + _this->name).c_str(), &_this->nbEnabled)) {
                _this->setNBEnabled(_this->nbEnabled);
            }
            if (!_this->nbEnabled && _this->enabled) { style::beginDisabled(); }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::SliderFloat(("##_radio_nb_lvl_" + _this->name).c_str(), &_this->nbLevel, _this->MIN_NB, _this->MAX_NB, "%.3fdB")) {
                _this->setNBLevel(_this->nbLevel);
            }
            if (!_this->nbEnabled && _this->enabled) { style::endDisabled(); }
        }

        // FM IF Noise Reduction
        if (_this->FMIFNRAllowed) {
            if (ImGui::Checkbox(("IF Noise Reduction##_radio_fmifnr_ena_" + _this->name).c_str(), &_this->FMIFNREnabled)) {
                _this->setFMIFNREnabled(_this->FMIFNREnabled);
            }
            if (_this->selectedDemodID == RADIO_DEMOD_NFM) {
                if (!_this->FMIFNREnabled && _this->enabled) { style::beginDisabled(); }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
                if (ImGui::Combo(("##_radio_fmifnr_ena_" + _this->name).c_str(), &_this->fmIFPresetId, _this->ifnrPresets.txt)) {
                    _this->setIFNRPreset(_this->ifnrPresets[_this->fmIFPresetId]);
                }
                if (!_this->FMIFNREnabled && _this->enabled) { style::endDisabled(); }
            }
        }

        // High pass
        if (_this->highPassAllowed) {
            if (ImGui::Checkbox(("High Pass##_radio_hpf_" + _this->name).c_str(), &_this->highPass)) {
                _this->setHighPass(_this->highPass);
            }
        }

        // Demodulator specific menu
        _this->selectedDemod->showMenu();

        // Display the squelch diagnostics
        switch (_this->squelchModes[_this->squelchModeId]) {
        case SQUELCH_MODE_CTCSS_MUTE:
            ImGui::TextUnformatted("Received Tone:");
            ImGui::SameLine();
            {
                auto ctone = _this->ctcss.getCurrentTone();
                auto dtone = _this->ctcssTones[_this->ctcssToneId];
                if (ctone != dsp::noise_reduction::CTCSS_TONE_NONE) {
                    if (dtone == dsp::noise_reduction::CTCSS_TONE_ANY || ctone == dtone) {
                        ImGui::TextColored(ImVec4(0, 1, 0, 1), "%.1fHz", dsp::noise_reduction::CTCSS_TONES[_this->ctcss.getCurrentTone()]);
                    }
                    else {
                        ImGui::TextColored(ImVec4(1, 0, 0, 1), "%.1fHz", dsp::noise_reduction::CTCSS_TONES[_this->ctcss.getCurrentTone()]);
                    }
                }
                else {
                    ImGui::TextUnformatted("None");
                }
            }
            break;
            
        case SQUELCH_MODE_CTCSS_DECODE:
            ImGui::TextUnformatted("Received Tone:");
            ImGui::SameLine();
            {
                auto ctone = _this->ctcss.getCurrentTone();
                if (ctone != dsp::noise_reduction::CTCSS_TONE_NONE) {
                    ImGui::TextColored(ImVec4(0, 1, 0, 1), "%.1fHz", dsp::noise_reduction::CTCSS_TONES[_this->ctcss.getCurrentTone()]);
                }
                else {
                    ImGui::TextUnformatted("None");
                }
            }
            break;
        }

        if (!_this->enabled) { style::endDisabled(); }
    }

    demod::Demodulator* instantiateDemod(DemodID id) {
        demod::Demodulator* demod = NULL;
        switch (id) {
            case DemodID::RADIO_DEMOD_NFM:  demod = new demod::NFM(); break;
            case DemodID::RADIO_DEMOD_WFM:  demod = new demod::WFM(); break;
            case DemodID::RADIO_DEMOD_AM:   demod = new demod::AM();  break;
            case DemodID::RADIO_DEMOD_DSB:  demod = new demod::DSB(); break;
            case DemodID::RADIO_DEMOD_USB:  demod = new demod::USB(); break;
            case DemodID::RADIO_DEMOD_CW:   demod = new demod::CW();  break;
            case DemodID::RADIO_DEMOD_LSB:  demod = new demod::LSB(); break;
            case DemodID::RADIO_DEMOD_RAW:  demod = new demod::RAW(); break;
            default:                        demod = NULL;             break;
        }
        if (!demod) { return NULL; }

        // Default config
        double bw = demod->getDefaultBandwidth();
        config.acquire();
        if (!config.conf[name].contains(demod->getName())) {
            config.conf[name][demod->getName()]["bandwidth"] = bw;
            config.conf[name][demod->getName()]["snapInterval"] = demod->getDefaultSnapInterval();
            config.conf[name][demod->getName()]["squelchLevel"] = MIN_SQUELCH;
            config.conf[name][demod->getName()]["squelchEnabled"] = false;
            config.release(true);
        }
        else {
            config.release();
        }
        bw = std::clamp<double>(bw, demod->getMinBandwidth(), demod->getMaxBandwidth());

        // Initialize
        demod->init(name, &config, ifChain.out, bw, stream.getSampleRate());

        return demod;
    }

    void selectDemodByID(DemodID id) {
        auto startTime = std::chrono::high_resolution_clock::now();
        demod::Demodulator* demod = instantiateDemod(id);
        if (!demod) {
            flog::error("Demodulator {0} not implemented", (int)id);
            return;
        }
        selectedDemodID = id;
        selectDemod(demod);

        // Save config
        config.acquire();
        config.conf[name]["selectedDemodId"] = id;
        config.release(true);
        auto endTime = std::chrono::high_resolution_clock::now();
        flog::warn("Demod switch took {0} us", (int64_t)((std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime)).count()));
    }

    void selectDemod(demod::Demodulator* demod) {
        // Stopcurrently selected demodulator and select new
        afChain.setInput(&dummyAudioStream, [=](dsp::stream<dsp::stereo_t>* out){ stream.setInput(out); });
        if (selectedDemod) {
            selectedDemod->stop();
            delete selectedDemod;
        }
        selectedDemod = demod;

        // Give the demodulator the most recent audio SR
        selectedDemod->AFSampRateChanged(audioSampleRate);

        // Set the demodulator's input
        selectedDemod->setInput(ifChain.out);

        // Set AF chain's input
        afChain.setInput(selectedDemod->getOutput(), [=](dsp::stream<dsp::stereo_t>* out){ stream.setInput(out); });

        // Load config
        bandwidth = selectedDemod->getDefaultBandwidth();
        minBandwidth = selectedDemod->getMinBandwidth();
        maxBandwidth = selectedDemod->getMaxBandwidth();
        bandwidthLocked = selectedDemod->getBandwidthLocked();
        snapInterval = selectedDemod->getDefaultSnapInterval();
        deempAllowed = selectedDemod->getDeempAllowed();
        deempId = deempModes.valueId((DeemphasisMode)selectedDemod->getDefaultDeemphasisMode());
        squelchModeId = squelchModes.valueId(SQUELCH_MODE_OFF);
        squelchLevel = MIN_SQUELCH;
        ctcssToneId = ctcssTones.valueId(dsp::noise_reduction::CTCSS_TONE_67Hz);
        highPass = false;

        postProcEnabled = selectedDemod->getPostProcEnabled();
        FMIFNRAllowed = selectedDemod->getFMIFNRAllowed();
        FMIFNREnabled = false;
        fmIFPresetId = ifnrPresets.valueId(IFNR_PRESET_VOICE);
        nbAllowed = selectedDemod->getNBAllowed();
        squelchAllowed = selectedDemod->getSquelchAllowed();
        highPassAllowed = selectedDemod->getHighPassAllowed();
        nbEnabled = false;
        nbLevel = 0.0f;
        double ifSamplerate = selectedDemod->getIFSampleRate();
        config.acquire();
        if (config.conf[name][selectedDemod->getName()].contains("bandwidth")) {
            bandwidth = config.conf[name][selectedDemod->getName()]["bandwidth"];
            bandwidth = std::clamp<double>(bandwidth, minBandwidth, maxBandwidth);
        }
        if (config.conf[name][selectedDemod->getName()].contains("snapInterval")) {
            snapInterval = config.conf[name][selectedDemod->getName()]["snapInterval"];
        }

        if (config.conf[name][selectedDemod->getName()].contains("squelchMode")) {
            std::string squelchModeStr = config.conf[name][selectedDemod->getName()]["squelchMode"];
            if (squelchModes.keyExists(squelchModeStr)) {
                squelchModeId = squelchModes.keyId(squelchModeStr);
            }
        }
        if (config.conf[name][selectedDemod->getName()].contains("squelchLevel")) {
            squelchLevel = config.conf[name][selectedDemod->getName()]["squelchLevel"];
        }
        if (config.conf[name][selectedDemod->getName()].contains("ctcssTone")) {
            int ctcssToneX10 = config.conf[name][selectedDemod->getName()]["ctcssTone"];
            if (ctcssTones.keyExists(ctcssToneX10)) {
                ctcssToneId = ctcssTones.keyId(ctcssToneX10);
            }
        }
        if (config.conf[name][selectedDemod->getName()].contains("highPass")) {
            highPass = config.conf[name][selectedDemod->getName()]["highPass"];
        }
        if (config.conf[name][selectedDemod->getName()].contains("deempMode")) {
            if (!config.conf[name][selectedDemod->getName()]["deempMode"].is_string()) {
                config.conf[name][selectedDemod->getName()]["deempMode"] = deempModes.key(deempId);
            }

            std::string deempOpt = config.conf[name][selectedDemod->getName()]["deempMode"];
            if (deempModes.keyExists(deempOpt)) {
                deempId = deempModes.keyId(deempOpt);
            }
        }
        if (config.conf[name][selectedDemod->getName()].contains("FMIFNREnabled")) {
            FMIFNREnabled = config.conf[name][selectedDemod->getName()]["FMIFNREnabled"];
        }
        if (config.conf[name][selectedDemod->getName()].contains("fmifnrPreset")) {
            std::string presetOpt = config.conf[name][selectedDemod->getName()]["fmifnrPreset"];
            if (ifnrPresets.keyExists(presetOpt)) {
                fmIFPresetId = ifnrPresets.keyId(presetOpt);
            }
        }
        if (config.conf[name][selectedDemod->getName()].contains("noiseBlankerEnabled")) {
            nbEnabled = config.conf[name][selectedDemod->getName()]["noiseBlankerEnabled"];
        }
        if (config.conf[name][selectedDemod->getName()].contains("noiseBlankerLevel")) {
            nbLevel = config.conf[name][selectedDemod->getName()]["noiseBlankerLevel"];
        }
        config.release();

        // Configure VFO
        if (vfo) {
            vfo->setBandwidthLimits(minBandwidth, maxBandwidth, selectedDemod->getBandwidthLocked());
            vfo->setReference(selectedDemod->getVFOReference());
            vfo->setSnapInterval(snapInterval);
            vfo->setSampleRate(ifSamplerate, bandwidth);
        }

        // Configure bandwidth
        setBandwidth(bandwidth);

        // Configure noise blanker
        nb.setRate(500.0 / ifSamplerate);
        setNBLevel(nbLevel);
        setNBEnabled(nbAllowed && nbEnabled);

        // Configure FM IF Noise Reduction
        setIFNRPreset((selectedDemodID == RADIO_DEMOD_NFM) ? ifnrPresets[fmIFPresetId] : IFNR_PRESET_BROADCAST);
        setFMIFNREnabled(FMIFNRAllowed ? FMIFNREnabled : false);

        // Configure squelch
        setSquelchMode(squelchAllowed ? squelchModes[squelchModeId] : SQUELCH_MODE_OFF);
        setSquelchLevel(squelchLevel);
        setCTCSSTone(ctcssTones[ctcssToneId]);

        // Configure AF chain
        if (postProcEnabled) {
            // Configure resampler
            afChain.stop();
            double afsr = selectedDemod->getAFSampleRate();
            ctcss.setSamplerate(afsr);
            resamp.setInSamplerate(afsr);
            afChain.enableBlock(&resamp, [=](dsp::stream<dsp::stereo_t>* out){ stream.setInput(out); });
            setAudioSampleRate(audioSampleRate);

            // Configure the HPF
            setHighPass(highPass && highPassAllowed);

            // Configure deemphasis
            setDeemphasisMode(deempModes[deempId]);
        }
        else {
            // Disable everything if post processing is disabled
            afChain.disableAllBlocks([=](dsp::stream<dsp::stereo_t>* out){ stream.setInput(out); });
        }

        // Start new demodulator
        selectedDemod->start();
    }


    void setBandwidth(double bw) {
        bw = std::clamp<double>(bw, minBandwidth, maxBandwidth);
        bandwidth = bw;
        if (!selectedDemod) { return; }
        vfo->setBandwidth(bandwidth);
        selectedDemod->setBandwidth(bandwidth);

        config.acquire();
        config.conf[name][selectedDemod->getName()]["bandwidth"] = bandwidth;
        config.release(true);
    }

    void setAudioSampleRate(double sr) {
        audioSampleRate = sr;
        if (!selectedDemod) { return; }
        selectedDemod->AFSampRateChanged(audioSampleRate);
        if (!postProcEnabled && vfo) {
            // If postproc is disabled, IF SR = AF SR
            minBandwidth = selectedDemod->getMinBandwidth();
            maxBandwidth = selectedDemod->getMaxBandwidth();
            bandwidth = selectedDemod->getIFSampleRate();
            vfo->setBandwidthLimits(minBandwidth, maxBandwidth, selectedDemod->getBandwidthLocked());
            vfo->setSampleRate(selectedDemod->getIFSampleRate(), bandwidth);
            return;
        }

        afChain.stop();

        // Configure resampler
        resamp.setOutSamplerate(audioSampleRate);

        // Configure the HPF sample rate
        hpTaps = dsp::taps::highPass(300.0, 100.0, audioSampleRate);
        hpf.setTaps(hpTaps);

        // Configure deemphasis sample rate
        deemp.setSamplerate(audioSampleRate);

        afChain.start();
    }

    void setHighPass(bool enabled) {
        // Update the state
        highPass = enabled;

        // Check if post-processing is enabled and that a demodulator is selected
        if (!postProcEnabled || !selectedDemod) { return; }

        // Set the state of the HPF in the AF chain
        afChain.setBlockEnabled(&hpf, enabled, [=](dsp::stream<dsp::stereo_t>* out){ stream.setInput(out); });

        // Save config
        config.acquire();
        config.conf[name][selectedDemod->getName()]["highPass"] = enabled;
        config.release(true);
    }

    void setDeemphasisMode(DeemphasisMode mode) {
        deempId = deempModes.valueId(mode);
        if (!postProcEnabled || !selectedDemod) { return; }
        bool deempEnabled = (mode != DEEMP_MODE_NONE);
        if (deempEnabled) { deemp.setTau(deempTaus[mode]); }
        afChain.setBlockEnabled(&deemp, deempEnabled, [=](dsp::stream<dsp::stereo_t>* out){ stream.setInput(out); });

        // Save config
        config.acquire();
        config.conf[name][selectedDemod->getName()]["deempMode"] = deempModes.key(deempId);
        config.release(true);
    }

    void setNBEnabled(bool enable) {
        nbEnabled = enable;
        if (!selectedDemod) { return; }
        ifChain.setBlockEnabled(&nb, nbEnabled, [=](dsp::stream<dsp::complex_t>* out){ selectedDemod->setInput(out); });

        // Save config
        config.acquire();
        config.conf[name][selectedDemod->getName()]["noiseBlankerEnabled"] = nbEnabled;
        config.release(true);
    }

    void setNBLevel(float level) {
        nbLevel = std::clamp<float>(level, MIN_NB, MAX_NB);
        nb.setLevel(nbLevel);
        if (!selectedDemod) { return; }

        // Save config
        config.acquire();
        config.conf[name][selectedDemod->getName()]["noiseBlankerLevel"] = nbLevel;
        config.release(true);
    }

    void setSquelchMode(SquelchMode mode) {
        squelchModeId = squelchModes.valueId(mode);
        if (!selectedDemod) { return; }

        // Disable all squelch blocks
        ifChain.disableBlock(&powerSquelch, [=](dsp::stream<dsp::complex_t>* out){ selectedDemod->setInput(out); });
        afChain.disableBlock(&ctcss, [=](dsp::stream<dsp::stereo_t>* out){ stream.setInput(out); });

        // Enable the block depending on the mode
        switch (mode) {
        case SQUELCH_MODE_OFF:
            break;

        case SQUELCH_MODE_POWER:
            // Enable the power squelch block
            ifChain.enableBlock(&powerSquelch, [=](dsp::stream<dsp::complex_t>* out){ selectedDemod->setInput(out); });
            break;

        case SQUELCH_MODE_SNR:
            // TODO
            break;

        case SQUELCH_MODE_CTCSS_MUTE:
            // Set the required tone and enable the CTCSS squelch block
            ctcss.setRequiredTone(ctcssTones[ctcssToneId]);
            afChain.enableBlock(&ctcss, [=](dsp::stream<dsp::stereo_t>* out){ stream.setInput(out); });
            break;

        case SQUELCH_MODE_CTCSS_DECODE:
            // Set the required tone to none and enable the CTCSS squelch block
            ctcss.setRequiredTone(dsp::noise_reduction::CTCSS_TONE_NONE);
            afChain.enableBlock(&ctcss, [=](dsp::stream<dsp::stereo_t>* out){ stream.setInput(out); });
            break;

        case SQUELCH_MODE_DCS_MUTE:
            // TODO
            break;

        case SQUELCH_MODE_DCS_DECODE:
            // TODO
            break;
        }

        // Save config
        config.acquire();
        config.conf[name][selectedDemod->getName()]["squelchMode"] = squelchModes.key(squelchModeId);
        config.release(true);
    }

    void setSquelchLevel(float level) {
        squelchLevel = std::clamp<float>(level, MIN_SQUELCH, MAX_SQUELCH);
        powerSquelch.setLevel(squelchLevel);
        if (!selectedDemod) { return; }

        // Save config
        config.acquire();
        config.conf[name][selectedDemod->getName()]["squelchLevel"] = squelchLevel;
        config.release(true);
    }

    void setCTCSSTone(dsp::noise_reduction::CTCSSTone tone) {
        // Check for an invalid value
        if (tone == dsp::noise_reduction::CTCSS_TONE_NONE) { return; }

        // If not in CTCSS mute mode, do nothing
        if (squelchModes[squelchModeId] != SQUELCH_MODE_CTCSS_MUTE) { return; }

        // Set the tone
        ctcssToneId = ctcssTones.valueId(tone);
        ctcss.setRequiredTone(tone);
        if (!selectedDemod) { return; }

        // Save config
        config.acquire();
        config.conf[name][selectedDemod->getName()]["ctcssTone"] = ctcssTones.key(ctcssToneId);
        config.release(true);
    }

    void setFMIFNREnabled(bool enabled) {
        FMIFNREnabled = enabled;
        if (!selectedDemod) { return; }
        ifChain.setBlockEnabled(&fmnr, FMIFNREnabled, [=](dsp::stream<dsp::complex_t>* out){ selectedDemod->setInput(out); });

        // Save config
        config.acquire();
        config.conf[name][selectedDemod->getName()]["FMIFNREnabled"] = FMIFNREnabled;
        config.release(true);
    }

    void setIFNRPreset(IFNRPreset preset) {
        // Don't save if in broadcast mode
        if (preset == IFNR_PRESET_BROADCAST) {
            if (!selectedDemod) { return; }
            fmnr.setBins(ifnrTaps[preset]);
            return;
        }

        fmIFPresetId = ifnrPresets.valueId(preset);
        if (!selectedDemod) { return; }
        fmnr.setBins(ifnrTaps[preset]);

        // Save config
        config.acquire();
        config.conf[name][selectedDemod->getName()]["fmifnrPreset"] = ifnrPresets.key(fmIFPresetId);
        config.release(true);
    }

    static void vfoUserChangedBandwidthHandler(double newBw, void* ctx) {
        RadioModule* _this = (RadioModule*)ctx;
        _this->setBandwidth(newBw);
    }

    static void sampleRateChangeHandler(float sampleRate, void* ctx) {
        RadioModule* _this = (RadioModule*)ctx;
        _this->setAudioSampleRate(sampleRate);
    }

    static void ifChainOutputChangeHandler(dsp::stream<dsp::complex_t>* output, void* ctx) {
        RadioModule* _this = (RadioModule*)ctx;
        if (!_this->selectedDemod) { return; }
        _this->selectedDemod->setInput(output);
    }

    static void moduleInterfaceHandler(int code, void* in, void* out, void* ctx) {
        RadioModule* _this = (RadioModule*)ctx;

        // If no demod is selected, reject the command
        if (!_this->selectedDemod) { return; }

        // Execute commands
        if (code == RADIO_IFACE_CMD_GET_MODE && out) {
            int* _out = (int*)out;
            *_out = _this->selectedDemodID;
        }
        else if (code == RADIO_IFACE_CMD_SET_MODE && in && _this->enabled) {
            int* _in = (int*)in;
            _this->selectDemodByID((DemodID)*_in);
        }
        else if (code == RADIO_IFACE_CMD_GET_BANDWIDTH && out) {
            float* _out = (float*)out;
            *_out = _this->bandwidth;
        }
        else if (code == RADIO_IFACE_CMD_SET_BANDWIDTH && in && _this->enabled) {
            float* _in = (float*)in;
            if (_this->bandwidthLocked) { return; }
            _this->setBandwidth(*_in);
        }
        else if (code == RADIO_IFACE_CMD_GET_SQUELCH_MODE && out) {
            SquelchMode* _out = (SquelchMode*)out;
            *_out = _this->squelchModes[_this->squelchModeId];
        }
        else if (code == RADIO_IFACE_CMD_SET_SQUELCH_MODE && in && _this->enabled) {
            SquelchMode* _in = (SquelchMode*)in;
            _this->setSquelchMode(*_in);
        }
        else if (code == RADIO_IFACE_CMD_GET_SQUELCH_LEVEL && out) {
            float* _out = (float*)out;
            *_out = _this->squelchLevel;
        }
        else if (code == RADIO_IFACE_CMD_SET_SQUELCH_LEVEL && in && _this->enabled) {
            float* _in = (float*)in;
            _this->setSquelchLevel(*_in);
        }
        else if (code == RADIO_IFACE_CMD_GET_CTCSS_TONE && out) {
            dsp::noise_reduction::CTCSSTone* _out = (dsp::noise_reduction::CTCSSTone*)out;
            *_out = _this->ctcssTones[_this->ctcssToneId];
        }
        else if (code == RADIO_IFACE_CMD_SET_CTCSS_TONE && in && _this->enabled) {
            dsp::noise_reduction::CTCSSTone* _in = (dsp::noise_reduction::CTCSSTone*)in;
            _this->setCTCSSTone(*_in);
        }
        else if (code == RADIO_IFACE_CMD_GET_HIGHPASS && out) {
            bool* _out = (bool*)out;
            *_out = _this->highPass;
        }
        else if (code == RADIO_IFACE_CMD_SET_HIGHPASS && in && _this->enabled) {
            bool* _in = (bool*)in;
            _this->setHighPass(*_in);
        }
        else {
            return;
        }

        // Success
        return;
    }

    // Handlers
    EventHandler<double> onUserChangedBandwidthHandler;
    EventHandler<int> onUserChangedDemodulatorHandler;
    EventHandler<float> srChangeHandler;
    EventHandler<std::string> onAddSubstreamHandler;
    EventHandler<std::string> onRemoveSubstreamHandler;

    EventHandler<dsp::stream<dsp::complex_t>*> ifChainOutputChanged;
    EventHandler<dsp::stream<dsp::stereo_t>*> afChainOutputChanged;

    VFOManager::VFO* vfo = NULL;

    // IF chain
    dsp::chain<dsp::complex_t> ifChain;
    dsp::stream<dsp::complex_t> ifChainInputStream;
    dsp::noise_reduction::NoiseBlanker nb;
    dsp::noise_reduction::FMIF fmnr;
    dsp::noise_reduction::PowerSquelch powerSquelch;

    // Audio chain
    dsp::stream<dsp::stereo_t> dummyAudioStream;
    dsp::chain<dsp::stereo_t> afChain;
    dsp::noise_reduction::CTCSSSquelch ctcss;
    dsp::multirate::RationalResampler<dsp::stereo_t> resamp;
    dsp::tap<float> hpTaps;
    dsp::filter::FIR<dsp::stereo_t, float> hpf;
    dsp::filter::Deemphasis<dsp::stereo_t> deemp;

    // Spectrum capture splitter (taps VFO output before IF chain)
    dsp::routing::Splitter<dsp::complex_t> ifSplitter;
    static constexpr int SPECTRUM_BUF_SIZE = 131072;
    dsp::complex_t spectrumBuf[SPECTRUM_BUF_SIZE];
    int spectrumBufPos = 0;
    std::mutex spectrumMtx;

    dsp::routing::Splitter<dsp::stereo_t> afsplitter;
    std::vector<std::shared_ptr<SinkManager::Stream>> streams;
    std::vector<std::string> streamNames;

    demod::Demodulator* selectedDemod = NULL;

    OptionList<std::string, DeemphasisMode> deempModes;
    OptionList<std::string, IFNRPreset> ifnrPresets;
    OptionList<std::string, SquelchMode> squelchModes;
    OptionList<int, dsp::noise_reduction::CTCSSTone> ctcssTones;

    double audioSampleRate = 48000.0;
    float minBandwidth;
    float maxBandwidth;
    float bandwidth;
    bool bandwidthLocked;
    int snapInterval;
    int selectedDemodID = 1;
    bool postProcEnabled;

    int squelchModeId = 0;
    float squelchLevel;
    int ctcssToneId = 0;
    bool squelchAllowed = false;

    bool highPass = false;
    bool highPassAllowed = false;

    int deempId = 0;
    bool deempAllowed;

    bool FMIFNRAllowed;
    bool FMIFNREnabled = false;
    int fmIFPresetId;

    bool notchEnabled = false;
    float notchPos = 0;
    float notchWidth = 500;

    bool nbAllowed;
    bool nbEnabled = false;
    float nbLevel = 10.0f;

    const double MIN_NB = 1.0;
    const double MAX_NB = 10.0;
    const double MIN_SQUELCH = -100.0;
    const double MAX_SQUELCH = 0.0;

    bool enabled = true;

    EventHandler<bool> txHandler;

    std::map<std::string, int> radioModes = {
        {"NFM", 0}, {"WFM", 1}, {"AM", 2}, {"DSB", 3},
        {"USB", 4}, {"CW", 5}, {"LSB", 6}, {"RAW", 7}
    };
};
