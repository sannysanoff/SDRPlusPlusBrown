#define _USE_MATH_DEFINES
#include <cmath>
#include <imgui.h>
#include <module.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <signal_path/signal_path.h>
#include <signal_path/sink.h>
#include <dsp/buffer/packer.h>
#include <dsp/convert/stereo_to_mono.h>
#include <utils/flog.h>
#include <RtAudio.h>
#include <config.h>
#include <core.h>
#include <utils/stream_tracker.h>
#include "shared_microphone.h"
#include "sink_config.h"
#include "output_buffer.h"

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "brown_audio_sink",
    /* Description:     */ "Mic enabled SDR++Brown Audio Sink",
    /* Author:          */ "Ryzerth;Sanny",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

ConfigManager config;

static bool rtaudioCallbackError;

#if !defined(RTAUDIO_VERSION_MAJOR) || RTAUDIO_VERSION_MAJOR < 6

static void rtaudioCallback(RtAudioError::Type type, const std::string& errorText) {
    rtaudioCallbackError = true;
}

#else
static void rtaudioCallback(int type, const std::string& errorText) {
    rtaudioCallbackError = true;
}

#endif


class AudioSink : SinkManager::Sink {
public:
    StreamTracker spackerTracker;

    std::atomic<int> underflow{0}; // 1 = small underflow, 2 = full underflow

    bool micInput = true;

