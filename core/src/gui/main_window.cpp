#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include <implot/implot.h>
#include <gui/main_window.h>
#include <gui/gui.h>
#include "utils/usleep.h"
#include "utils/cty.h"
#include <stdio.h>
#include <thread>
#include <complex>
#include <gui/widgets/waterfall.h>
#include <gui/widgets/frequency_select.h>
#include <signal_path/iq_frontend.h>
#include <gui/icons.h>
#include <gui/widgets/bandplan.h>
#include <gui/style.h>
#include <config.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <gui/menus/source.h>
#include <gui/menus/display.h>
#include <gui/menus/bandplan.h>
#include <gui/menus/sink.h>
#include <gui/menus/vfo_color.h>
#include <gui/menus/module_manager.h>
#include <gui/menus/theme.h>
#include <gui/dialogs/credits.h>
#include <filesystem>
#include <signal_path/source.h>
#include <gui/dialogs/loading_screen.h>
#include <gui/colormaps.h>
#include <gui/widgets/snr_meter.h>
#include <gui/tuner.h>
#include <dsp/buffer/buffer.h>
#include <utils/wstr.h>
#include <utils/proto/http.h>
#include <utils/mpeg.h>
#include "brown/imgui-notify/imgui_notify.h"
#include "utils/proto/brown_ai.h"
#include "../../decoder_modules/radio/src/radio_interface.h"

static BrownAIClient brownAIClient;


void MainWindow::init() {
    LoadingScreen::show("Initializing UI");
    gui::waterfall.init();
    gui::waterfall.setRawFFTSize(fftSize);

    credits::init();

    core::configManager.acquire();
    json menuElements = core::configManager.conf["menuElements"];
    std::string modulesDir = core::configManager.conf["modulesDirectory"];
    std::string resourcesDir = core::configManager.conf["resourcesDirectory"];
    core::configManager.release();

    // Assert that directories are absolute
    modulesDir = std::filesystem::absolute(modulesDir).string();
    resourcesDir = std::filesystem::absolute(resourcesDir).string();

    dsp::buffer::runVerifier();

    // Load menu elements
    gui::menu.order.clear();
    for (auto &elem: menuElements) {
        if (!elem.contains("name")) {
            flog::error("Menu element is missing name key");
            continue;
        }
        if (!elem["name"].is_string()) {
            flog::error("Menu element name isn't a string");
            continue;
        }
        if (!elem.contains("open")) {
            flog::error("Menu element is missing open key");
            continue;
        }
        if (!elem["open"].is_boolean()) {
            flog::error("Menu element name isn't a string");
            continue;
        }
        Menu::MenuOption_t opt;
        opt.name = elem["name"];
        opt.open = elem["open"];
        gui::menu.order.push_back(opt);
    }

    gui::menu.registerEntry("Source", sourcemenu::draw, NULL);
    gui::menu.registerEntry("Sinks", sinkmenu::draw, NULL);
    gui::menu.registerEntry("Band Plan", bandplanmenu::draw, NULL);
    gui::menu.registerEntry("Display", displaymenu::draw, NULL);
    gui::menu.registerEntry("Theme", thememenu::draw, NULL);
    gui::menu.registerEntry("VFO Color", vfo_color_menu::draw, NULL);
    gui::menu.registerEntry("Module Manager", module_manager_menu::draw, NULL);

    gui::freqSelect.init();

    // Set default values for waterfall in case no source init's it
    gui::waterfall.setBandwidth(DEFAULT_SAMPLE_RATE);
    gui::waterfall.setViewBandwidth(DEFAULT_SAMPLE_RATE * gui::waterfall.getUsableSpectrumRatio());

    sigpath::iqFrontEnd.init(&dummyStream, DEFAULT_SAMPLE_RATE, true, 1, false, 1024, 20.0, IQFrontEnd::FFTWindow::NUTTALL, acquireFFTBuffer, releaseFFTBuffer, this);
    sigpath::iqFrontEnd.start();

    vfoCreatedHandler.handler = vfoAddedHandler;
    vfoCreatedHandler.ctx = this;
    sigpath::vfoManager.onVfoCreated.bindHandler(&vfoCreatedHandler);

    flog::info("Loading modules");

    // Load modules from /module directory
    if (std::filesystem::is_directory(modulesDir)) {
        for (const auto& file : std::filesystem::directory_iterator(modulesDir)) {
            std::string path = file.path().generic_string();
            if (file.path().extension().generic_string() != SDRPP_MOD_EXTENTSION) {
                continue;
            }
            if (!file.is_regular_file()) { continue; }
            flog::info("Loading {0}", path);
            LoadingScreen::show("Loading " + file.path().filename().string());
            core::moduleManager.loadModule(path);
        }
    }
    else {
        flog::warn("Module directory {0} does not exist, not loading modules from directory", modulesDir);
    }

    // Read module config
    core::configManager.acquire();
    std::vector<std::string> modules = core::configManager.conf["modules"];
    auto modList = core::configManager.conf["moduleInstances"].items();
    core::configManager.release();

    // Load additional modules specified through config
    for (auto const& path : modules) {
#ifndef __ANDROID__
        std::string apath = std::filesystem::absolute(path).string();
        flog::info("Loading {0}", apath);
        LoadingScreen::show("Loading " + std::filesystem::path(path).filename().string());
        core::moduleManager.loadModule(apath);
#else
        core::moduleManager.loadModule(path);
#endif
    }

    // Create module instances
    usleep(100000);
    for (auto const& [name, _module] : modList) {
        std::string mod = _module["module"];
        bool enabled = _module["enabled"];
        flog::info("Initializing {0} ({1})", name, mod);
        LoadingScreen::show("Initializing " + name + " (" + mod + ")");
        core::moduleManager.createInstance(name, mod);
        if (!enabled) { core::moduleManager.disableInstance(name); }
    }

    // Load color maps
    LoadingScreen::show("Loading color maps");
    flog::info("Loading color maps");
    if (std::filesystem::is_directory(resourcesDir + "/colormaps")) {
        for (const auto& file : std::filesystem::directory_iterator(resourcesDir + "/colormaps")) {
            std::string path = file.path().generic_string();
            LoadingScreen::show("Loading " + file.path().filename().string());
            flog::info("Loading {0}", path);
            if (file.path().extension().generic_string() != ".json") {
                continue;
            }
            if (!file.is_regular_file()) { continue; }
            colormaps::loadMap(path);
        }
    }
    else {
        flog::warn("Color map directory {0} does not exist, not loading modules from directory", modulesDir);
    }

    gui::waterfall.updatePalletteFromArray(colormaps::maps["Turbo"].map, colormaps::maps["Turbo"].entryCount);

    utils::loadAllCty();


    sourcemenu::init();
    sinkmenu::init();
    bandplanmenu::init();
    displaymenu::init();
    vfo_color_menu::init();
    module_manager_menu::init();

    // TODO for 0.2.5
    // Fix gain not updated on startup, soapysdr

    // Update UI settings
    LoadingScreen::show("Loading configuration");
    core::configManager.acquire();
    fftMin = core::configManager.conf["min"];
    fftMax = core::configManager.conf["max"];
    bw = core::configManager.conf["zoomBw"];
    gui::waterfall.setFFTMin(fftMin);
    gui::waterfall.setWaterfallMin(fftMin);
    gui::waterfall.setFFTMax(fftMax);
    gui::waterfall.setWaterfallMax(fftMax);


    double frequency = core::configManager.conf["frequency"];

    showMenu = core::configManager.conf["showMenu"];
    startedWithMenuClosed = !showMenu;

    gui::freqSelect.setFrequency(frequency);
    gui::freqSelect.frequencyChanged = false;
    sigpath::sourceManager.tune(frequency);
    gui::waterfall.setCenterFrequency(frequency);
    gui::waterfall.vfoFreqChanged = false;
    gui::waterfall.centerFreqMoved = false;
    gui::waterfall.selectFirstVFO();

    menuWidth = core::configManager.conf["menuWidth"];
    newWidth = menuWidth;

    fftHeight = core::configManager.conf["fftHeight"];
    gui::waterfall.setFFTHeight(fftHeight);

    tuningMode = core::configManager.conf["centerTuning"] ? tuner::TUNER_MODE_CENTER : tuner::TUNER_MODE_NORMAL;
    gui::waterfall.VFOMoveSingleClick = (tuningMode == tuner::TUNER_MODE_CENTER);

    core::configManager.release();


    // Correct the offset of all VFOs so that they fit on the screen
    float finalBwHalf = gui::waterfall.getBandwidth() / 2.0;
    for (auto& [_name, _vfo] : gui::waterfall.vfos) {
        if (_vfo->lowerOffset < -finalBwHalf) {
            sigpath::vfoManager.setCenterOffset(_name, (_vfo->bandwidth / 2) - finalBwHalf);
            continue;
        }
        if (_vfo->upperOffset > finalBwHalf) {
            sigpath::vfoManager.setCenterOffset(_name, finalBwHalf - (_vfo->bandwidth / 2));
            continue;
        }
    }

    updateWaterfallZoomBandwidth(bw);

    autostart = core::args["autostart"].b();
    initComplete = true;

    core::moduleManager.doPostInitAll();




}

