#include <imgui.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <signal_path/sink.h>
#include <dsp/buffer/packer.h>
#include <dsp/convert/stereo_to_mono.h>
#include <utils/flog.h>
#include <config.h>
#include <core.h>
#include <AudioToolbox/AudioToolbox.h>

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "macos_coreaudio_sink",
    /* Description:     */ "CoreAudio sink module for macOS",
    /* Author:          */ "Sanny Sanoff and his aider",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

ConfigManager config;

class CoreAudioSink : SinkManager::Sink {
public:
    struct AudioDevice {
        AudioDeviceID id;
        std::string name;
        std::vector<double> sampleRates;
        std::string sampleRatesTxt;
        bool isInput = false;
    };

    dsp::stream<dsp::stereo_t> microphone = "coreaudio_sink.microphone";
    AudioUnit inputUnit = nullptr;
    bool useMic = false;
    int micDevId = -1;

    CoreAudioSink(SinkManager::Stream* stream, std::string streamName) {
        // Load mic config
        config.acquire();
        _stream = stream;
        _streamName = streamName;
        // Avoid full device enumeration during startup. Some macOS virtual/proxy
        // devices can block CoreAudio property queries before the HTTP debug
        // server is ready.
        enumerateDefaultDevices();

        if (!config.conf.contains(_streamName) || !config.conf[_streamName].is_object()) {
            config.conf[_streamName] = json({});
        }
        if (config.conf[_streamName].contains("useMic")) {
            useMic = config.conf[_streamName]["useMic"];
        }
        if (config.conf[_streamName].contains("micDevice")) {
            std::string micDevice = config.conf[_streamName]["micDevice"];
            for (int i = 0; i < devices.size(); i++) {
                if (devices[i].isInput && devices[i].name == micDevice) {
                    micDevId = i;
                    break;
                }
            }
        }
        config.release(true);
        s2m.init(_stream->sinkOut);
        stereoPacker.init(_stream->sinkOut, 8192);

        playStateHandler.handler = playStateChangeHandler;
        playStateHandler.ctx = this;
        gui::mainWindow.onPlayStateChange.bindHandler(&playStateHandler);

        // Load config
        config.acquire();
        if (!config.conf.contains(_streamName) || !config.conf[_streamName].is_object()) {
            config.conf[_streamName] = json({});
        }
        if (!config.conf[_streamName].contains("device") || !config.conf[_streamName]["device"].is_string()) {
            config.conf[_streamName]["device"] = "";
        }
        if (!config.conf[_streamName].contains("devices") || !config.conf[_streamName]["devices"].is_object()) {
            config.conf[_streamName]["devices"] = json({});
        }
        std::string device = config.conf[_streamName]["device"];
        config.release(true);

        selectByName(device);
    }

    ~CoreAudioSink() {
        stop();
        gui::mainWindow.onPlayStateChange.unbindHandler(&playStateHandler);
    }

    void start() {
        if (running || !gui::mainWindow.isPlaying()) { return; }
        running = doStart();
    }

    void stop() {
        if (!running) { return; }
        doStop();
        running = false;
    }

    void selectFirst() {
        if (!devices.empty()) {
            selectById(0);
        }
    }

    void selectByName(std::string name) {
        for (int i = 0; i < devices.size(); i++) {
            if (devices[i].name == name) {
                selectById(i);
                return;
            }
        }
        selectFirst();
    }

    void selectById(int id) {
        if (id < 0 || id >= devices.size()) { return; }
        
        devId = id;
        auto& dev = devices[devId];

        // Load sample rate from config
        config.acquire();
        if (!config.conf.contains(_streamName) || !config.conf[_streamName].is_object()) {
            config.conf[_streamName] = json({});
        }
        if (!config.conf[_streamName].contains("devices") || !config.conf[_streamName]["devices"].is_object()) {
            config.conf[_streamName]["devices"] = json({});
        }
        if (!config.conf[_streamName]["devices"].contains(dev.name)) {
            config.conf[_streamName]["devices"][dev.name] = dev.sampleRates[0];
        }
        sampleRate = config.conf[_streamName]["devices"][dev.name];
        config.release(true);

        // Find sample rate ID
        bool found = false;
        for (int i = 0; i < dev.sampleRates.size(); i++) {
            if (dev.sampleRates[i] == sampleRate) {
                srId = i;
                found = true;
                break;
            }
        }
        if (!found) {
            sampleRate = dev.sampleRates[0];
            srId = 0;
        }

        _stream->setSampleRate(sampleRate);


        // Restart audio if running
        if (running) {
            doStop();
            doStart();
        }
    }

