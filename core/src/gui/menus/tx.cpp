#include <gui/menus/tx.h>
#include <imgui.h>
#include <gui/gui.h>
#include <core.h>
#include <gui/main_window.h>
#include <gui/style.h>
#include <signal_path/signal_path.h>

namespace txmenu {

    int transmitterId = 0;

    EventHandler<std::string> transmitterRegisteredHandler;
    EventHandler<std::string> transmitterUnregisterHandler;
    EventHandler<std::string> transmitterUnregisteredHandler;

    std::vector<std::string> transmitterNames;
    std::string transmitterNamesTxt;
    std::string selectedTransmitter;

    void refreshTransmitters() {
        transmitterNames = sigpath::transmitterManager.getTransmitterNames();
        transmitterNamesTxt.clear();
        for (auto name : transmitterNames) {
            transmitterNamesTxt += name;
            transmitterNamesTxt += '\0';
        }
    }

    void selectTransmitter(std::string name) {
        if (transmitterNames.empty()) {
            selectedTransmitter.clear();
            return;
        }
        auto it = std::find(transmitterNames.begin(), transmitterNames.end(), name);
        if (it == transmitterNames.end()) {
            selectTransmitter(transmitterNames[0]);
            return;
        }
        transmitterId = std::distance(transmitterNames.begin(), it);
        selectedTransmitter = transmitterNames[transmitterId];
        sigpath::transmitterManager.selectTransmitter(transmitterNames[transmitterId]);
    }

    void onTransmitterRegistered(std::string name, void* ctx) {
        refreshTransmitters();

        if (selectedTransmitter.empty()) {
            transmitterId = 0;
            selectTransmitter(transmitterNames[0]);
            return;
        }

        transmitterId = std::distance(transmitterNames.begin(), std::find(transmitterNames.begin(), transmitterNames.end(), selectedTransmitter));
    }

    void onTransmitterUnregister(std::string name, void* ctx) {
        if (name != selectedTransmitter) { return; }

        // TODO: Stop everything
    }

    void onTransmitterUnregistered(std::string name, void* ctx) {
        refreshTransmitters();

        if (transmitterNames.empty()) {
            selectedTransmitter = "";
            return;
        }

        if (name == selectedTransmitter) {
            transmitterId = std::clamp<int>(transmitterId, 0, transmitterNames.size() - 1);
            selectTransmitter(transmitterNames[transmitterId]);
            return;
        }

        transmitterId = std::distance(transmitterNames.begin(), std::find(transmitterNames.begin(), transmitterNames.end(), selectedTransmitter));
    }

    void init() {
        core::configManager.acquire();
        std::string selected = core::configManager.conf["transmitter"];

        refreshTransmitters();
        selectTransmitter(selected);

        transmitterRegisteredHandler.handler = onTransmitterRegistered;
        transmitterUnregisterHandler.handler = onTransmitterUnregister;
        transmitterUnregisteredHandler.handler = onTransmitterUnregistered;
        sigpath::transmitterManager.onTransmitterRegistered.bindHandler(&transmitterRegisteredHandler);
        sigpath::transmitterManager.onTransmitterUnregister.bindHandler(&transmitterUnregisterHandler);
        sigpath::transmitterManager.onTransmitterUnregistered.bindHandler(&transmitterUnregisteredHandler);

        core::configManager.release();
    }

    void draw(void* ctx) {
        float itemWidth = ImGui::GetContentRegionAvailWidth();
        bool running = gui::mainWindow.sdrIsRunning();

        if (running) { style::beginDisabled(); }

        ImGui::SetNextItemWidth(itemWidth);
        if (ImGui::Combo("##transmitter", &transmitterId, transmitterNamesTxt.c_str())) {
            selectTransmitter(transmitterNames[transmitterId]);
            core::configManager.acquire();
            core::configManager.conf["transmitter"] = transmitterNames[transmitterId];
            core::configManager.release(true);
        }

        if (running) { style::endDisabled(); }

        sigpath::transmitterManager.showSelectedMenu();

    }
}