float* MainWindow::acquireFFTBuffer(void* ctx) {
    return gui::waterfall.getFFTBuffer();
}

void MainWindow::releaseFFTBuffer(void* ctx) {
    gui::waterfall.pushFFT();
}

void MainWindow::vfoAddedHandler(VFOManager::VFO* vfo, void* ctx) {
    MainWindow* _this = (MainWindow*)ctx;
    std::string name = vfo->getName();
    core::configManager.acquire();
    if (!core::configManager.conf["vfoOffsets"].contains(name)) {
        core::configManager.release();
        return;
    }
    double offset = core::configManager.conf["vfoOffsets"][name];
    core::configManager.release();

    double viewBW = gui::waterfall.getViewBandwidth();
    double viewOffset = gui::waterfall.getViewOffset();

    double viewLower = viewOffset - (viewBW / 2.0);
    double viewUpper = viewOffset + (viewBW / 2.0);

    double newOffset = std::clamp<double>(offset, viewLower, viewUpper);

    sigpath::vfoManager.setCenterOffset(name, _this->initComplete ? newOffset : offset);
}

void MainWindow::preDraw(ImGui::WaterfallVFO**vfo) {

    *vfo = NULL;
    if (gui::waterfall.selectedVFO != "") {
        *vfo = gui::waterfall.vfos[gui::waterfall.selectedVFO];
    }

    // Handle VFO movement
    if (*vfo != NULL) {
        if ((*vfo)->centerOffsetChanged) {
            if (tuningMode == tuner::TUNER_MODE_CENTER) {
                tuner::tune(tuner::TUNER_MODE_CENTER, gui::waterfall.selectedVFO, gui::waterfall.getCenterFrequency() + (*vfo)->generalOffset);
            }
            gui::freqSelect.setFrequency(gui::waterfall.getCenterFrequency() + (*vfo)->generalOffset);
            gui::freqSelect.frequencyChanged = false;

            core::configManager.acquire();
            core::configManager.conf["vfoOffsets"][gui::waterfall.selectedVFO] = (*vfo)->generalOffset;
            core::configManager.release(true);
        }
    }

    sigpath::vfoManager.updateFromWaterfall(&gui::waterfall);

    lockWaterfallControls = false;
    // Handle selection of another VFO
    if (gui::waterfall.selectedVFOChanged) {
        gui::freqSelect.setFrequency((*vfo != NULL) ? ((*vfo)->generalOffset + gui::waterfall.getCenterFrequency()) : gui::waterfall.getCenterFrequency());
        gui::waterfall.selectedVFOChanged = false;
        gui::freqSelect.frequencyChanged = false;
    }

    // Handle change in selected frequency
    if (gui::freqSelect.frequencyChanged) {
        gui::freqSelect.frequencyChanged = false;
        tuner::tune(tuningMode, gui::waterfall.selectedVFO, gui::freqSelect.frequency);
        if (*vfo != NULL) {
            (*vfo)->centerOffsetChanged = false;
            (*vfo)->lowerOffsetChanged = false;
            (*vfo)->upperOffsetChanged = false;
        }
        core::configManager.acquire();
        core::configManager.conf["frequency"] = gui::waterfall.getCenterFrequency();
        if (*vfo != NULL) {
            core::configManager.conf["vfoOffsets"][gui::waterfall.selectedVFO] = (*vfo)->generalOffset;
        }
        core::configManager.release(true);
    }

    // Handle dragging the uency scale
    if (gui::waterfall.centerFreqMoved) {
        gui::waterfall.centerFreqMoved = false;
        sigpath::sourceManager.tune(gui::waterfall.getCenterFrequency());
        if (*vfo != NULL) {
            gui::freqSelect.setFrequency(gui::waterfall.getCenterFrequency() + (*vfo)->generalOffset);
        }
        else {
            gui::freqSelect.setFrequency(gui::waterfall.getCenterFrequency());
        }
        core::configManager.acquire();
        core::configManager.conf["frequency"] = gui::waterfall.getCenterFrequency();
        core::configManager.release(true);
    }

    int _fftHeight = gui::waterfall.getFFTHeight();
    if (fftHeight != _fftHeight) {
        fftHeight = _fftHeight;
        core::configManager.acquire();
        core::configManager.conf["fftHeight"] = fftHeight;
        core::configManager.release(true);
    }
}

