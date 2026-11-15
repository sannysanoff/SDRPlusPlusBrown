#include <imgui.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <signal_path/sink.h>
#include <portaudio.h>
#include <dsp/convert/stereo_to_mono.h>
#include <dsp/sink/ring_buffer.h>
#include <utils/flog.h>
#include <config.h>
#include <core.h>

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "audio_sink",
    /* Description:     */ "Audio sink module for SDR++",
    /* Author:          */ "Ryzerth",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

ConfigManager config;

class AudioSink : SinkManager::Sink {
public:
    struct AudioDevice_t {
        std::string name;
        int index;
        int channels;
        int srId;
        int defaultSrId;
        std::vector<double> sampleRates;
        std::string txtSampleRates;
    };

    AudioSink(SinkManager::Stream* stream, std::string streamName) {
        _stream = stream;
        _streamName = streamName;
        s2m.init(_stream->sinkOut);
        monoRB.init(&s2m.out, 480);
        stereoRB.init(_stream->sinkOut, 480);

        // monoPacker.init(&s2m.out, 240);
        // stereoPacker.init(_stream->sinkOut, 240);

        bool created = false;
        std::string savedDev = "";
        config.acquire();
        if (!config.conf.contains(_streamName) || !config.conf[_streamName].is_object()) {
            created = true;
            config.conf[_streamName]["device"] = "";
            config.conf[_streamName]["devices"] = json({});
        }
        if (!config.conf[_streamName].contains("device") || !config.conf[_streamName]["device"].is_string()) {
            created = true;
            config.conf[_streamName]["device"] = "";
        }
        if (!config.conf[_streamName].contains("devices") || !config.conf[_streamName]["devices"].is_object()) {
            created = true;
            config.conf[_streamName]["devices"] = json({});
        }
        savedDev = config.conf[_streamName]["device"];
        config.release(created);

        // Initialize PortAudio
        devCount = Pa_GetDeviceCount();
        devId = Pa_GetDefaultOutputDevice();
        const PaDeviceInfo* deviceInfo;
        PaStreamParameters outputParams;
        outputParams.sampleFormat = paFloat32;
        outputParams.hostApiSpecificStreamInfo = NULL;

        // Gather hardware info
        for (int i = 0; i < devCount; i++) {
            deviceInfo = Pa_GetDeviceInfo(i);
            if (!deviceInfo || deviceInfo->maxOutputChannels < 1) {
                continue;
            }
            AudioDevice_t dev;
            dev.name = deviceInfo->name;
            dev.index = i;
            dev.channels = std::min<int>(deviceInfo->maxOutputChannels, 2);
            dev.defaultSrId = 0;
            dev.sampleRates.clear();
            dev.txtSampleRates = "";
            for (int j = 0; j < 6; j++) {
                outputParams.channelCount = dev.channels;
                outputParams.device = dev.index;
                outputParams.suggestedLatency = deviceInfo->defaultLowOutputLatency;
                PaError err = Pa_IsFormatSupported(NULL, &outputParams, POSSIBLE_SAMP_RATE[j]);
                if (err != paFormatIsSupported) {
                    continue;
                }
                if (POSSIBLE_SAMP_RATE[j] == deviceInfo->defaultSampleRate) {
                    dev.defaultSrId = (int)dev.sampleRates.size();
                }
                dev.sampleRates.push_back(POSSIBLE_SAMP_RATE[j]);
                dev.txtSampleRates += std::to_string((int)POSSIBLE_SAMP_RATE[j]);
                dev.txtSampleRates += '\0';
            }
            if (dev.sampleRates.size() == 0) {
                continue;
            }
            if (i == devId) {
                devListId = devices.size();
                defaultDev = devListId;
            }
            dev.srId = dev.defaultSrId;

            AudioDevice_t* _dev = new AudioDevice_t;
            *_dev = dev;
            devices.push_back(_dev);

            deviceNames.push_back(deviceInfo->name);
            txtDevList += deviceInfo->name;
            txtDevList += '\0';
        }

        // Load config from file
        selectByName(savedDev);
    }

    ~AudioSink() {
        stop();
        for (auto const& dev : devices) {
            delete dev;
        }
    }

    void start() {
        if (running || devices.empty()) {
            return;
        }
        running = doStart();
    }

