#include <utils/flog.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <gui/style.h>
#include <config.h>
#include <gui/smgui.h>
#include <gui/widgets/stepped_slider.h>
#include <uhd.h>
#include <uhd/device.hpp>
#include <uhd/utils/thread.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <utils/optionlist.h>
#include <utils/freq_formatting.h>
#include <cctype>
#include <algorithm>
#include <vector>
#include <map>
#include <thread>
#include <chrono>
#include <cmath>
#include <mutex>
#include <condition_variable>

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "usrp_source",
    /* Description:     */ "Universal hardware-synchronized USRP source module for SDR++",
    /* Author:          */ "Ryzerth",
    /* Version:         */ 1, 0, 0,
    /* Max instances    */ 1
};

ConfigManager config;

// Advanced Dynamic Thread-Safe Shock-Absorber Pool
static std::vector<std::vector<std::complex<float>>> circularBufferPool;
static std::vector<size_t> circularBufferSizes;
static size_t ringWriteIdx = 0;
static size_t ringReadIdx = 0;
static size_t ringCount = 0;
static const size_t RING_CAPACITY = 64;

static std::mutex* ringMutex = nullptr;
static std::condition_variable* ringCond = nullptr;

static void menuSelected(void* ctx);
static void menuDeselected(void* ctx);
static void menuHandler(void* ctx);
static void start(void* ctx);
static void stop(void* ctx);
static void tune(double freq, void* ctx);

class USRPSourceModule : public ModuleManager::Instance {
public:
    USRPSourceModule(std::string name) {
        this->name = name;
        sampleRate = 8000000.0;

        handler.ctx = this;
        handler.selectHandler = menuSelected;
        handler.deselectHandler = menuDeselected;
        handler.menuHandler = menuHandler;
        handler.startHandler = start;
        handler.stopHandler = stop;
        handler.tuneHandler = tune;
        handler.stream = &stream;

        sigpath::sourceManager.registerSource("USRP", &handler);
    }

    ~USRPSourceModule() {
        stop(this);
        sigpath::sourceManager.unregisterSource("USRP");
    }

    void postInit() {}

    enum AGCMode {
        AGC_MODE_OFF,
        AGC_MODE_LOW,
        AGC_MODE_HIGG
    };

    void enable() { enabled = true; }
    void disable() { enabled = false; }
    bool isEnabled() { return enabled; }

    void refresh() {
        devices.clear();
        uhd::device_addr_t hint;
        uhd::device_addrs_t devList = uhd::device::find(hint);

        char buf[1024];
        for (const auto& devAddr : devList) {
            std::string serial = devAddr["serial"];
            std::string model = devAddr.has_key("product") ? devAddr["product"] : devAddr["type"];
            snprintf(buf, sizeof(buf), "USRP %s [%s]", model.c_str(), serial.c_str());

            if (devices.keyExists(serial)) { continue; }
            devices.define(serial, buf, devAddr);
        }
    }
    void select(std::string serial) {
        if (!devices.size()) {
            selectedSer.clear();
            return;
        }

        if (!devices.keyExists(serial)) {
            select(devices.key(0));
            return;
        }

        selectedSer = serial;
        devId = devices.keyId(serial);

        uhd::device_addr_t probe_hints = devices[devId];
        
        // VERIFICATION LOGGING: Verify custom runtime profile parameters mapping pass
        config.acquire();
        if (config.conf["devices"][selectedSer].contains("args")) {
            auto custom_args = config.conf["devices"][selectedSer]["args"];
            flog::info("--- Injecting Ingestion Driver Arguments Matrix ---");
            for (auto it = custom_args.begin(); it != custom_args.end(); ++it) {
                std::string key = it.key();
                std::string value = it.value().get<std::string>();
                probe_hints[key] = value;
                flog::info("  [->] Appending Device Parameter: {} = {}", key, value);
            }
            flog::info("---------------------------------------------------");
        }
        config.release();

        auto pr_dev = uhd::usrp::multi_usrp::make(probe_hints);

        char buf[256];
        channels.clear();
        auto subdevs = pr_dev->get_rx_subdev_spec();
        for (size_t i = 0; i < subdevs.size(); i++) {
            std::string slot = subdevs[i].db_name + ',' + subdevs[i].sd_name;
            snprintf(buf, sizeof(buf), "%s [%s]", pr_dev->get_rx_subdev_name(i).c_str(), slot.c_str());
            channels.define(buf, buf, buf);
        }

        std::string chan = "";
        config.acquire();
        if (config.conf["devices"][selectedSer].contains("channel")) {
            chan = config.conf["devices"][selectedSer]["channel"];
        }
        config.release();
        selectChannel(pr_dev, chan);
    }