    void menuHandler() {
        float menuWidth = ImGui::GetContentRegionAvail().x;

        // Device selection
        ImGui::SetNextItemWidth(menuWidth);
        if (ImGui::Combo(("##_coreaudio_sink_dev_" + _streamName).c_str(), &devId, [](void* data, int idx, const char** out_text) {
            auto devices = (std::vector<AudioDevice>*)data;
            *out_text = devices->at(idx).name.c_str();
            return true;
        }, &devices, devices.size())) {
            selectById(devId);
            config.acquire();
            config.conf[_streamName]["device"] = devices[devId].name;
            config.release(true);
        }

        // Sample rate selection
        if (devId >= 0 && devId < devices.size()) {
            ImGui::SetNextItemWidth(menuWidth);
            if (ImGui::Combo(("##_coreaudio_sink_sr_" + _streamName).c_str(), &srId, devices[devId].sampleRatesTxt.c_str())) {
                sampleRate = devices[devId].sampleRates[srId];
                _stream->setSampleRate(sampleRate);
                if (running) {
                    doStop();
                    doStart();
                }
                config.acquire();
                config.conf[_streamName]["devices"][devices[devId].name] = sampleRate;
                config.release(true);
            }
        }
        
        if (underflow != 0) {
            // ImGui::SameLine();
            // ImGui::Text("Underflow %d", underflow);
        }

        // Microphone section
        if (ImGui::CollapsingHeader("Microphone")) {
            // Use microphone checkbox
            if (ImGui::Checkbox("Use Microphone", &useMic)) {
                config.acquire();
                config.conf[_streamName]["useMic"] = useMic;
                config.release(true);
                if (running) {
                    doStop();
                    doStart();
                }
            }

            // Microphone device selection
            if (useMic) {
                ImGui::SetNextItemWidth(menuWidth);
                
                // Build list of input devices
                std::vector<AudioDevice> inputDevices;
                std::string inputDevList;
                for (auto& dev : devices) {
                    if (dev.isInput) {
                        inputDevices.push_back(dev);
                        inputDevList += dev.name;
                        inputDevList += '\0';
                    }
                }

                // Find current micDevId in input devices
                int currentMicDevIdx = 0;
                if (micDevId >= 0 && micDevId < devices.size()) {
                    for (int i = 0; i < inputDevices.size(); i++) {
                        if (inputDevices[i].id == devices[micDevId].id) {
                            currentMicDevIdx = i;
                            break;
                        }
                    }
                }

                if (ImGui::Combo("##_coreaudio_sink_mic_dev", &currentMicDevIdx, inputDevList.c_str())) {
                    // Find the selected device in main devices list
                    for (int i = 0; i < devices.size(); i++) {
                        if (devices[i].id == inputDevices[currentMicDevIdx].id) {
                            micDevId = i;
                            break;
                        }
                    }
                    
                    config.acquire();
                    config.conf[_streamName]["micDevice"] = devices[micDevId].name;
                    config.release(true);
                    
                    if (running) {
                        doStop();
                        doStart();
                    }
                }
            }
        }
    }

private:
    bool doStart() {
        if (devId < 0 || devId >= devices.size()) { return false; }
        
        // Create output audio unit for the selected device
        AudioComponentDescription desc = {
            .componentType = kAudioUnitType_Output,
            .componentSubType = kAudioUnitSubType_DefaultOutput,
            .componentManufacturer = kAudioUnitManufacturer_Apple,
            .componentFlags = 0,
            .componentFlagsMask = 0
        };

        AudioComponent comp = AudioComponentFindNext(NULL, &desc);
        if (!comp) {
            flog::error("Could not find audio component for device");
            return false;
        }

        if (!comp) {
            flog::error("Could not find audio component");
            return false;
        }

        // Create the audio unit first
        OSStatus status = AudioComponentInstanceNew(comp, &audioUnit);
        if (status != noErr) {
            flog::error("Could not create audio unit instance");
            return false;
        }

        // Set stream format with lower latency settings
        AudioStreamBasicDescription streamFormat = {
            .mSampleRate = sampleRate,
            .mFormatID = kAudioFormatLinearPCM,
            .mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved,
            .mBytesPerPacket = sizeof(float),
            .mFramesPerPacket = 1,
            .mBytesPerFrame = sizeof(float),
            .mChannelsPerFrame = 2,
            .mBitsPerChannel = sizeof(float) * 8,
            .mReserved = 0
        };

        status = AudioUnitSetProperty(audioUnit,
                                    kAudioUnitProperty_StreamFormat,
                                    kAudioUnitScope_Input,
                                    0,
                                    &streamFormat,
                                    sizeof(streamFormat));
        if (status != noErr) {
            flog::error("Could not set stream format");
            return false;
        }

        // Set render callback
        AURenderCallbackStruct callback = {
            .inputProc = renderCallback,
            .inputProcRefCon = this
        };

        status = AudioUnitSetProperty(audioUnit,
                                    kAudioUnitProperty_SetRenderCallback,
                                    kAudioUnitScope_Input,
                                    0,
                                    &callback,
                                    sizeof(callback));
        if (status != noErr) {
            flog::error("Could not set render callback");
            return false;
        }

        // Initialize audio unit
        status = AudioUnitInitialize(audioUnit);
        if (status != noErr) {
            flog::error("Could not initialize audio unit");
            return false;
        }

        // Set buffer frame size for lower latency
        UInt32 bufferFrameSize = 4096;
        status = AudioUnitSetProperty(audioUnit,
                                    kAudioDevicePropertyBufferFrameSize,
                                    kAudioUnitScope_Global,
                                    0,
                                    &bufferFrameSize,
                                    sizeof(bufferFrameSize));
        if (status != noErr) {
            flog::warn("Could not set buffer frame size, using default");
        }

        // Start audio unit
        status = AudioOutputUnitStart(audioUnit);
        if (status != noErr) {
            flog::error("Could not start audio unit");
            return false;
        }

        // Set up microphone input if enabled
        if (useMic && micDevId >= 0 && micDevId < devices.size() && devices[micDevId].isInput) {
            AudioComponentDescription desc = {
                .componentType = kAudioUnitType_Output,
                .componentSubType = kAudioUnitSubType_HALOutput,
                .componentManufacturer = kAudioUnitManufacturer_Apple,
                .componentFlags = 0,
                .componentFlagsMask = 0
            };

            AudioComponent comp = AudioComponentFindNext(NULL, &desc);
            if (!comp) {
                flog::error("Could not find audio component for microphone");
                return false;
            }

            status = AudioComponentInstanceNew(comp, &inputUnit);
            if (status != noErr) {
                flog::error("Could not create microphone audio unit");
                return false;
            }

            // Enable input on the audio unit
            UInt32 enableInput = 1;
            status = AudioUnitSetProperty(inputUnit,
                                        kAudioOutputUnitProperty_EnableIO,
                                        kAudioUnitScope_Input,
                                        1, // Input element
                                        &enableInput,
                                        sizeof(enableInput));
            if (status != noErr) {
                flog::error("Could not enable input on microphone device");
                return false;
            }

            // Disable output on the audio unit
            UInt32 disableOutput = 0;
            status = AudioUnitSetProperty(inputUnit,
                                        kAudioOutputUnitProperty_EnableIO,
                                        kAudioUnitScope_Output,
                                        0, // Output element
                                        &disableOutput,
                                        sizeof(disableOutput));
            if (status != noErr) {
                flog::error("Could not disable output on microphone device");
                return false;
            }

            // Set microphone device
            status = AudioUnitSetProperty(inputUnit,
                                        kAudioOutputUnitProperty_CurrentDevice,
                                        kAudioUnitScope_Global,
                                        0,
                                        &devices[micDevId].id,
                                        sizeof(devices[micDevId].id));
            if (status != noErr) {
                flog::error("Could not set microphone device: {}", status);
                return false;
            }

            // Set stream format
            AudioStreamBasicDescription streamFormat = {
                .mSampleRate = sampleRate,
                .mFormatID = kAudioFormatLinearPCM,
                .mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked,
                .mBytesPerPacket = sizeof(float),
                .mFramesPerPacket = 1,
                .mBytesPerFrame = sizeof(float),
                .mChannelsPerFrame = 1,
                .mBitsPerChannel = sizeof(float) * 8,
                .mReserved = 0
            };

            status = AudioUnitSetProperty(inputUnit,
                                        kAudioUnitProperty_StreamFormat,
                                        kAudioUnitScope_Output,
                                        1,
                                        &streamFormat,
                                        sizeof(streamFormat));
            if (status != noErr) {
                flog::error("Could not set microphone stream format");
                return false;
            }

            // Set input callback
            AURenderCallbackStruct callback = {
                .inputProc = inputCallback,
                .inputProcRefCon = this
            };

            status = AudioUnitSetProperty(inputUnit,
                                        kAudioOutputUnitProperty_SetInputCallback,
                                        kAudioUnitScope_Global,
                                        0,
                                        &callback,
                                        sizeof(callback));
            if (status != noErr) {
                flog::error("Could not set microphone input callback");
                return false;
            }

            // Initialize and start microphone
            status = AudioUnitInitialize(inputUnit);
            if (status != noErr) {
                flog::error("Could not initialize microphone audio unit");
                return false;
            }

            status = AudioOutputUnitStart(inputUnit);
            if (status != noErr) {
                flog::error("Could not start microphone audio unit");
                return false;
            }

            sigpath::sinkManager.defaultInputAudio.setInput(&microphone);
            sigpath::sinkManager.defaultInputAudio.start();
        }

        // Set packer buffer size to match audio unit buffer size
        stereoPacker.setSampleCount(bufferFrameSize);
        stereoPacker.start();
        return true;
    }

