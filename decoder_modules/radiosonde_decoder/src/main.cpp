#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <imgui.h>
#include <config.h>
#include <core.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <module.h>
#include <signal_path/signal_path.h>
#include <utils/flog.h>
#include "main.hpp"
#include "utils.hpp"

#define CONCAT(a, b) ((std::string(a) + b).c_str())

#define SNAP_INTERVAL 1000
#define UNCAL_COLOR IM_COL32(255, 234, 0, 255)
#define OUT_SAMPLE_RATE 48000

SDRPP_MOD_INFO{
    /* Name:            */ "radiosonde_decoder",
    /* Description:     */ "Radiosonde decoder for SDR++",
    /* Author:          */ "dbdexter-dev",
    /* Version:         */ 0, 10, 0,
    /* Max instances    */ -1
};

ConfigManager config;

RadiosondeDecoderModule::RadiosondeDecoderModule(std::string name) {
    this->name = name;

    const int typeCount = (int)(sizeof(supportedTypes) / sizeof(supportedTypes[0]));
    bool modified = false;
    int typeToSelect = 0;
    std::string gpxPath = getTempFile("radiosonde.gpx");
    std::string ptuPath = getTempFile("radiosonde_ptu.csv");

    config.acquire();
    if (!config.conf.contains(name) || !config.conf[name].is_object()) {
        config.conf[name] = json::object();
        modified = true;
    }
    json& cfg = config.conf[name];
    if (!cfg.contains("gpxPath") || !cfg["gpxPath"].is_string()) {
        cfg["gpxPath"] = gpxPath;
        modified = true;
    }
    if (!cfg.contains("ptuPath") || !cfg["ptuPath"].is_string()) {
        cfg["ptuPath"] = ptuPath;
        modified = true;
    }
    if (!cfg.contains("sondeType") || !cfg["sondeType"].is_number_integer()) {
        cfg["sondeType"] = 0;
        modified = true;
    }
    gpxPath = cfg["gpxPath"].get<std::string>();
    ptuPath = cfg["ptuPath"].get<std::string>();
    typeToSelect = cfg["sondeType"].get<int>();
    if (typeToSelect < 0 || typeToSelect >= typeCount) {
        typeToSelect = 0;
        cfg["sondeType"] = 0;
        modified = true;
    }
    config.release(modified);

    snprintf(gpxFilename, sizeof(gpxFilename), "%s", gpxPath.c_str());
    snprintf(ptuFilename, sizeof(ptuFilename), "%s", ptuPath.c_str());
    selectedType = typeToSelect;

    const float bw = std::get<1>(supportedTypes[selectedType]);
    vfo = sigpath::vfoManager.createVFO(name, ImGui::WaterfallVFO::REF_CENTER, 0, bw, bw, bw, bw, true);
    vfo->setSnapInterval(SNAP_INTERVAL);
    fmDemod.init(vfo->output, bw, bw / 2.0f, false, false);

    /* Resampler to 48kHz */
    resampler.init(&fmDemod.out, bw, OUT_SAMPLE_RATE);

    dfm09decoder.init(&resampler.out, OUT_SAMPLE_RATE, sondeDataHandler, this);
    c50decoder.init(&resampler.out, OUT_SAMPLE_RATE, sondeDataHandler, this);
    imet4decoder.init(&resampler.out, OUT_SAMPLE_RATE, sondeDataHandler, this);
    ims100decoder.init(&resampler.out, OUT_SAMPLE_RATE, sondeDataHandler, this);
    m10decoder.init(&resampler.out, OUT_SAMPLE_RATE, sondeDataHandler, this);
    mrzn1decoder.init(&resampler.out, OUT_SAMPLE_RATE, sondeDataHandler, this);
    rs41decoder.init(&resampler.out, OUT_SAMPLE_RATE, sondeDataHandler, this);

    fmDemod.start();
    resampler.start();
    activeDecoder = std::get<2>(supportedTypes[selectedType]);
    if (activeDecoder) { activeDecoder->start(); }
    enabled = true;

    gui::menu.registerEntry(name, menuHandler, this, this);
}

RadiosondeDecoderModule::~RadiosondeDecoderModule() {
    gui::menu.removeEntry(name);
    disable();
    {
        std::lock_guard<std::mutex> lck(dataMtx);
        gpxWriter.deinit();
        ptuWriter.deinit();
    }
}

void RadiosondeDecoderModule::postInit() {}

