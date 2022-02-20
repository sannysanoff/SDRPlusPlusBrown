#pragma once
#include <map>
#include <string>
#include <dsp/stream.h>
#include <dsp/types.h>
#include <dsp/routing.h>
#include <dsp/processing.h>
#include <dsp/sink.h>
#include <mutex>
#include <utils/event.h>
#include <vector>

class SinkManager {

public:
    SinkManager();
    static const char * secondarySuffixSeparator;

    static std::string secondaryStreamSuffix(int index) {
        if (index == 0) return "";
        std::string x;
        x.append(secondarySuffixSeparator);
        x.append(std::to_string(index+1));
        return x;
    }

    static bool isSecondaryStream(const std::string &name) {
        return name.find(secondarySuffixSeparator) != std::string::npos;
    }

//    static int getSecondaryStreamIndex(const std::string &name) {
//        auto pos = name.find(secondarySuffixSeparator);
//        if (pos != name.npos) {
//            auto number = name.substr(pos + strlen(secondarySuffixSeparator));
//            return atoi(number.c_str());
//        } else {
//            return 0;
//        }
//    }
//
    class Sink {
    public:
        virtual ~Sink() {}
        virtual void start() = 0;
        virtual void stop() = 0;
        virtual void menuHandler() = 0;
    };

    class Stream {
    public:
        Stream() {}
//        Stream(dsp::stream<dsp::stereo_t>* in, EventHandler<float>* srChangeHandler, float sampleRate);

        void init(EventHandler<float>* srChangeHandler, float sampleRate);

        void start();
        void stop();

        void setVolume(float volume);
        float getVolume();

        void setSampleRate(float sampleRate);
        float getSampleRate();

        dsp::stream<dsp::stereo_t>*  getInput() {
            return &_in;
        }

        dsp::stream<dsp::stereo_t>* bindStream();
        void unbindStream(dsp::stream<dsp::stereo_t>* stream);

        friend SinkManager;
        friend SinkManager::Sink;

        dsp::stream<dsp::stereo_t>* sinkOut;

        Event<float> srChange;

    private:
        dsp::stream<dsp::stereo_t> _in;
        dsp::Splitter<dsp::stereo_t> splitter;
        SinkManager::Sink* sink;
        dsp::stream<dsp::stereo_t> volumeInput;
        dsp::Volume<dsp::stereo_t> volumeAjust;
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
        dsp::NullSink<dsp::stereo_t> ns;
    };

    void registerSinkProvider(std::string name, SinkProvider provider);
    void unregisterSinkProvider(std::string name);

    void registerStream(std::string name, Stream* stream);
    void unregisterStream(std::string name);

    void startStream(std::string name);
    void stopStream(std::string name);

    float getStreamSampleRate(std::string name);

    void setStreamSink(std::string name, std::string providerName);

    void showVolumeSlider(std::string name, std::string prefix, float width, float btnHeight = -1.0f, int btwBorder = 0, bool sameLine = false);

    dsp::stream<dsp::stereo_t>* bindStream(std::string name);
    void unbindStream(std::string name, dsp::stream<dsp::stereo_t>* stream);

    void loadSinksFromConfig();
    void showMenu();

    std::vector<std::string> getStreamNames();
    bool configContains(const std::string &name) const;

    Event<std::string> onSinkProviderRegistered;
    Event<std::string> onSinkProviderUnregister;
    Event<std::string> onSinkProviderUnregistered;

    Event<std::string> onStreamRegistered;
    Event<std::string> onStreamUnregister;
    Event<std::string> onStreamUnregistered;

    Event<std::string> onAddSubstream;
    Event<std::string> onRemoveSubstream;

private:
    void loadStreamConfig(std::string name);
    void saveStreamConfig(std::string name);
    void refreshProviders();

    std::map<std::string, SinkProvider> providers;
    std::map<std::string, Stream*> streams;
    std::vector<std::string> providerNames;
    std::string providerNamesTxt;
    std::vector<std::string> streamNames;

};