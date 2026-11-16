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

// Length of one output period, i.e. how much audio the device keeps ahead of the
// speaker. The device period applies to every application sharing this output,
// not just to SDR++, so it also sets how much slack the audio system has when
// the machine stalls for a moment (disk, Wi-Fi, GPU, scheduler). A short period
// with RTAUDIO_MINIMIZE_LATENCY - which makes RtAudio ask the device for its
// smallest buffer - leaves almost no slack, and those stalls then become audible
// clicks in whatever is playing. 20 ms is inaudible for listening.
static constexpr unsigned int DEVICE_BUFFER_MS = 20;

// The ring between the DSP chain and the device has to cover how bursty the
// producer is, not the device period: a DAB+ service emits a 120 ms superframe's
// worth of audio at once and then nothing until the next one, so a ring of a few
// device periods starves in between and the sink underflows. This is latency the
// listener pays once, at the start.
static constexpr unsigned int JITTER_BUFFER_MS = 300;

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
    // Callbacks that found less audio than the device asked for, for diagnosis.
    static std::atomic<uint64_t> underflowCallbacks;
    // Diagnosis only: written by the audio and DSP threads, read by the status
    // command. Rates are computed by the caller from two samples.
    static uint64_t callbackCount;
    static uint64_t consumedFrames;
    static uint64_t writtenFrames;
    static uint64_t zeroFilledFrames;
    static uint64_t writeBlocks;
    static unsigned minCallbackFrames;
    static unsigned maxCallbackFrames;
    static unsigned maxWriteFrames;

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
        unsigned int bufferFrames = (sampleRate * DEVICE_BUFFER_MS) / 1000;

        RtAudio::StreamOptions opts;
        opts.streamName = _streamName;
        std::replace(opts.streamName.begin(), opts.streamName.end(), '#', '_');

        try {
            unsigned int periodFrames = bufferFrames;
            // Capture always belongs to the shared service, including duplex devices.
#if RTAUDIO_VERSION_MAJOR >= 6
            if (audio.openStream(&outputParameters, nullptr, RTAUDIO_FLOAT32, sampleRate, &periodFrames, &callback, this, &opts) != RTAUDIO_NO_ERROR) {
                throw std::runtime_error("Could not open output stream");
            }
#else
            audio.openStream(&outputParameters, nullptr, RTAUDIO_FLOAT32, sampleRate, &microBuffer, &callback, this, &opts);
#endif
            this->periodFrames = periodFrames;
            outputBuffer.prepare((std::max)(size_t(bufferFrames), size_t(sampleRate) * JITTER_BUFFER_MS / 1000));
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
        callbackCount++;
        consumedFrames += received;
        zeroFilledFrames += nBufferFrames - received;
        if (nBufferFrames < minCallbackFrames) { minCallbackFrames = nBufferFrames; }
        if (nBufferFrames > maxCallbackFrames) { maxCallbackFrames = nBufferFrames; }
        _this->underflow.store(received < nBufferFrames, std::memory_order_relaxed);
        if (received < nBufferFrames) { underflowCallbacks.fetch_add(1, std::memory_order_relaxed); }
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

    friend class AudioSinkModule;

    std::vector<RtAudio::DeviceInfo> devList;
    std::vector<unsigned int> deviceIds;
    std::string txtDevList;

    std::vector<unsigned int> sampleRates;
    std::string sampleRatesTxt;
    unsigned int sampleRate = 48000;
    unsigned int periodFrames = 0;   // what RtAudio actually negotiated

    RtAudio audio;
    float lastPhase = 0;
    static bool microphoneConfigured;
    static SharedMicrophone sharedMicrophone;
    static std::vector<AudioSink*> instances;
};

