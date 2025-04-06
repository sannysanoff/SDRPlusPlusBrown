


#if defined(__MACH__) || defined(__ANDROID__)
#define register
#endif



#include <itpp/itcomm.h>
#ifndef ITCOMM_H
// KEEP THIS CRAP TOGETHER DONT REMOVE INCLUDE.
#error "ITCOMM_H is not defined"
#endif


#include <imgui.h>
#include "../../src/gui/style.h"
#include "../../../core/src/config.h"


#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include "../../radio/src/radio_module_interface.h"
#include <core.h>
#include <utils/optionlist.h>
#include "./demod.h"

ConfigManager config;

#define CONCAT(a, b) ((std::string(a) + b).c_str())


class VhfVoiceRadioModule : public ModuleManager::Instance {
public:

    std::string name;

    VhfVoiceRadioModule(std::string name) {

    }

    ~VhfVoiceRadioModule() {
    }

    EventHandler<std::string> moduleCreatedListener;
    EventHandler<std::string> moduleDeleteListener;

    struct Injection {
        RadioModuleInterface *radio;
        VhfVoiceRadioModule *thiz;
        EventHandler<ImGuiContext *> drawModeButtonsHandler;
        RadioModuleInterface::demodProviderFunction demodFunction;
    };

    static const int RADIO_DEMOD_DSD = 0x1301;
    static const int RADIO_DEMOD_OLDDSD = 0x1302;

    bool injected = false;

    std::vector<std::shared_ptr<Injection>> injections;

    std::shared_ptr<Injection> getInjection(RadioModuleInterface *radio) {
        for(auto& x: injections) {
            if (x->radio == radio) {
                return x;
            }
        }
        auto rv =std::make_shared<Injection>();
        injections.emplace_back(rv);
        rv->radio = radio;
        rv->thiz = this;
        rv->demodFunction = [](int id) -> demod::Demodulator* {
            switch (id) {
                case RADIO_DEMOD_DSD:  return new demod::DSD(); break;
                case RADIO_DEMOD_OLDDSD:  return new demod::OldDSD(); break;
                default:                        return NULL; break;
            }
        };
        return rv;
    }

    void injectIntoRadio(RadioModuleInterface *radio) {
        auto inj = getInjection(radio);
        inj->drawModeButtonsHandler.ctx = inj.get();
        //inj->drawModeButtonsHandler.handler = onDrawModeButtons;
        //radio->onDrawModeButtons.bindHandler(&inj->drawModeButtonsHandler);
        radio->demodulatorProviders.emplace_back(inj->demodFunction);
        radio->radioModes.emplace_back();
        radio->radioModes.back().first = "DSD";
        radio->radioModes.back().second = RADIO_DEMOD_DSD;
        radio->radioModes.emplace_back();
        radio->radioModes.back().first = "OLD DSD";
        radio->radioModes.back().second = RADIO_DEMOD_OLDDSD;

    }

    static void onModuleCreated(std::string modName, void *ctx) {
        auto _this = (VhfVoiceRadioModule *)ctx;
        auto radio = (RadioModuleInterface *)core::moduleManager.getInterface(modName, "RadioModuleInterface");
        if (radio) {
            _this->injectIntoRadio(radio);
        }

    }

    static void onModuleDelete(std::string modName, void *ctx) {
        auto _this = (VhfVoiceRadioModule *)ctx;
        auto radio = (RadioModuleInterface *)core::moduleManager.getInterface(modName, "RadioModuleInterface");
        if (radio) {
            _this->uninjectFromRadio(radio);
        }
    }

    void inject() {
        if (!injected) {
            injected = true;
            moduleCreatedListener.handler = onModuleCreated;
            moduleCreatedListener.ctx = this;
            moduleDeleteListener.handler = onModuleDelete;
            moduleDeleteListener.ctx = this;
            core::moduleManager.onInstanceCreated.bindHandler(&moduleCreatedListener);
            core::moduleManager.onInstanceDelete.bindHandler(&moduleDeleteListener);
            int ix = 0;
            auto radios = core::moduleManager.getAllInterfaces<RadioModuleInterface>("RadioModuleInterface");
            for (auto &r: radios) {
                injectIntoRadio(r);
                ix++;
            }
        }
    }

    void uninjectFromRadio(RadioModuleInterface *radio) {
        auto inj = getInjection(radio);
        //radio->onDrawModeButtons.unbindHandler(&inj->drawModeButtonsHandler);
        auto wh = std::find(radio->demodulatorProviders.begin(), radio->demodulatorProviders.end(), inj->demodFunction);
        if (wh != radio->demodulatorProviders.end()) {
            radio->demodulatorProviders.erase(wh);
        }
        for(int i=0; i<radio->radioModes.size(); i++) {
            if (radio->radioModes[i].second == RADIO_DEMOD_DSD || radio->radioModes[i].second == RADIO_DEMOD_OLDDSD) {
                radio->radioModes.erase(radio->radioModes.begin() + i);
                i--;
            }
        }
    }

    void uninject() {
        if (injected) {
            core::moduleManager.onInstanceCreated.unbindHandler(&moduleCreatedListener);
            core::moduleManager.onInstanceDelete.unbindHandler(&moduleDeleteListener);
            for (auto x: core::moduleManager.instances) {
                Instance *pInstance = x.second.instance;
                auto radio = (RadioModuleInterface *) pInstance->getInterface("RadioModuleInterface");
                if (radio) {
                    uninjectFromRadio(radio);
                }
            }
            injected = false;
        }
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
        uninject();
    }

    bool isEnabled() {
        return enabled;
    }


private:
    static void menuHandler(void* ctx) {
        ImGui::Text("See new modes in Radio.");
        ImGui::Text("These modes are provided");
        ImGui::Text("for educational purposes.");
    }

    bool enabled = true;
};


SDRPP_MOD_INFO{
    /* Name:            */ "ch_extravhf_decoder",
    /* Description:     */ "Additional modes for V/UHF voice",
    /* Author:          */ "cropinghigh",
    /* Version:         */ 0, 0, 6,
    /* Max instances    */ -1
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    config.setPath(std::string(core::getRoot()) + "/ch_extravhf_decoder_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new VhfVoiceRadioModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    auto mymod = (VhfVoiceRadioModule*)instance;
    mymod->uninject();
    delete mymod;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
