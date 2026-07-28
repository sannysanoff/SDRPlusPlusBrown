#pragma once
#include <map>
#include <string>
#include <dsp/stream.h>
#include <dsp/types.h>
#include "../dsp/routing/splitter.h"
#include "../dsp/routing/merger.h"
#include "../dsp/audio/volume.h"
#include "../dsp/sink/null_sink.h"
#include <mutex>
#include <utils/event.h>
#include <vector>



class SinkManager {

    static constexpr auto secondarySuffixSeparator = "__##";

public:
    SinkManager();

    class Sink {
    public:
        virtual ~Sink() {}
        virtual void start() = 0;
        virtual void stop() = 0;
        virtual void menuHandler() = 0;
    };

    class Stream {
    public:
        Stream() {
            _in = &_in0;
            _in0.origin = "stream._in0";
            volumeInput.origin = "stream.volumeInput";
            splitter.origin = "SinkManager.Stream.splitter";
        }
        Stream(dsp::stream<dsp::stereo_t>* in, EventHandler<float>* srChangeHandler, float sampleRate);

        void init(EventHandler<float>* srChangeHandler, float sampleRate);

        void start();
        void stop();

        void setVolume(float volume);
        float getVolume();

        void setSampleRate(float sampleRate);
        float getSampleRate();

        void setInput(dsp::stream<dsp::stereo_t>* in);

        dsp::stream<dsp::stereo_t>* getInput() {
            return _in;
        }

        dsp::stream<dsp::stereo_t>* bindStream();
        void unbindStream(dsp::stream<dsp::stereo_t>* stream);

        dsp::routing::Merger<dsp::stereo_t> *getMerger() {
            return &merger;
        }

        friend SinkManager;
        friend SinkManager::Sink;

        dsp::stream<dsp::stereo_t>* sinkOut;

        Event<float> srChange;
        dsp::audio::Volume volumeAjust;

    private:
        dsp::stream<dsp::stereo_t> _in0;

        dsp::stream<dsp::stereo_t> *_in = nullptr;

        dsp::routing::Splitter<dsp::stereo_t> splitter;     // multiple outputs
        dsp::routing::Merger<dsp::stereo_t> merger;         // multiple inputs
        SinkManager::Sink* sink;
        dsp::stream<dsp::stereo_t> volumeInput;
        std::mutex ctrlMtx;
        float _sampleRate;
        int providerId = 0;
        std::string providerName = "";
        bool running = false;


        float guiVolume = 1.0f;

    };

    struct SinkProvider {
        SinkManager::Sink* (*create)(SinkManager::Stream* stream, std::string streamName, void* ctx);
        void* ctx;
    };

    class NullSink : SinkManager::Sink {
    public:
        NullSink(SinkManager::Stream* stream) {
            ns.init(stream->sinkOut);
        }
        void start() { ns.start(); }
        void stop() { ns.stop(); }
        void menuHandler() {}

        static SinkManager::Sink* create(SinkManager::Stream* stream, std::string streamName, void* ctx) {
            stream->setSampleRate(48000);
            return new SinkManager::NullSink(stream);
        }

    private:
        dsp::sink::Null<dsp::stereo_t> ns;
    };


    static std::string makeSecondaryStreamName(const std::string &name, int index) {
        std::string x = name;
        if (index == 0) return x;
        x.append(secondarySuffixSeparator);
        x.append(std::to_string(index));
        return x;
    }

    static bool isSecondaryStream(const std::string& name) {
        return name.find(secondarySuffixSeparator) != std::string::npos;
    }

    static std::pair<std::string, int> getSecondaryStreamIndex(const std::string& name) {
        auto pos = name.find(secondarySuffixSeparator);
        if (pos != std::string::npos) {
            return std::make_pair(name.substr(0, pos), std::atoi(name.substr(pos+ strlen(secondarySuffixSeparator)).c_str()));
        }
        return std::make_pair(name, 0);
    }

    void registerSinkProvider(std::string name, SinkProvider provider);
    void unregisterSinkProvider(std::string name);

    /** Get the list of registered sink provider names */
    std::vector<std::string> getSinkProviderNames() { return providerNames; }

    /** Get the name of the sink assigned to a specific stream */
    std::string getStreamSinkName(std::string name) {
        if (streams.find(name) == streams.end()) return "";
        return streams[name]->providerName;
    }

    /** Check if a stream is running */
    bool isStreamRunning(std::string name) {
        if (streams.find(name) == streams.end()) return false;
        return streams[name]->running;
    }

    void registerStream(std::string name, Stream* stream);
    void unregisterStream(std::string name);

    void startStream(std::string name);
    void stopStream(std::string name);

    float getStreamSampleRate(std::string name);

    void setStreamSink(std::string name, std::string providerName);

    void showVolumeSlider(std::string name, std::string prefix, float width, float btnHeight = -1.0f, int btnBorder = 0, bool sameLine = false);

    dsp::stream<dsp::stereo_t>* bindStream(std::string name);
    void unbindStream(std::string name, dsp::stream<dsp::stereo_t>* stream);

    void loadSinksFromConfig();
    bool configContains(const std::string& name) const;
    void showMenu();

    std::vector<std::string> getStreamNames();

    Event<std::string> onSinkProviderRegistered;
    Event<std::string> onSinkProviderUnregister;
    Event<std::string> onSinkProviderUnregistered;

    Event<std::string> onStreamRegistered;
    Event<std::string> onStreamUnregister;
    Event<std::string> onStreamUnregistered;

    Event<std::string> onAddSubstream;
    Event<std::string> onRemoveSubstream;

    dsp::routing::Splitter<dsp::stereo_t> defaultInputAudio;

    // 0 = silence
    // 1 = cw tone
    // 2 = click sound
    std::atomic<int> toneGenerator;
    Stream * recentStreeam = nullptr;

    struct StreamHook {
        enum SourceType {
            SOURCE_DEMOD_OUTPUT,
            SOURCE_RAW_RECEIVED_DATA,
            SOURCE_FEEDBACK_GENERATOR,
            SOURCE_MICROPHONE_OR_DIGI,
        } sourceType;
        std::string source;
        int priority;   // for merging
        int sampleRate;
        std::shared_ptr<std::vector<dsp::stereo_t>> stereoData;
        std::shared_ptr<std::vector<dsp::complex_t>> iqData;

        StreamHook(const std::string &source,
                   SourceType sourceType,
                   int priority,
                   int sampleRate,
                   const std::shared_ptr<std::vector<dsp::stereo_t>> &stereoData,
                   const std::shared_ptr<std::vector<dsp::complex_t>> &iqData) :
                                                                                 source(source),
                                                                                 sourceType(sourceType),
                                                                                 priority(priority),
                                                                                 stereoData(stereoData),
                                                                                 sampleRate(sampleRate),
                                                                                 iqData(iqData) {}

    };

    Event<std::shared_ptr<StreamHook>> onStream;
    void setAllMuted(bool muted);
    dsp::routing::Merger<dsp::stereo_t>* getMerger(std::string name);

    std::map<std::string, Stream*> streams;

private:
    void loadStreamConfig(std::string name);
    void saveStreamConfig(std::string name);
    void refreshProviders();

    std::map<std::string, SinkProvider> providers;
    std::vector<std::string> providerNames;
    std::string providerNamesTxt;
    std::vector<std::string> streamNames;

    EventHandler<bool> txHandler;
};