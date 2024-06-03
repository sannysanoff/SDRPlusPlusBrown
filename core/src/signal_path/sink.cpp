#include <signal_path/sink.h>
#include <utils/flog.h>
#include <imgui/imgui.h>
#include <gui/style.h>
#include <gui/icons.h>

#include <core.h>
#include "signal_path.h"

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SinkManager::SinkManager() : defaultInputAudio(nullptr) {
    SinkManager::SinkProvider prov;
    prov.create = SinkManager::NullSink::create;
    registerSinkProvider("None", prov);

    sigpath::txState.bindHandler(&this->txHandler);
    txHandler.ctx = this;
    txHandler.handler = [](bool txOn, void* ctx) {
        SinkManager* _this = (SinkManager*)ctx;
        flog::info("_this->setAllMuted: {}", txOn);
        _this->setAllMuted(txOn);
    };

    defaultInputAudio.origin = "SinkManager.defaultInputAudio";

}

SinkManager::Stream::Stream(dsp::stream<dsp::stereo_t>* in, EventHandler<float>* srChangeHandler, float sampleRate) {
    _in = in;
    init(srChangeHandler, sampleRate);
}

void SinkManager::Stream::init(EventHandler<float>* srChangeHandler, float sampleRate) {
    srChange.bindHandler(srChangeHandler);
    _sampleRate = sampleRate;
    if (!_in) {
        _in = &_in0;
    }
    merger.bindStream(0, _in);
    splitter.init(merger.getOutput());
    splitter.bindStream(&volumeInput);
    volumeAjust.init(&volumeInput, 1.0f, false);
    sinkOut = &volumeAjust.out;
}

void SinkManager::Stream::start() {
    if (running) {
        return;
    }

    merger.start();
    splitter.start();
    volumeAjust.start();
    sink->start();
    running = true;
}

void SinkManager::Stream::stop() {
    if (!running) {
        return;
    }
    splitter.stop();
    merger.stop();
    volumeAjust.stop();
    sink->stop();
    running = false;
}

void SinkManager::Stream::setVolume(float volume) {
    guiVolume = volume;
    volumeAjust.setVolume(volume);
}

float SinkManager::Stream::getVolume() {
    return guiVolume;
}

float SinkManager::Stream::getSampleRate() {
    return _sampleRate;
}

void SinkManager::Stream::setInput(dsp::stream<dsp::stereo_t>* in) {
    std::lock_guard<std::mutex> lck(ctrlMtx);
    merger.unbindStream(_in);
    _in = in;
    merger.bindStream(0, _in);
}

dsp::stream<dsp::stereo_t>* SinkManager::Stream::bindStream() {
    dsp::stream<dsp::stereo_t>* stream = new dsp::stream<dsp::stereo_t>;
    splitter.bindStream(stream);
    stream->origin = "SinkManager::Stream::bindStream(new)";
    return stream;
}

void SinkManager::Stream::unbindStream(dsp::stream<dsp::stereo_t>* stream) {
    splitter.unbindStream(stream);
    delete stream;
}

void SinkManager::Stream::setSampleRate(float sampleRate) {
    std::lock_guard<std::mutex> lck(ctrlMtx);
    _sampleRate = sampleRate;
    srChange.emit(sampleRate);
}

void SinkManager::registerSinkProvider(std::string name, SinkProvider provider) {
    if (providers.find(name) != providers.end()) {
        flog::error("Cannot register sink provider '{0}', this name is already taken", name);
        return;
    }

    // Add the provider to the lists
    providers[name] = provider;
    providerNames.push_back(name);

    // Recreatd the text list for the menu
    refreshProviders();

    // Update the IDs of every stream
    for (auto& [streamName, stream] : streams) {
        stream->providerId = std::distance(providerNames.begin(), std::find(providerNames.begin(), providerNames.end(), stream->providerName));
    }

    onSinkProviderRegistered.emit(name);
}