void MainWindow::drawUpperLine(ImGui::WaterfallVFO* vfo) {
    ImVec2 btnSize(30 * style::uiScale, 30 * style::uiScale);
    ImVec4 textCol = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    bool tmpPlaySate = playing;
    if (playButtonLocked && !tmpPlaySate) { style::beginDisabled(); }
    if (playing) {
        ImGui::PushID(ImGui::GetID("sdrpp_stop_btn"));
        if (ImGui::ImageButton(icons::STOP, btnSize, ImVec2(0, 0), ImVec2(1, 1), 5, ImVec4(0, 0, 0, 0), textCol) || ImGui::IsKeyPressed(ImGuiKey_End, false)) {
            setPlayState(false);
        }
        ImGui::PopID();
    }
    else { // TODO: Might need to check if there even is a device
        ImGui::PushID(ImGui::GetID("sdrpp_play_btn"));
        if (ImGui::ImageButton(icons::PLAY, btnSize, ImVec2(0, 0), ImVec2(1, 1), 5, ImVec4(0, 0, 0, 0), textCol) || ImGui::IsKeyPressed(ImGuiKey_End, false)) {
            if (!playButtonLocked) {
                setPlayState(true);
            }
        }
        ImGui::PopID();
    }
    if (playButtonLocked && !tmpPlaySate) { style::endDisabled(); }

    // Handle auto-start
    if (autostart) {
        autostart = false;
        setPlayState(true);
    }

    ImGui::SameLine();
    float origY = ImGui::GetCursorPosY();

    sigpath::sinkManager.showVolumeSlider(gui::waterfall.selectedVFO, "##_sdrpp_main_volume_", 248 * style::uiScale, btnSize.x, 5, true);

    ImGui::SameLine();

    ImGui::SetCursorPosY(origY);
    gui::freqSelect.draw();

    ImGui::SameLine();

    ImGui::SetCursorPosY(origY);
    if (tuningMode == tuner::TUNER_MODE_CENTER) {
        ImGui::PushID(ImGui::GetID("sdrpp_ena_st_btn"));
        if (ImGui::ImageButton(icons::CENTER_TUNING, btnSize, ImVec2(0, 0), ImVec2(1, 1), 5, ImVec4(0, 0, 0, 0), textCol)) {
            tuningMode = tuner::TUNER_MODE_NORMAL;
            gui::waterfall.VFOMoveSingleClick = false;
            core::configManager.acquire();
            core::configManager.conf["centerTuning"] = false;
            core::configManager.release(true);
        }
        ImGui::PopID();
    }
    else { // TODO: Might need to check if there even is a device
        ImGui::PushID(ImGui::GetID("sdrpp_dis_st_btn"));
        if (ImGui::ImageButton(icons::NORMAL_TUNING, btnSize, ImVec2(0, 0), ImVec2(1, 1), 5, ImVec4(0, 0, 0, 0), textCol)) {
            tuningMode = tuner::TUNER_MODE_CENTER;
            gui::waterfall.VFOMoveSingleClick = true;
            tuner::tune(tuner::TUNER_MODE_CENTER, gui::waterfall.selectedVFO, gui::freqSelect.frequency);
            core::configManager.acquire();
            core::configManager.conf["centerTuning"] = true;
            core::configManager.release(true);
        }
        ImGui::PopID();
    }

    ImGui::SameLine();

    int snrOffset = 87.0f * style::uiScale;
    int snrWidth = std::clamp<int>(ImGui::GetWindowSize().x - ImGui::GetCursorPosX() - snrOffset, 100.0f * style::uiScale, 300.0f * style::uiScale);
    int snrPos = std::max<int>(ImGui::GetWindowSize().x - (snrWidth + snrOffset), ImGui::GetCursorPosX());

    ImGui::SetCursorPosX(snrPos);
    ImGui::SetCursorPosY(origY + (5.0f * style::uiScale));
    ImGui::SetNextItemWidth(snrWidth);
    ImGui::SNRMeter((vfo != NULL) ? gui::waterfall.selectedVFOSNR : 0);

}

long long lastDrawTime = 0;
long long lastDrawTimeBackend = 0;
int glSleepTime = 0;