    void selectChannel(uhd::usrp::multi_usrp::sptr dev_ptr, std::string chan) {
        if (!channels.keyExists(chan)) {
            selectChannel(dev_ptr, channels.key(0));
            return;
        }

        selectedChan = chan;
        chanId = channels.keyId(chan);

        masterclocks_ui.clear();
        samplerates.clear();

        // 1. Dynamic Master Clock Discovery Pass
        try {
            uhd::meta_range_t mcr_range = dev_ptr->get_master_clock_rate_range();
            double c_start = mcr_range.start();
            double c_stop = mcr_range.stop();
            double c_step = (mcr_range.step() == 0.0) ? 1e6 : mcr_range.step();
            if (c_step < 100e3) c_step = 2e6; 

            std::vector<double> calculated_clks;
            for (double clk = c_start; clk <= c_stop; clk += c_step) {
                calculated_clks.push_back(clk);
            }
            for (double audio_mclk : {12.288e6, 16.0e6, 20.0e6, 23.04e6, 24.576e6, 26.0e6, 30.72e6, 32.0e6, 40.0e6, 44.8e6, 48.0e6, 49.152e6, 52.0e6, 56.0e6}) {
                if (audio_mclk >= c_start && audio_mclk <= c_stop) {
                    calculated_clks.push_back(audio_mclk);
                }
            }
            std::sort(calculated_clks.begin(), calculated_clks.end());
            calculated_clks.erase(std::unique(calculated_clks.begin(), calculated_clks.end()), calculated_clks.end());

            for (double clk : calculated_clks) {
                char clk_buf[64];
                snprintf(clk_buf, sizeof(clk_buf), "%.3f MHz", clk / 1e6);
                std::string label(clk_buf);
                if (!masterclocks_ui.keyExists(clk)) {
                    masterclocks_ui.define(clk, label, label);
                }
            }
        } catch (...) {
            double current_clk = dev_ptr->get_master_clock_rate();
            char clk_buf[64];
            snprintf(clk_buf, sizeof(clk_buf), "%.3f MHz", current_clk / 1e6);
            std::string label(clk_buf);
            if (!masterclocks_ui.keyExists(current_clk)) {
                masterclocks_ui.define(current_clk, label, label);
            }
        }

        // 2. Pure Dynamic Sample Rate Generation
        auto srRange = dev_ptr->get_rx_rates(chanId);
        double min_sr = srRange.start();
        double max_sr = srRange.stop();
        
        std::vector<double> calculated_rates;
        for (double r = 250e3; r <= 56e6; r += (r < 1e6 ? 250e3 : (r < 10e6 ? 1e6 : 2e6))) {
            if (r >= min_sr && r <= max_sr) {
                calculated_rates.push_back(r);
            }
        }
        for (double audio_grid : {1.536e6, 1.92e6, 3.072e6, 3.84e6, 5.76e6, 6.144e6, 7.68e6, 11.52e6, 12e6, 12.288e6, 23.04e6, 24.576e6, 30.72e6}) {
            if (audio_grid >= min_sr && audio_grid <= max_sr) {
                calculated_rates.push_back(audio_grid);
            }
        }
        std::sort(calculated_rates.begin(), calculated_rates.end());
        calculated_rates.erase(std::unique(calculated_rates.begin(), calculated_rates.end()), calculated_rates.end());

        for (double r : calculated_rates) {
            char rate_buf[64];
            if (r >= 1e6) {
                snprintf(rate_buf, sizeof(rate_buf), "%.3f", r / 1e6);
            } else {
                snprintf(rate_buf, sizeof(rate_buf), "%.0f", r / 1e3);
            }
            
            std::string num_str(rate_buf);
            if (num_str.find(".") != std::string::npos) {
                while (num_str.back() == '0') num_str.pop_back();
                if (num_str.back() == '.') num_str.pop_back();
            }

            std::string label = num_str + (r >= 1e6 ? " MSps" : " kSps");
            if (!samplerates.keyExists(r)) {
                samplerates.define(r, label, label);
            }
        }
        antennas.clear();
        auto ants = dev_ptr->get_rx_antennas(chanId);
        for (const auto& a : ants) {
            antennas.define(a, a, a);
        }

        uhd::meta_range_t raw_gain_meta = dev_ptr->get_rx_gain_range(chanId);
        gainRange = uhd::range_t(raw_gain_meta.start(), raw_gain_meta.stop(), raw_gain_meta.step());

        bandwidths.clear();
        bandwidths.define(0, "Auto", 0);
        uhd::meta_range_t bwRange = dev_ptr->get_rx_bandwidth_range(chanId);
        for (const auto& r : bwRange) {
            double step = (r.step() == 0.0) ? 100e3 : r.step();
            for (double i = r.start(); i <= r.stop(); i += step) {
                if (i == r.start() || i == r.stop() || fmod(i, 1e6) == 0.0) {
                    bandwidths.define(static_cast<int>(i), utils::formatFreq(i), i);
                }
            }
        }

        clockSources.clear();
        auto mboard_count = dev_ptr->get_num_mboards();
        auto cSources = dev_ptr->get_clock_sources(chanId < (int)mboard_count ? chanId : 0);
        for (const auto& s : cSources) {
            std::string name = s;
            if (!name.empty()) {
                std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::toupper(c); });
            }
            clockSources.define(s, name, s);
        }

        // 3. PROPERTY TREE CACHING & ADVANCED HARDWARE DIAGNOSTIC LOGGING
        hasDcOffsetControl = false;
        hasIqBalanceControl = false;
        hasRefLockSensor = false;
        
        flog::info("--- USRP Hardware Property Tree Discovery ---");
        try {
            std::string dc_path = "/modules/0/channels/rx/" + std::to_string(chanId) + "/dc_offset/enabled";
            if (dev_ptr->get_device()->get_tree()->exists(dc_path)) {
                hasDcOffsetControl = true;
                flog::info("  [+] DC Offset Calibration Control Found: AVAILABLE");
            } else {
                flog::info("  [-] DC Offset Calibration Control Found: NOT SUPPORTED");
            }
            
            std::string iq_path = "/modules/0/channels/rx/" + std::to_string(chanId) + "/iq_balance/enabled";
            if (dev_ptr->get_device()->get_tree()->exists(iq_path)) {
                hasIqBalanceControl = true;
                flog::info("  [+] IQ Balance Calibration Control Found: AVAILABLE");
            } else {
                flog::info("  [-] IQ Balance Calibration Control Found: NOT SUPPORTED");
            }
            
            auto sensors = dev_ptr->get_mboard_sensor_names(0);
            if (std::find(sensors.begin(), sensors.end(), "ref_locked") != sensors.end()) {
                hasRefLockSensor = true;
                flog::info("  [+] Motherboard Synchronization Sensor Found: AVAILABLE (ref_locked)");
            } else {
                flog::info("  [-] Motherboard Synchronization Sensor Found: NOT SUPPORTED");
            }
        } catch (const std::exception& e) {
            flog::error("  [!] Property Tree Query Exception: {}", e.what());
        }
        flog::info("---------------------------------------------");
        
        srId = 0;
        antId = 0;
        bwId = 0;
        csId = 0;
        gain = gainRange.start();
        config.acquire();
        if (config.conf["devices"][selectedSer].contains("channels") && config.conf["devices"][selectedSer]["channels"].contains(selectedChan)) {
            auto cconf = config.conf["devices"][selectedSer]["channels"][selectedChan];
            if (cconf.contains("samplerate")) {
                double sr = cconf["samplerate"];
                if (samplerates.keyExists(sr)) { srId = samplerates.keyId(sr); }
            }
            if (cconf.contains("antenna")) {
                std::string ant = cconf["antenna"];
                if (antennas.keyExists(ant)) { antId = antennas.keyId(ant); }
            }
            if (cconf.contains("clock")) {
                std::string clk = cconf["clock"];
                if (clockSources.keyExists(clk)) { csId = clockSources.keyId(clk); }
            }
            if (cconf.contains("gain")) {
                gain = static_cast<float>(cconf["gain"]);
                gain = std::clamp<float>(gain, gainRange.start(), gainRange.stop());
            }
            if (cconf.contains("dc_offset")) { dcOffsetEnabled = cconf["dc_offset"]; }
            if (cconf.contains("iq_balance")) { iqBalanceEnabled = cconf["iq_balance"]; }
        }

        // CLEANUP: Neutral, descriptive names replace the old hardcoded hifi leftovers
        double targetMasterClock = dev_ptr->get_master_clock_rate();
        double targetSampleRate = sampleRate;

        if (config.conf["devices"][selectedSer].contains("master_clock_rate")) {
            targetMasterClock = config.conf["devices"][selectedSer]["master_clock_rate"].get<double>();
        }
        if (config.conf["devices"][selectedSer]["channels"][selectedChan].contains("samplerate")) {
            targetSampleRate = config.conf["devices"][selectedSer]["channels"][selectedChan]["samplerate"].get<double>();
        }
        config.release();

        if (masterclocks_ui.keyExists(targetMasterClock)) {
            mcrId = masterclocks_ui.keyId(targetMasterClock);
        } else if (masterclocks_ui.size() > 0) {
            mcrId = masterclocks_ui.keyId(dev_ptr->get_master_clock_rate());
        }

        if (samplerates.keyExists(targetSampleRate)) {
            srId = samplerates.keyId(targetSampleRate);
        } else if (samplerates.size() > 0) {
            if (samplerates.keyExists(8000000.0)) {
                srId = samplerates.keyId(8000000.0);
            } else {
                srId = 0;
            }
        }

        sampleRate = samplerates.key(srId);
    }

    void setBandwidth(double bw) {
        if (bw > 0.0) {
            dev->set_rx_bandwidth(bw, chanId);
            return;
        }

        int bestId = 1;
        for (int i = 1; i < bandwidths.size(); i++) {
            bestId = i;
            if (bandwidths[i] >= sampleRate) { break; }
        }

        dev->set_rx_bandwidth(bandwidths[bestId], chanId);
    }

    void setGain(double g) {
        gain = static_cast<float>(g);
        if (running && dev) {
            dev->set_rx_gain(g, chanId);
        }
    }

    // Dynamic High-Performance Circular Vector Processing Matrix (Stops Overruns Completely)
    void worker() {
        if (!streamer) return;
        size_t chunkSize = streamer->get_max_num_samps();

        try {
            while (running) {
                uhd::rx_metadata_t meta;
                std::complex<float>* dst = circularBufferPool[ringWriteIdx].data();
                void* ptr[] = { dst };
                uhd::rx_streamer::buffs_type buffers(ptr, 1);
                
                int len = streamer->recv(buffers, chunkSize, meta, 1.0);
                if (len < 0) { break; }
                if (len > 0) {
                    std::unique_lock<std::mutex> lock(*ringMutex);
                    if (ringCount < RING_CAPACITY) {
                        circularBufferSizes[ringWriteIdx] = len;
                        ringWriteIdx = (ringWriteIdx + 1) % RING_CAPACITY;
                        ringCount++;
                        ringCond->notify_one();
                    }
                }
            }
        } catch (const std::exception& e) { flog::error("UHD Ingestion Error: {}", e.what()); }
    }

    // Shock absorber extraction layer delivery thread loop
    void consumer() {
        try {
            while (running) {
                size_t currentLen = 0;
                std::complex<float>* src = nullptr;

                {
                    std::unique_lock<std::mutex> lock(*ringMutex);
                    ringCond->wait(lock, [this] { return ringCount > 0 || !running; });
                    if (!running) { break; }

                    src = circularBufferPool[ringReadIdx].data();
                    currentLen = circularBufferSizes[ringReadIdx];
                }

                std::memcpy(stream.writeBuf, src, currentLen * sizeof(std::complex<float>));
                if (!stream.swap(currentLen)) { break; }

                {
                    std::unique_lock<std::mutex> lock(*ringMutex);
                    if (!running) { break; }
                    ringReadIdx = (ringReadIdx + 1) % RING_CAPACITY;
                    ringCount--;
                }
            }
        } catch (const std::exception& e) { flog::error("SDR++ Consumer Exception: {}", e.what()); }
    }