void SinkManager::unregisterSinkProvider(std::string name) {
    if (providers.find(name) == providers.end()) {
        flog::error("Cannot unregister sink provider '{0}', no such provider exists.", name);
        return;
    }

    onSinkProviderUnregister.emit(name);

    // Switch all sinks using it to a null sink
    for (auto& [streamName, stream] : streams) {
        if (providerNames[stream->providerId] != name) { continue; }
        setStreamSink(streamName, "None");
    }

    // Erase the provider from the lists
    providers.erase(name);
    providerNames.erase(std::find(providerNames.begin(), providerNames.end(), name));

    // Recreatd the text list for the menu
    refreshProviders();

    // Update the IDs of every stream
    for (auto& [streamName, stream] : streams) {
        stream->providerId = std::distance(providerNames.begin(), std::find(providerNames.begin(), providerNames.end(), stream->providerName));
    }

    onSinkProviderUnregistered.emit(name);
}

bool SinkManager::configContains(const std::string& name) const {
    core::configManager.acquire();
    bool available = core::configManager.conf["streams"].contains(name);
    core::configManager.release();
    return available;
}


void SinkManager::registerStream(std::string name, SinkManager::Stream* stream) {
    if (streams.find(name) != streams.end()) {
        flog::error("Cannot register stream '{0}', this name is already taken", name);
        return;
    }

    SinkManager::SinkProvider provider;

    provider = providers["None"];

    stream->sink = provider.create(stream, name, provider.ctx);
    stream->providerId = std::distance(providerNames.begin(), std::find(providerNames.begin(), providerNames.end(), "None"));
    stream->providerName = "None";

    streams[name] = stream;
    streamNames.push_back(name);

    // Load config
    bool available = configContains(name);
    if (available) { loadStreamConfig(name); }

    onStreamRegistered.emit(name);
}

void SinkManager::unregisterStream(std::string name) {
    if (streams.find(name) == streams.end()) {
        flog::error("Cannot unregister stream '{0}', this stream doesn't exist", name);
        return;
    }
    onStreamUnregister.emit(name);
    SinkManager::Stream* stream = streams[name];
    stream->stop();
    delete stream->sink;
    streams.erase(name);
    streamNames.erase(std::remove(streamNames.begin(), streamNames.end(), name), streamNames.end());
    onStreamUnregistered.emit(name);
}

void SinkManager::startStream(std::string name) {
    if (streams.find(name) == streams.end()) {
        flog::error("Cannot start stream '{0}', this stream doesn't exist", name);
        return;
    }
    streams[name]->start();
}

void SinkManager::stopStream(std::string name) {
    if (streams.find(name) == streams.end()) {
        flog::error("Cannot stop stream '{0}', this stream doesn't exist", name);
        return;
    }
    streams[name]->stop();
}

float SinkManager::getStreamSampleRate(std::string name) {
    if (streams.find(name) == streams.end()) {
        flog::error("Cannot get sample rate of stream '{0}', this stream doesn't exist", name);
        return -1.0f;
    }
    return streams[name]->getSampleRate();
}

dsp::stream<dsp::stereo_t>* SinkManager::bindStream(std::string name) {
    if (streams.find(name) == streams.end()) {
        flog::error("Cannot bind to stream '{0}'. Stream doesn't exist", name);
        return NULL;
    }
    return streams[name]->bindStream();
}

void SinkManager::unbindStream(std::string name, dsp::stream<dsp::stereo_t>* stream) {
    if (streams.find(name) == streams.end()) {
        flog::error("Cannot unbind from stream '{0}'. Stream doesn't exist", name);
        return;
    }
    streams[name]->unbindStream(stream);
}

dsp::routing::Merger<dsp::stereo_t> *SinkManager::getMerger(std::string name) {
    if (streams.find(name) == streams.end()) {
        flog::error("Cannot unbind from stream '{0}'. Stream doesn't exist", name);
        return nullptr;
    }
    return streams[name]->getMerger();
}

