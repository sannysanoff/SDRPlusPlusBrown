#include <imgui.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <signal_path/sink.h>
#include <dsp/buffer/packer.h>
#include <utils/flog.h>
#include <config.h>
#include <utils/optionlist.h>
#include <aaudio/AAudio.h>
#include <core.h>
#include <android/log.h>
#include <jni.h>
#include <android_native_app_glue.h>
#include <android_backend.h>
#include <utils/strings.h>
#include <unistd.h>
#include <gui/smgui.h>

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "audio_sink",
    /* Description:     */ "Android audio sink module for SDR++",
    /* Author:          */ "Ryzerth",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

ConfigManager config;

namespace backend {
    struct android_app* app = NULL;
}

enum AudioDeviceType {
    TYPE_UNKNOWN = 0,
    TYPE_BUILTIN_EARPIECE = 1,
    TYPE_BUILTIN_SPEAKER = 2,
    TYPE_WIRED_HEADSET = 3,
    TYPE_WIRED_HEADPHONES = 4,
    TYPE_LINE_ANALOG = 5,
    TYPE_LINE_DIGITAL = 6,
    TYPE_BLUETOOTH_SCO = 7,
    TYPE_BLUETOOTH_A2DP = 8,
    TYPE_HDMI = 9,
    TYPE_HDMI_ARC = 10,
    TYPE_USB_DEVICE = 11,
    TYPE_USB_ACCESSORY = 12,
    TYPE_DOCK = 13,
    TYPE_FM = 14,
    TYPE_BUILTIN_MIC = 15,
    TYPE_FM_TUNER = 16,
    TYPE_TV_TUNER = 17,
    TYPE_TELEPHONY = 18,
    TYPE_AUX_LINE = 19,
    TYPE_IP = 20,
    TYPE_BUS = 21,
    TYPE_USB_HEADSET = 22,
    TYPE_HEARING_AID = 23,
    TYPE_BUILTIN_SPEAKER_SAFE = 24,
    TYPE_REMOTE_SUBMIX = 25,
    TYPE_BLE_HEADSET = 26,
    TYPE_BLE_SPEAKER = 27,
    TYPE_HDMI_EARC = 29,
    TYPE_BLE_BROADCAST = 30
};

std::unordered_map<int, std::string> audioDeviceTypes = {
        {TYPE_AUX_LINE, "Aux Line"},
        {TYPE_BLE_BROADCAST, "BLE Broadcast"},
        {TYPE_BLE_HEADSET, "BLE Headset"},
        {TYPE_BLE_SPEAKER, "BLE Speaker"},
        {TYPE_BLUETOOTH_A2DP, "BT A2DP"},
        {TYPE_BLUETOOTH_SCO, "BT SCO"},
        {TYPE_BUILTIN_EARPIECE, "Built-in Earpc"},
        {TYPE_BUILTIN_MIC, "Built-in Mic"},
        {TYPE_BUILTIN_SPEAKER, "Built-in Spkr"},
        {TYPE_BUILTIN_SPEAKER_SAFE, "Safe Spkr"},
        {TYPE_BUS, "Audio Bus"},
        {TYPE_DOCK, "Dock Output"},
        {TYPE_FM, "FM Radio"},
        {TYPE_FM_TUNER, "FM Tuner"},
        {TYPE_HDMI, "HDMI Output"},
        {TYPE_HDMI_ARC, "HDMI ARC"},
        {TYPE_HDMI_EARC, "HDMI eARC"},
        {TYPE_HEARING_AID, "Hearing Aid"},
        {TYPE_IP, "IP Audio"},
        {TYPE_LINE_ANALOG, "Analog Line"},
        {TYPE_LINE_DIGITAL, "Digital Line"},
        {TYPE_REMOTE_SUBMIX, "Remote Submix"},
        {TYPE_TELEPHONY, "Telephony"},
        {TYPE_TV_TUNER, "TV Tuner"},
        {TYPE_UNKNOWN, "Unknown"},
        {TYPE_USB_ACCESSORY, "USB Accessory"},
        {TYPE_USB_DEVICE, "USB Device"},
        {TYPE_USB_HEADSET, "USB Headset"},
        {TYPE_WIRED_HEADPHONES, "Wired Headph"},
        {TYPE_WIRED_HEADSET, "Wired Headset"}
};

struct MyAudioDevice {
    int enumId;
    std::string typeString;
    std::string productString;

    std::string getPair() {
        return typeString +" - "+productString;
    }
};

