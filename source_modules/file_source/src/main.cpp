#define NOMINMAX
#include <imgui.h>
#include <utils/flog.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <utils/wav.h>
#include <core.h>
#include <gui/widgets/file_select.h>
#include <filesystem>
#include <regex>
#include <gui/tuner.h>
#include <time.h>
#include "gui/smgui.h"
#include "utils/usleep.h"
#include "utils/optionlist.h"
#include "utils/wstr.h"
#include <algorithm>
#include <stdexcept>
#include "utils/wstr.h"
#include "server.h"

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "file_source",
    /* Description:     */ "Wav file source module for SDR++",
    /* Author:          */ "Ryzerth",
    /* Version:         */ 0, 1, 1,
    /* Max instances    */ 1
};

ConfigManager config;

class FileSourceModule : public ModuleManager::Instance {
public:
    FileSourceModule(std::string name) : fileSelect("", { "Wav IQ Files (*.wav)", "*.wav", "All Files", "*" }) {
        this->name = name;
        isServer = core::args["server"].b() ? 1 : 0;

//        if (core::args["server"].b()) { return; }

        config.acquire();
        fileSelect.setPath(config.conf["path"], true);
        config.release();

        handler.ctx = this;
        handler.selectHandler = menuSelected;
        handler.deselectHandler = menuDeselected;
        handler.menuHandler = menuHandler;
        handler.startHandler = start;
        handler.stopHandler = stop;
        handler.tuneHandler = tune;
        handler.stream = &stream;
        sigpath::sourceManager.registerSource("File", &handler);
    }

    ~FileSourceModule() {
        stop(this);
        sigpath::sourceManager.unregisterSource("File");
    }

    std::vector<std::string> getWavFiles(const std::string& directoryPath) {
        std::vector<std::string> wavFiles;

        // Iterate over the directory entries
        for (const auto& entry : std::filesystem::directory_iterator(wstr::str2wstr(directoryPath))) {
            // Check if the entry is a file and has a .wav extension
            if (entry.is_regular_file() && entry.path().extension() == ".wav") {
                wavFiles.push_back(entry.path().string());
            }
        }

        return wavFiles;
    }


    std::string getDirectoryPath(const std::string& fullPath) {
        // Create a path object from the full path
        std::filesystem::path filePath(wstr::str2wstr(fullPath));

        // Get the parent path, which is the directory path
        std::filesystem::path directoryPath = filePath.parent_path();

        // Convert the directory path to a string and return it
        return directoryPath.string();
    }

    std::string getFileName(const std::string& fullPath) {
        // Create a path object from the full path
        std::filesystem::path filePath(wstr::str2wstr(fullPath));

        // Get the file name
        std::filesystem::path fileName = filePath.filename();

        // Convert the file name to a string and return it
        return fileName.string();
    }

    void refreshFiles() {
        if (isServer) {
            std::string fullFilePath = config.conf.contains("path") ? config.conf["path"] : "";
            if (fullFilePath.empty()) {
                return;
            }
            auto dp = getDirectoryPath(fullFilePath);
            auto fn = getFileName(fullFilePath);
            if (dp.empty()|| fn.empty()) {
                return;
            }
            auto wavFiles = getWavFiles(dp);
            currentDirList.clear();
            for(auto& file : wavFiles) {
                auto fn0 = getFileName(file);
                currentDirList.define(fn0, fn0, fn0);
            }
            try {
                currentDirListId = currentDirList.valueId(fn);
            } catch (std::exception& e) {
                currentDirListId = -1; // some file doesn't exist
            }
        }
    }

    void postInit() {
        refreshFiles();
    }

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
    static void menuSelected(void* ctx) {
        FileSourceModule* _this = (FileSourceModule*)ctx;
        core::setInputSampleRate(_this->sampleRate);
        if (_this->centerFreqSet) {
            tuner::tune(tuner::TUNER_MODE_IQ_ONLY, "", _this->centerFreq);
        }
        sigpath::iqFrontEnd.setBuffering(false);
        gui::waterfall.centerFrequencyLocked = true;
        //gui::freqSelect.minFreq = _this->centerFreq - (_this->sampleRate/2);
        //gui::freqSelect.maxFreq = _this->centerFreq + (_this->sampleRate/2);
        //gui::freqSelect.limitFreq = true;
        flog::info("FileSourceModule '{0}': Menu Select!", _this->name);
    }

    static void menuDeselected(void* ctx) {
        FileSourceModule* _this = (FileSourceModule*)ctx;
        sigpath::iqFrontEnd.setBuffering(true);
        //gui::freqSelect.limitFreq = false;
        gui::waterfall.centerFrequencyLocked = false;
        flog::info("FileSourceModule '{0}': Menu Deselect!", _this->name);
    }