void SinkManager::setStreamSink(std::string name, std::string providerName) {
    if (streams.find(name) == streams.end()) {
        flog::error("Cannot set sink for stream '{0}'. Stream doesn't exist", name);
        return;
    }
    Stream* stream = streams[name];
    if (providers.find(providerName) == providers.end()) {
        flog::error("Unknown sink provider '{0}'", providerName);
        return;
    }

    if (stream->running) {
        stream->sink->stop();
    }
    delete stream->sink;
    stream->providerId = std::distance(providerNames.begin(), std::find(providerNames.begin(), providerNames.end(), providerName));
    stream->providerName = providerName;
    SinkManager::SinkProvider prov = providers[providerName];
    stream->sink = prov.create(stream, name, prov.ctx);
    if (stream->running) {
        stream->sink->start();
    }
}

void SinkManager::setAllMuted(bool muted) {
    flog::info("::setAllMuted: {}", muted);
    for(auto &k : streams) {
        k.second->volumeAjust.setTempMuted(muted);
    }
}

void SinkManager::showVolumeSlider(std::string name, std::string prefix, float width, float btnHeight, int btnBorder, bool sameLine) {
    // TODO: Replace map with some hashmap for it to be faster
    float height = ImGui::GetTextLineHeightWithSpacing() + 2;
    float sliderHeight = height;
    if (btnHeight > 0) {
        height = btnHeight;
    }

    float ypos = ImGui::GetCursorPosY();
    float sliderOffset = 8.0f * style::uiScale;

    if (streams.find(name) == streams.end() || name == "") {
        float dummy = 0.0f;
        style::beginDisabled();
        ImGui::PushID(ImGui::GetID(("sdrpp_unmute_btn_" + name).c_str()));
        ImGui::ImageButton(icons::MUTED, ImVec2(height, height), ImVec2(0, 0), ImVec2(1, 1), btnBorder, ImVec4(0, 0, 0, 0), ImGui::GetStyleColorVec4(ImGuiCol_Text));
        ImGui::PopID();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(width - height - sliderOffset);
        ImGui::SetCursorPosY(ypos + ((height - sliderHeight) / 2.0f) + btnBorder);
        ImGui::SliderFloat((prefix + name).c_str(), &dummy, 0.0f, 1.0f, "");
        style::endDisabled();
        if (sameLine) { ImGui::SetCursorPosY(ypos); }
        return;
    }

    SinkManager::Stream* stream = streams[name];

    if (stream->volumeAjust.getMuted()) {
        ImGui::PushID(ImGui::GetID(("sdrpp_unmute_btn_" + name).c_str()));
        if (ImGui::ImageButton(icons::MUTED, ImVec2(height, height), ImVec2(0, 0), ImVec2(1, 1), btnBorder, ImVec4(0, 0, 0, 0), ImGui::GetStyleColorVec4(ImGuiCol_Text))) {
            stream->volumeAjust.setMuted(false);
            core::configManager.acquire();
            saveStreamConfig(name);
            core::configManager.release(true);
        }
        ImGui::PopID();
    }
    else {
        ImGui::PushID(ImGui::GetID(("sdrpp_mute_btn_" + name).c_str()));
        if (ImGui::ImageButton(icons::UNMUTED, ImVec2(height, height), ImVec2(0, 0), ImVec2(1, 1), btnBorder, ImVec4(0, 0, 0, 0), ImGui::GetStyleColorVec4(ImGuiCol_Text))) {
            stream->volumeAjust.setMuted(true);
            core::configManager.acquire();
            saveStreamConfig(name);
            core::configManager.release(true);
        }
        ImGui::PopID();
    }

    ImGui::SameLine();

    ImGui::SetNextItemWidth(width - height - sliderOffset);
    ImGui::SetCursorPosY(ypos + ((height - sliderHeight) / 2.0f) + btnBorder);
    if (ImGui::SliderFloat((prefix + name).c_str(), &stream->guiVolume, 0.0f, 1.0f, "")) {
        stream->setVolume(stream->guiVolume);
        core::configManager.acquire();
        saveStreamConfig(name);
        core::configManager.release(true);
    }
    if (sameLine) { ImGui::SetCursorPosY(ypos); }
    //ImGui::SetCursorPosY(ypos);
}