typedef std::vector<MyAudioDevice> AudioDeviceList;

AudioDeviceList parseDevices(const std::string &array3) {
    // tab-separated 3-element sequential concatenated array
    AudioDeviceList rv;
    rv.emplace_back(MyAudioDevice{-1, "Default", "default"});
    std::vector<std::string> split;
    splitStringV(array3, "\t", split);
    if (split.size() < 3) {
        return rv;
    }
    for(int i=0; i<split.size()/3; i++) {
        int enumId = atoi(split[i*3 + 0].c_str());
        int typeId = atoi(split[i*3 + 1].c_str());
        std::string typeStr = "??";
        if (auto it = audioDeviceTypes.find(typeId); it != audioDeviceTypes.end()) {
            typeStr = it->second;
        }
        rv.emplace_back(MyAudioDevice{enumId, typeStr, split[i*3 + 2]});
    }
    return rv;
}

int findDeviceByPair(AudioDeviceList  &lst, const std::string &pair) {
    int ix = 0;
    for(auto &v : lst) {
        if (v.getPair() == pair) {
            return ix;
        }
        ix++;
    }
    return 0;
}

std::string generateComboboxFromDevices(AudioDeviceList &lst) {
    std::string rv;
    for(auto &v : lst) {
        rv += v.getPair();
        rv += '\0';
    }
    return rv;
}

class AudioSink : SinkManager::Sink {

    JNIEnv* java_env = NULL;
    int selectedSourceIndex = -1;
    int selectedSinkIndex = -1;

    AudioDeviceList sinks;
    AudioDeviceList sources;

    std::string sinkStatus = "not init";
    std::string sourceStatus = "not init";

    std::string sinksTxt;
    std::string sourcesTxt;

    long long scheduledStreamRestart = 0;

public:
    AudioSink(SinkManager::Stream* stream, std::string streamName) {
        _stream = stream;
        _streamName = streamName;

        packer.init(_stream->sinkOut, 512);

        refreshSystemAudioDevices();

        config.acquire();
        if (config.conf.contains("useRawInput")) {
            this->useRawInput = config.conf["useRawInput"];
        }
        if (config.conf.contains("sinkPair")) {
            std::string pair = config.conf["sinkPair"];
            selectedSinkIndex = findDeviceByPair(sinks, pair);
        }
        if (config.conf.contains("sourcePair")) {
            std::string pair = config.conf["sourcePair"];
            selectedSourceIndex = findDeviceByPair(sources, pair);
        }
        config.release(true);

        // TODO: Add choice? I don't think anyone cares on android...
        sampleRate = 48000;
        _stream->setSampleRate(sampleRate);
        if (selectedSourceIndex < 0) selectedSourceIndex = 0; // default
        if (selectedSinkIndex < 0) selectedSinkIndex = 0; // default
    }

    void refreshSystemAudioDevices() {

        backend::scanAudioDevices(); // will change the order inside array and whatever

        // save pairs in case of indices changed
        std::string sourcePairBefore = sources.empty() ? "": sources[selectedSourceIndex].getPair();
        std::string sinkPairBefore = sinks.empty() ? "": sinks[selectedSinkIndex].getPair();

        // string -> array[struct]
        sinks = parseDevices(backend::getAudioSinkIds());
        sources = parseDevices(backend::getAudioSourceIds());

        // data for UI
        sinksTxt = generateComboboxFromDevices(sinks);
        sourcesTxt = generateComboboxFromDevices(sources);

        // reset selection indices for now.
        selectedSourceIndex = 0;
        selectedSinkIndex = 0;

        // restore indices if possible.
        if (!sourcePairBefore.empty()) {
            selectedSourceIndex = findDeviceByPair(sources, sourcePairBefore);
        }
        if (!sinkPairBefore.empty()) {
            selectedSinkIndex = findDeviceByPair(sinks, sinkPairBefore);
        }

        if (sourcePairBefore != sources[selectedSourceIndex].getPair() || sinkPairBefore != sinks[selectedSinkIndex].getPair()) {
            restart(); // device removed, just to cleanup maybe.
        }

    }

    ~AudioSink() {
    }

    void start() {
        if (running) {
            return;
        }
        doStart();
        running = true;
    }

    void stop() {
        if (!running) {
            return;
        }
        doStop();
        running = false;
    }

    bool isSecondary() {
        return SinkManager::isSecondaryStream(_streamName);
    }