    void stop() {
        if (!running) {
            return;
        }
        doStop();
        running = false;
    }

    void selectById(int id) {
        if (id < 0 || id >= (int)devices.size()) {
            return;
        }
        devListId = id;
        AudioDevice_t* dev = devices[devListId];

        bool created = false;
        config.acquire();
        if (!config.conf[_streamName]["devices"].contains(dev->name)) {
            created = true;
            config.conf[_streamName]["devices"][dev->name] = dev->sampleRates[dev->defaultSrId];
        }
        double savedSr = config.conf[_streamName]["devices"][dev->name];
        config.release(created);

        int matched = dev->defaultSrId;
        for (int i = 0; i < (int)dev->sampleRates.size(); i++) {
            if (dev->sampleRates[i] == savedSr) {
                matched = i;
                break;
            }
        }
        dev->srId = matched;
        _stream->setSampleRate(dev->sampleRates[dev->srId]);
    }

    void selectByName(const std::string& name) {
        if (!name.empty()) {
            for (int i = 0; i < (int)devices.size(); i++) {
                if (devices[i]->name == name) {
                    selectById(i);
                    return;
                }
            }
        }
        selectById(defaultDev);
    }

    void menuHandler() {
        if (devices.empty()) {
            ImGui::TextUnformatted("No audio devices");
            return;
        }
        float menuWidth = ImGui::GetContentRegionAvail().x;

        ImGui::SetNextItemWidth(menuWidth);
        if (ImGui::Combo(("##_audio_sink_dev_" + _streamName).c_str(), &devListId, txtDevList.c_str())) {
            selectById(devListId);
            if (running) {
                doStop();
                running = doStart();
            }
            config.acquire();
            config.conf[_streamName]["device"] = devices[devListId]->name;
            config.release(true);
        }

        AudioDevice_t* dev = devices[devListId];

        ImGui::SetNextItemWidth(menuWidth);
        if (ImGui::Combo(("##_audio_sink_sr_" + _streamName).c_str(), &dev->srId, dev->txtSampleRates.c_str())) {
            _stream->setSampleRate(dev->sampleRates[dev->srId]);
            if (running) {
                doStop();
                running = doStart();
            }
            config.acquire();
            config.conf[_streamName]["devices"][dev->name] = dev->sampleRates[dev->srId];
            config.release(true);
        }
    }

private:
    bool doStart() {
        const PaDeviceInfo* deviceInfo;
        AudioDevice_t* dev = devices[devListId];
        PaStreamParameters outputParams;
        deviceInfo = Pa_GetDeviceInfo(dev->index);
        outputParams.channelCount = dev->channels;
        outputParams.sampleFormat = paFloat32;
        outputParams.hostApiSpecificStreamInfo = NULL;
        outputParams.device = dev->index;
        outputParams.suggestedLatency = deviceInfo->defaultLowOutputLatency;
        PaError err;

        float sampleRate = dev->sampleRates[dev->srId];
        int bufferSize = sampleRate / 60.0f;

        if (dev->channels == 2) {
            stereoRB.data.setMaxLatency(bufferSize * 2);
            stereoRB.start();
            // stereoPacker.setSampleCount(bufferSize);
            // stereoPacker.start();
            err = Pa_OpenStream(&stream, NULL, &outputParams, sampleRate, paFramesPerBufferUnspecified, 0, _stereo_cb, this);
            //err = Pa_OpenStream(&stream, NULL, &outputParams, sampleRate, bufferSize, 0, _stereo_cb, this);
        }
        else {
            s2m.start();
            monoRB.data.setMaxLatency(bufferSize * 2);
            monoRB.start();
            // stereoPacker.setSampleCount(bufferSize);
            // monoPacker.start();
            err = Pa_OpenStream(&stream, NULL, &outputParams, sampleRate, paFramesPerBufferUnspecified, 0, _mono_cb, this);
            //err = Pa_OpenStream(&stream, NULL, &outputParams, sampleRate, bufferSize, 0, _mono_cb, this);
        }

        if (err != 0) {
            flog::error("Error while opening audio stream: ({0}) => {1}", err, Pa_GetErrorText(err));
            stream = NULL;
            doStop();
            return false;
        }

        err = Pa_StartStream(stream);
        if (err != 0) {
            flog::error("Error while starting audio stream: ({0}) => {1}", err, Pa_GetErrorText(err));
            Pa_CloseStream(stream);
            stream = NULL;
            doStop();
            return false;
        }
        flog::info("Audio device open.");
        return true;
    }

