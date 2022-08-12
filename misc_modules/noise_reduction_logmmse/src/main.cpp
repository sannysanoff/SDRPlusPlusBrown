#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui.h>
#include <module.h>
#include <gui/gui.h>
#include <config.h>
#include <core.h>
#include <imgui/imgui_internal.h>
#include <vector>
#include <gui/widgets/snr_meter.h>
#include "logmmse_math.h"
#include "signal_path/signal_path.h"
#include <radio_interface.h>
#include "if_nr.h"
#include "af_nr.h"

using namespace ImGui;

ConfigManager config;

static long long currentTimeMillis() {
    std::chrono::system_clock::time_point t1 = std::chrono::system_clock::now();
    long long msec = std::chrono::time_point_cast<std::chrono::milliseconds>(t1).time_since_epoch().count();
    return msec;
}


SDRPP_MOD_INFO{
    /* Name:            */ "noise_reduction_logmmse",
    /* Description:     */ "LOGMMSE noise reduction",
    /* Author:          */ "sannysanoff",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ -1
};

class NRModule : public ModuleManager::Instance {

    dsp::IFNRLogMMSE ifnrProcessor;

    std::unordered_map<std::string, std::shared_ptr<dsp::AFNRLogMMSE>> afnrProcessors;       // instance by radio name.

public:
    NRModule(std::string name) {
        this->name = name;
        config.acquire();
        if (config.conf.contains("IFNR")) ifnr = config.conf["IFNR"];
        if (config.conf.contains("SNRChartWidget")) snrChartWidget = config.conf["SNRChartWidget"];
        config.release(true);

        gui::menu.registerEntry(name, menuHandler, this, NULL);
        updateBindings();
        actuateIFNR();
    }

    ~NRModule() {
        gui::menu.removeEntry(name);
    }

    void postInit() {}

    void enable() {
        if (!enabled) {
            enabled = true;
            updateBindings();
            actuateIFNR();
        }
    }

    void disable() {
        if (enabled) {
            enabled = false;
            actuateIFNR();
            updateBindings();
        }
    }

    bool isEnabled() {
        return enabled;
    }


private:
    bool ifnr = false;

    bool afnrEnabled = false;
    bool snrChartWidget = false;

    void attachAFToRadio(const std::string& instanceName) {
        auto thing = std::make_shared<dsp::AFNRLogMMSE>();
        afnrProcessors[instanceName] = thing;
        thing->init(nullptr);
        core::modComManager.callInterface(instanceName, RADIO_IFACE_CMD_ADD_TO_IFCHAIN, thing.get(), NULL);
        config.acquire();
        bool afnr = false;
        if (config.conf.contains("AF_NR_"+instanceName)) afnr = config.conf["AF_NR_"+instanceName];
        auto frequency = 10;
        if (config.conf.contains("AF_NRF_"+instanceName)) frequency = config.conf["AF_NRF_"+instanceName];
        config.release(true);
        thing->afnrBandwidth = frequency;
        thing->setProcessingBandwidth(frequency * 1000);
        thing->allowed = afnr;
        actuateAFNR();
    }

    void detachAFFromRadio(const std::string& instanceName) {
        if (afnrProcessors.find(instanceName) != afnrProcessors.end()) {
            core::modComManager.callInterface(name, RADIO_IFACE_CMD_REMOVE_FROM_IFCHAIN, afnrProcessors[instanceName].get(), NULL);
            afnrProcessors.erase(instanceName);
        }
    }

    void updateBindings() {
        if (enabled) {
            spdlog::info("Enabling noise reduction things");
            gui::mainWindow.onWaterfallDrawn.bindHandler(&waterfallDrawnHandler);
            waterfallDrawnHandler.ctx = this;
            waterfallDrawnHandler.handler = [](void*, void* ctx) {
                NRModule* _this = (NRModule*)ctx;
                _this->drawSNRMeterAverages();
            };
            ImGui::onSNRMeterExtPoint.bindHandler(&snrMeterExtPointHandler);
            snrMeterExtPointHandler.ctx = this;
            snrMeterExtPointHandler.handler = [](ImGui::SNRMeterExtPoint extp, void *ctx) {
                NRModule* _this = (NRModule*)ctx;
                if (_this->enabled) {
                    _this->lastsnr.insert(_this->lastsnr.begin(), extp.lastDrawnValue);
                    if (_this->lastsnr.size() > NLASTSNR)
                        _this->lastsnr.resize(NLASTSNR);

                    _this->postSnrLocation = extp.postSnrLocation;
                }
            };

            sigpath::iqFrontEnd.addPreprocessor(&ifnrProcessor, false);

            sigpath::sourceManager.onTuneChanged.bindHandler(&currentFrequencyChangedHandler);
            currentFrequencyChangedHandler.ctx = this;
            currentFrequencyChangedHandler.handler = [](double v, void *ctx) {
                auto _this = (NRModule *)ctx;
                _this->ifnrProcessor.reset();   // reset noise profile
            };

            auto names = core::modComManager.findInterfaces("radio");
            for (auto &name : names) {
                attachAFToRadio(name);
            }
            core::moduleManager.onInstanceCreated.bindHandler(&instanceCreatedHandler);
            instanceCreatedHandler.ctx = this;
            instanceCreatedHandler.handler = [](std::string v, void *ctx) {
                auto _this = (NRModule *)ctx;
                _this->ifnrProcessor.reset();   // reset noise profile
            };

        } else {
            sigpath::iqFrontEnd.removePreprocessor(&ifnrProcessor);
            ImGui::onSNRMeterExtPoint.unbindHandler(&snrMeterExtPointHandler);
            gui::mainWindow.onWaterfallDrawn.unbindHandler(&waterfallDrawnHandler);
        }
    }