    void menuHandler() {
        // Draw menu here
        if (SmGui::Button("Refresh devices (new bluetooth etc)")) {
            refreshSystemAudioDevices();
        }
        SmGui::LeftLabel("Speaker:");
        SmGui::FillWidth();
        if (ImGui::Combo(("##_audio_sink_dev_" + _streamName).c_str(), &selectedSinkIndex, sinksTxt.c_str())) {
            restart();
//            stopPlaybackStream(false); // putting it before saving config, because possible (valid) crash.
//            restartPlaybackStream();
            config.acquire();
            config.conf["sinkPair"] = sinks[selectedSinkIndex].getPair();
            config.release(true);
        }
        if (sinkStatus.find("OK") == std::string::npos)
            SmGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0, 0, 1.0f));
        ImGui::Text("Speaker Status: %s", sinkStatus.c_str());
        if (sinkStatus.find("OK") == std::string::npos)
            SmGui::PopStyleColor();
        if (!isSecondary()) {
            SmGui::LeftLabel("Microphone:");
            SmGui::FillWidth();
            if (ImGui::Combo(("##_audio_source_dev_" + _streamName).c_str(), &selectedSourceIndex,
                             sourcesTxt.c_str())) {
                restart();
                config.acquire();
                config.conf["sourcePair"] = sources[selectedSourceIndex].getPair();
                config.release(true);
            }
            if (sourceStatus.find("OK") == std::string::npos)
                SmGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0, 0, 1.0f));
            ImGui::Text("MIC Status: %s", sourceStatus.c_str());
            if (sourceStatus.find("OK") == std::string::npos)
                SmGui::PopStyleColor();
            if (ImGui::Checkbox("Use Raw (no NR) Mic Input", &this->useRawInput)) {
                restart();
                config.acquire();
                config.conf["useRawInput"] = this->useRawInput;
                config.release(true);
            }
        }
    }