void RadiosondeDecoderModule::enable() {
    if (enabled) { return; }

    const int typeCount = (int)(sizeof(supportedTypes) / sizeof(supportedTypes[0]));
    if (selectedType < 0 || selectedType >= typeCount) { selectedType = 0; }
    const float bw = std::get<1>(supportedTypes[selectedType]);

    vfo = sigpath::vfoManager.createVFO(name, ImGui::WaterfallVFO::REF_CENTER, 0, bw, bw, bw, bw, true);
    vfo->setSnapInterval(SNAP_INTERVAL);
    fmDemod.setInput(vfo->output);
    fmDemod.setSamplerate(bw);
    fmDemod.setBandwidth(bw / 2.0f);
    resampler.setInSamplerate(bw);

    fmDemod.start();
    resampler.start();
    activeDecoder = std::get<2>(supportedTypes[selectedType]);
    resetActiveDecoder();
    if (activeDecoder) { activeDecoder->start(); }
    enabled = true;
}

void RadiosondeDecoderModule::disable() {
    if (!enabled) { return; }
    enabled = false;

    if (activeDecoder) { activeDecoder->stop(); }
    activeDecoder = nullptr;
    resampler.stop();
    fmDemod.stop();

    if (vfo) {
        sigpath::vfoManager.deleteVFO(vfo);
        vfo = nullptr;
    }

    {
        std::lock_guard<std::mutex> lck(dataMtx);
        gpxWriter.stopTrack();
        lastData.init();
    }
}

bool RadiosondeDecoderModule::isEnabled() {
    return enabled;
}

std::string RadiosondeDecoderModule::handleDebugCommand(const std::string& cmd, const std::string& args) {
    if (cmd == "get_status") {
        SondeFullData copy;
        uint64_t pkts = 0;
        bool gpxOn = false;
        bool ptuOn = false;
        {
            std::lock_guard<std::mutex> lck(dataMtx);
            copy = lastData;
            pkts = packetCount;
            gpxOn = gpxOutput;
            ptuOn = ptuOutput;
        }
        const int typeCount = (int)(sizeof(supportedTypes) / sizeof(supportedTypes[0]));
        const char* typeName = (selectedType >= 0 && selectedType < typeCount)
            ? std::get<0>(supportedTypes[selectedType])
            : "Unknown";
        json j = {
            {"enabled", enabled},
            {"type", selectedType},
            {"type_name", typeName},
            {"packet_count", pkts},
            {"serial", copy.serial},
            {"seq", copy.seq},
            {"lat", std::isfinite(copy.lat) ? copy.lat : 0.0f},
            {"lon", std::isfinite(copy.lon) ? copy.lon : 0.0f},
            {"alt", std::isfinite(copy.alt) ? copy.alt : 0.0f},
            {"temp", std::isfinite(copy.temp) ? copy.temp : 0.0f},
            {"rh", std::isfinite(copy.rh) ? copy.rh : 0.0f},
            {"pressure", std::isfinite(copy.pressure) ? copy.pressure : 0.0f},
            {"calibrated", copy.calibrated},
            {"calib_percent", copy.calib_percent},
            {"gpx_enabled", gpxOn},
            {"ptu_enabled", ptuOn}
        };
        return j.dump();
    }
    if (cmd == "set_type") {
        try {
            int idx = std::stoi(args);
            const int typeCount = (int)(sizeof(supportedTypes) / sizeof(supportedTypes[0]));
            if (idx < 0 || idx >= typeCount) {
                return json({{"error", "invalid type index"}}).dump();
            }
            onTypeSelected(this, idx);
            return json({{"status", "ok"}, {"type", selectedType}}).dump();
        } catch (...) {
            return json({{"error", "invalid argument"}}).dump();
        }
    }
    return json({{"error", "unknown command: " + cmd}}).dump();
}

void RadiosondeDecoderModule::resetActiveDecoder() {
    switch (selectedType) {
    case 0: rs41decoder.reset(); break;
    case 1: dfm09decoder.reset(); break;
    case 2: ims100decoder.reset(); break;
    case 3: m10decoder.reset(); break;
    case 4: imet4decoder.reset(); break;
    case 5: c50decoder.reset(); break;
    case 6: mrzn1decoder.reset(); break;
    default: break;
    }
}

