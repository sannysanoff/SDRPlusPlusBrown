#include <config.h>
#include <core.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <module.h>
#include <dsp/stream.h>
#include "dsdcc_decoder.h"

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "dsdcc_decoder",
    /* Description:     */ "DSDCC DMR etc Decoder for SDR++",
    /* Author:          */ "san",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ -1
};

ConfigManager config;

#define INPUT_SAMPLE_RATE 14400

class DSDCCDecoderModule : public ModuleManager::Instance {
public:
    DSDCCDecoderModule(std::string name) {
        this->name = name;

        // Setup audio stream
        srChangeHandler.ctx = this;
        srChangeHandler.handler = sampleRateChangeHandler;
        stream.init(&srChangeHandler, audioSampRate);
        sigpath::sinkManager.registerStream(name, &stream);
        stream.start();

        gui::menu.registerEntry(name, menuHandler, this, this);
    }

    ~DSDCCDecoderModule() {
        gui::menu.removeEntry(name);
        stream.stop();
        if (enabled) {
            sigpath::vfoManager.deleteVFO(vfo);
        }

        sigpath::sinkManager.unregisterStream(name);
    }

    void postInit() {}

    void enable() {
        double bw = gui::waterfall.getBandwidth();
        vfo = sigpath::vfoManager.createVFO(name, ImGui::WaterfallVFO::REF_CENTER, std::clamp<double>(0, -bw / 2.0, bw / 2.0), 9600, INPUT_SAMPLE_RATE, 9600, 9600, true);
        vfo->setSnapInterval(250);

        enabled = true;
    }

    void disable() {
        sigpath::vfoManager.deleteVFO(vfo);
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }

private:
    static void menuHandler(void* ctx) {
    }

    static void sampleRateChangeHandler(float, void*) {
    }

    std::string name;
    bool enabled = false;
    VFOManager::VFO* vfo;
    double audioSampRate = 48000;
    EventHandler<float> srChangeHandler;
    SinkManager::Stream stream;
};

MOD_EXPORT void _INIT_() {
    // Create default recording directory
    json def = json({});
    config.setPath(std::string(core::getRoot()) + "/dsdcc_decoder_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new DSDCCDecoderModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (DSDCCDecoderModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
