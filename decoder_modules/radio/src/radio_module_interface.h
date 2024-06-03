#pragma once

#include <imgui.h>
#include "demod.h"

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

struct RadioModuleInterface {


    typedef demod::Demodulator *(*demodProviderFunction)(int);

    std::vector<demodProviderFunction> demodulatorProviders;
    std::vector<std::pair<std::string, int>> radioModes;

    RadioModuleInterface() {
        // fill in default modes.
        radioModes.push_back(std::make_pair("FM", RADIO_DEMOD_NFM));
        radioModes.push_back(std::make_pair("WFM", RADIO_DEMOD_WFM));
        radioModes.push_back(std::make_pair("AM", RADIO_DEMOD_AM));
        radioModes.push_back(std::make_pair("DSB", RADIO_DEMOD_DSB));
        radioModes.push_back(std::make_pair("USB", RADIO_DEMOD_USB));
        radioModes.push_back(std::make_pair("CW", RADIO_DEMOD_CW));
        radioModes.push_back(std::make_pair("LSB", RADIO_DEMOD_LSB));
        radioModes.push_back(std::make_pair("Raw", RADIO_DEMOD_RAW));

    }


    virtual int getSelectedDemodId() = 0;

    virtual bool selectDemodByID(DemodID id) = 0;

    int getDemodIndex(int demodId) {
        for (int i = 0; i < radioModes.size(); i++) {
            if (radioModes[i].second == demodId) {
                return i;
            }
        }
        return -1;
    }

    DemodID getDemodByIndex(int index) {
        if (index >= 0 && index < radioModes.size()) {
            return (DemodID)radioModes[index].second;
        }
        return RADIO_DEMOD_NFM;
    }

};
