#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui.h>
#include <module.h>
#include <gui/gui.h>
#include <config.h>
#include <core.h>
#include <imgui/imgui_internal.h>
#include <vector>

#include "picovoice.h"
#include "pv_picovoice.h"
#include "signal_path/signal_path.h"

ConfigManager config;

SDRPP_MOD_INFO{
        /* Name:            */ "voice_commands",
        /* Description:     */ "Voice Commands",
        /* Author:          */ "sannysanoff",
        /* Version:         */ 0, 1, 0,
        /* Max instances    */ 1
};

static const char *(*pv_status_to_string_func)(pv_status_t);
static int32_t (*pv_sample_rate_func)();
static pv_status_t (*pv_picovoice_init_func)(
        const char *access_key,
        const char *porcupine_model_path,
        const char *keyword_path,
        float porcupine_sensitivity,
        void (*wake_word_callback)(void),
        const char *rhino_model_path,
        const char *context_path,
        float rhino_sensitivity,
        float endpoint_duration_sec,
        bool require_endpoint,
        void (*inference_callback)(pv_inference_t *),
        pv_picovoice_t **object);
static void (*pv_picovoice_delete_func)(pv_picovoice_t *);
static int32_t (*pv_picovoice_frame_length_func)();
static pv_status_t (*pv_picovoice_process_func)(pv_picovoice_t *, const int16_t *);
static const char *(*pv_picovoice_version_func)();
static void (*pv_inference_delete_func)(pv_inference_t *);

static int pvSampleRate;
static int pvFrameLength;



class VoiceCommands : public ModuleManager::Instance {


    std::string accessKey;

public:
    VoiceCommands(std::string name) {
        this->name = name;
        config.acquire();
        if (config.conf.contains("accessKey")) accessKey = config.conf["accessKey"];
        strcpy(_pvkey, accessKey.c_str());
//        if (config.conf.contains("SNRChartWidget")) snrChartWidget = config.conf["SNRChartWidget"];
        config.release(true);

        maybeInitPicovoice();

        gui::menu.registerEntry(name, menuHandler, this, NULL);


//        updateBindings();
//        actuateIFNR();

        enable();
    }

    ~VoiceCommands() {
        if (picoVoiceMainHandle) {
            pv_picovoice_delete(picoVoiceMainHandle);
            picoVoiceMainHandle = nullptr;
        }
        gui::menu.removeEntry(name);
    }

    void postInit() override  {}

    pv_picovoice_t *picoVoiceMainHandle = NULL;

    static void wake_word_callback(void) {
        fprintf(stdout, "[wake word]\n");
        fflush(stdout);
    }

    static void inference_callback(pv_inference_t *inference) {
        fprintf(stdout, "{\n");
        fprintf(stdout, "    is_understood : '%s',\n", (inference->is_understood ? "true" : "false"));
        if (inference->is_understood) {
            fprintf(stdout, "    intent : '%s',\n", inference->intent);
            if (inference->num_slots > 0) {
                fprintf(stdout, "    slots : {\n");
                for (int32_t i = 0; i < inference->num_slots; i++) {
                    fprintf(stdout, "        '%s' : '%s',\n", inference->slots[i], inference->values[i]);
                }
                fprintf(stdout, "    }\n");
            }
        }
        fprintf(stdout, "}\n\n");
        fflush(stdout);

        pv_inference_delete(inference);
    }

    static void *open_dl(const char *dl_path) {

#if defined(_WIN32) || defined(_WIN64)

        return LoadLibrary(dl_path);

#else

        return dlopen(dl_path, RTLD_NOW);

#endif
    }

    static void *load_symbol(void *handle, const char *symbol) {

#if defined(_WIN32) || defined(_WIN64)

        return GetProcAddress((HMODULE) handle, symbol);

#else

        return dlsym(handle, symbol);

#endif
    }

    static void close_dl(void *handle) {

#if defined(_WIN32) || defined(_WIN64)

        FreeLibrary((HMODULE) handle);

#else

        dlclose(handle);

#endif
    }

    static void print_dl_error(const char *message) {

#if defined(_WIN32) || defined(_WIN64)

        fprintf(stderr, "%s with code '%lu'.\n", message, GetLastError());

#else

        fprintf(stderr, "%s with '%s'.\n", message, dlerror());

#endif
    }