    static OSStatus inputCallback(void* inRefCon,
                                AudioUnitRenderActionFlags* ioActionFlags,
                                const AudioTimeStamp* inTimeStamp,
                                UInt32 inBusNumber,
                                UInt32 inNumberFrames,
                                AudioBufferList* ioData) {
        CoreAudioSink* _this = (CoreAudioSink*)inRefCon;

        AudioBufferList bufferList;
        bufferList.mNumberBuffers = 1;
        bufferList.mBuffers[0].mDataByteSize = inNumberFrames * sizeof(float);
        bufferList.mBuffers[0].mData = malloc(inNumberFrames * sizeof(float));
        bufferList.mBuffers[0].mNumberChannels = 1;

        OSStatus status = AudioUnitRender(_this->inputUnit,
                                        ioActionFlags,
                                        inTimeStamp,
                                        inBusNumber,
                                        inNumberFrames,
                                        &bufferList);
        if (status != noErr) {
            free(bufferList.mBuffers[0].mData);
            return status;
        }

        float* input = (float*)bufferList.mBuffers[0].mData;
        auto out = (dsp::stereo_t*)_this->microphone.writeBuf;
        for (UInt32 i = 0; i < inNumberFrames; i++) {
            out[i].l = input[i];
            out[i].r = input[i];
        }
        _this->microphone.swap(inNumberFrames);

        free(bufferList.mBuffers[0].mData);
        return noErr;
    }

