


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
#include <chrono>
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
        radio->radioModes.back().first = "oldDSD";
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

    std::string handleDebugCommand(const std::string& cmd, const std::string& args) override {
        if (cmd == "get_dmr_status") {
            std::string radioName = args.empty() ? "Radio" : args;
            demod::DSD::DebugStatus st;
            if (!demod::DSD::getStatusForRadio(radioName, st)) {
                return std::string("{\"error\": \"no active DSD demod for radio '") + radioName + "'\"}";
            }
            return debugStatusToJson(st, 0, false);
        }
        if (cmd == "wait_dmr_sync_voice") {
            std::string radioName = "Radio";
            int stableMs = 2000;
            int timeoutMs = 10000;
            parseWaitArgs(args, radioName, stableMs, timeoutMs);
            demod::DSD::DebugStatus st;
            int waitedMs = 0;
            bool ok = demod::DSD::waitForDMRSyncVoice(radioName, stableMs, timeoutMs, st, waitedMs);
            return debugStatusToJson(st, waitedMs, ok);
        }
        return "{}";
    }


private:
    static void parseWaitArgs(const std::string& args, std::string& radioName, int& stableMs, int& timeoutMs) {
        if (args.empty()) {
            return;
        }
        std::vector<std::string> parts;
        size_t start = 0;
        while (true) {
            size_t pos = args.find(',', start);
            if (pos == std::string::npos) {
                parts.push_back(args.substr(start));
                break;
            }
            parts.push_back(args.substr(start, pos - start));
            start = pos + 1;
        }
        if (parts.size() > 0 && !parts[0].empty()) { radioName = parts[0]; }
        if (parts.size() > 1 && !parts[1].empty()) {
            try { stableMs = std::stoi(parts[1]); } catch (...) {}
        }
        if (parts.size() > 2 && !parts[2].empty()) {
            try { timeoutMs = std::stoi(parts[2]); } catch (...) {}
        }
    }

    static std::string escapeJson(const std::string& in) {
        std::string out;
        out.reserve(in.size() + 8);
        for (char c : in) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default: out += c; break;
            }
        }
        return out;
    }

    static std::string debugStatusToJson(const demod::DSD::DebugStatus& st, int waitedMs, bool ok) {
        return std::string("{\"status\":\"") + (ok ? "ok" : "timeout") +
               "\",\"available\":" + (st.available ? "true" : "false") +
               ",\"sync\":" + (st.sync ? "true" : "false") +
               ",\"dmr\":" + (st.dmr ? "true" : "false") +
               ",\"voice\":" + (st.voice ? "true" : "false") +
               ",\"mbe_decoding\":" + (st.mbe_decoding ? "true" : "false") +
               ",\"waited_ms\":" + std::to_string(waitedMs) +
               ",\"color_code\":" + std::to_string(st.color_code) +
               ",\"slot0_burst\":" + std::to_string(st.slot0_burst) +
               ",\"slot1_burst\":" + std::to_string(st.slot1_burst) +
               ",\"slot0_type\":\"" + escapeJson(st.slot0_type) +
               "\",\"slot1_type\":\"" + escapeJson(st.slot1_type) +
               "\",\"mbe_errorbar\":\"" + escapeJson(st.mbe_errorbar) + "\"}";
    }

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