    AudioSink(SinkManager::Stream* stream, std::string streamName) : spackerTracker("output stereo packer") {
        _stream = stream;
        _streamName = streamName;
        //        s2m.init(_stream->sinkOut);
        //        monoPacker.init(&s2m.out, 512);
        outputBuffer.init(_stream->sinkOut);

#if RTAUDIO_VERSION_MAJOR >= 6
        audio.setErrorCallback(&errorCallback);
#endif

        std::string device;
        {
            SinkConfigLock lock(config);
            auto& settings = sinkSettings(config.conf, _streamName);
            device = settings["device"].get<std::string>();
            if (settings["micInput"].is_boolean()) { micInput = settings["micInput"].get<bool>(); }
            if (config.conf["micInput"].is_boolean()) { micInput = config.conf["micInput"].get<bool>(); }
            lock.modified = true;
        }

        RtAudio::DeviceInfo info;
#if RTAUDIO_VERSION_MAJOR >= 6
        for (int i : audio.getDeviceIds()) {
#else
        int count = audio.getDeviceCount();
        for (int i = 0; i < count; i++) {
#endif
            try {
                info = audio.getDeviceInfo(i);
#if !defined(RTAUDIO_VERSION_MAJOR) || RTAUDIO_VERSION_MAJOR < 6
                if (!info.probed) {
                    RtAudio::StreamParameters parameters;
                    parameters.deviceId = i;
                    parameters.nChannels = 2;
                    unsigned int bufferFrames = sampleRate / 60;
                    RtAudio::StreamOptions opts;
                    //                opts.flags = RTAUDIO_MINIMIZE_LATENCY;
                    opts.streamName = _streamName;
                    info.probed = true;
                    rtaudioCallbackError = false;
                    try {
                        audio.openStream(&parameters, NULL, RTAUDIO_FLOAT32, sampleRate, &bufferFrames, &callback, this, &opts, rtaudioCallback);
                    }
                    catch (RtAudioError &err) {
                        rtaudioCallbackError = true;
                    }
                    if (rtaudioCallbackError) {
                        continue;
                    }
                    audio.closeStream();
                    info.outputChannels = 2;
                }
#endif
                if (info.outputChannels == 0) { continue; }
                if (info.isDefaultOutput) { defaultOutputDevId = devList.size(); }
                devList.push_back(info);
                txtDevList += info.name;
                txtDevList += '\0';
                auto ni = info;
                ni.name += " -> left";
                devList.push_back(ni);
                txtDevList += ni.name;
                txtDevList += '\0';
                ni = info;
                ni.name += " -> right";
                devList.push_back(ni);
                txtDevList += ni.name;
                txtDevList += '\0';
                deviceIds.push_back(i);
                deviceIds.push_back(i);
                deviceIds.push_back(i);
            } catch (const std::exception e) {
                flog::error("BrownAudioSinkModule Error getting audio device ({}) info: {}", i, e.what());
            }
        }
        selectByName(device);
        instances.push_back(this);
        if (microphoneController() == this && !microphoneConfigured) {
            sharedMicrophone.configure(micInput, sampleRate);
            microphoneConfigured = true;
            SinkConfigLock lock(config);
            config.conf["micInput"] = micInput;
            lock.modified = true;
        }
    }

    ~AudioSink() {
        stop();
        instances.erase(std::remove(instances.begin(), instances.end(), this), instances.end());
    }

    void start() {
        if (running) { return; }
        running = doStart();
    }

    void stop() {
        doStop();
        running = false;
    }

    void selectFirst() {
        selectById(defaultOutputDevId);
    }

    void selectByName(std::string name) {
        for (int i = 0; i < devList.size(); i++) {
            if (devList[i].name == name) {
                selectById(i);
                return;
            }
        }
        selectFirst();
    }

    void selectById(int id) {
        if (id < 0 || id >= (int)devList.size()) { return; }
        bool restart = running;
        stop();
        devId = id;
        {
            SinkConfigLock lock(config);
            auto& settings = sinkSettings(config.conf, _streamName);
            sampleRate = sinkSampleRate(settings["devices"][devList[id].name], devList[id].preferredSampleRate);
            lock.modified = true;
        }

        sampleRates = devList[id].sampleRates;
        sampleRatesTxt = "";
        char buf[256];
        bool found = false;
        unsigned int defaultId = 0;
        sampleRates.erase(std::remove_if(sampleRates.begin(), sampleRates.end(), [](unsigned int rate) { return rate < 8000 || rate > 768000; }), sampleRates.end());
        if (sampleRates.empty()) { sampleRates.push_back(48000); }
        unsigned int defaultSr = devList[id].preferredSampleRate;
        if (std::find(sampleRates.begin(), sampleRates.end(), defaultSr) == sampleRates.end()) { defaultSr = sampleRates.front(); }
        for (int i = 0; i < sampleRates.size(); i++) {
            if (sampleRates[i] == sampleRate) {
                found = true;
                srId = i;
            }
            if (sampleRates[i] == defaultSr) {
                defaultId = i;
            }
            snprintf(buf, sizeof buf, "%d", sampleRates[i]);
            sampleRatesTxt += buf;
            sampleRatesTxt += '\0';
        }
        if (!found) {
            sampleRate = defaultSr;
            srId = defaultId;
        }

        {
            SinkConfigLock lock(config);
            sinkSettings(config.conf, _streamName)["devices"][devList[id].name] = sampleRate;
            lock.modified = true;
        }
        _stream->setSampleRate(sampleRate);

        if (microphoneController() == this) { sharedMicrophone.configure(sharedMicrophone.enabled, sampleRate); }
        if (restart) { start(); }
    }

    void menuHandler() {
        float menuWidth = ImGui::GetContentRegionAvail().x;
        if (devList.empty()) {
            ImGui::TextUnformatted("No audio output devices available");
            return;
        }

        ImGui::SetNextItemWidth(menuWidth);
        int selectedDevice = devId;
        if (ImGui::Combo(("##_brown_audio_sink_dev_" + _streamName).c_str(), &selectedDevice, txtDevList.c_str())) {
            selectById(selectedDevice);
            SinkConfigLock lock(config);
            config.conf[_streamName]["device"] = devList[devId].name;
            lock.modified = true;
        }

        if (SinkManager::getSecondaryStreamIndex(_streamName).second == 0) {
            // only primary one has frequency selection
            if (ImGui::Combo(("##_brown_audio_sink_sr_" + _streamName).c_str(), &srId, sampleRatesTxt.c_str())) {
                bool restart = running;
                stop();
                sampleRate = sampleRates[srId];
                _stream->setSampleRate(sampleRate);
                if (microphoneController() == this) { sharedMicrophone.configure(sharedMicrophone.enabled, sampleRate); }
                if (restart) { start(); }
                SinkConfigLock lock(config);
                config.conf[_streamName]["devices"][devList[devId].name] = sampleRate;
                lock.modified = true;
            }
            if (underflow != 0) {
                ImGui::SameLine();
                ImGui::Text("Underflow %d", underflow.load());
            }
        }
        bool controller = microphoneController() == this;
        bool enabled = sharedMicrophone.enabled;
        if (!controller) { style::beginDisabled(); }
        if (ImGui::Checkbox(("Mic input##_brown_mic_" + _streamName).c_str(), &enabled) && controller) {
            micInput = enabled;
            sharedMicrophone.configure(enabled, sampleRate);
            SinkConfigLock lock(config);
            config.conf["micInput"] = enabled;
            config.conf[_streamName]["micInput"] = enabled;
            lock.modified = true;
        }
        if (!controller) {
            style::endDisabled();
            ImGui::TextUnformatted("Microphone is shared; controlled by the first primary output");
        }
    }

    const int microFrames = 4;

#if RTAUDIO_VERSION_MAJOR >= 6
    static void errorCallback(RtAudioErrorType type, const std::string& errorText) {
        switch (type) {
        case RtAudioErrorType::RTAUDIO_NO_ERROR:
            return;
        case RtAudioErrorType::RTAUDIO_WARNING:
        case RtAudioErrorType::RTAUDIO_NO_DEVICES_FOUND:
        case RtAudioErrorType::RTAUDIO_DEVICE_DISCONNECT:
            flog::warn("BrownAudioSinkModule Warning: {} ({})", errorText, (int)type);
            break;
        default:
            flog::error("BrownAudioSinkModule Error: {} ({})", errorText, (int)type);
        }
    }
#endif

private:
    static AudioSink* microphoneController() {
        for (auto* instance : instances) {
            if (!SinkManager::isSecondaryStream(instance->_streamName)) { return instance; }
        }
        return nullptr;
    }

    bool doStart() {
        if (devList.empty()) { return false; }
        RtAudio::StreamParameters outputParameters;
        outputParameters.deviceId = deviceIds[devId];
        outputParameters.nChannels = 2;
        unsigned int bufferFrames = sampleRate / 60;

        RtAudio::StreamOptions opts;
        opts.flags = RTAUDIO_MINIMIZE_LATENCY;
        opts.streamName = _streamName;
        std::replace(opts.streamName.begin(), opts.streamName.end(), '#', '_');

        try {
            unsigned int microBuffer = bufferFrames / microFrames;
            // Capture always belongs to the shared service, including duplex devices.
#if RTAUDIO_VERSION_MAJOR >= 6
            if (audio.openStream(&outputParameters, nullptr, RTAUDIO_FLOAT32, sampleRate, &microBuffer, &callback, this, &opts) != RTAUDIO_NO_ERROR) {
                throw std::runtime_error("Could not open output stream");
            }
#else
            audio.openStream(&outputParameters, nullptr, RTAUDIO_FLOAT32, sampleRate, &microBuffer, &callback, this, &opts);
#endif
            outputBuffer.prepare((std::max)(bufferFrames, microBuffer * 2));
            outputBuffer.start();
#if RTAUDIO_VERSION_MAJOR >= 6
            if (audio.startStream() != RTAUDIO_NO_ERROR) {
                throw std::runtime_error("Could not start output stream");
            }
#else
            audio.startStream();
#endif
            sharedMicrophone.acquire(this);
            return true;
        } catch (const std::exception& e) {
            flog::error("Could not open audio device: {}", e.what());
        } catch (...) {
            flog::error("Could not open audio device. generic exception");
        }
        doStop();
        return false;
    }

    void doStop() {
        // Idempotent, including partially started sinks and changed UI settings.
        sharedMicrophone.release(this);
        outputBuffer.stop();
        if (audio.isStreamOpen()) { audio.closeStream(); }

    }

    static int callback(void* outputBuffer, void*, unsigned int nBufferFrames, double, RtAudioStreamStatus, void* userData) {
        AudioSink* _this = (AudioSink*)userData;
        auto* output = static_cast<dsp::stereo_t*>(outputBuffer);
        size_t received = _this->outputBuffer.consume(output, nBufferFrames);
        _this->underflow.store(received < nBufferFrames, std::memory_order_relaxed);
        if (sigpath::sinkManager.toneGenerator.load(std::memory_order_relaxed) == 1) {
            float tick = 2 * M_PI * 800 / _this->sampleRate;
            for (size_t i = 0; i < received; i++) {
                output[i].l = output[i].r = sin(_this->lastPhase);
                _this->lastPhase += tick;
                if (_this->lastPhase >= 2 * M_PI) { _this->lastPhase -= 2 * M_PI; }
            }
        }
        int channel = _this->devId % 3;
        for (size_t i = 0; i < received; i++) {
            if (channel == 1) { output[i].r = 0; }
            if (channel == 2) { output[i].l = 0; }
        }
        return 0;
    }

    SinkManager::Stream* _stream;
    dsp::convert::StereoToMono s2m;
    dsp::buffer::Packer<float> monoPacker;
    AudioOutputBuffer outputBuffer;

    std::string _streamName;

    int srId = 0;
    int devCount;
    int devId = 0;
    bool running = false;

    unsigned int defaultOutputDevId = 0;    // dev == index in our reduced list

    std::vector<RtAudio::DeviceInfo> devList;
    std::vector<unsigned int> deviceIds;
    std::string txtDevList;

    std::vector<unsigned int> sampleRates;
    std::string sampleRatesTxt;
    unsigned int sampleRate = 48000;

    RtAudio audio;
    float lastPhase = 0;
    static bool microphoneConfigured;
    static SharedMicrophone sharedMicrophone;
    static std::vector<AudioSink*> instances;
};

bool AudioSink::microphoneConfigured = false;
SharedMicrophone AudioSink::sharedMicrophone;
std::vector<AudioSink*> AudioSink::instances;

class AudioSinkModule : public ModuleManager::Instance {
public:
    AudioSinkModule(std::string name) {
        this->name = name;
        provider.create = create_sink;
        provider.ctx = this;

        sigpath::sinkManager.registerSinkProvider("Brown Audio", provider);
    }

    ~AudioSinkModule() {
        // Unregister sink, this will automatically stop and delete all instances of the audio sink
        sigpath::sinkManager.unregisterSinkProvider("Brown Audio");
    }

    void postInit() {}

    void enable() {
        enabled = true;
    }

    void disable() {
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }

private:
    static SinkManager::Sink* create_sink(SinkManager::Stream* stream, std::string streamName, void* ctx) {
        return (SinkManager::Sink*)(new AudioSink(stream, streamName));
    }

    std::string name;
    bool enabled = true;
    SinkManager::SinkProvider provider;
};


MOD_EXPORT void _INIT_() {
    json def = json({});
    config.setPath(std::string(core::getRoot()) + "/brown_audio_sink_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT void* _CREATE_INSTANCE_(std::string name) {
    AudioSinkModule* instance = new AudioSinkModule(name);
    return instance;
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (AudioSinkModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