    static void playStateChangeHandler(bool newState, void* ctx) {
        CoreAudioSink* _this = (CoreAudioSink*)ctx;
        if (newState) {
            _this->start();
        }
        else {
            _this->stop();
        }
    }

    void doStop() {
        if (audioUnit) {
            // Stop and uninitialize in the correct order
            OSStatus status = AudioOutputUnitStop(audioUnit);
            if (status != noErr) {
                flog::warn("Failed to stop audio unit: {}", status);
            }
            
            status = AudioUnitUninitialize(audioUnit);
            if (status != noErr) {
                flog::warn("Failed to uninitialize audio unit: {}", status);
            }
            
            status = AudioComponentInstanceDispose(audioUnit);
            if (status != noErr) {
                flog::warn("Failed to dispose audio unit: {}", status);
            }
            audioUnit = nullptr;
        }
        
        if (inputUnit) {
            // Stop and uninitialize in the correct order
            OSStatus status = AudioOutputUnitStop(inputUnit);
            if (status != noErr) {
                flog::warn("Failed to stop input unit: {}", status);
            }
            
            status = AudioUnitUninitialize(inputUnit);
            if (status != noErr) {
                flog::warn("Failed to uninitialize input unit: {}", status);
            }
            
            status = AudioComponentInstanceDispose(inputUnit);
            if (status != noErr) {
                flog::warn("Failed to dispose input unit: {}", status);
            }
            inputUnit = nullptr;
        }
        
        stereoPacker.stop();
        if (useMic) {
            sigpath::sinkManager.defaultInputAudio.stop();
            sigpath::sinkManager.defaultInputAudio.setInput(nullptr);
        }
    }