void MainWindow::ShowLogWindow() {
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize * 0.8, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Log Console", &gui::mainWindow.logWindow))
    {
        ImGui::End();
        return;
    }

    // As a specific feature guaranteed by the library, after calling Begin() the last Item represent the title bar.
    // So e.g. IsItemHovered() will return true when hovering the title bar.
    // Here we create a context menu only available from the title bar.
    if (ImGui::BeginPopupContextItem())
    {
        if (ImGui::MenuItem("Close Console"))
            gui::mainWindow.logWindow = false;
        ImGui::EndPopup();
    }



    // Reserve enough left-over height for 1 separator + 1 input text
    const float footer_height_to_reserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), false, ImGuiWindowFlags_HorizontalScrollbar);
    if (ImGui::BeginPopupContextWindow())
    {
        if (ImGui::Selectable("Clear")) {
            flog::outMtx.lock();
            flog::logRecords.clear();
            flog::outMtx.unlock();
        }
        ImGui::EndPopup();
    }

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1)); // Tighten spacing
    flog::outMtx.lock();
    for (int i = 0; i < flog::logRecords.size(); i++)
    {
        auto item = flog::logRecords[i];
        // Normally you would store more information in your item than just a string.
        // (e.g. make Items[] an array of structure, store color/type etc.)
        ImVec4 color;
        bool has_color = false;
        if (item.typ == flog::TYPE_ERROR)          { color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f); has_color = true; }
        else if (item.typ == flog::TYPE_WARNING) { color = ImVec4(1.0f, 0.8f, 0.6f, 1.0f); has_color = true; }
        if (has_color)
            ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(item.message.c_str());
        if (has_color)
            ImGui::PopStyleColor();
    }

    flog::outMtx.unlock();
    ImGui::SetScrollHereY(1.0f);

    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::Separator();

    ImGui::End();
}

void initBrownAIClient(std::function<void(const std::string&, const std::string&)> callback) {
    static bool initialized = false;
    if (initialized) return;
    
    brownAIClient.init("brownai.san.systems:8080"); // Change this to your Brown AI server address
    brownAIClient.onCommandReceived = [callback](const std::string& command) {
        // Process the command received from Brown AI server
        callback("", command);
    };
    brownAIClient.start();
    initialized = true;
}

void MainWindow::displayVariousWindows() {
    if (demoWindow) {
        lockWaterfallControls = true;
        ImGui::ShowDemoWindow();
    }
    if (logWindow) {
        lockWaterfallControls = true;
        ShowLogWindow();
    }
    if (sigpath::iqFrontEnd.detectorPreprocessor.isEnabled()) {
        auto &toPlot = sigpath::iqFrontEnd.detectorPreprocessor.sigs_smoothed;
        if (!toPlot.empty()) {
            ImGui::SetNextWindowSize(ImVec2(1800, 300), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Signal Detector Output")) {
                if (ImPlot::BeginPlot("##DetectorPlot", ImVec2(-1, -1))) {
                    ImPlot::SetupAxes("Frequency Bin", "Smoothed Value");
                    ImPlot::PlotLine("Smoothed Signal", toPlot.data(), toPlot.size());
                    ImPlot::EndPlot();
                }
            }
            ImGui::End();
        }
    }
    if (showCredits) {
        lockWaterfallControls = true;
    }
}