void SinkManager::loadStreamConfig(std::string name) {
    json conf = core::configManager.conf["streams"][name];
    SinkManager::Stream* stream = streams[name];
    std::string provName = conf["sink"];
    if (providers.find(provName) == providers.end()) {
        provName = providerNames[0];
    }
    long newProvider = std::distance(providerNames.begin(), std::find(providerNames.begin(), providerNames.end(), provName));
    if (newProvider != stream->providerId) {
        if (stream->running) {
            stream->sink->stop();
        }
        delete stream->sink;
        SinkManager::SinkProvider prov = providers[provName];
        stream->providerId = newProvider;
        stream->providerName = provName;
        stream->sink = prov.create(stream, name, prov.ctx);
        if (stream->running) {
            stream->sink->start();
        }
    }
    stream->setVolume(conf["volume"]);
    stream->volumeAjust.setMuted(conf["muted"]);
}

void SinkManager::saveStreamConfig(std::string name) {
    SinkManager::Stream* stream = streams[name];
    json conf;
    conf["sink"] = providerNames[stream->providerId];
    conf["volume"] = stream->getVolume();
    conf["muted"] = stream->volumeAjust.getMuted();
    core::configManager.conf["streams"][name] = conf;
}

// Note: acquire and release config before running this
void SinkManager::loadSinksFromConfig() {
    for (auto const& [name, stream] : streams) {
        if (!core::configManager.conf["streams"].contains(name)) { continue; }
        loadStreamConfig(name);
    }
}

void SinkManager::showMenu() {
    float menuWidth = ImGui::GetContentRegionAvail().x;
    int count = 0;
    int maxCount = streams.size();

    std::string provStr = "";
    for (auto const& name : providerNames) {
        provStr += name;
        provStr += '\0';
    }

    std::vector<std::function<void()>> postActions;
    for (auto const& [name, stream] : streams) {
        ImGui::SetCursorPosX((menuWidth / 2.0f) - (ImGui::CalcTextSize(name.c_str()).x / 2.0f));
        auto [primaryName, index] = getSecondaryStreamIndex(name);
        ImGui::Text("%s", (primaryName + (index <= 0 ? "" : " (output " + std::to_string(index+1) + ")")).c_str());

        ImGui::SetNextItemWidth(menuWidth);
        if (ImGui::Combo(CONCAT("##_sdrpp_sink_select_", name), &stream->providerId, provStr.c_str())) {
            setStreamSink(name, providerNames[stream->providerId]);
            core::configManager.acquire();
            saveStreamConfig(name);
            core::configManager.release(true);
        }

        stream->sink->menuHandler();

        auto v1 = ImGui::GetContentRegionAvail();
        showVolumeSlider(name, "##_sdrpp_sink_menu_vol_", menuWidth);
        auto v2 = ImGui::GetContentRegionAvail();

        if (!SinkManager::isSecondaryStream(name)) {
            if (ImGui::Button(("Add secondary for " + name).c_str(), ImVec2(menuWidth, v1.y-v2.y))) {
                auto name0 = name;
                postActions.emplace_back([=]{onAddSubstream.emit(name0);});
            }
        } else {
            if (ImGui::Button("Remove secondary", ImVec2(menuWidth, v1.y - v2.y))) {
                auto name0 = name;
                postActions.emplace_back([=]{onRemoveSubstream.emit(name0);});
            }
        }


        count++;
        if (count < maxCount) {
            ImGui::Spacing();
            ImGui::Separator();
        }
        ImGui::Spacing();
    }

    for(auto &_: postActions) _();  // to optionally modify the streams list after loop over it.
}

std::vector<std::string> SinkManager::getStreamNames() {
    return streamNames;
}

void SinkManager::refreshProviders() {
    providerNamesTxt.clear();
    for (auto& provName : providerNames) {
        providerNamesTxt += provName;
        providerNamesTxt += '\0';
    }
}