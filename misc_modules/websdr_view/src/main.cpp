
#ifdef _WIN32
#define _WINSOCKAPI_ // stops windows.h including winsock.h
#endif

#include "gui/brown/kiwisdr_map.h"
#include "gui/brown/small_waterfall.h"

#include <imgui.h>
#include <module.h>
#include <gui/gui.h>
#include <core.h>
#include <config.h>
#include <gui/widgets/waterfall.h>

#define MAX_COMMAND_LENGTH 8192

SDRPP_MOD_INFO{
        /* Name:            */ "websdr_view",
        /* Description:     */ "View Multiple Websdr",
        /* Author:          */ "San",
        /* Version:         */ 0, 1, 0,
        /* Max instances    */ -1
};

ConfigManager config;

class ReportsMonitorModule : public ModuleManager::Instance {

    bool intlSupport;
    bool mouseWheel;
    bool zoomSave;
    KiwiSDRMapSelector selector;

public:
    struct SingleReceiver {
        std::string id;
        std::string url;
        std::string loc;
        std::string bottomWindowName;
        std::shared_ptr<KiwiSDRClient> client;
        std::shared_ptr<SubWaterfall> wf;
        bool started = false;
        long long startTime;
        ReportsMonitorModule *mod;
        EventHandler<gui::VFOFrequencyChange> vfoFrequencyChangedHandler;


        explicit SingleReceiver(const std::string &id, const std::string &url, const std::string &loc, ReportsMonitorModule *mod) : id(id), url(url), loc(loc), mod(mod) {
            bottomWindowName = "SingleReceiver:" + url;
            vfoFrequencyChangedHandler.ctx = this;
            vfoFrequencyChangedHandler.handler = [](gui::VFOFrequencyChange e, void *ctx) {
                auto *self = (SingleReceiver *) ctx;
                if (self->client) {
                    self->client->tune(e.freq, KiwiSDRClient::TUNE_REAL);
                }
            };
            gui::vfoFrequencyChanged.bindHandler(&vfoFrequencyChangedHandler);
        }


        ~SingleReceiver() {
            gui::vfoFrequencyChanged.unbindHandler(&vfoFrequencyChangedHandler);
        }

        void start() {
            if (!started) {
                client = std::make_shared<KiwiSDRClient>();
                client->init(url);
                started = true;
                startTime = currentTimeMillis();
                client->onConnected = [=]() {
                    client->tune(gui::freqSelect.frequency, KiwiSDRClient::TUNE_REAL);
                };
                client->start();
                wf = std::make_shared<SubWaterfall>(client->IQDATA_FREQUENCY, 6000, id);
                wf->setFreqVisible(false);
                wf->init();
            }
        }

        std::vector<float> peaks;

        void stop() {
            started = false;
            if (client) {
                client->stop();
                client.reset();
            }
            peaks = wf->peaks;
            if (peaks.size() > 10) {
                peaks.erase(peaks.begin(), peaks.begin() + 10); // remove first, because power-on noise.
            }
            wf.reset();                                         // all destruction added
        }

