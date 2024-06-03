
#include <bandconfig.h>
#include <gui/smgui.h>

AllBands allBandsConfig = {
    {
        { "160M", 0, 200000, 1 },
        { "80M", 200000, (int)4e6, 2 },
        { "60M", (int)4e6, (int)6e6, 4 },
        { "40M", (int)6e6, (int)9e6, 4 },
        { "30M", (int)9e6, (int)12e6, 8 },
        { "20M", (int)12e6, (int)16e6, 8 },
        { "17M", (int)16e6, (int)19e6, 16 },
        { "15M", (int)19e6, (int)23e6, 16 },
        { "12M", (int)23e6, (int)25e6, 32 },
        { "10M", (int)25e6, (int)60e6, 32 },
    },
};

AllBands rxBands;
AllBands txBands;

void initBands() {
    rxBands = allBandsConfig;
    txBands = allBandsConfig;
}

json toJson(const BandSpec& data) {
    json c;
    c["label"] = data.label;
    c["low"] = data.lowRange;
    c["high"] = data.highRange;
    c["bits"] = data.bits;
    return c;
}
json toJson(const AllBands& data) {
    auto arr = json::array();
    for (auto& v : data.values) {
        arr.push_back(toJson(v));
    }
    return arr;
}

BandSpec parseBandSpec(json j) {
    BandSpec rv;
    if (j.contains("label")) {
        rv.label = j["label"];
    }
    else {
        rv.label = "";
    }
    if (j.contains("low")) {
        rv.lowRange = j["low"];
    }
    else {
        rv.lowRange = 0;
    }
    if (j.contains("high")) {
        rv.highRange = j["high"];
    }
    else {
        rv.lowRange = 0;
    }
    if (j.contains("bits")) {
        rv.bits = j["bits"];
    }
    else {
        rv.lowRange = 0;
    }
    return rv;
}

AllBands parseAllBands(json j) {
    AllBands rv;
    if (j.is_array()) {
        for (const auto & r : j) {
            auto bs = parseBandSpec(r);
            if (!bs.label.empty()) {
                rv.values.emplace_back(bs);
            }
        }
    }
    return rv;
}

void saveBandsConfig(ConfigManager &config) {
    json rxtx;
    rxtx["tx"] = toJson(txBands);
    rxtx["rx"] = toJson(rxBands);
    config.conf["bandsBits"] = rxtx;
}

// returns if some change happened
bool renderCheckboxes(ConfigManager &manager, AllBands &bands, int blockOffset, int currentFreq) {
    bool retval = false;
    for (auto &b : bands.values) {
        bool bits[8];
        for(int i=0; i<7; i++) {
            bits[i] = ((b.bits >> i) & 0x01)!= 0;
            char theId[100];
            snprintf(theId, sizeof(theId), "##hl2bcb_%d_%s_%d", blockOffset, b.label.c_str(), i);
            if (SmGui::Checkbox(theId, &bits[i])) {
                retval = true;
                if (bits[i]) {
                    b.bits |= (int)(0x1 << i);
                } else {
                    b.bits &= (~(int)(0x1 << i)) & 0xFF;
                }
                manager.acquire();
                saveBandsConfig(manager);
                manager.release(true);
            }
            SmGui::SameLine();
        }
        bool shouldPop = false;
        if (currentFreq >= b.lowRange && currentFreq < b.highRange) {
            SmGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0, 1.0f));
            shouldPop = true;
        }
        SmGui::Text(b.label.c_str());
        if (shouldPop) {
            SmGui::PopStyleColor(1);
        }
    }
    return retval;
}

std::pair<int, int> getBitsForBand(int frequency, bool tx) {
    AllBands &scan = tx ? txBands : rxBands;
    int bandNo = 0;
    for (auto &b : scan.values) {
        if (frequency >= b.lowRange && frequency < b.highRange) {
            return std::make_pair(b.bits, bandNo);
        }
        bandNo++;
    }
    return std::make_pair(0, -1);
}

bool bandsEditor(ConfigManager &config, bool isTx, int currentFreq) {
    SmGui::Text("Bands relays config");
    SmGui::Text("RX mode:");
    auto changed = renderCheckboxes(config, rxBands, 1, (!isTx) ? currentFreq: -1);
    SmGui::Text("TX mode:");
    changed |= renderCheckboxes(config, txBands, 2, (isTx) ? currentFreq: -1);
    return changed;
}


void loadBandsConfig(ConfigManager &config) {
    if (config.conf.contains("bandsBits")) {
        auto bits = config.conf["bandsBits"];
        auto newRX = parseAllBands(bits["rx"]);
        auto newTX = parseAllBands(bits["tx"]);
        if (newRX.values.empty() || newTX.values.empty()) {
            // ignore
        } else {
            rxBands = newRX;
            txBands = newTX;
        }
    }
}