    static void start(void* ctx) {
        FileSourceModule* _this = (FileSourceModule*)ctx;
        if (_this->running) { return; }
        if (_this->reader == NULL) { return; }
        _this->running = true;
        _this->workerThread = _this->float32Mode ? std::thread(floatWorker, _this) : std::thread(worker, _this);
        flog::info("FileSourceModule '{0}': Start!", _this->name);
    }

    static void stop(void* ctx) {
        FileSourceModule* _this = (FileSourceModule*)ctx;
        if (!_this->running) { return; }
        if (_this->reader == NULL) { return; }
        _this->stream.stopWriter();
        _this->workerThread.join();
        _this->stream.clearWriteStop();
        _this->running = false;
        _this->reader->rewind();
        flog::info("FileSourceModule '{0}': Stop!", _this->name);
    }

    static void tune(double freq, void* ctx) {
        FileSourceModule* _this = (FileSourceModule*)ctx;
        flog::info("FileSourceModule '{0}': Tune: {1}!", _this->name, freq);
    }

    static int isServer;

    static void menuHandler(void* ctx) {
        FileSourceModule* _this = (FileSourceModule*)ctx;
        if (isServer) {
            if (!_this->reader) {
                _this->openPathFromFileSelect();
            }
            SmGui::LeftLabel("File:");
            SmGui::FillWidth();
            if (_this->running) {
                SmGui::BeginDisabled();
            }
            if (SmGui::Combo("##sdrpp_srv_source_fname", &_this->currentDirListId, _this->currentDirList.txt)) {
                if (!_this->running) {
                    auto dp = _this->getDirectoryPath(_this->fileSelect.path);
                    // Save config
                    auto fullPath = dp + "/"+_this->currentDirList.value(_this->currentDirListId);
                    config.acquire();
                    config.conf["path"] = fullPath;
                    config.release(true);
                    try {
                        _this->openPath(fullPath);
                    }
                    catch (const std::exception& e) {
                        flog::error("Error: {}", e.what());
                    }
                }
            }
            if (_this->running) {
                SmGui::EndDisabled();
            }
            return;
        }

        if (_this->fileSelect.render("##file_source_" + _this->name)) {
            if (_this->fileSelect.pathIsValid()) {
                if (_this->reader != NULL) {
                    _this->reader->close();
                    delete _this->reader;
                }
                try {
                    _this->openPathFromFileSelect();
                }
                catch (const std::exception& e) {
                    flog::error("Error: {}", e.what());
                }
                config.acquire();
                config.conf["path"] = _this->fileSelect.path;
                config.release(true);
            }
        }

        if (_this->lastError.size() > 0) {
            SmGui::TextColored(ImVec4(1, 0, 0, 1), _this->lastError.c_str());
            _this->lastError = "";
        }

        if (!isServer) {
            long long int cst = sigpath::iqFrontEnd.getCurrentStreamTime();
            std::time_t t = cst /1000;
            auto tmm = std::localtime(&t);
            char streamTime[64];
            strftime(streamTime, sizeof(streamTime), "%Y-%m-%d %H:%M:%S", tmm);
            ImGui::Text("Stream pos: %s", streamTime);
            ImGui::Checkbox("Float32 Mode##_file_source", &_this->float32Mode);
        }
    }

    void openPath(const std::string &path) {
        try {
            lastError = "";
            reader = new wav::Reader(path);
            sampleRate = reader->getSampleRate();
            if (reader->getSampleRate() == 0) {
                reader->close();
                delete reader;
                reader = NULL;
                throw std::runtime_error("Sample rate may not be zero");
            }
            core::setInputSampleRate(sampleRate);
            std::string filename = getFileName(path);
            double newFrequency = getFrequency(filename);
            streamStartTime = getStartTime(filename);
            bool fineTune = gui::waterfall.containsFrequency(newFrequency);
            //                    auto prevFrequency = sigpath::vfoManager.getName();
            centerFreq = newFrequency;
            centerFreqSet = true;
            tuner::tune(tuner::TUNER_MODE_IQ_ONLY, "", centerFreq);
            if (isServer) {
                server::sendCenterFrequency(centerFreq);
            }
            if (fineTune) {
                // restore the fine tune. When working with file source and restarting the app, the fine tune is lost
                //                        tuner::tune(tuner::TUNER_MODE_NORMAL, "_current", prevFrequency);
            }

        }
        catch (std::exception &e) {
            lastError = e.what();
            flog::error("Error: {0}", e.what());
        }
    }

    void openPathFromFileSelect() {
        openPath(fileSelect.path);
    }

    long long streamStartTime = 0;