    std::vector<dsp::stereo_t> stereoBuffer;

    static OSStatus renderCallback(void* inRefCon,
                                 AudioUnitRenderActionFlags* ioActionFlags,
                                 const AudioTimeStamp* inTimeStamp,
                                 UInt32 inBusNumber,
                                 UInt32 inNumberFrames,
                                 AudioBufferList* ioData) {
        CoreAudioSink* _this = (CoreAudioSink*)inRefCon;

        if (!ioData || ioData->mNumberBuffers < 1 || !ioData->mBuffers[0].mData) {
            return noErr;
        }

        float* left = (float*)ioData->mBuffers[0].mData;
        float* right = left;
        if (ioData->mNumberBuffers > 1 && ioData->mBuffers[1].mData) {
            right = (float*)ioData->mBuffers[1].mData;
        }

        if (!gui::mainWindow.isPlaying()) {
            memset(left, 0, inNumberFrames * sizeof(float));
            memset(right, 0, inNumberFrames * sizeof(float));
            return noErr;
        }


        memset(left, 0, inNumberFrames * sizeof(float));
        memset(right, 0, inNumberFrames * sizeof(float));

        // Read audio data from packer
        int count = 0;
        if (_this->stereoPacker.out.isDataReady()) {
            count = _this->stereoPacker.out.read();
            if (count <= 0) {
                return noErr;
            }
        }
        size_t oldSize = _this->stereoBuffer.size();
        _this->stereoBuffer.resize(oldSize + count);

        for (int i = 0; i < count; i++) {
            _this->stereoBuffer[oldSize + i] = _this->stereoPacker.out.readBuf[i];
        }

        int limit = std::min<uint32_t>(inNumberFrames, _this->stereoBuffer.size());

        // Copy data to output buffers
        for (UInt32 i = 0; i < limit; i++) {
            left[i] = _this->stereoBuffer[i].l;
            right[i] = _this->stereoBuffer[i].r;
        }
        if (limit < inNumberFrames) {
            _this->underflow = 1;
        } else {
            _this->underflow = 0;
        }

        _this->stereoBuffer.erase(_this->stereoBuffer.begin(), _this->stereoBuffer.begin() + limit);
        _this->stereoPacker.out.flush();
        return noErr;
    }

    void enumerateDevices() {
        devices.clear();
        AudioObjectPropertyAddress prop = {
            .mSelector = kAudioHardwarePropertyDevices,
            .mScope = kAudioObjectPropertyScopeGlobal,
            .mElement = kAudioObjectPropertyElementMain
        };

        UInt32 size = 0;
        OSStatus status = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &prop, 0, NULL, &size);
        if (status != noErr) {
            flog::error("Could not get audio devices size");
            return;
        }