void MainWindow::draw() {
    auto ctm = currentTimeNanos();
    ImGui::WaterfallVFO* vfo;
    this->preDraw(&vfo);
    ImGui::Begin("Main", NULL, WINDOW_FLAGS);
    ImVec4 textCol = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    // To Bar
    // ImGui::BeginChild("TopBarChild", ImVec2(0, 49.0f * style::uiScale), false, ImGuiWindowFlags_HorizontalScrollbar);
    ImVec2 btnSize(30 * style::uiScale, 30 * style::uiScale);
    ImGui::PushID(ImGui::GetID("sdrpp_menu_btn"));
    if (ImGui::ImageButton(icons::MENU, btnSize, ImVec2(0, 0), ImVec2(1, 1), 5, ImVec4(0, 0, 0, 0), textCol) || ImGui::IsKeyPressed(ImGuiKey_Menu, false)) {
        showMenu = !showMenu;
        core::configManager.acquire();
        core::configManager.conf["showMenu"] = showMenu;
        core::configManager.release(true);
    }
    ImGui::PopID();

    // Handle space key for microphone input
    static bool wasSpacePressed = false;
    static std::vector<dsp::stereo_t> micSamples;
    
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)  && sigpath::sinkManager.defaultInputAudio.hasInput()) {
        spacePressed = true;
        if (!wasSpacePressed) {
            sigpath::sinkManager.setAllMuted(true);
            // Start recording
            micSamples.clear();
            micStream.clearReadStop();
            micStream.clearWriteStop();
            sigpath::sinkManager.defaultInputAudio.bindStream(&micStream);
            wasSpacePressed = true;

            // Start mic stream thread if not already running
            if (!micThread) {
                micThreadRunning = true;
                micThread = std::make_shared<std::thread>([this]() {
                    SetThreadName("MicStreamReader");
                    while (micThreadRunning) {
                        int rd = micStream.read();
                        if (rd > 0) {
                            std::lock_guard<std::mutex> lck(micSamplesMutex);
                            micSamples.insert(micSamples.end(), micStream.readBuf, micStream.readBuf + rd);
                            micStream.flush();
                        }
                        else if (rd < 0) {
                            break;
                        }
                    }
                });
            }
            initBrownAIClient([this](const std::string& whisperResult, const std::string& result) {
                try {
                    // Parse JSON response
                    auto json = nlohmann::json::parse(result);

                    // Check for error
                    if (json.contains("error") && !json["error"].empty()) {
                        flog::error("Brown AI error: {}", json["error"].get<std::string>());
                        ImGui::InsertNotification({ImGuiToastType_Error, 5000, "Brown AI error: %s", json["error"].get<std::string>().c_str()});
                        return;
                    }

                    // Extract command if present
                    std::string command = json.value("command", "");
                    if (command.empty()) {
                        flog::warn("Empty command received from Brown AI");
                        ImGui::InsertNotification({ImGuiToastType_Warning, 5000, "Empty command received"});
                        return;
                    }

                    // Process the command
                    flog::info("Response from Brown AI: {}", command);
                    this->performDetectedLLMAction(whisperResult, command);
                } catch (const std::exception& e) {
                    flog::error("Failed to parse Brown AI response: {}", e.what());
                    ImGui::InsertNotification({ImGuiToastType_Error, 5000, "Failed to parse Brown AI response"});
                }
            });
        }
    }

    mainThreadTasksMutex.lock();
    for(auto& task : mainThreadTasks) {
        task();
    }
    mainThreadTasks.clear();
    mainThreadTasksMutex.unlock();

    if (ImGui::IsKeyReleased(ImGuiKey_Space)) {
        spacePressed = false;
        if (wasSpacePressed) {
            flog::info("Space key released");
            sigpath::sinkManager.setAllMuted(false);
            // Stop recording
            sigpath::sinkManager.defaultInputAudio.unbindStream(&micStream);
            wasSpacePressed = false;
            
            // Stop mic stream thread
            if (micThread) {
                micThreadRunning = false;
                micStream.stopReader();
                if (micThread->joinable()) {
                    micThread->join();
                }
                micThread.reset();
            }
            
            // micSamples now contains all recorded audio
            flog::info("Recorded {} microphone samples", (int)micSamples.size());
            auto [speechFile, extension] = mpeg::produce_speech_file_for_whisper(micSamples, 48000);
            flog::info("Compressed to {} bytes as {}", (int)speechFile.size(), extension);
            brownAIClient.sendAudioData(speechFile);

        }
    }

    ImGui::SameLine();

    this->drawUpperLine(vfo);
    // Note: this is what makes the vertical size correct, needs to be fixed
    ImGui::SameLine();

    // ImGui::EndChild();

    // Logo button
    ImGui::SetCursorPosX(ImGui::GetWindowSize().x - (48 * style::uiScale));
    ImGui::SetCursorPosY(10.0f * style::uiScale);
    if (ImGui::ImageButton(icons::LOGO, ImVec2(32 * style::uiScale, 32 * style::uiScale), ImVec2(0, 0), ImVec2(1, 1), 0)) {
        showCredits = true;
    }
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (showCredits) {
            showCredits = false;
            lockWaterfallControls = true;
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        showCredits = false;
    }

    // Reset waterfall lock
    lockWaterfallControls = showCredits;

    // Handle menu resize
    ImVec2 winSize = ImGui::GetWindowSize();
    ImVec2 mousePos = ImGui::GetMousePos();
    if (!lockWaterfallControls && showMenu &&  ImGui::GetTopMostPopupModal() == NULL) {
        float curY = ImGui::GetCursorPosY();
        bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        if (grabbingMenu) {
            newWidth = mousePos.x;
            newWidth = std::clamp<float>(newWidth, 250, winSize.x - 250);
            ImGui::GetForegroundDrawList()->AddLine(ImVec2(newWidth, curY), ImVec2(newWidth, winSize.y - 10), ImGui::GetColorU32(ImGuiCol_SeparatorActive));
        }
        if (mousePos.x >= newWidth - (2.0f * style::uiScale) && mousePos.x <= newWidth + (2.0f * style::uiScale) && mousePos.y > curY) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (click) {
                grabbingMenu = true;
            }
        }
        else {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
        }
        if (!down && grabbingMenu) {
            grabbingMenu = false;
            menuWidth = newWidth;
            core::configManager.acquire();
            core::configManager.conf["menuWidth"] = menuWidth;
            core::configManager.release(true);
        }
    }

    // Process menu keybinds
    displaymenu::checkKeybinds();

    // Left Column
    if (showMenu) {
        ImGui::Columns(3, "WindowColumns", false);
        ImGui::SetColumnWidth(0, menuWidth);
        ImGui::SetColumnWidth(1, std::max<int>(winSize.x - menuWidth - (60.0f * style::uiScale), 100.0f * style::uiScale));
        ImGui::SetColumnWidth(2, 60.0f * style::uiScale);

        ImGui::BeginChild("Left Column");


        if (gui::menu.draw(firstMenuRender)) {
            core::configManager.acquire();
            json arr = json::array();
            for (int i = 0; i < gui::menu.order.size(); i++) {
                arr[i]["name"] = gui::menu.order[i].name;
                arr[i]["open"] = gui::menu.order[i].open;
            }
            core::configManager.conf["menuElements"] = arr;

            // Update enabled and disabled modules
            for (auto [_name, inst] : core::moduleManager.instances) {
                if (!core::configManager.conf["moduleInstances"].contains(_name)) { continue; }
                core::configManager.conf["moduleInstances"][_name]["enabled"] = inst.instance->isEnabled();
            }

            core::configManager.release(true);
        }
        if (startedWithMenuClosed) {
            startedWithMenuClosed = false;
        }
        else {
            firstMenuRender = false;
        }


        this->drawDebugMenu();

        ImGui::EndChild();


    }
    else {
        // When hiding the menu bar
        ImGui::Columns(3, "WindowColumns", false);
        ImGui::SetColumnWidth(0, 8 * style::uiScale);
        ImGui::SetColumnWidth(1, winSize.x - ((8 + 60) * style::uiScale));
        ImGui::SetColumnWidth(2, 60.0f * style::uiScale);
    }

    // Right Column
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::NextColumn();
    ImGui::PopStyleVar();

    this->displayVariousWindows();


    ImVec2 wfSize = ImVec2(0, 0);
    if (!bottomWindows.empty()) {
        wfSize.y -= bottomWindows[0].size.y;
    }
    ImGui::BeginChild("Waterfall", wfSize);

    gui::waterfall.draw();
    onWaterfallDrawn.emit(GImGui);

    ImGui::EndChild();

    this->handleWaterfallInput(vfo);

    ImGui::NextColumn();
    ImGui::BeginChild("WaterfallControls");
#ifdef __ANDROID__
    if (displaymenu::showBattery) {
        displaymenu::currentBatteryLevel = backend::getBatteryLevel();
        ImGui::Text("B%s%%", displaymenu::currentBatteryLevel.c_str());
    }