    static void worker(void* ctx) {
        FileSourceModule* _this = (FileSourceModule*)ctx;
        double sampleRate = std::max(_this->reader->getSampleRate(), (uint32_t)1);
        int blockSize = std::min((int)(sampleRate / 200.0f), (int)STREAM_BUFFER_SIZE);
        int16_t* inBuf = new int16_t[blockSize * 2];

        long long samplesRead = 0;
        sigpath::iqFrontEnd.setCurrentStreamTime(_this->streamStartTime);
        auto serverMode = core::args["server"].b();

        auto ctm = currentTimeMillis();
        while (true) {
            _this->reader->readSamples(inBuf, blockSize * 2 * sizeof(int16_t));
            volk_16i_s32f_convert_32f((float*)_this->stream.writeBuf, inBuf, 32768.0f, blockSize * 2);
            if (!_this->stream.swap(blockSize)) { break; };

            samplesRead += blockSize;
            if (_this->streamStartTime != 0) {
                long long currentTime = _this->streamStartTime + samplesRead * 1000 / sampleRate;
                sigpath::iqFrontEnd.setCurrentStreamTime(currentTime);
            }
            if (serverMode) {
                auto blockLength = (1000 * blockSize) / sampleRate;
                ctm += blockLength;
                auto now = currentTimeMillis();
                auto delay = ctm - now;
                if (delay > 0) {
                    usleep(delay * 1000);
                }
            }
        }

        delete[] inBuf;
    }

    static void floatWorker(void* ctx) {
        FileSourceModule* _this = (FileSourceModule*)ctx;
        double sampleRate = std::max(_this->reader->getSampleRate(), (uint32_t)1);
        int blockSize = std::min((int)(sampleRate / 200.0f), (int)STREAM_BUFFER_SIZE);
        dsp::complex_t* inBuf = new dsp::complex_t[blockSize];

        long long samplesRead = 0;
        sigpath::iqFrontEnd.setCurrentStreamTime(_this->streamStartTime);

        while (true) {
            _this->reader->readSamples(_this->stream.writeBuf, blockSize * sizeof(dsp::complex_t));
            if (!_this->stream.swap(blockSize)) { break; };

            samplesRead += blockSize;
            if (_this->streamStartTime != 0) {
                long long currentTime = _this->streamStartTime + samplesRead * 1000 / sampleRate;
                sigpath::iqFrontEnd.setCurrentStreamTime(currentTime);
            }
        }

        delete[] inBuf;
    }

    double getFrequency(std::string filename) {
        std::regex expr("[0-9]+Hz");
        std::smatch matches;
        std::regex_search(filename, matches, expr);
        if (matches.empty()) { return 0; }
        std::string freqStr = matches[0].str();
        return std::atof(freqStr.substr(0, freqStr.size() - 2).c_str());
    }

    long long getStartTime(std::string filename) {
        // like baseband_14235774Hz_12-19-14_10-07-2022.wav

        if (filename.substr(0, 8) != "baseband") return 0;
        if (".wav" != filename.substr(filename.size() - 4, 4)) return 0;
        auto pos = filename.find("Hz");
        if (pos == std::string::npos) return 0;
        std::string dateTimeStre = filename.substr(pos+3, 19);
        std::tm tm;
        memset(&tm, 0, sizeof(tm));
        char* end;
#ifdef _WIN32
        int n = sscanf(dateTimeStre.c_str(), "%d-%d-%d_%d-%d-%d", &tm.tm_hour, &tm.tm_min, &tm.tm_sec, &tm.tm_mday, &tm.tm_mon, &tm.tm_year);
        tm.tm_mon--;
        tm.tm_year-=1900;
        if (n == 6) {
            end = nullptr;
        }
        else {
            end = "X";
        }
#else
        end = strptime(dateTimeStre.c_str(), "%H-%M-%S_%d-%m-%Y", &tm);
#endif
        if (!end || *end == 0) {
            std::time_t t1 = std::mktime(&tm);
            if (t1 < 0) {
                return 0;
            }
                //            std::cout << std::asctime(&tm) << '\n';
            return ((long long)t1) * 1000;
        } else {
            return 0;
        }
    }

    FileSelect fileSelect;
    std::string name;
    dsp::stream<dsp::complex_t> stream;
    SourceManager::SourceHandler handler;
    wav::Reader* reader = NULL;
    bool running = false;
    bool enabled = true;
    float sampleRate = 1000000;
    std::thread workerThread;
    std::string lastError;
    OptionList<std::string, std::string> currentDirList;
    int currentDirListId = -1;

    double centerFreq = 100000000;
    bool centerFreqSet = false;

    bool float32Mode = false;
};

int FileSourceModule::isServer;

MOD_EXPORT void _INIT_() {
    json def = json({});
    def["path"] = "";
    config.setPath(std::string(core::getRoot()) + "/file_source_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT void* _CREATE_INSTANCE_(std::string name) {
    return new FileSourceModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (FileSourceModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