        UInt32 count = size / sizeof(AudioDeviceID);
        AudioDeviceID* deviceIDs = new AudioDeviceID[count];
        status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &prop, 0, NULL, &size, deviceIDs);
        if (status != noErr) {
            flog::error("Could not get audio devices");
            delete[] deviceIDs;
            return;
        }

        for (UInt32 i = 0; i < count; i++) {
            AudioDevice device;
            device.id = deviceIDs[i];

            // Get device name
            prop.mSelector = kAudioObjectPropertyName;
            CFStringRef name = NULL;
            size = sizeof(name);
            status = AudioObjectGetPropertyData(device.id, &prop, 0, NULL, &size, &name);
            if (status == noErr && name) {
                char buffer[256];
                CFStringGetCString(name, buffer, 256, kCFStringEncodingUTF8);
                device.name = buffer;
                CFRelease(name);
            }

            // Check if device has output channels
            prop.mSelector = kAudioDevicePropertyStreamConfiguration;
            prop.mScope = kAudioDevicePropertyScopeOutput;
            status = AudioObjectGetPropertyDataSize(device.id, &prop, 0, NULL, &size);
            if (status == noErr) {
                AudioBufferList* bufferList = (AudioBufferList*)malloc(size);
                status = AudioObjectGetPropertyData(device.id, &prop, 0, NULL, &size, bufferList);
                if (status == noErr && bufferList->mNumberBuffers > 0) {
                    device.isInput = false;
                    // Get supported sample rates
                    prop.mSelector = kAudioDevicePropertyAvailableNominalSampleRates;
                    status = AudioObjectGetPropertyDataSize(device.id, &prop, 0, NULL, &size);
                    if (status == noErr) {
                        AudioValueRange* ranges = (AudioValueRange*)malloc(size);
                        status = AudioObjectGetPropertyData(device.id, &prop, 0, NULL, &size, ranges);
                        if (status == noErr) {
                            UInt32 rangeCount = size / sizeof(AudioValueRange);
                            for (UInt32 j = 0; j < rangeCount; j++) {
                                double min = ranges[j].mMinimum;
                                double max = ranges[j].mMaximum;
                                // Add common rates between min and max
                                if (min <= 44100 && max >= 44100) device.sampleRates.push_back(44100);
                                if (min <= 48000 && max >= 48000) device.sampleRates.push_back(48000);
                                if (min <= 96000 && max >= 96000) device.sampleRates.push_back(96000);
                                if (min <= 192000 && max >= 192000) device.sampleRates.push_back(192000);
                            }
                            // Sort and create text list
                            std::sort(device.sampleRates.begin(), device.sampleRates.end());
                            for (auto sr : device.sampleRates) {
                                device.sampleRatesTxt += std::to_string((int)sr);
                                device.sampleRatesTxt += '\0';
                            }
                        }
                        free(ranges);
                    }

                    if (!device.sampleRates.empty()) {
                        devices.push_back(device);
                    }
                }
                free(bufferList);
            }

            // Check if device has input channels
            prop.mSelector = kAudioDevicePropertyStreamConfiguration;
            prop.mScope = kAudioDevicePropertyScopeInput;
            status = AudioObjectGetPropertyDataSize(device.id, &prop, 0, NULL, &size);
            if (status == noErr) {
                AudioBufferList* bufferList = (AudioBufferList*)malloc(size);
                status = AudioObjectGetPropertyData(device.id, &prop, 0, NULL, &size, bufferList);
                if (status == noErr && bufferList->mNumberBuffers > 0) {
                    device.isInput = true;
                    devices.push_back(device);
                }
                free(bufferList);
            }
        }

        delete[] deviceIDs;
    }

    bool addDevice(AudioDeviceID id, bool isInput) {
        if (id == kAudioObjectUnknown) { return false; }

        AudioDevice device;
        device.id = id;
        device.isInput = isInput;

        AudioObjectPropertyAddress prop = {
            .mSelector = kAudioObjectPropertyName,
            .mScope = kAudioObjectPropertyScopeGlobal,
            .mElement = kAudioObjectPropertyElementMain
        };

        CFStringRef name = NULL;
        UInt32 size = sizeof(name);
        OSStatus status = AudioObjectGetPropertyData(device.id, &prop, 0, NULL, &size, &name);
        if (status == noErr && name) {
            char buffer[256];
            CFStringGetCString(name, buffer, 256, kCFStringEncodingUTF8);
            device.name = buffer;
            CFRelease(name);
        }
        if (device.name.empty()) { return false; }

        if (!isInput) {
            prop.mSelector = kAudioDevicePropertyAvailableNominalSampleRates;
            status = AudioObjectGetPropertyDataSize(device.id, &prop, 0, NULL, &size);
            if (status == noErr && size > 0) {
                AudioValueRange* ranges = (AudioValueRange*)malloc(size);
                status = AudioObjectGetPropertyData(device.id, &prop, 0, NULL, &size, ranges);
                if (status == noErr) {
                    UInt32 rangeCount = size / sizeof(AudioValueRange);
                    for (UInt32 j = 0; j < rangeCount; j++) {
                        double min = ranges[j].mMinimum;
                        double max = ranges[j].mMaximum;
                        if (min <= 44100 && max >= 44100) device.sampleRates.push_back(44100);
                        if (min <= 48000 && max >= 48000) device.sampleRates.push_back(48000);
                        if (min <= 96000 && max >= 96000) device.sampleRates.push_back(96000);
                        if (min <= 192000 && max >= 192000) device.sampleRates.push_back(192000);
                    }
                }
                free(ranges);
            }
            if (device.sampleRates.empty()) {
                device.sampleRates.push_back(44100);
                device.sampleRates.push_back(48000);
            }
            std::sort(device.sampleRates.begin(), device.sampleRates.end());
            device.sampleRates.erase(std::unique(device.sampleRates.begin(), device.sampleRates.end()), device.sampleRates.end());
            for (auto sr : device.sampleRates) {
                device.sampleRatesTxt += std::to_string((int)sr);
                device.sampleRatesTxt += '\0';
            }
        }

        devices.push_back(device);
        return true;
    }

    void enumerateDefaultDevices() {
        devices.clear();

        AudioObjectPropertyAddress prop = {
            .mSelector = kAudioHardwarePropertyDefaultOutputDevice,
            .mScope = kAudioObjectPropertyScopeGlobal,
            .mElement = kAudioObjectPropertyElementMain
        };

        AudioDeviceID id = kAudioObjectUnknown;
        UInt32 size = sizeof(id);
        OSStatus status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &prop, 0, NULL, &size, &id);
        if (status == noErr) {
            addDevice(id, false);
        }

        prop.mSelector = kAudioHardwarePropertyDefaultInputDevice;
        id = kAudioObjectUnknown;
        size = sizeof(id);
        status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &prop, 0, NULL, &size, &id);
        if (status == noErr) {
            addDevice(id, true);
        }
    }

    SinkManager::Stream* _stream;
    dsp::convert::StereoToMono s2m;
    dsp::buffer::Packer<dsp::stereo_t> stereoPacker;

    std::string _streamName;

    int srId = 0;
    int devId = -1;
    bool running = false;
    int underflow = 0; // 1 = small underflow, 2 = full underflow
    EventHandler<bool> playStateHandler;

    std::vector<AudioDevice> devices;
    double sampleRate = 48000;

    AudioUnit audioUnit = nullptr;
};

class CoreAudioSinkModule : public ModuleManager::Instance {
public:
    CoreAudioSinkModule(std::string name) {
        this->name = name;
        provider.create = create_sink;
        provider.ctx = this;

        sigpath::sinkManager.registerSinkProvider("CoreAudio", provider);
    }

    ~CoreAudioSinkModule() {
        sigpath::sinkManager.unregisterSinkProvider("CoreAudio");
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
        return (SinkManager::Sink*)(new CoreAudioSink(stream, streamName));
    }

    std::string name;
    bool enabled = true;
    SinkManager::SinkProvider provider;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    config.setPath(core::args["root"].s() + "/coreaudio_sink_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT void* _CREATE_INSTANCE_(std::string name) {
    CoreAudioSinkModule* instance = new CoreAudioSinkModule(name);
    return instance;
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (CoreAudioSinkModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