void RadiosondeDecoderModule::menuHandler(void* ctx) {
    RadiosondeDecoderModule* _this = (RadiosondeDecoderModule*)ctx;
    char timeBuf[64];
    bool gpxStatusChanged = false;
    bool ptuStatusChanged = false;

    SondeFullData dataCopy;
    if (_this->enabled) {
        std::lock_guard<std::mutex> lck(_this->dataMtx);
        dataCopy = _this->lastData;
    }
    else {
        style::beginDisabled();
    }

    const int typeCount = (int)(sizeof(_this->supportedTypes) / sizeof(_this->supportedTypes[0]));
    const char* currentTypeName = (_this->selectedType >= 0 && _this->selectedType < typeCount)
        ? std::get<0>(_this->supportedTypes[_this->selectedType])
        : "Unknown";

    ImGui::LeftLabel("Type");
    ImGui::FillWidth();
    if (ImGui::BeginCombo(CONCAT("##_radiosonde_type_", _this->name), currentTypeName)) {
        for (int i = 0; i < typeCount; i++) {
            const char* curItem = std::get<0>(_this->supportedTypes[i]);
            bool selected = (_this->selectedType == i);
            if (ImGui::Selectable(curItem, selected)) {
                onTypeSelected(ctx, i);
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (ImGui::BeginTable(CONCAT("##radiosonde_data_", _this->name), 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Serial no.");
        ImGui::TableNextColumn();
        if (_this->enabled) {
            ImGui::TextUnformatted(dataCopy.serial.c_str());
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Frame no.");
        ImGui::TableNextColumn();
        if (_this->enabled) {
            ImGui::Text("%d", dataCopy.seq);
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Onboard time");
        ImGui::TableNextColumn();
        if (_this->enabled) {
            struct tm* tmInfo = gmtime(&dataCopy.time);
            if (tmInfo && strftime(timeBuf, sizeof(timeBuf), "%a %b %d %Y %H:%M:%S", tmInfo)) {
                ImGui::TextUnformatted(timeBuf);
            }
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(" ");
        ImGui::TableNextColumn();

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Latitude");
        ImGui::TableNextColumn();
        if (_this->enabled) {
            const float lat = std::isfinite(dataCopy.lat) ? dataCopy.lat : 0.0f;
            ImGui::Text("%8.5f%c", fabsf(lat), (lat >= 0.0f ? 'N' : 'S'));
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Longitude");
        ImGui::TableNextColumn();
        if (_this->enabled) {
            const float lon = std::isfinite(dataCopy.lon) ? dataCopy.lon : 0.0f;
            ImGui::Text("%8.5f%c", fabsf(lon), (lon >= 0.0f ? 'E' : 'W'));
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Altitude");
        ImGui::TableNextColumn();
        if (_this->enabled) {
            ImGui::Text("%.1fm", dataCopy.alt);
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Speed");
        ImGui::TableNextColumn();
        if (_this->enabled) {
            ImGui::Text("%.1fm/s", dataCopy.spd);
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Heading");
        ImGui::TableNextColumn();
        if (_this->enabled) {
            ImGui::Text("%.0f\xC2\xB0", dataCopy.hdg);
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Climb");
        ImGui::TableNextColumn();
        if (_this->enabled) {
            ImGui::Text("%.1fm/s", dataCopy.climb);
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(" ");
        ImGui::TableNextColumn();

        auto drawCalibRow = [&](const char* label, const char* fmt, float val) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(label);
            ImGui::TableNextColumn();
            if (_this->enabled) {
                if (!dataCopy.calibrated) { ImGui::PushStyleColor(ImGuiCol_Text, UNCAL_COLOR); }
                ImGui::Text(fmt, val);
                if (!dataCopy.calibrated) {
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Calibration data not yet complete (%.0f%%).", dataCopy.calib_percent);
                    }
                }
            }
        };

        drawCalibRow("Temperature", "%.1f\xC2\xB0" "C", dataCopy.temp);
        drawCalibRow("Humidity", "%.1f%%", dataCopy.rh);
        drawCalibRow("Dew point", "%.1f\xC2\xB0" "C", dataCopy.dewpt);
        drawCalibRow("Pressure", "%.1fhPa", dataCopy.pressure);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Aux. data");
        ImGui::TableNextColumn();
        if (_this->enabled) {
            ImGui::TextUnformatted(dataCopy.auxData.c_str());
        }

        ImGui::EndTable();
    }

    gpxStatusChanged = ImGui::Checkbox(CONCAT("GPX track##_gpx_track_", _this->name), &_this->gpxOutput);
    ImGui::SameLine();
    ImGui::FillWidth();
    gpxStatusChanged |= ImGui::InputText(CONCAT("##_gpx_fname_", _this->name), _this->gpxFilename, sizeof(_this->gpxFilename),
                                         ImGuiInputTextFlags_EnterReturnsTrue);
    if (gpxStatusChanged) { onGPXOutputChanged(ctx); }

    ptuStatusChanged = ImGui::Checkbox(CONCAT("Log data##_ptu_log_", _this->name), &_this->ptuOutput);
    ImGui::SameLine();
    ImGui::FillWidth();
    ptuStatusChanged |= ImGui::InputText(CONCAT("##_ptu_fname_", _this->name), _this->ptuFilename, sizeof(_this->ptuFilename),
                                         ImGuiInputTextFlags_EnterReturnsTrue);
    if (ptuStatusChanged) { onPTUOutputChanged(ctx); }

    if (!_this->enabled) { style::endDisabled(); }
}

void RadiosondeDecoderModule::sondeDataHandler(SondeFullData* data, void* ctx) {
    RadiosondeDecoderModule* _this = (RadiosondeDecoderModule*)ctx;
    std::lock_guard<std::mutex> lck(_this->dataMtx);
    _this->lastData = *data;
    _this->packetCount++;

    if (!data->serial.empty()) {
        _this->gpxWriter.startTrack(data->serial.c_str());
    }
    _this->gpxWriter.addTrackPoint(data->time, data->lat, data->lon, data->alt, data->spd, data->hdg);
    _this->ptuWriter.addPoint(data);
}

void RadiosondeDecoderModule::onGPXOutputChanged(void* ctx) {
    RadiosondeDecoderModule* _this = (RadiosondeDecoderModule*)ctx;
    bool active = false;
    {
        std::lock_guard<std::mutex> lck(_this->dataMtx);
        if (_this->gpxOutput) {
            _this->gpxOutput = _this->gpxWriter.init(_this->gpxFilename);
            if (!_this->gpxOutput) {
                flog::warn("[radiosonde_decoder] Failed to open GPX output file: {}", _this->gpxFilename);
            }
        }
        else {
            _this->gpxWriter.deinit();
        }
        active = _this->gpxOutput;
    }

    if (active) {
        config.acquire();
        config.conf[_this->name]["gpxPath"] = _this->gpxFilename;
        config.release(true);
    }
}

void RadiosondeDecoderModule::onPTUOutputChanged(void* ctx) {
    RadiosondeDecoderModule* _this = (RadiosondeDecoderModule*)ctx;
    bool active = false;
    {
        std::lock_guard<std::mutex> lck(_this->dataMtx);
        if (_this->ptuOutput) {
            _this->ptuOutput = _this->ptuWriter.init(_this->ptuFilename);
            if (!_this->ptuOutput) {
                flog::warn("[radiosonde_decoder] Failed to open PTU log file: {}", _this->ptuFilename);
            }
        }
        else {
            _this->ptuWriter.deinit();
        }
        active = _this->ptuOutput;
    }

    if (active) {
        config.acquire();
        config.conf[_this->name]["ptuPath"] = _this->ptuFilename;
        config.release(true);
    }
}

void RadiosondeDecoderModule::onTypeSelected(void* ctx, int selection) {
    RadiosondeDecoderModule* _this = (RadiosondeDecoderModule*)ctx;
    const int typeCount = (int)(sizeof(_this->supportedTypes) / sizeof(_this->supportedTypes[0]));

    if (selection < 0 || selection >= typeCount) { return; }
    if (!_this->enabled) { return; }

    if (_this->activeDecoder) { _this->activeDecoder->stop(); }
    _this->activeDecoder = nullptr;
    _this->resampler.stop();
    _this->fmDemod.stop();

    {
        std::lock_guard<std::mutex> lck(_this->dataMtx);
        _this->gpxWriter.stopTrack();
        _this->lastData.init();
    }

    _this->selectedType = selection;

    config.acquire();
    config.conf[_this->name]["sondeType"] = selection;
    config.release(true);

    const float bw = std::get<1>(_this->supportedTypes[selection]);
    if (_this->vfo) {
        _this->vfo->setBandwidthLimits(bw, bw, true);
        _this->vfo->setSampleRate(bw, bw);
    }
    _this->fmDemod.setSamplerate(bw);
    _this->fmDemod.setBandwidth(bw / 2.0f);
    _this->resampler.setInSamplerate(bw);

    _this->fmDemod.start();
    _this->resampler.start();

    _this->activeDecoder = std::get<2>(_this->supportedTypes[selection]);
    _this->resetActiveDecoder();
    if (_this->activeDecoder) { _this->activeDecoder->start(); }
}

MOD_EXPORT void _INIT_() {
    json def = json({});
    config.setPath(core::args["root"].s() + "/radiosonde_decoder_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new RadiosondeDecoderModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (RadiosondeDecoderModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