std::atomic<uint64_t> AudioSink::underflowCallbacks{0};
uint64_t AudioSink::callbackCount = 0;
uint64_t AudioSink::consumedFrames = 0;
uint64_t AudioSink::writtenFrames = 0;
uint64_t AudioSink::zeroFilledFrames = 0;
uint64_t AudioSink::writeBlocks = 0;
unsigned AudioSink::minCallbackFrames = (unsigned)-1;
unsigned AudioSink::maxCallbackFrames = 0;
unsigned AudioSink::maxWriteFrames = 0;
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

    // Diagnostic readout for the e2e tests and the debug HTTP API: the sink's own
    // counters, not the demodulator's view of them.
    std::string handleDebugCommand(const std::string& cmd, const std::string& args) override {
        if (cmd == "sink_capture") {
            unsigned seconds = 5;
            try { if (!args.empty()) { seconds = std::max(1u, std::min(60u, (unsigned)std::stoi(args))); } } catch (...) {}
            for (auto* sink : AudioSink::instances) { sink->outputBuffer.startCapture(size_t(sink->sampleRate) * seconds); }
            return "{\"status\":\"ok\",\"seconds\":" + std::to_string(seconds) + "}";
        }
        if (cmd == "sink_capture_save") {
            FILE* f = fopen(args.c_str(), "wb");
            if (!f) { return "{\"error\":\"cannot open output\"}"; }
            std::vector<dsp::stereo_t> pcm;
            for (auto* sink : AudioSink::instances) { pcm = sink->outputBuffer.capture; }
            const uint32_t frames = uint32_t(pcm.size());
            const uint16_t channels = 2, bits = 16;
            const uint32_t rate = AudioSink::instances.empty() ? 48000 : AudioSink::instances.front()->sampleRate;
            const uint32_t dataBytes = frames * channels * (bits / 8);
            auto put32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
            auto put16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
            fwrite("RIFF", 1, 4, f); put32(36 + dataBytes); fwrite("WAVE", 1, 4, f);
            fwrite("fmt ", 1, 4, f); put32(16); put16(1); put16(channels); put32(rate);
            put32(rate * channels * (bits / 8)); put16(channels * (bits / 8)); put16(bits);
            fwrite("data", 1, 4, f); put32(dataBytes);
            for (auto& s : pcm) {
                const int16_t l = int16_t(std::max(-1.0f, std::min(1.0f, s.l)) * 32767.0f);
                const int16_t r = int16_t(std::max(-1.0f, std::min(1.0f, s.r)) * 32767.0f);
                fwrite(&l, 2, 1, f); fwrite(&r, 2, 1, f);
            }
            fclose(f);
            return "{\"status\":\"ok\",\"frames\":" + std::to_string(frames) + ",\"rate\":" + std::to_string(rate) + "}";
        }
        if (cmd != "sink_status") { return ""; }
        std::string out = "{\"sinks\":[";
        bool first = true;
        for (auto* sink : AudioSink::instances) {
            if (!first) { out += ","; }
            first = false;
            out += "{\"stream\":\"" + sink->_streamName + "\"";
            out += ",\"running\":" + std::string(sink->running ? "true" : "false");
            out += ",\"device_period_ms\":" + std::to_string(DEVICE_BUFFER_MS);
            out += ",\"period_frames\":" + std::to_string(sink->periodFrames);
            out += ",\"ring_frames\":" + std::to_string(sink->outputBuffer.available());
            out += ",\"ring_capacity\":" + std::to_string(sink->outputBuffer.capacity());
            out += ",\"jitter_ms\":" + std::to_string(JITTER_BUFFER_MS);
            out += ",\"audio_rate\":" + std::to_string(sink->sampleRate) + "}";
        }
        out += "],\"underflow_callbacks\":" + std::to_string(AudioSink::underflowCallbacks.load());
        out += ",\"callbacks\":" + std::to_string(AudioSink::callbackCount);
        out += ",\"consumed_frames\":" + std::to_string(AudioSink::consumedFrames);
        out += ",\"written_frames\":" + std::to_string(AudioSink::writtenFrames);
        out += ",\"zero_filled_frames\":" + std::to_string(AudioSink::zeroFilledFrames);
        out += ",\"write_blocks\":" + std::to_string(AudioSink::writeBlocks);
        out += ",\"callback_frames_min\":" + std::to_string(AudioSink::minCallbackFrames);
        out += ",\"callback_frames_max\":" + std::to_string(AudioSink::maxCallbackFrames);
        out += ",\"write_block_max\":" + std::to_string(AudioSink::maxWriteFrames);
        out += "}";
        return out;
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
