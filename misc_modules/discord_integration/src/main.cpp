#include <imgui.h>
#include <utils/flog.h>
#include <module.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <core.h>
#include <discord_rpc.h>
#include <thread>
#include <radio_interface.h>

SDRPP_MOD_INFO{
    /* Name:            */ "discord_integration",
    /* Description:     */ "Discord Rich Presence module for SDR++",
    /* Author:          */ "Cam K.;Ryzerth",
    /* Version:         */ 0, 0, 2,
    /* Max instances    */ 1
};

#define DISCORD_APP_ID "834590435708108860"

class DiscordIntegrationModule : public ModuleManager::Instance {
public:
    DiscordIntegrationModule(std::string name) {
        this->name = name;

        // Change to timer start later on
        workerRunning = true;
        workerThread = std::thread(&DiscordIntegrationModule::worker, this);

        startPresence();
    }

    ~DiscordIntegrationModule() {
        // Change to timer stop later on
        workerRunning = false;
        if (workerThread.joinable()) { workerThread.join(); }
        Discord_ClearPresence();
        Discord_Shutdown();
    }

    void postInit() {}

    void enable() {
        // Change to timer start later on
        workerRunning = true;
        workerThread = std::thread(&DiscordIntegrationModule::worker, this);
        enabled = true;
    }

    void disable() {
        // Change to timer stop later on
        workerRunning = false;
        if (workerThread.joinable()) { workerThread.join(); }

        enabled = false;
        Discord_ClearPresence();
    }

    bool isEnabled() {
        return enabled;
    }

private:
    // Main thread
    void worker() {
        // TODO: Switch out for condition variable to terminate thread instantly
        // OR even better, the new timer class that I still need to add
        while (workerRunning) {
            workerCounter++;
            if (workerCounter >= 1000) {
                workerCounter = 0;
                updatePresence();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void updatePresence() {
        char freq[200];
        char mode[200];
        double selectedFreq = gui::freqSelect.frequency;
        std::string selectedName = gui::waterfall.selectedVFO;
        strcpy(mode, "Raw");
        if (core::modComManager.interfaceExists(selectedName)) {
            if (core::modComManager.getModuleName(selectedName) == "radio") {
                int modeNum;
                core::modComManager.callInterface(selectedName, RADIO_IFACE_CMD_GET_MODE, NULL, &modeNum);
                if (modeNum == RADIO_IFACE_MODE_NFM) { strcpy(mode, "NFM"); }
                else if (modeNum == RADIO_IFACE_MODE_WFM) {
                    strcpy(mode, "FM");
                }
                else if (modeNum == RADIO_IFACE_MODE_AM) {
                    strcpy(mode, "AM");
                }
                else if (modeNum == RADIO_IFACE_MODE_DSB) {
                    strcpy(mode, "DSB");
                }
                else if (modeNum == RADIO_IFACE_MODE_USB) {
                    strcpy(mode, "USB");
                }
                else if (modeNum == RADIO_IFACE_MODE_CW) {
                    strcpy(mode, "CW");
                }
                else if (modeNum == RADIO_IFACE_MODE_LSB) {
                    strcpy(mode, "LSB");
                }
            }
        }

        if (selectedFreq != lastFreq || mode != lastMode) {
            lastFreq = selectedFreq;
            lastMode = mode;

            // Print out frequency to buffer
            if (selectedFreq >= 1000000.0) {
                snprintf(freq, sizeof freq, "%.3lfMHz %.*s", selectedFreq / 1000000.0, 26, mode);
            }
            else if (selectedFreq >= 1000.0) {
                snprintf(freq, sizeof freq, "%.3lfKHz %.*s", selectedFreq / 1000.0, 26, mode);
            }
            else {
                snprintf(freq, sizeof freq, "%.3lfHz %.*s", selectedFreq, 26, mode);
            }

            // Fill in the rest of the details and send to discord
            presence.details = "Listening to";
            presence.state = freq;
            Discord_UpdatePresence(&presence);
        }
    }

    void startPresence() {
        // Discord initialization
        DiscordEventHandlers handlers;
        memset(&handlers, 0, sizeof(handlers));
        memset(&presence, 0, sizeof(presence));
        Discord_Initialize(DISCORD_APP_ID, &handlers, 1, "");

        // Set the first presence
        presence.details = "Initializing rich presence...";
        presence.startTimestamp = time(0);
        presence.largeImageKey = "sdrpp_large";
        presence.smallImageKey = "github";
        presence.smallImageText = "SDRPlusPlus on GitHub";
        Discord_UpdatePresence(&presence);
    }

    std::string name;
    bool enabled = true;

    // Rich Presence
    DiscordRichPresence presence;
    double lastFreq;
    std::string lastMode = "";

    // Threading
    int workerCounter = 0;
    std::thread workerThread;
    bool workerRunning;
};

MOD_EXPORT void _INIT_() {
    // Nothing here
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new DiscordIntegrationModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (DiscordIntegrationModule*)instance;
}

MOD_EXPORT void _END_() {
    // Nothing here
}