    void setAFNREnabled(bool enable) {
        afnrEnabled = enable;
        //        if (!selectedDemod) { return; }
        //        ifChain.setState(&lmmsenr, logmmseNrEnabled);

        // Save config
        //        config.acquire();
        //        config.conf[name][selectedDemod->getName()]["logmmseNrEnabled"] = logmmseNrEnabled;
        //        config.release(true);
    }

    void setLogMMSEBandwidth(int bandwidthHz) {
        //        lmmsenr.block.setBandwidth(bandwidthHz);
        //        config.acquire();
        //        config.conf[name][selectedDemod->getName()]["logmmseNrEnabled"] = logmmseNrEnabled;
        //        config.release(true);
    }


    std::unordered_map<std::string, long long> firstTimeHover;
    bool mustShowTooltip(const std::string& key) {
        if (ImGui::IsItemHovered()) {
            auto what = firstTimeHover[key];
            if (what == 0) {
                firstTimeHover[key] = currentTimeMillis();
                return false;
            }
            else {
                return currentTimeMillis() - what > 1000;
            }
        }
        else {
            firstTimeHover[key] = 0;
            return false;
        }
    }

    void actuateAFNR() {
        for(auto [k, v] : afnrProcessors) {
            core::modComManager.callInterface(k, !v->allowed ? RADIO_IFACE_CMD_DISABLE_IN_IFCHAIN : RADIO_IFACE_CMD_ENABLE_IN_IFCHAIN, v.get(), NULL);
        }

    }

    void actuateIFNR() {
        bool shouldRun = enabled && ifnr;
        if (ifnrProcessor.bypass != !shouldRun) {
            ifnrProcessor.bypass = !shouldRun;
            sigpath::iqFrontEnd.togglePreprocessor(&ifnrProcessor, shouldRun);
        }
    }

    void menuHandler() {
        float menuWidth = ImGui::GetContentRegionAvail().x;
        if (ImGui::Checkbox("IF NR##_sdrpp_if_nr", &ifnr)) {
            //            sigpath::signalPath.setWidebandNR(_this->widebandNR);
            config.acquire();
            config.conf["IFNR"] = ifnr;
            config.release(true);

            actuateIFNR();


        }
        if (mustShowTooltip("IFNR"))
            ImGui::SetTooltip("Algorithm running on full bandwidth. Can be slow.");

        for(auto [k, v] : afnrProcessors) {
            if (ImGui::Checkbox(("AF NR "+k+"##_radio_logmmse_nr_" + k).c_str(), &v->allowed)) {
                actuateAFNR();
                config.acquire();
                config.conf["AF_NR_"+k] = v->allowed;
                config.release(true);
            }
            if (mustShowTooltip("AFNR"+k))
                ImGui::SetTooltip("Noise reduction over the audio frequency.");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::SliderInt(("##_radio_logmmse_wf" + k).c_str(), &v->afnrBandwidth, 1, 24, "%d KHz")) {
                v->setProcessingBandwidth(v->afnrBandwidth * 1000);
                config.acquire();
                config.conf["AF_NRF_"+k] = v->afnrBandwidth;
                config.release(true);
            }
        }
        if (ImGui::Checkbox(("SNR Chart##_radio_logmmse_nr_" + name).c_str(), &snrChartWidget)) {
            config.acquire();
            config.conf["SNRChartWidget"] = snrChartWidget;
            config.release(true);
        }
    }

    static void menuHandler(void* ctx) {
        NRModule* _this = (NRModule*)ctx;
        _this->menuHandler();
    }

    static const int NLASTSNR = 1500;
    std::vector<float> lastsnr;


    ImVec2 postSnrLocation;

#pragma clang diagnostic push
#pragma ide diagnostic ignored "UnreachableCode"
    void drawSNRMeterAverages() {

        if (!snrChartWidget || !enabled) {
            return;
        }
        static std::vector<float> r;
        static int counter = 0;
        static const int winsize = 10;
        counter++;
        if (counter % winsize == winsize - 1) {
            r = dsp::math::maxeach(winsize, lastsnr);
        }
        ImGuiWindow* window = GetCurrentWindow();
        ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
        for (int q = 1; q < r.size(); q++) {
            window->DrawList->AddLine(postSnrLocation + ImVec2(0 + r[q - 1], q - 1 + window->Pos.y), postSnrLocation + ImVec2(0 + r[q], q + window->Pos.y), text);
        }
    }
#pragma clang diagnostic pop


    std::string name;
    bool enabled = true;
    EventHandler<void*> waterfallDrawnHandler;
    EventHandler<ImGui::SNRMeterExtPoint> snrMeterExtPointHandler;
    EventHandler<double> currentFrequencyChangedHandler;
    EventHandler<std::string> instanceCreatedHandler;
};



MOD_EXPORT void _INIT_() {
    config.setPath(core::args["root"].s() + "/noise_reduction_logmmse_config.json");
    config.load(json::object());
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new NRModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (NRModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}