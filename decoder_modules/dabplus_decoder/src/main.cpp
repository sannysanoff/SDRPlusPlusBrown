// DAB/DAB+ as a mode of the existing radio module.
//
// This module owns no radio of its own: it registers a demodulator provider and
// a "DAB" entry with every RadioModuleInterface, so the receiver runs on the
// radio module's VFO, IF chain and audio sinks. Selecting DAB in the radio's
// mode list is all it takes; the radio's menu then hosts the station list and the
// constellation, and the radio's AF chain carries the decoded programme.

#include <imgui.h>
#include <module.h>
#include <core.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <signal_path/signal_path.h>
#include <utils/flog.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "../../radio/src/radio_module_interface.h"
#include "dab_demod.h"

SDRPP_MOD_INFO{
    /* Name:            */ "dabplus_decoder",
    /* Description:     */ "DAB/DAB+ mode for the radio module (Mode I-IV, DAB+ AAC and DAB MP2 audio)",
    /* Author:          */ "Alexander Bitney",
    /* Version:         */ 0, 2, 0,
    /* Max instances    */ -1
};

// Mode ids above the radio's own enum get routed through the provider list.
static const int RADIO_DEMOD_DAB = 0x1303;

class DabPlusDecoderModule : public ModuleManager::Instance {
public:
    DabPlusDecoderModule(std::string name) {
        this->name = name;
    }

    ~DabPlusDecoderModule() {
        uninject();
    }

    void postInit() {
        inject();
    }

    void enable() {
        enabled = true;
        inject();
    }

    void disable() {
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }

private:
    struct Injection {
        RadioModuleInterface* radio = nullptr;
        RadioModuleInterface::demodProviderFunction provider = nullptr;
    };

    std::shared_ptr<Injection> getInjection(RadioModuleInterface* radio) {
        for (auto& injection : injections) {
            if (injection->radio == radio) { return injection; }
        }
        auto injection = std::make_shared<Injection>();
        injection->radio = radio;
        injection->provider = [](int id) -> demod::Demodulator* {
            if (id == RADIO_DEMOD_DAB) { return new demod::DAB(); }
            return nullptr;
        };
        injections.push_back(injection);
        return injection;
    }

    void injectIntoRadio(RadioModuleInterface* radio) {
        auto injection = getInjection(radio);
        auto& providers = radio->demodulatorProviders;
        if (std::find(providers.begin(), providers.end(), injection->provider) == providers.end()) {
            providers.push_back(injection->provider);
        }
        auto& modes = radio->radioModes;
        bool hasMode = false;
        for (auto& mode : modes) {
            if (mode.second == RADIO_DEMOD_DAB) { hasMode = true; break; }
        }
        if (!hasMode) { modes.emplace_back("DAB", RADIO_DEMOD_DAB); }
    }

    void uninjectFromRadio(RadioModuleInterface* radio) {
        for (auto& injection : injections) {
            if (injection->radio != radio) { continue; }
            auto& providers = radio->demodulatorProviders;
            auto found = std::find(providers.begin(), providers.end(), injection->provider);
            if (found != providers.end()) { providers.erase(found); }
            auto& modes = radio->radioModes;
            modes.erase(std::remove_if(modes.begin(), modes.end(), [](const std::pair<std::string, int>& mode) {
                return mode.second == RADIO_DEMOD_DAB;
            }), modes.end());
        }
    }

    void inject() {
        if (injected) { return; }
        injected = true;

        createdHandler.ctx = this;
        createdHandler.handler = onInstanceCreated;
        core::moduleManager.onInstanceCreated.bindHandler(&createdHandler);
        deletedHandler.ctx = this;
        deletedHandler.handler = onInstanceDeleted;
        core::moduleManager.onInstanceDelete.bindHandler(&deletedHandler);

        for (auto& radio : core::moduleManager.getAllInterfaces<RadioModuleInterface>("RadioModuleInterface")) {
            injectIntoRadio(radio);
        }
    }

    void uninject() {
        if (!injected) { return; }
        core::moduleManager.onInstanceCreated.unbindHandler(&createdHandler);
        core::moduleManager.onInstanceDelete.unbindHandler(&deletedHandler);
        for (auto& radio : core::moduleManager.getAllInterfaces<RadioModuleInterface>("RadioModuleInterface")) {
            uninjectFromRadio(radio);
        }
        injections.clear();
        injected = false;
    }

    static void onInstanceCreated(std::string moduleName, void* ctx) {
        DabPlusDecoderModule* _this = (DabPlusDecoderModule*)ctx;
        auto* radio = (RadioModuleInterface*)core::moduleManager.getInterface(moduleName, "RadioModuleInterface");
        if (radio) { _this->injectIntoRadio(radio); }
    }

    static void onInstanceDeleted(std::string moduleName, void* ctx) {
        DabPlusDecoderModule* _this = (DabPlusDecoderModule*)ctx;
        auto* radio = (RadioModuleInterface*)core::moduleManager.getInterface(moduleName, "RadioModuleInterface");
        if (radio) { _this->uninjectFromRadio(radio); }
    }

    std::string name;
    bool enabled = false;
    bool injected = false;
    std::vector<std::shared_ptr<Injection>> injections;
    EventHandler<std::string> createdHandler;
    EventHandler<std::string> deletedHandler;
};

MOD_EXPORT void _INIT_() {}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new DabPlusDecoderModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (DabPlusDecoderModule*)instance;
}

MOD_EXPORT void _END_() {}