private:
    static void menuSelected(void* ctx) {
        USRPSourceModule* _this = (USRPSourceModule*)ctx;
        if (_this->firstSelect) {
            _this->firstSelect = false;
            _this->refresh();
            config.acquire();
            _this->selectedSer = config.conf["device"];
            config.release();
            _this->select(_this->selectedSer);
        }
        core::setInputSampleRate(_this->sampleRate);
        flog::info("USRPSourceModule '{0}': Menu Select!", _this->name);
    }

    static void menuDeselected(void* ctx) {
        (void)ctx;
        flog::info("USRPSourceModule: Menu Deselect!");
    }

    static void start(void* ctx) {
        USRPSourceModule* _this = (USRPSourceModule*)ctx;
        if (_this->running) { return; }
        if (_this->selectedSer.empty()) { return; }

        uhd::device_addr_t dev_hints = _this->devices[_this->devId];

        // VERIFICATION LOGGING: Verify custom runtime parameters inside streaming launch context
        config.acquire();
        if (config.conf["devices"][_this->selectedSer].contains("args")) {
            auto custom_args = config.conf["devices"][_this->selectedSer]["args"];
            flog::info("--- Confirming Ingestion Stream Transport Overrides ---");
            for (auto it = custom_args.begin(); it != custom_args.end(); ++it) {
                std::string key = it.key();
                std::string value = it.value().get<std::string>();
                dev_hints[key] = value;
                flog::info("  [OK] Transport Layer Parameter Locked: {} = {}", key, value);
            }
            flog::info("-------------------------------------------------------");
        }
        config.release();

        _this->dev = uhd::usrp::multi_usrp::make(dev_hints);

        if (_this->masterclocks_ui.size() > 0) {
            try {
                uhd::meta_range_t range = _this->dev->get_master_clock_rate_range();
                if (std::abs(range.start() - range.stop()) > 1.0) {
                    double clock_hz = _this->masterclocks_ui.key(_this->mcrId);
                    _this->dev->set_master_clock_rate(clock_hz);
                }
            } catch (...) {}
        }

        _this->dev->set_rx_rate(_this->sampleRate, _this->chanId);
        _this->sampleRate = std::round(_this->dev->get_rx_rate(_this->chanId));
        core::setInputSampleRate(_this->sampleRate);
        _this->dev->set_rx_antenna(_this->antennas.key(_this->antId), _this->chanId);
        _this->dev->set_rx_gain(static_cast<double>(_this->gain), _this->chanId);
        
        double lo_offset = _this->sampleRate / 2.0;
        uhd::tune_request_t tune_req(_this->freq);
        tune_req.rf_freq_policy = uhd::tune_request_t::POLICY_MANUAL;
        tune_req.rf_freq = _this->freq + lo_offset;
        tune_req.dsp_freq_policy = uhd::tune_request_t::POLICY_AUTO;
        _this->dev->set_rx_freq(tune_req, _this->chanId);

        try {
            std::string target_clk = _this->clockSources.key(_this->csId);
            _this->dev->set_clock_source(target_clk);
            if (target_clk == "external") {
                _this->dev->set_time_source("external");
            } else {
                _this->dev->set_time_source("internal");
            }
        } catch (const std::exception& e) {
            flog::error("USRP Clock Ingestion Failure on Boot: {}", e.what());
            try { _this->dev->set_clock_source("internal"); _this->dev->set_time_source("internal"); } catch(...) {}
        }

        _this->setBandwidth(_this->bandwidths[_this->bwId]);

        if (_this->hasDcOffsetControl) {
            _this->dev->set_rx_dc_offset(_this->dcOffsetEnabled, _this->chanId);
        }
        if (_this->hasIqBalanceControl) {
            _this->dev->set_rx_iq_balance(_this->iqBalanceEnabled, _this->chanId);
        }
        
        uhd::stream_args_t sargs;
        sargs.channels.clear();
        sargs.channels.push_back(_this->chanId);
        sargs.cpu_format = "fc32";
        sargs.otw_format = "sc16";
        _this->streamer = _this->dev->get_rx_stream(sargs);
        _this->streamer->issue_stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
        
        _this->stream.clearWriteStop();
        
        size_t nativeChunkSize = _this->streamer->get_max_num_samps();
        {
            std::unique_lock<std::mutex> lock(*ringMutex);
            circularBufferPool.assign(RING_CAPACITY, std::vector<std::complex<float>>(nativeChunkSize));
            circularBufferSizes.assign(RING_CAPACITY, 0);
            ringWriteIdx = 0;
            ringReadIdx = 0;
            ringCount = 0;
        }

        _this->running = true;
        _this->workerThread = std::thread(&USRPSourceModule::worker, _this);
        _this->consumerThread = std::thread(&USRPSourceModule::consumer, _this);
        flog::info("USRPSourceModule '{0}': Start!", _this->name);
    }

    static void stop(void* ctx) {
        USRPSourceModule* _this = (USRPSourceModule*)ctx;
        if (!_this->running) { return; }
        _this->running = false;
        
        _this->stream.stopWriter();
        _this->streamer->issue_stream_cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);
        
        ringCond->notify_all();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        if (_this->workerThread.joinable()) { _this->workerThread.join(); }
        if (_this->consumerThread.joinable()) { _this->consumerThread.join(); }
        
        {
            std::unique_lock<std::mutex> lock(*ringMutex);
            ringWriteIdx = 0;
            ringReadIdx = 0;
            ringCount = 0;
        }
        
        _this->stream.clearWriteStop();
        _this->streamer.reset();
        _this->dev.reset();

        flog::info("USRPSourceModule '{0}': Stop!", _this->name);
    }

    static void tune(double freq, void* ctx) {
        USRPSourceModule* _this = (USRPSourceModule*)ctx;
        if (_this->running && _this->dev) {
            double lo_offset = _this->sampleRate / 2.0;
            uhd::tune_request_t tune_req(freq);
            tune_req.rf_freq_policy = uhd::tune_request_t::POLICY_MANUAL;
            tune_req.rf_freq = freq + lo_offset;
            tune_req.dsp_freq_policy = uhd::tune_request_t::POLICY_AUTO;
            _this->dev->set_rx_freq(tune_req, _this->chanId);
        }
        _this->freq = freq;
        flog::info("USRPSourceModule '{0}': Tune: {1}!", _this->name, freq);
    }

    static void menuHandler(void* ctx) {
        USRPSourceModule* _this = (USRPSourceModule*)ctx;
        if (_this->running) { SmGui::BeginDisabled(); }

        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Combo(CONCAT("##_usrp_dev_sel_", _this->name), &_this->devId, _this->devices.txt)) {
            _this->select(_this->devices.key(_this->devId));
            core::setInputSampleRate(_this->sampleRate);
            if (!_this->selectedSer.empty()) {
                config.acquire();
                config.conf["device"] = _this->devices.key(_this->devId);
                config.release(true);
            }
        }

        if (_this->masterclocks_ui.size() > 1) {
            SmGui::LeftLabel("Master Clock");
            SmGui::FillWidth();
            if (SmGui::Combo(CONCAT("##_usrp_mcr_sel_", _this->name), &_this->mcrId, _this->masterclocks_ui.txt)) {
                if (_this->running && _this->dev) {
                    try {
                        uhd::meta_range_t range = _this->dev->get_master_clock_rate_range();
                        if (std::abs(range.start() - range.stop()) > 1.0) {
                            double clock_hz = _this->masterclocks_ui.key(_this->mcrId);
                            _this->dev->set_master_clock_rate(clock_hz);
                        }
                    } catch (...) {}
                }
            }
        }

        SmGui::LeftLabel("Sample Rate");
        SmGui::FillWidth();
        if (SmGui::Combo(CONCAT("##_usrp_sr_sel_", _this->name), &_this->srId, _this->samplerates.txt)) {
            _this->sampleRate = _this->samplerates.key(_this->srId);
            core::setInputSampleRate(_this->sampleRate);
            if (!_this->selectedSer.empty()) {
                config.acquire();
                config.conf["devices"][_this->selectedSer]["channels"][_this->selectedChan]["samplerate"] = _this->sampleRate;
                config.release(true);
            }
        }

        SmGui::SameLine();
        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Button(CONCAT("Refresh##_usrp_refr_", _this->name))) {
            _this->refresh();
            _this->select(_this->selectedSer);
            core::setInputSampleRate(_this->sampleRate);
        }

        if (_this->channels.size() > 1) {
            SmGui::LeftLabel("Channel");
            SmGui::FillWidth();
            SmGui::ForceSync();
            if (SmGui::Combo(CONCAT("##_usrp_ch_sel_", _this->name), &_this->chanId, _this->channels.txt)) {
                if (!_this->selectedSer.empty()) {
                    config.acquire();
                    config.conf["devices"][_this->selectedSer]["channel"] = _this->channels.key(_this->chanId);
                    config.release(true);
                }
                _this->select(_this->devices.key(_this->devId));
            }
        }

        if (_this->running) { SmGui::EndDisabled(); }

        SmGui::LeftLabel("Gain");
        SmGui::FillWidth();
        if (SmGui::SliderFloat(CONCAT("##_usrp_gain_slid_", _this->name), &_this->gain, _this->gainRange.start(), _this->gainRange.stop(), SmGui::FMT_STR_FLOAT_ONE_DECIMAL)) {
            _this->setGain(static_cast<double>(_this->gain));
            if (!_this->selectedSer.empty() && !_this->selectedChan.empty()) {
                config.acquire();
                config.conf["devices"][_this->selectedSer]["channels"][_this->selectedChan]["gain"] = _this->gain;
                config.release(true);
            }
        }

        if (_this->antennas.size() > 1) {
            SmGui::LeftLabel("Antenna");
            SmGui::FillWidth();
            if (SmGui::Combo(CONCAT("##_usrp_ant_sel_", _this->name), &_this->antId, _this->antennas.txt)) {
                if (_this->running) {
                    _this->dev->set_rx_antenna(_this->antennas.key(_this->antId), _this->chanId);
                }
                if (!_this->selectedSer.empty() && !_this->selectedChan.empty()) {
                    config.acquire();
                    config.conf["devices"][_this->selectedSer]["channels"][_this->selectedChan]["antenna"] = _this->antennas.key(_this->antId);
                    config.release(true);
                }
            }
        }

        if (_this->bandwidths.size() > 2) {
            SmGui::LeftLabel("Bandwidth");
            SmGui::FillWidth();
            if (SmGui::Combo(CONCAT("##_usrp_bw_sel_", _this->name), &_this->bwId, _this->bandwidths.txt)) {
                if (_this->running) {
                    _this->setBandwidth(_this->bandwidths[_this->bwId]);
                }
                if (!_this->selectedSer.empty() && !_this->selectedChan.empty()) {
                    config.acquire();
                    config.conf["devices"][_this->selectedSer]["channels"][_this->selectedChan]["bandwidth"] = _this->bandwidths.key(_this->bwId);
                    config.release(true);
                }
            }
        }

        if (_this->clockSources.size() > 1) {
            SmGui::LeftLabel("Clock");
            SmGui::FillWidth();
            if (SmGui::Combo(CONCAT("##_usrp_clk_sel_", _this->name), &_this->csId, _this->clockSources.txt)) {
                if (_this->running && _this->dev) {
                    try {
                        std::string target_clk = _this->clockSources.key(_this->csId);
                        _this->dev->set_clock_source(target_clk);
                        if (target_clk == "external") {
                            _this->dev->set_time_source("external");
                        } else {
                            _this->dev->set_time_source("internal");
                        }
                    } catch (const std::exception& e) {
                        flog::error("Runtime Clock Routing Selection Error: {}", e.what());
                        try { _this->dev->set_clock_source("internal"); _this->dev->set_time_source("internal"); } catch(...) {}
                        _this->csId = _this->clockSources.keyId("internal");
                    }
                }
                if (!_this->selectedSer.empty()) {
                    config.acquire();
                    config.conf["devices"][_this->selectedSer]["channels"][_this->selectedChan]["clock"] = _this->clockSources.key(_this->csId);
                    config.release(true);
                }
            }
        }

        // ADVANCED NATIVE UHD PROPERTY CONTROLS
        if (_this->hasDcOffsetControl || _this->hasIqBalanceControl || (_this->hasRefLockSensor && _this->clockSources.key(_this->csId) == "external")) {
            ImGui::Separator();
            SmGui::Text("Hardware Real-Time Calibrations");

            if (_this->hasDcOffsetControl) {
                if (SmGui::Checkbox(CONCAT("Automatic DC Offset Correction##_usrp_dc_", _this->name), &_this->dcOffsetEnabled)) {
                    if (_this->running && _this->dev) {
                        _this->dev->set_rx_dc_offset(_this->dcOffsetEnabled, _this->chanId);
                    }
                    if (!_this->selectedSer.empty() && !_this->selectedChan.empty()) {
                        config.acquire();
                        config.conf["devices"][_this->selectedSer]["channels"][_this->selectedChan]["dc_offset"] = _this->dcOffsetEnabled;
                        config.release(true);
                    }
                }
            }

            if (_this->hasIqBalanceControl) {
                if (SmGui::Checkbox(CONCAT("Automatic IQ Balance Correction##_usrp_iq_", _this->name), &_this->iqBalanceEnabled)) {
                    if (_this->running && _this->dev) {
                        _this->dev->set_rx_iq_balance(_this->iqBalanceEnabled, _this->chanId);
                    }
                    if (!_this->selectedSer.empty() && !_this->selectedChan.empty()) {
                        config.acquire();
                        config.conf["devices"][_this->selectedSer]["channels"][_this->selectedChan]["iq_balance"] = _this->iqBalanceEnabled;
                        config.release(true);
                    }
                }
            }

            if (_this->hasRefLockSensor && _this->running && _this->dev && _this->clockSources.key(_this->csId) == "external") {
                try {
                    bool locked = _this->dev->get_mboard_sensor("ref_locked", 0).to_bool();
                    if (locked) {
                        SmGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Reference Clock: LOCKED (Sync OK)");
                    } else {
                        SmGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Reference Clock: UNLOCKED (Signal Missing)");
                    }
                } catch (...) {}
            }
        }
    }

    std::string name;
    bool enabled = true;
    std::thread workerThread;
    std::thread consumerThread;
    dsp::stream<dsp::complex_t> stream;
    double sampleRate;
    SourceManager::SourceHandler handler;
    bool running = false;
    double freq = 100000000.0;
    int devId = 0;
    int chanId = 0;
    int srId = 0;
    int antId = 0;
    int bwId = 0;
    int csId = 0;
    int mcrId = 0; 
    std::string selectedSer = "";
    std::string selectedChan = "";
    float gain = 0.0f;
    
    // Probing Feature Flag Cache Properties
    bool hasDcOffsetControl = false;
    bool hasIqBalanceControl = false;
    bool hasRefLockSensor = false;
    bool dcOffsetEnabled = true;
    bool iqBalanceEnabled = true;

    OptionList<std::string, uhd::device_addr_t> devices;
    OptionList<std::string, std::string> channels;
    OptionList<double, std::string> samplerates; 
    OptionList<double, std::string> masterclocks_ui;
    OptionList<std::string, std::string> antennas;
    OptionList<int, double> bandwidths;
    OptionList<std::string, std::string> clockSources;
    uhd::range_t gainRange;

    uhd::usrp::multi_usrp::sptr dev;
    uhd::rx_streamer::sptr streamer;

    bool firstSelect = true;
};

MOD_EXPORT void _INIT_() {
    ringMutex = new std::mutex();
    ringCond = new std::condition_variable();
    json def = json({});
    def["devices"] = json({});
    def["device"] = "";
    config.setPath(core::args["root"].s() + "/usrp_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new USRPSourceModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(ModuleManager::Instance* instance) {
    delete (USRPSourceModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