    void doStop() {
        monoRB.data.stopReader();
        stereoRB.data.stopReader();
        // monoPacker.out.stopReader();
        // stereoPacker.out.stopReader();
        if (stream) {
            Pa_AbortStream(stream);
            Pa_CloseStream(stream);
            stream = NULL;
        }
        s2m.stop();
        monoRB.stop();
        stereoRB.stop();
        // monoPacker.stop();
        // stereoPacker.stop();
        monoRB.data.clearReadStop();
        stereoRB.data.clearReadStop();
        // monoPacker.out.clearReadStop();
        // stereoPacker.out.clearWriteStop();
    }

    template <typename SampleT>
    static int nonBlockingCallback(dsp::sink::RingBuffer<SampleT>& rb, void* output, unsigned long frameCount) {
        memset(output, 0, frameCount * sizeof(SampleT));
        if (!gui::mainWindow.isPlaying()) {
            return 0;
        }
        int avail = rb.data.getReadable();
        int toRead = (std::min<int>)(avail, frameCount);
        if (toRead > 0 && rb.data.read((SampleT*)output, toRead) < 0) {
            memset(output, 0, frameCount * sizeof(SampleT));
        }
        return 0;
    }

    static int _mono_cb(const void* input, void* output, unsigned long frameCount,
                        const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags, void* userData) {
        return nonBlockingCallback(((AudioSink*)userData)->monoRB, output, frameCount);
    }

    static int _stereo_cb(const void* input, void* output, unsigned long frameCount,
                          const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags, void* userData) {
        return nonBlockingCallback(((AudioSink*)userData)->stereoRB, output, frameCount);
    }

    // static int _mono_cb(const void *input, void *output, unsigned long frameCount,
    //     const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags, void *userData) {
    //     AudioSink* _this = (AudioSink*)userData;
    //     if (_this->monoPacker.out.read() < 0) { return 0; }
    //     memcpy((float*)output, _this->monoPacker.out.readBuf, frameCount * sizeof(float));
    //     _this->monoPacker.out.flush();
    //     return 0;
    // }

    // static int _stereo_cb(const void *input, void *output, unsigned long frameCount,
    //     const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags, void *userData) {
    //     AudioSink* _this = (AudioSink*)userData;
    //     if (_this->stereoPacker.out.read() < 0) { flog::warn("CB killed"); return 0; }
    //     memcpy((dsp::stereo_t*)output, _this->stereoPacker.out.readBuf, frameCount * sizeof(dsp::stereo_t));
    //     _this->stereoPacker.out.flush();
    //     return 0;
    // }


    SinkManager::Stream* _stream;
    dsp::convert::StereoToMono s2m;
    dsp::sink::RingBuffer<float> monoRB;
    dsp::sink::RingBuffer<dsp::stereo_t> stereoRB;

    // dsp::Packer<float> monoPacker;
    // dsp::Packer<dsp::stereo_t> stereoPacker;

    std::string _streamName;
    PaStream* stream = NULL;

    int srId = 0;
    int devCount;
    int devId = 0;
    int devListId = 0;
    int defaultDev = 0;
    bool running = false;

    const double POSSIBLE_SAMP_RATE[6] = {
        48000.0f,
        44100.0f,
        24000.0f,
        22050.0f,
        12000.0f,
        11025.0f
    };

    std::vector<AudioDevice_t*> devices;
    std::vector<std::string> deviceNames;
    std::string txtDevList;
};

class AudioSinkModule : public ModuleManager::Instance {
public:
    AudioSinkModule(std::string name) {
        this->name = name;
        provider.create = create_sink;
        provider.ctx = this;

        Pa_Initialize();

        sigpath::sinkManager.registerSinkProvider("Audio", provider);
    }

    ~AudioSinkModule() {
        sigpath::sinkManager.unregisterSinkProvider("Audio");
        Pa_Terminate();
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
    config.setPath(std::string(core::getRoot()) + "/audio_sink_config.json");
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