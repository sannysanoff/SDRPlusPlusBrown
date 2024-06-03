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
#include <dsp/noise_reduction/squelch.h>
#include <dsp/multirate/rational_resampler.h>
#include <dsp/filter/deephasis.h>
#include <core.h>
#include <stdint.h>
#include <utils/optionlist.h>
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
        ifChain.init(vfo->output);

        nb.init(NULL, 500.0 / 24000.0, 10.0);
        fmnr.init(NULL, 32);
        squelch.init(NULL, MIN_SQUELCH);

        ifChain.addBlock(&nb, false);
        ifChain.addBlock(&squelch, false);
        ifChain.addBlock(&fmnr, false);

        // Initialize audio DSP chain
        afChain.init(&dummyAudioStream);

        resamp.init(NULL, 250000.0, 48000.0);
        deemp.init(NULL, 50e-6, 48000.0);

        afChain.addBlock(&resamp, true);
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



    void *getInterface(const char *name) override {
        if (!strcmp(name,"RadioModule")) {
            return (RadioModule*)this;
        }
        if (!strcmp(name,"RadioModuleInterface")) {
            return (RadioModuleInterface*)this;
        }
        return nullptr;
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




    ~RadioModule() {
        sigpath::txState.unbindHandler(&txHandler);
        core::modComManager.unregisterInterface(name);
        gui::menu.removeEntry(name);
        afsplitter.stop();
        for(auto &s: streams) {
            s->stop();
        }
        if (enabled) {
            disable();
        }

        for(int i=0; i<streams.size(); i++) {
            sigpath::sinkManager.unregisterStream(SinkManager::makeSecondaryStreamName(name, i));
        }
        sigpath::sinkManager.onAddSubstream.unbindHandler(&onAddSubstreamHandler);
        sigpath::sinkManager.onRemoveSubstream.unbindHandler(&onRemoveSubstreamHandler);

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
        // if (_this->enabled) {
        //     stream->stop();
        // }
        _this->afsplitter.unbindStream(stream->getInput());
        //        stream->init(&srChangeHandler, audioSampleRate);
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



    void postInit() override {
        // Select the demodulator
        if (enabled) {
            if (!selectDemodByID((DemodID) selectedDemodID)) {
                // can happen if module not loaded.
                selectedDemodID = 1;
                selectDemodByID((DemodID) selectedDemodID);
            }
        }


    }

    void enable() override {
        enabled = true;
        if (!vfo) {
            vfo = sigpath::vfoManager.createVFO(name, ImGui::WaterfallVFO::REF_CENTER, 0, 200000, 200000, 50000, 200000, false);
            vfo->wtfVFO->onUserChangedBandwidth.bindHandler(&onUserChangedBandwidthHandler);
            vfo->wtfVFO->onUserChangedDemodulator.bindHandler(&onUserChangedDemodulatorHandler);
        }
        ifChain.setInput(vfo->output, [=](dsp::stream<dsp::complex_t>* out){ ifChainOutputChangeHandler(out, this); });
        ifChain.start();
        selectDemodByID((DemodID)selectedDemodID);
        afChain.start();
    }

    void disable() override {
        enabled = false;
        ifChain.stop();
        if (selectedDemod) { selectedDemod->stop(); }
        afChain.stop();
        if (vfo) { sigpath::vfoManager.deleteVFO(vfo); }
        vfo = NULL;
    }

    bool isEnabled() override {
        return enabled;
    }

    dsp::chain<dsp::complex_t> *getIfChain() {
        return &ifChain;
    }

    std::string name;

    int getSelectedDemodId() override  {
        return selectedDemodID;
    }

    bool selectDemodByID(DemodID id) override {
        auto startTime = std::chrono::high_resolution_clock::now();
        demod::Demodulator* demod = instantiateDemod(id);
        if (!demod) {
            flog::error("Demodulator {0} not implemented", (int)id);
            return false;
        }
        selectedDemodID = id;
        selectDemod(demod);

        // Save config
        config.acquire();
        config.conf[name]["selectedDemodId"] = id;
        config.release(true);
        auto endTime = std::chrono::high_resolution_clock::now();
        flog::warn("Demod switch took {0} us", (int64_t)((std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime)).count()));
        return true;
    }

private:
    static void menuHandler(void* ctx) {
        RadioModule* _this = (RadioModule*)ctx;

        if (!_this->enabled) { style::beginDisabled(); }

        float menuWidth = ImGui::GetContentRegionAvail().x;
        ImGui::BeginGroup();

        ImGui::Columns(4, CONCAT("RadioModeColumns##_", _this->name), false);
        char boo[1024];
        for(int i=0; i<8; i++) {
            snprintf(boo, sizeof boo, "%s##_%s", _this->radioModes[i].first.c_str(), _this->name.c_str());
            if (ImGui::RadioButton(boo, _this->selectedDemodID == _this->radioModes[i].second) && _this->selectedDemodID != _this->radioModes[i].second) {
                _this->selectDemodByID((DemodID)_this->radioModes[i].second);
            }
            if (i % 2 == 1 && i != 7) {
                ImGui::NextColumn();
            }
        }
        ImGui::Columns(1, CONCAT("EndRadioModeColumns##_", _this->name), false);

        for(int i=8; i<_this->radioModes.size(); i++) {
            snprintf(boo, sizeof boo, "%s##_%s", _this->radioModes[i].first.c_str(), _this->name.c_str());
            if (ImGui::RadioButton(boo, _this->selectedDemodID == _this->radioModes[i].second) && _this->selectedDemodID != _this->radioModes[i].second) {
                _this->selectDemodByID((DemodID)_this->radioModes[i].second);
            }
        }

        ImGui::EndGroup();

        if (!_this->bandwidthLocked) {
            ImGui::LeftLabel("Bandwidth");
            const ImVec2 width = ImGui::CalcTextSize("123456    +    -    ");
            ImGui::SetNextItemWidth(width.x);
            if (ImGui::InputFloat(("##_radio_bw_" + _this->name).c_str(), &_this->bandwidth, 1, 100, "%.0f")) {
                _this->bandwidth = std::clamp<float>(_this->bandwidth, _this->minBandwidth, _this->maxBandwidth);
                _this->setBandwidth(_this->bandwidth);
            }
            int limit = 12000;
            switch(_this->selectedDemodID) {        // convenience to fully utilize slider, edit with above field for outside values
            case RADIO_DEMOD_LSB:
            case RADIO_DEMOD_USB:
                limit = 3500;
                break;
            case RADIO_DEMOD_CW:
                limit = 1000;
                break;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::SliderFloat(("##_radio_bw_slider_" + _this->name).c_str(), &_this->bandwidth, 50, limit, "")) {
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
        

        // Squelch
        if (ImGui::Checkbox(("Squelch##_radio_sqelch_ena_" + _this->name).c_str(), &_this->squelchEnabled)) {
            _this->setSquelchEnabled(_this->squelchEnabled);
        }
        if (!_this->squelchEnabled && _this->enabled) { style::beginDisabled(); }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::SliderFloat(("##_radio_sqelch_lvl_" + _this->name).c_str(), &_this->squelchLevel, _this->MIN_SQUELCH, _this->MAX_SQUELCH, "%.3fdB")) {
            _this->setSquelchLevel(_this->squelchLevel);
        }
        if (!_this->squelchEnabled && _this->enabled) { style::endDisabled(); }

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

        // Demodulator specific menu
        if (_this->selectedDemod) {
            _this->selectedDemod->showMenu();
        }

        if (!_this->enabled) { style::endDisabled(); }
    }

    demod::Demodulator* instantiateDemod(DemodID id) {
        demod::Demodulator* demod = NULL;
        switch (id) {
            case DemodID::RADIO_DEMOD_NFM:  demod = new demod::NFM(); break;
            case DemodID::RADIO_DEMOD_WFM:  demod = new demod::WFM(); break;
            case DemodID::RADIO_DEMOD_AM:   demod = new demod::AM(); break;
            case DemodID::RADIO_DEMOD_DSB:  demod = new demod::DSB(); break;
            case DemodID::RADIO_DEMOD_USB:  demod = new demod::USB(); break;
            case DemodID::RADIO_DEMOD_CW:   demod = new demod::CW(); break;
            case DemodID::RADIO_DEMOD_LSB:  demod = new demod::LSB(); break;
            case DemodID::RADIO_DEMOD_RAW:  demod = new demod::RAW(); break;
            default:                        demod = NULL; break;
        }
        if (!demod) {
            for (int i = 0; i < demodulatorProviders.size(); i++) {
                demod = demodulatorProviders[i](id);
                if (demod) { break; }
            }
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
        demod->init(name, &config, ifChain.out, bw, streams.front()->getSampleRate());

        return demod;
    }

    void selectDemod(demod::Demodulator* demod) {
        // Stop currently selected demodulator and select new
        afChain.setInput(&dummyAudioStream, [=](dsp::stream<dsp::stereo_t>* out){ afsplitter.setInput(out); });
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
        afChain.setInput(selectedDemod->getOutput(), [=](dsp::stream<dsp::stereo_t>* out){ afsplitter.setInput(out); });

        // Load config
        bandwidth = selectedDemod->getDefaultBandwidth();
        minBandwidth = selectedDemod->getMinBandwidth();
        maxBandwidth = selectedDemod->getMaxBandwidth();
        bandwidthLocked = selectedDemod->getBandwidthLocked();
        snapInterval = selectedDemod->getDefaultSnapInterval();
        squelchLevel = MIN_SQUELCH;
        deempAllowed = selectedDemod->getDeempAllowed();
        deempId = deempModes.valueId((DeemphasisMode)selectedDemod->getDefaultDeemphasisMode());
        squelchEnabled = false;
        postProcEnabled = selectedDemod->getPostProcEnabled();
        FMIFNRAllowed = selectedDemod->getFMIFNRAllowed();
        FMIFNREnabled = false;
        fmIFPresetId = ifnrPresets.valueId(IFNR_PRESET_VOICE);
        nbAllowed = selectedDemod->getNBAllowed();
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
        if (config.conf[name][selectedDemod->getName()].contains("squelchLevel")) {
            squelchLevel = config.conf[name][selectedDemod->getName()]["squelchLevel"];
        }
        if (config.conf[name][selectedDemod->getName()].contains("squelchEnabled")) {
            squelchEnabled = config.conf[name][selectedDemod->getName()]["squelchEnabled"];
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
        setSquelchLevel(squelchLevel);
        setSquelchEnabled(squelchEnabled);

        // Configure AF chain
        if (postProcEnabled) {
            // Configure resampler
            afChain.stop();
            resamp.setInSamplerate(selectedDemod->getAFSampleRate());
            setAudioSampleRate(audioSampleRate);
            afChain.enableBlock(&resamp, [=](dsp::stream<dsp::stereo_t>* out){ afsplitter.setInput(out); });

            // Configure deemphasis
            setDeemphasisMode(deempModes[deempId]);
        }
        else {
            // Disable everything if post processing is disabled
            afChain.disableAllBlocks([=](dsp::stream<dsp::stereo_t>* out){ afsplitter.setInput(out); });
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

        // Configure deemphasis sample rate
        deemp.setSamplerate(audioSampleRate);

        afChain.start();
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

        // Save config
        config.acquire();
        config.conf[name][selectedDemod->getName()]["noiseBlankerLevel"] = nbLevel;
        config.release(true);
    }

    void setSquelchEnabled(bool enable) {
        squelchEnabled = enable;
        if (!selectedDemod) { return; }
        ifChain.setBlockEnabled(&squelch, squelchEnabled, [=](dsp::stream<dsp::complex_t>* out){ selectedDemod->setInput(out); });

        // Save config
        config.acquire();
        config.conf[name][selectedDemod->getName()]["squelchEnabled"] = squelchEnabled;
        config.release(true);
    }

    void setSquelchLevel(float level) {
        squelchLevel = std::clamp<float>(level, MIN_SQUELCH, MAX_SQUELCH);
        squelch.setLevel(squelchLevel);

        // Save config
        config.acquire();
        config.conf[name][selectedDemod->getName()]["squelchLevel"] = squelchLevel;
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

    void setDeemphasisMode(DeemphasisMode mode) {
        deempId = deempModes.valueId(mode);
        if (!postProcEnabled || !selectedDemod) { return; }
        bool deempEnabled = (mode != DEEMP_MODE_NONE);
        if (deempEnabled) { deemp.setTau(deempTaus[mode]); }
        afChain.setBlockEnabled(&deemp, deempEnabled, [=](dsp::stream<dsp::stereo_t>* out){ afsplitter.setInput(out); });

        // Save config
        config.acquire();
        config.conf[name][selectedDemod->getName()]["deempMode"] = deempModes.key(deempId);
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

    static void vfoUserChangedDemodulatorHandler(int newDemodulator, void* ctx) {
        RadioModule* _this = (RadioModule*)ctx;
        if (_this->selectedDemodID != (DemodID)newDemodulator) {
            _this->selectDemodByID((DemodID)newDemodulator);
        }
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
        if(in) {
            switch(code) {
                case RADIO_IFACE_CMD_ADD_TO_IFCHAIN:
                    _this->ifChain.addBlock((dsp::Processor<dsp::complex_t, dsp::complex_t> *)in, false);
                    return;
                case RADIO_IFACE_CMD_ADD_TO_AFCHAIN:
                    _this->afChain.addBlock((dsp::Processor<dsp::stereo_t, dsp::stereo_t> *)in, false);
                    return;
                case RADIO_IFACE_CMD_REMOVE_FROM_IFCHAIN:
                    _this->ifChain.removeBlock((dsp::Processor<dsp::complex_t, dsp::complex_t> *)in, [=](dsp::stream<dsp::complex_t>* out){ _this->selectedDemod->setInput(out); });
                    return;
                case RADIO_IFACE_CMD_REMOVE_FROM_AFCHAIN:
                    _this->afChain.removeBlock((dsp::Processor<dsp::stereo_t, dsp::stereo_t> *)in, [=](dsp::stream<dsp::stereo_t>* out){ _this->afsplitter.setInput(out); });
                    return;
                case RADIO_IFACE_CMD_ENABLE_IN_IFCHAIN:
                    _this->ifChain.setBlockEnabled((dsp::Processor<dsp::complex_t, dsp::complex_t> *)in, true, [=](dsp::stream<dsp::complex_t>* out){ _this->selectedDemod->setInput(out); });
                    return;
                case RADIO_IFACE_CMD_ENABLE_IN_AFCHAIN:
                    _this->afChain.setBlockEnabled((dsp::Processor<dsp::stereo_t, dsp::stereo_t> *)in, true, [=](dsp::stream<dsp::stereo_t>* out){ _this->afsplitter.setInput(out); });
                    return;
                case RADIO_IFACE_CMD_DISABLE_IN_IFCHAIN:
                    _this->ifChain.setBlockEnabled((dsp::Processor<dsp::complex_t, dsp::complex_t> *)in, false, [=](dsp::stream<dsp::complex_t>* out){ _this->selectedDemod->setInput(out); });
                    return;
                case RADIO_IFACE_CMD_DISABLE_IN_AFCHAIN:
                    _this->afChain.setBlockEnabled((dsp::Processor<dsp::stereo_t, dsp::stereo_t> *)in, false, [=](dsp::stream<dsp::stereo_t>* out){ _this->afsplitter.setInput(out); });
                    return;
            }
        }

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
        else if (code == RADIO_IFACE_CMD_GET_SQUELCH_ENABLED && out) {
            bool* _out = (bool*)out;
            *_out = _this->squelchEnabled;
        }
        else if (code == RADIO_IFACE_CMD_SET_SQUELCH_ENABLED && in && _this->enabled) {
            bool* _in = (bool*)in;
            _this->setSquelchEnabled(*_in);
        }
        else if (code == RADIO_IFACE_CMD_GET_SQUELCH_LEVEL && out) {
            float* _out = (float*)out;
            *_out = _this->squelchLevel;
        }
        else if (code == RADIO_IFACE_CMD_SET_SQUELCH_LEVEL && in && _this->enabled) {
            float* _in = (float*)in;
            _this->setSquelchLevel(*_in);
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
    dsp::noise_reduction::NoiseBlanker nb;
    dsp::noise_reduction::FMIF fmnr;
    dsp::noise_reduction::Squelch squelch;

    // Audio chain
    dsp::stream<dsp::stereo_t> dummyAudioStream;
    dsp::chain<dsp::stereo_t> afChain;
    dsp::multirate::RationalResampler<dsp::stereo_t> resamp;
    dsp::filter::Deemphasis<dsp::stereo_t> deemp;

    dsp::routing::Splitter<dsp::stereo_t> afsplitter;
    std::vector<std::shared_ptr<SinkManager::Stream>> streams;
    std::vector<std::string> streamNames;


    demod::Demodulator* selectedDemod = NULL;

    OptionList<std::string, DeemphasisMode> deempModes;
    OptionList<std::string, IFNRPreset> ifnrPresets;

    double audioSampleRate = 48000.0;
    float minBandwidth;
    float maxBandwidth;
    float bandwidth;
    bool bandwidthLocked;
    int snapInterval;
    int selectedDemodID = 1;
    bool postProcEnabled;

    bool squelchEnabled = false;
    float squelchLevel;

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
};