#endif

    static int sliderDynamicAdjustmentY = 0;

    ImVec2 wfSliderSize((displaymenu::phoneLayout ? 40.0 : 20.0) * style::uiScale, (displaymenu::phoneLayout ? 100.0 : 140.0) * style::uiScale - sliderDynamicAdjustmentY);
    const int MIN_SLIDER_HEIGHT = 10;
    if (wfSliderSize.y < MIN_SLIDER_HEIGHT) {
        wfSliderSize.y = MIN_SLIDER_HEIGHT;    // Prevents dynamic adjustment too large;
    }
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2.0) - (ImGui::CalcTextSize("Zoom").x / 2.0));
    ImGui::TextUnformatted("Zoom");
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2.0) - wfSliderSize.x/2);

    if (ImGui::VSliderFloat("##_7_", wfSliderSize, &bw, 1.0, 0.0, "")) {
        core::configManager.acquire();
        core::configManager.conf["zoomBw"] = bw;
        core::configManager.release(true);
        updateWaterfallZoomBandwidth(bw);
    }


    auto addMaxSlider = [&]() {
        ImGui::NewLine();
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2.0) - (ImGui::CalcTextSize("Max").x / 2.0));
        ImGui::TextUnformatted("Max");
        ImGui::SameLine();
        ImVec2 textSize = ImGui::CalcTextSize("Max");
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2.0) - (ImGui::CalcTextSize("Max").x / 2.0));
        if (ImGui::InvisibleButton("##max_button_auto", textSize)) {
            const std::pair<int, int>& range = gui::waterfall.autoRange();
            if (range.first == 0 && range.second == 0) {
                // bad case

            } else {
                fftMin = range.first;
                fftMax = range.second;
            }
        }
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2.0) - wfSliderSize.x / 2);
        if (ImGui::VSliderFloat("##_8_", wfSliderSize, &fftMax, 0.0, -180.0f, "")) {
            fftMax = std::max<float>(fftMax, fftMin + 10);
            core::configManager.acquire();
            core::configManager.conf["max"] = fftMax;
            core::configManager.release(true);
            gui::waterfall.setFFTMax(fftMax);
            gui::waterfall.setWaterfallMax(fftMax);
        }

    };

    auto addMinSlider = [&]() {
        ImGui::NewLine();
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2.0) - (ImGui::CalcTextSize("Min").x / 2.0));
        ImGui::TextUnformatted("Min");
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2.0) - wfSliderSize.x / 2);
        ImGui::SetItemUsingMouseWheel();
        if (ImGui::VSliderFloat("##_9_", wfSliderSize, &fftMin, 0.0, -200.0f, "")) {
            fftMin = std::min<float>(fftMax - 10, fftMin);
            core::configManager.acquire();
            core::configManager.conf["min"] = fftMin;
            core::configManager.release(true);
            gui::waterfall.setFFTMin(fftMin);
            gui::waterfall.setWaterfallMin(fftMin);
        }
    };

    if (displaymenu::phoneLayout) {
        // min slider is used much more often, if you ask me. So on small screen there should be no need to scroll down to operate it.
        addMinSlider();
        addMaxSlider();
    } else {
        addMaxSlider();
        addMinSlider();
    }
    const ImVec2 remainder = ImGui::GetContentRegionAvail();
    if (remainder.y <= 0 && wfSliderSize.y > MIN_SLIDER_HEIGHT) {
        // this fits the sliders to any height.
        sliderDynamicAdjustmentY++;
    }

    ImGui::EndChild();

    this->drawBottomWindows(0);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(43.f / 255.f, 43.f / 255.f, 43.f / 255.f, 100.f / 255.f));
    ImGui::PushFont(style::notificationFont);
    ImGui::RenderNotifications();
    ImGui::PopFont();
    ImGui::PopStyleVar(1); // Don't forget to Pop()
    ImGui::PopStyleColor(1);


    ImGui::End();

    if (showCredits) {
        credits::show();
    }

    // Draw listening popup if space is pressed
    if (spacePressed) {
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), 
                               ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowBgAlpha(0.7f);
        if (ImGui::Begin("##ListeningPopup", nullptr, 
                        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | 
                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | 
                        ImGuiWindowFlags_NoNav)) {
            ImGui::Text("Listening... Please give your command...");
            ImGui::End();
        }
    }

    lastDrawTime = (currentTimeNanos() - ctm)/1000;

}

void MainWindow::setPlayState(bool _playing) {
    if (_playing == playing) { return; }
    if (_playing) {
        sigpath::iqFrontEnd.flushInputBuffer();
        sigpath::sourceManager.start();
        sigpath::sourceManager.tune(gui::waterfall.getCenterFrequency());
        playing = true;
        onPlayStateChange.emit(true);
    }
    else {
        playing = false;
        onPlayStateChange.emit(false);
        sigpath::sourceManager.stop();
        sigpath::iqFrontEnd.flushInputBuffer();
    }
}

bool MainWindow::sdrIsRunning() {
    return playing;
}



int hzRange(double freq) {
    return round(log10(freq)/3);
}