        void DrawChart(const std::vector<float> &values, int width, int height) {
            if (values.empty()) {
                return;
            }

            // Calculate min and max values
            float minValue = *std::min_element(values.begin(), values.end());
            float maxValue = *std::max_element(values.begin(), values.end());
            if (maxValue - minValue < 10 ) {
                minValue = minValue - 10;
                maxValue = maxValue + 10;
            }

            // Set the next window size and disable resizing
            ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
            if (ImGui::BeginChild(("websdr_peakchart_" + id).c_str(), ImVec2(0, 0), false, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoTitleBar)) {
                ImDrawList *draw_list = ImGui::GetWindowDrawList();

                // Calculate scale factors to map the values to the drawable area
                float scaleX = float(width) / (values.size() - 1);
                float scaleY = float(height) / (maxValue - minValue);

                ImVec2 origin = ImGui::GetCursorScreenPos(); // top-left of the drawable area

                // Draw lines or points
                if (values.size() < width) {
                    // Draw lines if there are fewer values than the drawable width
                    for (size_t i = 1; i < values.size(); i++) {
                        draw_list->AddLine(
                                ImVec2(origin.x + (i - 1) * scaleX, origin.y + (maxValue - values[i - 1]) * scaleY),
                                ImVec2(origin.x + i * scaleX, origin.y + (maxValue - values[i]) * scaleY),
                                IM_COL32(255, 255, 255, 255));
                    }
                } else {
                    // Draw points if there are more or equal values than the drawable width
                    for (size_t i = 0; i < values.size(); i++) {
                        draw_list->AddCircleFilled(
                                ImVec2(origin.x + i * scaleX, origin.y + (maxValue - values[i]) * scaleY),
                                2.0f, IM_COL32(255, 255, 255, 255));
                    }
                }

                // Calculate the step size for the grid lines based on the font size
                float stepY = ImGui::GetTextLineHeight();
                int numGridLines = int(height / stepY);
                float valueStepY = (maxValue - minValue) / numGridLines;
                for (int i = 0; i <= numGridLines; ++i) {
                    float valueY = minValue + i * valueStepY;

                    // Convert value to screen coordinates
                    float screenY = origin.y + (maxValue - valueY) * scaleY;

                    // Draw label
                    ImGui::SetCursorScreenPos(ImVec2(origin.x, screenY));
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%.2f", valueY);

                    // Draw grid line
                    draw_list->AddLine(
                            ImVec2(origin.x, screenY),
                            ImVec2(origin.x + width, screenY),
                            IM_COL32(128, 128, 128, 128));
                }

                // Find maximum value and add circles
                auto maxElemIt = std::max_element(values.begin(), values.end());
                if (maxElemIt != values.end()) {
                    float posX = scaleX * std::distance(values.begin(), maxElemIt);
                    float posY = 0;

                    // Draw circle
                    draw_list->AddCircleFilled(ImVec2(origin.x + posX, origin.y + posY), 5, IM_COL32(0, 255, 0, 255));

                    //                    // Draw text
                    //                    ImGui::SetCursorScreenPos(ImVec2(origin.x + posX, origin.y + ImGui::GetTextLineHeight()));
                    //                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%.2f", maxValue);
                }
                ImGui::EndChild();
            }
        }

        void draw() {
            if (client && !client->running) {
                stop();
            }
            if (!started) {
//                ImGui::TextUnformatted(loc.c_str());
                if (peaks.empty()) {
                    ImGui::TextUnformatted(("KiwiSDR: " + url).c_str());
                    ImGui::TextUnformatted("Auto STOP in 15 sec");
                }
                if (doFingerButton("Start all")) {
                    for (auto &r: mod->receivers) {
                        r->start();
                    }
                }
                const ImVec2 avail = ImGui::GetContentRegionAvail();
                if (!peaks.empty()) {
                    ImGui::SameLine();
                    float maxValue = *std::max_element(peaks.begin(), peaks.end());
                    float minValue = *std::min_element(peaks.begin(), peaks.end());
                    ImGui::Text("SNR: %.2f dB", maxValue - minValue);
                    DrawChart(peaks, avail.x, avail.y);
                }
            } else {
                int remains = mod->receiveDuration - (currentTimeMillis() - startTime) / 1000;
                //                    ImGui::Text("Remains: %d", remains);
                if (remains == 0) {
                    stop();
                } else if (client) {
                    client->iqDataLock.lock();
                    if (client->iqData.size() > 0) {
                        std::vector<dsp::stereo_t> vec(client->iqData.size(), {0, 0});
                        for (int i = 0; i < client->iqData.size(); i++) {
                            vec[i].r = vec[i].l = client->iqData[i].real();
                        }
                        //                            flog::info("addAudioSamples: {}", vec.size());
                        wf->addAudioSamples(vec.data(), vec.size(), client->IQDATA_FREQUENCY);
                        client->iqData.clear();
                    }
                    client->iqDataLock.unlock();


                    const ImVec2 sz = {0, 0}; // ImGui::GetContentRegionAvail();
                    ImGui::BeginChild(("child_" + id).c_str(), sz, false, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
                    const ImVec2 savePos = ImGui::GetCursorPos();
                    //                        ImGui::TextUnformatted("Inside child");
                    wf->draw();
//                    ImGui::SetCursorPos(savePos);
//                    doRightText(url);
//                    doRightText(loc);
//                    doRightText(client->connectionStatus);
                    ImGui::EndChild();
                }
            }
        }
    };