private:
    dsp::stream<dsp::stereo_t> microphone;

    bool isBtSco = false;

    void restartMicrophoneStream() {
        // Create stream builder
        AAudioStreamBuilder *builder;
        isBtSco = sources[selectedSourceIndex].typeString == audioDeviceTypes[TYPE_BLUETOOTH_SCO];
        flog::info("INFO restartMicrophoneStream: isBtSco={}", std::to_string(isBtSco));
        aaudio_result_t result = AAudio_createStreamBuilder(&builder);
        if (result == 0) {
            // Set stream options
            if (isBtSco) {
                backend::startBtSco();
            }
            if (false && isBtSco) {
                AAudioStreamBuilder_setInputPreset(builder,
                                                   AAUDIO_INPUT_PRESET_VOICE_COMMUNICATION);
            } else {
                if (this->useRawInput) {
                    AAudioStreamBuilder_setInputPreset(builder,
                                                       AAUDIO_INPUT_PRESET_VOICE_PERFORMANCE);
                } else {
                    AAudioStreamBuilder_setInputPreset(builder,
                                                       AAUDIO_INPUT_PRESET_VOICE_COMMUNICATION);
                }
            }
            auto deviceId = sources[selectedSourceIndex].enumId;
            if (deviceId != -1) {
                AAudioStreamBuilder_setDeviceId(builder, deviceId);
            }
            AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
            AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
            AAudioStreamBuilder_setSampleRate(builder, sampleRate);
            AAudioStreamBuilder_setChannelCount(builder, 2);
            AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
            AAudioStreamBuilder_setBufferCapacityInFrames(builder, bufferSize);
            AAudioStreamBuilder_setErrorCallback(builder, errorCallback, this);

            // Open the stream
            result = AAudioStreamBuilder_openStream(builder, &streamW);
            switch(result) {
                case AAUDIO_ERROR_INTERNAL:
                    sourceStatus = "not open";
                    break;
            }
            if (result == 0) {
                sourceStatus = "created, not started";

                // Stream stream and packer
                result = AAudioStream_requestStart(streamW);
                if (result == 0) {
                    sourceStatus = "OK (running)";
                } else {
                    AAudioStream_close(streamW);
                    streamW = nullptr;
                }
            }

            // We no longer need the builder
            AAudioStreamBuilder_delete(builder);
        }
    }

    void restartPlaybackStream() {
        // Create stream builder
        AAudioStreamBuilder *builder;
        aaudio_result_t result = AAudio_createStreamBuilder(&builder);

        auto deviceId = sinks[selectedSinkIndex].enumId;
        if (deviceId != -1) {
            AAudioStreamBuilder_setDeviceId(builder, deviceId);
        }
        // Set stream options
        AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
        AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
        AAudioStreamBuilder_setSampleRate(builder, sampleRate);
        AAudioStreamBuilder_setChannelCount(builder, 2);
        AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
        AAudioStreamBuilder_setBufferCapacityInFrames(builder, bufferSize);
        AAudioStreamBuilder_setErrorCallback(builder, errorCallback, this);
        packer.setSampleCount(bufferSize);

        // Open the stream
        result = AAudioStreamBuilder_openStream(builder, &stream);
        if (result == 0) {
            sinkStatus = "open, not started";
        }

        // Stream stream and packer
        result = AAudioStream_requestStart(stream);
        if (result == 0) {
            sinkStatus = "OK (running)";
            packer.start();
        } else {
            AAudioStream_close(stream);
            stream = nullptr;
        }

        // We no longer need the builder
        AAudioStreamBuilder_delete(builder);

    }

    void doStart() {
        bufferSize = round(sampleRate / 60.0);
        if (!isSecondary()) {
            sigpath::sinkManager.defaultInputAudio.setInput(&microphone);
            sigpath::sinkManager.defaultInputAudio.start();
            microphone.setBufferSize(sampleRate / 60);
            restartMicrophoneStream();
        }
        restartPlaybackStream();
        if (streamW) {
            workerThreadW = std::thread(&AudioSink::workerW, this);
        }
        if (stream) {
            workerThread = std::thread(&AudioSink::worker, this);
        }

    }

    void stopPlaybackStream(bool forever) {
        if (stream) {
            packer.stop();
            if (forever) {
                packer.out.stopReader();
            }
            scheduledStreamRestart = currentTimeMillis();
            AAudioStream_requestStop(stream);
            AAudioStream_close(stream);
            if (forever) {
                packer.out.clearReadStop();
            }
            stream = nullptr;
        }
    }

    void stopMicrophoneStream() {
        if (streamW) {
            scheduledStreamRestart = currentTimeMillis();
            streamWMutex.lock();
            AAudioStream_requestStop(streamW);
            streamWMutex.unlock();
            usleep(200000);
            streamWMutex.lock();
            AAudioStream_close(streamW);
            if (isBtSco) {
                backend::stopBtSco();
            }
            streamWMutex.unlock();
            usleep(200000);
            streamWMutex.lock();
            streamW = nullptr;
            streamWMutex.unlock();
        }

    }

    void doStop() {
        bool streamWExists = streamW != nullptr;
        bool streamExists = stream != nullptr;
        flog::info("doStop: streamWExists={}", streamWExists);

        stopPlaybackStream(true);
        stopMicrophoneStream();

        if (streamExists) {
            if (workerThread.joinable()) {
                workerThread.join();
                flog::info("android workerThread joined (playback)");
            }
        }
        if (streamWExists) {
            if (workerThreadW.joinable()) {
                workerThreadW.join();
                flog::info("android workerThreadW joined (microphone)");
            }
            sigpath::sinkManager.defaultInputAudio.stop();
            sigpath::sinkManager.defaultInputAudio.setInput(nullptr);
        }

    }

    void worker() {
        SetThreadName("Andr:audiowrite");
        float hz = 800;
        int period = (int)(sampleRate / hz);
        float tick = M_PI * 2 / period;
        while (true) {
            int count = packer.out.read();
            if (count < 0) { return; }

            static float lastPhase = 0;
            auto stereoOut = (dsp::stereo_t*)packer.out.readBuf;
            switch (sigpath::sinkManager.toneGenerator.load()) {
                case 0:
                    lastPhase = 0;
                    break;
                case 1:
                    for(int q=0; q<count; q++) {        // normally by 800
                        stereoOut[q].r = stereoOut[q].l = sin(lastPhase);
                        lastPhase += tick;
                    }
                    break;
            }

            if (stream) {
                AAudioStream_write(stream, packer.out.readBuf, count, 100000000); // 100 msec
            }
            packer.out.flush();
        }
    }

    void workerW() {
        SetThreadName("Andr:audioread");
        std::vector<dsp::stereo_t> samplesBuffer;
        samplesBuffer.resize(bufferSize);
        int nInBuffer = 0;
        long long lastReport = 0;
        long long totalRead = 0;
        while (true) {
            if (nInBuffer < bufferSize && streamW != nullptr) {
                streamWMutex.lock();
                int rd = 0;
                if (streamW) {
                    rd = AAudioStream_read(streamW, samplesBuffer.data() + nInBuffer,
                                           bufferSize - nInBuffer, 100000000);
                }
                streamWMutex.unlock();
                if (rd <= 0) {
                    break;
                }
                nInBuffer += rd;
                totalRead+= rd;
                auto ctm = currentTimeMillis();
                if (ctm > lastReport + 1800000) {
                    lastReport = ctm;
                    flog::info("(each 30 min) Got data from microphone: {}", std::to_string(totalRead));
                    totalRead = 0;
                }
            }
/*
            static int counter = 0;
            if (counter++ % 30 == 0) {
                auto ib = (float*)samplesBuffer.data();
                char buf[1000];
                sprintf(buf, "audio_main input %d: %f %f %f %f %f %f %f %f", nInBuffer, ib[0], ib[1], ib[2], ib[3], ib[4], ib[5], ib[6], ib[7]);
                logcat(buf);
            }
*/
            memmove(microphone.writeBuf, samplesBuffer.data(), nInBuffer * 2 * sizeof(float));
            if (!microphone.swap(bufferSize)) {
                break;
            }
            nInBuffer = 0;
        }
    }

    static int logcat(const std::string &s) {
        flog::info("{}", s);
        android_app *app = backend::app;
        if (!app) {
            return 0;
        }
        JavaVM* java_vm = app->activity->vm;
        JNIEnv* env = NULL;
        jint jni_return = java_vm->GetEnv((void**)&env, JNI_VERSION_1_6);
        if (jni_return == JNI_ERR)
            return -1;

        jni_return = java_vm->AttachCurrentThread(&env, NULL);
        if (jni_return != JNI_OK)
            return -2;

        jclass native_activity_clazz = env->FindClass("org/sdrpp/sdrpp/AndroidAudioInput");
        if (native_activity_clazz == NULL)
            return -3;

        jmethodID method_id = env->GetStaticMethodID(native_activity_clazz, "logcat",
                                                     "([B)V");
        if (method_id == NULL)
            return -4;


        int byteCount = s.length();
        auto pNativeMessage = reinterpret_cast<const jbyte*>(s.c_str());
        auto bytes = env->NewByteArray(byteCount);
        env->SetByteArrayRegion(bytes, 0, byteCount, pNativeMessage);

        env->CallStaticVoidMethod(native_activity_clazz, method_id, bytes);

        jni_return = java_vm->DetachCurrentThread();
        if (jni_return != JNI_OK)
            return -5;
        return 0;
    }

    static void errorCallback(AAudioStream *stream, void *userData, aaudio_result_t error){
        // detect an audio device detached and restart the stream
        auto thiz = (AudioSink *)userData;
        if (error == AAUDIO_ERROR_DISCONNECTED){
            if (currentTimeMillis() - thiz->scheduledStreamRestart > 1000) { // not scheduled restart
                flog::info("INFO errorCallback with AAUDIO_ERROR_DISCONNECTED unplanned");
                std::thread thr(&AudioSink::restart, thiz);
                thr.detach();
            } else {
                flog::info("INFO errorCallback with AAUDIO_ERROR_DISCONNECTED, planned");
            }
        } else {
            flog::info("INFO errorCallback with {}", std::to_string(error));
        }
    }

    void restart() {
        flog::info("INFO android audio: restart()");
        if (running) { doStop(); }
        if (running) { doStart(); }
    }

    std::thread workerThread;
    std::thread workerThreadW;

    AAudioStream *stream = NULL;
    AAudioStream *streamW = NULL;
    std::mutex streamWMutex;
    SinkManager::Stream* _stream;
    dsp::buffer::Packer<dsp::stereo_t> packer;

    std::string _streamName;
    double sampleRate;
    int bufferSize;

    bool running = false;
    bool useRawInput = false;
};

class AudioSinkModule : public ModuleManager::Instance {
public:


    AudioSinkModule(std::string name) {
        this->name = name;
        provider.create = create_sink;
        provider.ctx = this;

        sigpath::sinkManager.registerSinkProvider("Audio", provider);

    }

    ~AudioSinkModule() {
        // Unregister sink, this will automatically stop and delete all instances of the audio sink
        sigpath::sinkManager.unregisterSinkProvider("Audio");
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
    config.setPath(std::string(core::getRoot()) + "/android_audio_sink_config.json");
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