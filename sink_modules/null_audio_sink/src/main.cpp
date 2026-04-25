#include <imgui.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <signal_path/sink.h>
#include <dsp/buffer/packer.h>
#include <dsp/sink.h>
#include <utils/flog.h>
#include <config.h>
#include <core.h>
#include <http_debug_server.h>
#include <atomic>
#include <thread>
#include <chrono>

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "null_audio_sink",
    /* Description:     */ "Null audio sink with sample counter for testing",
    /* Author:          */ "Sanny;Hermes",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

ConfigManager config;

class NullAudioSink : public SinkManager::Sink {
public:
    std::atomic<int64_t> totalSamples{0};
    std::atomic<float> currentSampleRate{48000.0f};
    std::string streamName;

    NullAudioSink(SinkManager::Stream* stream, std::string streamName) {
        _stream = stream;
        this->streamName = streamName;
        ns.init(stream->sinkOut);
        
        flog::info("NullAudioSink created for stream: {}", streamName);
    }

    ~NullAudioSink() {
        stop();
    }

    void start() {
        if (running) { return; }
        currentSampleRate = _stream->getSampleRate();
        totalSamples = 0;
        ns.start();
        running = true;
        flog::info("NullAudioSink started at {} Hz", (float)currentSampleRate);
    }

    void stop() {
        if (!running) { return; }
        ns.stop();
        running = false;
        flog::info("NullAudioSink stopped. Total samples: {}", (int64_t)totalSamples);
    }

    void menuHandler() {
        ImGui::Text("Null Audio Sink (Test)");
        ImGui::Text("Sample rate: %.0f Hz", (float)currentSampleRate.load());
        ImGui::Text("Total samples: %lld", (long long)totalSamples.load());
        if (running) {
            ImGui::Text("Status: RUNNING");
        } else {
            ImGui::Text("Status: STOPPED");
        }
    }

    SinkManager::Stream* _stream;

private:
    // Custom null sink DSP block that counts samples
    class CountingNull : public dsp::Sink<dsp::stereo_t> {
    public:
        std::atomic<int64_t>* counter;
        
        CountingNull(std::atomic<int64_t>* counter) : counter(counter) {}
        
        int run() override {
            int count = _in->read();
            if (count < 0) { return -1; }
            if (count > 0) {
                *counter += count;
            }
            _in->flush();
            return count;
        }
    };

    CountingNull ns{&totalSamples};
    bool running = false;
};

class NullAudioSinkModule : public ModuleManager::Instance {
public:
    NullAudioSinkModule(std::string name) {
        this->name = name;
        provider.create = create_sink;
        provider.ctx = this;

        sigpath::sinkManager.registerSinkProvider("NullAudioSink", provider);
        
        // Register procfs endpoints for control and monitoring
        httpdebug::procfs::registerEndpoint("/null_audio_sink/samples", 
            [this]() -> std::string {
                if (sink) {
                    return std::to_string((long long)sink->totalSamples.load());
                }
                return "0";
            }, 
            nullptr, 
            httpdebug::procfs::Type::Int);
        
        httpdebug::procfs::registerEndpoint("/null_audio_sink/sample_rate",
            [this]() -> std::string {
                if (sink) {
                    return std::to_string((double)sink->currentSampleRate.load());
                }
                return "48000";
            },
            nullptr,
            httpdebug::procfs::Type::Float);
        
        httpdebug::procfs::registerEndpoint("/null_audio_sink/status",
            [this]() -> std::string {
                if (sink) {
                    return sink->totalSamples.load() > 0 ? "active" : "idle";
                }
                return "uninitialized";
            },
            nullptr,
            httpdebug::procfs::Type::String);
        
        httpdebug::procfs::registerEndpoint("/null_audio_sink/select",
            [this]() -> std::string {
                // GET returns current stream info
                return "{\"stream\":\"" + selectedStream + "\", \"selected\":" + (selectedStream.empty() ? "false" : "true") + "}";
            },
            [this](const std::string& val) {
                // POST: set sink for the specified stream
                std::string streamName = val;
                if (streamName.empty()) {
                    // Default: use first available stream
                    auto names = sigpath::sinkManager.getStreamNames();
                    if (!names.empty()) {
                        streamName = names[0];
                    }
                }
                if (!streamName.empty()) {
                    flog::info("NullAudioSink: selecting for stream '{}'", streamName);
                    sigpath::sinkManager.setStreamSink(streamName, "NullAudioSink");
                    selectedStream = streamName;
                }
            },
            httpdebug::procfs::Type::String);

        flog::info("NullAudioSinkModule created");
    }

    ~NullAudioSinkModule() {
        httpdebug::procfs::unregister("/null_audio_sink/samples");
        httpdebug::procfs::unregister("/null_audio_sink/sample_rate");
        httpdebug::procfs::unregister("/null_audio_sink/status");
        httpdebug::procfs::unregister("/null_audio_sink/select");
        sigpath::sinkManager.unregisterSinkProvider("NullAudioSink");
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
        auto module = (NullAudioSinkModule*)ctx;
        auto sink = new NullAudioSink(stream, streamName);
        module->sink = sink;
        module->selectedStream = streamName;
        return sink;
    }

    std::string name;
    std::string selectedStream;
    bool enabled = true;
    SinkManager::SinkProvider provider;
    NullAudioSink* sink = nullptr;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    config.setPath(core::args["root"].s() + "/null_audio_sink_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT void* _CREATE_INSTANCE_(std::string name) {
    NullAudioSinkModule* instance = new NullAudioSinkModule(name);
    return instance;
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (NullAudioSinkModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