    ReportsMonitorModule(std::string name, std::string root) : selector(root, &config, "WebSDR View") {
        this->name = name;
        gui::menu.registerEntry(name, _menuHandler, this, NULL);
    }

    ~ReportsMonitorModule() {
        gui::menu.removeEntry(name);
    }

    void postInit() {
        config.acquire();
        if (config.conf.contains("duration")) {
            receiveDuration = config.conf["duration"];
        }
        if (config.conf.contains("visible")) {
            visible = config.conf["visible"];
        }
        for (auto [k, c]: config.conf["receivers"].items()) {
            std::string url = c["url"];
            std::string loc = c["loc"];
            const std::shared_ptr<SingleReceiver> &recvr = std::make_shared<SingleReceiver>(k, url, loc, this);
            receivers.emplace_back(recvr);
            if (visible) {
                gui::mainWindow.addBottomWindow(recvr->bottomWindowName, [=]() {
                    recvr->draw();
                });
            }
        }
        config.release(false);
    }

    void enable() {
        enabled = true;
    }

    void disable() {
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }


    std::vector<std::shared_ptr<SingleReceiver>> receivers;
    int receiveDuration = 15;
    bool visible = true;

private:
    void menuHandler() {
        auto removeIndex = -1;
        ImGui::LeftLabel("Seconds to receive:");
        ImGui::SameLine();
        if (ImGui::SliderInt("##_websdr_duration_", &receiveDuration, 15, 45)) {
            config.acquire();
            config.conf["duration"] = receiveDuration;
            config.release(true);

        }
        ImGui::LeftLabel("Enabled");
        if (ImGui::Checkbox("##_websdr_visible_", &visible)) {
            config.acquire();
            config.conf["visible"] = visible;
            config.release(true);
            for (auto &r: receivers) {
                if (!visible && gui::mainWindow.hasBottomWindow(r->bottomWindowName)) {
                    gui::mainWindow.removeBottomWindow(r->bottomWindowName);
                }
                if (visible && !gui::mainWindow.hasBottomWindow(r->bottomWindowName)) {
                    gui::mainWindow.addBottomWindow(r->bottomWindowName, [=]() {
                        r->draw();
                    });
                }
            }
        }
        if (doFingerButton("Add new...")) {
            selector.openPopup();
        }
        for (int ri=0; ri< receivers.size(); ri++) {
            std::shared_ptr<SingleReceiver> recvr = receivers[ri];
            ImGui::BeginDisabled(recvr->started);
            if (ImGui::Button(("delete: "+recvr->loc).c_str())) {
                removeIndex = ri;
                gui::mainWindow.removeBottomWindow(recvr->bottomWindowName);
            }
            ImGui::EndDisabled();
        }
        if (removeIndex != -1) {
            std::shared_ptr<SingleReceiver> recvr = receivers[removeIndex];
            config.acquire();
            config.conf["receivers"].erase(recvr->id);
            config.release(true);
            receivers.erase(receivers.begin() + removeIndex);
        }
        selector.drawPopup([=](const std::string &hostPort, const std::string &loc) {
            if (std::find_if(receivers.begin(), receivers.end(), [&](const std::shared_ptr<SingleReceiver> &r) {
                return r->url == hostPort;
            }) == receivers.end()) {
                auto recvr = std::make_shared<SingleReceiver>(std::to_string(currentTimeMillis()), hostPort, loc, this);
                receivers.push_back(recvr);
                config.acquire();
                config.conf["receivers"][recvr->id]["url"] = recvr->url;
                config.conf["receivers"][recvr->id]["loc"] = recvr->loc;
                config.release(true);
                gui::mainWindow.addBottomWindow(recvr->bottomWindowName, [=]() {
                    recvr->draw();
                });
            }
        });
    }

    static void _menuHandler(void *ctx) {
        ((ReportsMonitorModule *) ctx)->menuHandler();
    }

    std::string name;
    bool enabled = true;
};

MOD_EXPORT void _INIT_() {
    config.setPath(std::string(core::getRoot()) + "/websdr_view.json");
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