void MainWindow::performDetectedLLMAction(const std::string &whisperResult, std::string command) {
    std::string detectedCommand;
    flog::info("Whisper result: {}", whisperResult);
    flog::info("Detected command: {}", detectedCommand);

    if (command.empty()) {
        return;
    }
    if (command[0] == ':') { // just lazy
        command = "COMMAND"+command;
    }

    // Split command into lines
    std::istringstream stream(command);
    std::string line;
    std::string lastCaps;
    while (std::getline(stream, line)) {
        // Find last line containing "COMMAND"
        size_t pos = line.find("COMMAND");
        if (pos != std::string::npos) {
            // Extract everything after "COMMAND"
            detectedCommand = line.substr(pos + 8); // and colon
        }
        // check, if line is all capitals, assitn to lastCaps
        bool allCaps = true;
        for (char c : line) {
            if (!std::isupper(c) && std::isalpha(c)) {
                allCaps = false;
                break;
            }
        }
        if (allCaps) {
            lastCaps = line;
        }
    }

    if (detectedCommand.empty() && lastCaps.empty()) {
        flog::info("Invalid LLM reply, no command: {}", command);
        ImGui::InsertNotification({ImGuiToastType_Info, 5000, "Invalid LLM reply, no command"});
    }

    if (!detectedCommand.empty()) {
        detectedCommand.erase(0, detectedCommand.find_first_not_of(' '));
        detectedCommand.erase(detectedCommand.find_last_not_of(' ') + 1);
    } else {
        detectedCommand = lastCaps;
    }
    // Trim leading/trailing whitespace


    // Split into parts by spaces
    std::vector<std::string> parts;
    splitStringV(detectedCommand," ", parts);
    while(parts.size()) {
        if (parts[0] == "") {
            parts.erase(parts.begin(), parts.begin()+1);
        } else {
            break;
        }
    }

    if (parts[0] == "VOLUME_UP") {
        if (sigpath::sinkManager.recentStreeam) {
            auto volume = sigpath::sinkManager.recentStreeam->getVolume();
            volume += 0.2;
            if (volume > 1.0) {
                volume = 1.0;
            }
            sigpath::sinkManager.recentStreeam->setVolume(volume);
        }
    }
    if (parts[0] == "VOLUME_DOWN") {
        if (sigpath::sinkManager.recentStreeam) {
            auto volume = sigpath::sinkManager.recentStreeam->getVolume();
            volume -= 0.2;
            if (volume <= 0) {
                volume = 0;
            }
            sigpath::sinkManager.recentStreeam->setVolume(volume);
        }
    }
        auto vfo = gui::waterfall.selectedVFO;
    if (parts[0] == "USB") {
        auto mode = RADIO_IFACE_MODE_USB;
        core::modComManager.callInterface(vfo, RADIO_IFACE_CMD_SET_MODE, &mode, NULL);
    }
    if (parts[0] == "LSB") {
        auto mode = RADIO_IFACE_MODE_LSB;
        core::modComManager.callInterface(vfo, RADIO_IFACE_CMD_SET_MODE, &mode, NULL);
    }
    if (parts[0] == "AM") {
        auto mode = RADIO_IFACE_MODE_AM;
        core::modComManager.callInterface(vfo, RADIO_IFACE_CMD_SET_MODE, &mode, NULL);
    }
    if (parts[0] == "FM") {
        auto mode = RADIO_IFACE_MODE_NFM;
        core::modComManager.callInterface(vfo, RADIO_IFACE_CMD_SET_MODE, &mode, NULL);
    }
    if (parts[0] == "STOP") {
        setPlayState(false);
    }
    if (parts[0] == "START") {
        setPlayState(true);
    }
    if (parts[0] == "MUTE") {
        if (sigpath::sinkManager.recentStreeam) {
            sigpath::sinkManager.recentStreeam->volumeAjust.setMuted(!sigpath::sinkManager.recentStreeam->volumeAjust.getMuted());
        }
    }
    if (parts[0] == "FREQ") {
        float freq = atof(parts[1].c_str());
        float mult = 1;
        if (parts.size() > 2) {
            if (parts[2][0] == 'K' || parts[2][0]=='k') {
                mult = 1e3;
            }
            if (parts[2][0] == 'M' || parts[2][0]=='m') {
                mult = 1e6;
            }
        }
        float currentFrequency = gui::waterfall.getCenterFrequency();
        double intPartF;
        float frac = std::modf((double)freq, &intPartF);
        long long intPart = (long long)intPartF;
        double newFreq = 0;
        if (frac != 0 && mult == 1) {  // e.g 1.7
            mult = 1e6; // mhz
            newFreq = mult * freq;   // make 1.7 mhz
        } else if (mult * freq < 100000 && mult == 1e3) { // 1200 khz spoken as 1.200 khz
                mult = 1e6;
                newFreq = mult * freq;
        } else if (mult == 1e6 || mult == 1e3) { // exact mhz freq
            newFreq = mult * freq;
        } else {
            while (hzRange(freq) < hzRange(currentFrequency)) {
                freq *= 1000;
            }
            while (hzRange(freq) > hzRange(currentFrequency)) {
                freq /= 1000;
            }
            newFreq = freq;
        }
        tuner::tune(tuningMode, gui::waterfall.selectedVFO, newFreq);
    }

    ImGui::InsertNotification({ImGuiToastType_Info, 5000, "Voice command: %s", detectedCommand.c_str()});
}

bool MainWindow::isPlaying() {
    return playing;
}

void MainWindow::updateWaterfallZoomBandwidth(float bw) {
    ImGui::WaterfallVFO* vfo = NULL;
    if (gui::waterfall.selectedVFO != "") {
        vfo = gui::waterfall.vfos[gui::waterfall.selectedVFO];
    }
    double factor = (double)bw * (double)bw;

    // Map 0.0 -> 1.0 to 1000.0 -> bandwidth
    double wfBw = gui::waterfall.getBandwidth();
    double delta = wfBw - 1000.0;
    double finalBw = std::min<double>(1000.0 + (factor * delta), wfBw);

    gui::waterfall.setViewBandwidth(finalBw * gui::waterfall.getUsableSpectrumRatio());
    if (vfo != NULL) {
        gui::waterfall.setViewOffset(vfo->centerOffset); // center vfo on screen
    }
}