    void maybeInitPicovoice() {
        if (picoVoiceMainHandle) {
            return;
        }
        if (accessKey.empty()) {
            picoVoiceStatus = "empty key";
            return;
        }
        if (pv_picovoice_init_func == nullptr) {
            void *picovoice_library = open_dl("libpicovoice.so");
            if (!picovoice_library) {
                picoVoiceStatus = "libpicovoice.so load failed";
                return;
            }

            auto trim5 = [](const char *str) {
                static char buf[100] = {0};
                strcpy(buf, str);
                buf[strlen(buf)-5] = 0;
                return buf;
            };

#define INITSYMBOL(name) \
        name = reinterpret_cast<decltype(name)>(load_symbol(picovoice_library, trim5(#name))); \
        if (!name) { picoVoiceStatus = "libpicovoice.so symbol load error"; return; }

            INITSYMBOL(pv_status_to_string_func);
            INITSYMBOL(pv_sample_rate_func);
            INITSYMBOL(pv_picovoice_init_func);
            INITSYMBOL(pv_picovoice_delete_func);
            INITSYMBOL(pv_status_to_string_func);
            INITSYMBOL(pv_picovoice_frame_length_func);
            INITSYMBOL(pv_picovoice_process_func);
            INITSYMBOL(pv_picovoice_version_func);
            INITSYMBOL(pv_inference_delete_func);
            pvSampleRate = pv_sample_rate_func();
            pvFrameLength = pv_picovoice_frame_length_func();
        }


        pv_status_t status = pv_picovoice_init_func(
                accessKey.c_str(),
                "/home/san/Fun/SDRPlusPlus/misc_modules/voice_commands/model/porcupine_params_ru.pv", // porcupine_model_path,
                "/home/san/Fun/SDRPlusPlus/misc_modules/voice_commands/model/radio_ru_linux_v2_2_0.ppn", // keyword_path,
                0.9f, // porcupine_sensitivity,
                wake_word_callback,
                "/home/san/Fun/SDRPlusPlus/misc_modules/voice_commands/model/rhino_params_ru.pv", // rhino_model_path,
                "/home/san/Fun/SDRPlusPlus/misc_modules/voice_commands/model/sdrppbrown_ru_linux_v2_2_0.rhn", //context_path,
                0.5f,
                1.0f,
                false,
                inference_callback,
                &picoVoiceMainHandle);
        if (status != PV_STATUS_SUCCESS) {
            picoVoiceStatus = std::string("init error: ")+pv_status_to_string_func(status);
            fprintf(stderr, "'pv_picovoice_init' failed with '%s'\n", pv_status_to_string_func(status));
            return;
        } else {
            fprintf(stderr, "Picovoice initialized");
            picoVoiceStatus = "init ok, waiting for mic";
        }

    }

    dsp::stream<dsp::stereo_t> audioIn;
    void enable() override  {
        if (!enabled) {
            enabled = true;
            audioIn.clearReadStop();
            sigpath::sinkManager.defaultInputAudio.bindStream(&audioIn);
            std::thread inputProcessor([this] {
                std::vector<int16_t> queue;

                dsp::multirate::RationalResampler<dsp::stereo_t> res;
                bool initialized = false;
                std::vector<dsp::stereo_t> resampleBuf(100000, dsp::stereo_t());

                while(enabled) {
                    int rd = audioIn.read();
                    if (rd < 0 || !enabled) {
                        break;
                    }
                    if (picoVoiceMainHandle && !initialized) {
                        initialized = true;
                        res.init(nullptr, 48000, pvSampleRate);
                        picoVoiceStatus = "processing";
                    }
                    if (initialized) {
                        auto processed = res.process(rd, audioIn.readBuf, resampleBuf.data());
                        for(int q=0; q<processed; q++) {
                            auto sample = audioIn.readBuf[q].l;
                            if (sample > 1) {
                                sample = 0;
                            } else if (sample < -1) {
                                sample = -1;
                            }
                            queue.insert(queue.end(), (int)(32767 * 8 * sample));
                        }
                    }
                    audioIn.flush();
                    if (initialized) {
                        while (queue.size() > pvFrameLength) {
                            auto status = pv_picovoice_process_func(picoVoiceMainHandle, queue.data());
                            if (status != PV_STATUS_SUCCESS) {
                                fprintf(stderr, "'pv_picovoice_process' failed with '%s'\n", pv_status_to_string_func(status));
                            }
                            if (false) {
                                int minn = 10101010;
                                int maxx = -10101010;
                                for (int q = 0; q < queue.size(); q++) {
                                    auto v = queue[q];
                                    minn = std::min<float>(minn, v);
                                    maxx = std::max<float>(maxx, v);
                                }
                                printf("%d %d\n", minn, maxx);
                            }
                            queue.erase(queue.begin(), queue.begin() + pvFrameLength);
                        }
                    }
                }
            });
            inputProcessor.detach();
        }
    }

    void disable() override {
        if (enabled) {
            enabled = false;
            sigpath::sinkManager.defaultInputAudio.unbindStream(&audioIn);
            audioIn.stopReader();
        }
    }

    bool isEnabled() override {
        return enabled;
    }




private:


    char _pvkey[300];

    std::string picoVoiceStatus = "init not started";

    void menuHandler() {
        float menuWidth = ImGui::GetContentRegionAvail().x;
        ImGui::Text("Voice Commands here");
        if (ImGui::InputText("##picovoice_access_key", _pvkey, 1000)) {
            config.acquire();
            accessKey = _pvkey;
            config.conf["accessKey"] = accessKey;
            config.release(true);
            maybeInitPicovoice();
        }
        ImGui::Text("Picovoice: %s", picoVoiceStatus.c_str());
    }

    static void menuHandler(void* ctx) {
        VoiceCommands* _this = (VoiceCommands*)ctx;
        _this->menuHandler();
    }

    std::string name;
    bool enabled = false;

};

MOD_EXPORT void _INIT_() {
    config.setPath(core::args["root"].s() + "/voice_commands.json");
    config.load(json::object());
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new VoiceCommands(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (VoiceCommands*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}