void MainWindow::handleWaterfallInput(ImGui::WaterfallVFO* vfo) {
    if (!lockWaterfallControls && ImGui::GetTopMostPopupModal() == NULL) {
        // Handle arrow keys
        if (vfo != NULL && (gui::waterfall.mouseInFFT || gui::waterfall.mouseInWaterfall)) {
            bool freqChanged = false;
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && !gui::freqSelect.digitHovered) {
                double nfreq = gui::waterfall.getCenterFrequency() + vfo->generalOffset - vfo->snapInterval;
                nfreq = roundl(nfreq / vfo->snapInterval) * vfo->snapInterval;
                tuner::tune(tuningMode, gui::waterfall.selectedVFO, nfreq);
                freqChanged = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && !gui::freqSelect.digitHovered) {
                double nfreq = gui::waterfall.getCenterFrequency() + vfo->generalOffset + vfo->snapInterval;
                nfreq = roundl(nfreq / vfo->snapInterval) * vfo->snapInterval;
                tuner::tune(tuningMode, gui::waterfall.selectedVFO, nfreq);
                freqChanged = true;
            }
            if (freqChanged) {
                core::configManager.acquire();
                core::configManager.conf["frequency"] = gui::waterfall.getCenterFrequency();
                if (vfo != NULL) {
                    core::configManager.conf["vfoOffsets"][gui::waterfall.selectedVFO] = vfo->generalOffset;
                }
                core::configManager.release(true);
            }
        }

        // Handle scrollwheel
        int wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0 && (gui::waterfall.mouseInFFT || gui::waterfall.mouseInWaterfall)) {
            double nfreq;
            if (vfo != NULL) {
                // Select factor depending on modifier keys
                double interval;
                if (ImGui::IsKeyDown(ImGuiKey_LeftShift)) {
                    interval = vfo->snapInterval * 10.0;
                }
                else if (ImGui::IsKeyDown(ImGuiKey_LeftAlt)) {
                    interval = vfo->snapInterval * 0.1;
                }
                else {
                    interval = vfo->snapInterval;
                }

                nfreq = gui::waterfall.getCenterFrequency() + vfo->generalOffset + (interval * wheel);
                nfreq = roundl(nfreq / interval) * interval;
            }
            else {
                nfreq = gui::waterfall.getCenterFrequency() - (gui::waterfall.getViewBandwidth() * wheel / 20.0);
            }
            tuner::tune(tuningMode, gui::waterfall.selectedVFO, nfreq);
            gui::freqSelect.setFrequency(nfreq);
            core::configManager.acquire();
            core::configManager.conf["frequency"] = gui::waterfall.getCenterFrequency();
            if (vfo != NULL) {
                core::configManager.conf["vfoOffsets"][gui::waterfall.selectedVFO] = vfo->generalOffset;
            }
            core::configManager.release(true);
        }
    }

}

void MainWindow::drawBottomWindows(int dy) {
    if (!showMenu) {
        if (bottomWindows.size() > 0) {
            updateBottomWindowLayout();
            for (int i = 0; i < bottomWindows.size(); i++) {
                ImGui::Begin(("bottomwindow_" + bottomWindows[i].name).c_str(),
                             NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
                ImGui::SetWindowPos(ImVec2(bottomWindows[i].loc.x, gui::waterfall.wfMax.y + dy));
                ImGui::SetWindowSize(ImVec2(bottomWindows[i].size.x, bottomWindows[i].size.y));
                bottomWindows[i].drawFunc();
                ImGui::End();
            }
        }
    }
}
void MainWindow::addBottomWindow(std::string name, std::function<void()> drawFunc) {
    int foundIndex = -1;
    for (int i = 0; i < bottomWindows.size(); i++) {
        if (bottomWindows[i].name == name) {
            foundIndex = i;
        }
    }
    if (foundIndex != -1) {
        bottomWindows[foundIndex].drawFunc = drawFunc;
    } else {
        bottomWindows.emplace_back(ButtomWindow{ name, drawFunc });
    }
    updateBottomWindowLayout();
}

void MainWindow::updateBottomWindowLayout() {
    auto fullWidth = gui::waterfall.fftAreaMax.x - gui::waterfall.fftAreaMin.x;
    auto fullHeight = ImGui::GetIO().DisplaySize.y;
    int nWindows = bottomWindows.size();
    if (nWindows < 5) {
        nWindows = 5;
    }
    auto size = fullWidth / nWindows;
    auto scan = 0;
    for(int i=0; i<bottomWindows.size(); i++) {
        bottomWindows[i].loc.x = scan;
        bottomWindows[i].loc.y = 0;
        bottomWindows[i].size.x = size;
        bottomWindows[i].size.y = fullHeight/5;
        scan += size;
    }
}

bool MainWindow::hasBottomWindow(std::string name) {
    for (int i = 0; i < bottomWindows.size(); i++) {
        if (bottomWindows[i].name == name) {
            return true;
        }
    }
    return false;
}

void MainWindow::removeBottomWindow(std::string name) {
    for (int i = 0; i < bottomWindows.size(); i++) {
        if (bottomWindows[i].name == name) {
            bottomWindows.erase(bottomWindows.begin() + i);
            updateBottomWindowLayout();
            return;
        }
    }
}

void MainWindow::drawDebugMenu() {
    if (ImGui::CollapsingHeader("Debug")) {
        float deltaTimeSec = ImGui::GetIO().DeltaTime;
        ImGui::Text("Frame time: %.3f ms/frame", deltaTimeSec * 1000.0f);
        ImGui::Text("Draw time: imgui:%lld glwf:%lld us busy=%d%%", lastDrawTime, lastDrawTimeBackend, (int)((double)(lastDrawTime+lastDrawTimeBackend)*100.0/ (deltaTimeSec*1e6)));
        ImGui::Text("Framerate: %.1f FPS", ImGui::GetIO().Framerate);
        ImGui::Text("Center Frequency: %.0f Hz", gui::waterfall.getCenterFrequency());
        ImGui::Text("Source name: %s", sourceName.c_str());
        ImGui::Checkbox("Show demo window", &demoWindow);
        ImGui::Checkbox("Show log", &logWindow);
        ImGui::Text("ImGui version: %s", ImGui::GetVersion());

        // ImGui::Checkbox("Bypass buffering", &sigpath::iqFrontEnd.inputBuffer.bypass);

        // ImGui::Text("Buffering: %d", (sigpath::iqFrontEnd.inputBuffer.writeCur - sigpath::iqFrontEnd.inputBuffer.readCur + 32) % 32);

        if (ImGui::Button("Test Bug")) {
            flog::error("Will this make the software crash?");
        }

        if (ImGui::Button("Testing something")) {
            addBottomWindow("bw1", []() {
                ImGui::Text("Hello world1");
            });
            addBottomWindow("bw2", []() {
                ImGui::Text("Hello world2");
            });
            addBottomWindow("bw3", []() {
                ImGui::Text("Hello world3");
            });
        }
#ifdef __ANDROID__
        if (ImGui::Button("Perm.Request")) {
            backend::doPermissionsDialogs();
        }
#endif

        ImGui::Checkbox("WF Single Click", &gui::waterfall.VFOMoveSingleClick);
        onDebugDraw.emit(GImGui);


        ImGui::Spacing();
    }
}
