#include <utils/flog.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <gui/style.h>
#include <gui/smgui.h>
#include <iio.h>
#include <ad9361.h>
#include <utils/optionlist.h>
#include <algorithm>
#include <cctype>
#include <regex>

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "plutosdr_source",
    /* Description:     */ "PlutoSDR source module for SDR++",
    /* Author:          */ "Ryzerth",
    /* Version:         */ 0, 2, 0,
    /* Max instances    */ 1
};

ConfigManager config;

const std::vector<const char*> deviceWhiteList = {
    "PlutoSDR",
    "ANTSDR",
    "LibreSDR",
    "Pluto+",
    "ad9361",
    "FISH"
};

static std::string toLowerCopy(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });
    return str;
}

static bool containsInsensitive(const std::string& haystack, const std::string& needle) {
    return toLowerCopy(haystack).find(toLowerCopy(needle)) != std::string::npos;
}

static iio_device* findDeviceByNameContains(iio_context* ctx, const std::string& needle) {
    unsigned int devCount = iio_context_get_devices_count(ctx);
    for (unsigned int i = 0; i < devCount; i++) {
        iio_device* dev = iio_context_get_device(ctx, i);
        const char* name = iio_device_get_name(dev);
        if (name != nullptr && containsInsensitive(name, needle)) {
            return dev;
        }
    }
    return nullptr;
}

static iio_device* findRxStreamingDevice(iio_context* ctx) {
    iio_device* dev = iio_context_find_device(ctx, "cf-ad9361-lpc");
    if (dev != nullptr) {
        return dev;
    }

    unsigned int devCount = iio_context_get_devices_count(ctx);
    for (unsigned int i = 0; i < devCount; i++) {
        dev = iio_context_get_device(ctx, i);
        if (iio_device_find_channel(dev, "voltage0", false) != nullptr &&
            iio_device_find_channel(dev, "voltage1", false) != nullptr) {
            return dev;
        }
    }
    return nullptr;
}

static bool looksLikePlutoCompatibleContext(const std::string& uri) {
    iio_context* ctx = iio_create_context_from_uri(uri.c_str());
    if (ctx == nullptr) {
        return false;
    }

    iio_device* phy = iio_context_find_device(ctx, "ad9361-phy");
    if (phy == nullptr) {
        phy = findDeviceByNameContains(ctx, "ad9361-phy");
    }
    iio_device* rxDev = findRxStreamingDevice(ctx);

    bool compatible = (phy != nullptr && rxDev != nullptr);
    iio_context_destroy(ctx);
    return compatible;
}

static std::string getContextAttr(iio_context* ctx, const char* key) {
    const char* value = iio_context_get_attr_value(ctx, key);
    if (value == nullptr) {
        return "";
    }
    return value;
}

static std::string buildContextDescription(iio_context* ctx, const std::string& uri) {
    std::string model = getContextAttr(ctx, "hw_model");
    std::string serial = getContextAttr(ctx, "hw_serial");
    std::string ipAddr = getContextAttr(ctx, "ip,ip-addr");

    if (ipAddr.empty()) {
        ipAddr = uri;
    }
    if (model.empty()) {
        model = "Unknown";
    }
    if (serial.empty()) {
        serial = "unknown";
    }

    return ipAddr + " (" + model + "), serial=" + serial + " [" + uri + "]";
}

static std::string stripUriSuffix(const std::string& desc) {
    size_t uriStart = desc.rfind(" [");
    size_t uriEnd = desc.size();
    if (uriStart != std::string::npos && uriEnd > 0 && desc[uriEnd - 1] == ']') {
        return desc.substr(0, uriStart);
    }
    return desc;
}

static std::string extractSerialFromDescription(const std::string& desc) {
    std::regex serialRgx("serial=([0-9A-Za-z]+)", std::regex::ECMAScript);
    std::smatch serialMatch;
    if (std::regex_search(desc, serialMatch, serialRgx) && serialMatch.size() > 1) {
        return serialMatch[1].str();
    }
    return "";
}

static std::string findCompatibleConfigKey(const json& devicesConf, const std::string& desc) {
    if (!devicesConf.is_object()) {
        return "";
    }

    if (devicesConf.contains(desc)) {
        return desc;
    }

    std::string normalizedDesc = stripUriSuffix(desc);
    std::string serial = extractSerialFromDescription(desc);

    for (auto it = devicesConf.begin(); it != devicesConf.end(); ++it) {
        const std::string existingKey = it.key();
        if (stripUriSuffix(existingKey) == normalizedDesc) {
            return existingKey;
        }
        if (!serial.empty() && extractSerialFromDescription(existingKey) == serial) {
            return existingKey;
        }
    }

    return "";
}

static std::string getPreferredNetworkUri(iio_context* ctx, const std::string& fallbackUri) {
    std::string ipAddr = getContextAttr(ctx, "ip,ip-addr");
    if (!ipAddr.empty()) {
        return "ip:" + ipAddr;
    }
    return fallbackUri;
}

static std::string buildContextDeviceName(const std::string& uri, const std::string& desc) {
    std::regex backendRgx(".+(?=:)", std::regex::ECMAScript);
    std::regex modelRgx("\\(.+(?=\\),)", std::regex::ECMAScript);
    std::regex serialRgx("serial=[0-9A-Za-z]+", std::regex::ECMAScript);

    std::string backend = "unknown";
    std::smatch backendMatch;
    if (std::regex_search(uri, backendMatch, backendRgx)) {
        backend = backendMatch[0];
    }

    std::string model = "Unknown";
    std::smatch modelMatch;
    if (std::regex_search(desc, modelMatch, modelRgx)) {
        model = modelMatch[0];
        int parenthPos = model.find('(');
        if (parenthPos != std::string::npos) {
            model = model.substr(parenthPos + 1);
        }
    }

    std::string serial = "unknown";
    std::smatch serialMatch;
    if (std::regex_search(desc, serialMatch, serialRgx)) {
        serial = serialMatch[0].str().substr(7);
    }

    return '(' + backend + ") " + model + " [" + serial + ']';
}

class PlutoSDRSourceModule : public ModuleManager::Instance {
public:
    PlutoSDRSourceModule(std::string name) {
        this->name = name;

        // Define valid samplerates
        for (int sr = 1000000; sr <= 61440000; sr += 500000) {
            samplerates.define(sr, getBandwdithScaled(sr), sr);
        }
        samplerates.define(61440000, getBandwdithScaled(61440000.0), 61440000.0);

        // Define valid bandwidths
        bandwidths.define(0, "Auto", 0);
        for (int bw = 1000000.0; bw <= 52000000; bw += 500000) {
            bandwidths.define(bw, getBandwdithScaled(bw), bw);
        }

        // Define gain modes
        gainModes.define("manual", "Manual", "manual");
        gainModes.define("fast_attack", "Fast Attack", "fast_attack");
        gainModes.define("slow_attack", "Slow Attack", "slow_attack");
        gainModes.define("hybrid", "Hybrid", "hybrid");

        iqModeSelect.define("cs16", "CS16", "cs16");
        iqModeSelect.define("cs8", "CS8", "cs8");

        // Enumerate devices
        refresh();

        // Select device
        config.acquire();
        devDesc = config.conf["device"];
        config.release();
        select(devDesc);

        // Register source
        handler.ctx = this;
        handler.selectHandler = menuSelected;
        handler.deselectHandler = menuDeselected;
        handler.menuHandler = menuHandler;
        handler.startHandler = start;
        handler.stopHandler = stop;
        handler.tuneHandler = tune;
        handler.stream = &stream;
        sigpath::sourceManager.registerSource("PlutoSDR", &handler);
    }

    ~PlutoSDRSourceModule() {
        stop(this);
        sigpath::sourceManager.unregisterSource("PlutoSDR");
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

    static void applySampleRateChange(PlutoSDRSourceModule* _this) {
        bool wasRunning = _this->running;
        double keepFreq = _this->freq;

        core::setInputSampleRate(_this->samplerate);

        if (!wasRunning) {
            return;
        }

        stop(_this);
        start(_this);
        tune(keepFreq, _this);
    }

private:
    std::string getBandwdithScaled(double bw) {
        char buf[1024];
        if (bw >= 1000000.0) {
            snprintf(buf, sizeof buf, "%.1lfMHz", bw / 1000000.0);
        }
        else if (bw >= 1000.0) {
            snprintf(buf, sizeof buf, "%.1lfKHz", bw / 1000.0);
        }
        else {
            snprintf(buf, sizeof buf, "%.1lfHz", bw);
        }
        return std::string(buf);
    }

    void refresh() {
        // Clear device list
        devices.clear();

        // Create scan context
        iio_scan_context* sctx = iio_create_scan_context(NULL, 0);
        if (!sctx) {
            flog::error("Failed get scan context");
            return;
        }

        // Enumerate devices
        iio_context_info** ctxInfoList;
        ssize_t count = iio_scan_context_get_info_list(sctx, &ctxInfoList);
        if (count < 0) {
            flog::error("Failed to enumerate contexts");
            iio_scan_context_destroy(sctx);
            return;
        }
        for (ssize_t i = 0; i < count; i++) {
            iio_context_info* info = ctxInfoList[i];
            std::string desc = iio_context_info_get_description(info);
            std::string duri = iio_context_info_get_uri(info);

            // If the device is not a plutosdr, don't include it
            bool isPluto = false;
            for (const auto type : deviceWhiteList) {
                if (containsInsensitive(desc, type)) {
                    isPluto = true;
                    break;
                }
            }
            if (!isPluto) {
                isPluto = looksLikePlutoCompatibleContext(duri);
                if (isPluto) {
                    flog::info("Accepted IIO device by capability probe: [{}] {}", duri, desc);
                }
            }
            if (!isPluto) {
                flog::warn("Ignored IIO device: [{}] {}", duri, desc);
                continue;
            }

            // Construct the device name
            std::string devName = buildContextDeviceName(duri, desc);

            // Skip duplicate devices
            if (devices.keyExists(desc) || devices.nameExists(devName) || devices.valueExists(duri)) { continue; }

            // Save device
            devices.define(desc, devName, duri);
        }
        iio_context_info_list_free(ctxInfoList);
        
        // Destroy scan context
        iio_scan_context_destroy(sctx);

        const std::vector<std::string> manualUris = {
            "ip:192.168.2.1",
            "ip:libresdr.local"
        };

        for (const auto& manualUri : manualUris) {
            if (devices.valueExists(manualUri)) {
                continue;
            }

            iio_context* ctx = iio_create_context_from_uri(manualUri.c_str());
            if (ctx == nullptr) {
                continue;
            }

            iio_device* phy = iio_context_find_device(ctx, "ad9361-phy");
            if (phy == nullptr) {
                phy = findDeviceByNameContains(ctx, "ad9361-phy");
            }
            iio_device* rxDev = findRxStreamingDevice(ctx);
            if (phy == nullptr || rxDev == nullptr) {
                iio_context_destroy(ctx);
                continue;
            }

            std::string preferredUri = getPreferredNetworkUri(ctx, manualUri);
            std::string desc = buildContextDescription(ctx, preferredUri);
            std::string devName = buildContextDeviceName(preferredUri, desc);
            iio_context_destroy(ctx);

            if (devices.keyExists(desc) || devices.nameExists(devName) || devices.valueExists(preferredUri)) {
                continue;
            }

            flog::info("Accepted IIO device by direct URI probe: [{}] {}", preferredUri, desc);
            devices.define(desc, devName, preferredUri);
        }

#ifdef __ANDROID__
        // On Android, a default IP entry must be made (TODO: This is not ideal since the IP cannot be changed)
        const char* androidURI = "ip:192.168.2.1";
        const char* androidName = "Default (192.168.2.1)";
        devices.define(androidName, androidName, androidURI);
#endif
    }

    void select(const std::string& desc) {
        // If no device is available, give up
        if (devices.empty()) {
            devDesc.clear();
            uri.clear();
            return;
        }

        std::string selectedDesc = desc;

        // If the device is not available, select the first one
        if (!devices.keyExists(selectedDesc)) {
            selectedDesc = devices.key(0);
        }

        // Update URI
        devDesc = selectedDesc;
        devId = devices.keyId(selectedDesc);
        uri = devices.value(devId);

        // TODO: Enumerate capabilities

        // Load defaults
        samplerate = 4000000;
        bandwidth = 0;
        gmId = 0;
        gain = -1.0f;
        iqModeId = 0;

        // Load device config
        config.acquire();
        std::string configKey = findCompatibleConfigKey(config.conf["devices"], devDesc);
        if (!configKey.empty() && configKey != devDesc) {
            config.conf["devices"][devDesc] = config.conf["devices"][configKey];
            if (config.conf["device"] == configKey) {
                config.conf["device"] = devDesc;
            }
            configKey = devDesc;
            config.release(true);
            config.acquire();
        }

        if (!configKey.empty() && config.conf["devices"][configKey].contains("samplerate")) {
            samplerate = config.conf["devices"][configKey]["samplerate"];
        }
        if (!configKey.empty() && config.conf["devices"][configKey].contains("bandwidth")) {
            bandwidth = config.conf["devices"][configKey]["bandwidth"];
        }
        if (!configKey.empty() && config.conf["devices"][configKey].contains("gainMode")) {
            // Select given gain mode or default if invalid
            std::string gm = config.conf["devices"][configKey]["gainMode"];
            if (gainModes.keyExists(gm)) {
                gmId = gainModes.keyId(gm);
            }
            else {
                gmId = 0;
            }
        }
        if (!configKey.empty() && config.conf["devices"][configKey].contains("gain")) {
            gain = config.conf["devices"][configKey]["gain"];
            gain = std::clamp(gain, -1.0f, 73.0f);
        }
        if (!configKey.empty() && config.conf["devices"][configKey].contains("iqmode")) {
            std::string iqMode = config.conf["devices"][configKey]["iqmode"];
            if (iqModeSelect.keyExists(iqMode)) {
                iqModeId = iqModeSelect.keyId(iqMode);
            }
        }
        config.release();

        // Update samplerate ID
        if (samplerates.keyExists(samplerate)) {
            srId = samplerates.keyId(samplerate);
        }
        else {
            srId = 0;
            samplerate = samplerates.value(srId);
        }

        // Update bandwidth ID
        if (bandwidths.keyExists(bandwidth)) {
            bwId = bandwidths.keyId(bandwidth);
        }
        else {
            bwId = 0;
            bandwidth = bandwidths.value(bwId);
        }
    }

    static void menuSelected(void* ctx) {
        PlutoSDRSourceModule* _this = (PlutoSDRSourceModule*)ctx;
        core::setInputSampleRate(_this->samplerate);
        flog::info("PlutoSDRSourceModule '{0}': Menu Select!", _this->name);
    }

    static void menuDeselected(void* ctx) {
        PlutoSDRSourceModule* _this = (PlutoSDRSourceModule*)ctx;
        flog::info("PlutoSDRSourceModule '{0}': Menu Deselect!", _this->name);
    }

    static void start(void* ctx) {
        PlutoSDRSourceModule* _this = (PlutoSDRSourceModule*)ctx;
        if (_this->running) { return; }

        // If no device is selected, give up
        if (_this->devDesc.empty() || _this->uri.empty()) { return; }

        // Open context
        _this->ctx = iio_create_context_from_uri(_this->uri.c_str());
        if (_this->ctx == NULL) {
            flog::error("Could not open pluto ({})", _this->uri);
            return;
        }

        // Get phy and device handle
        _this->phy = iio_context_find_device(_this->ctx, "ad9361-phy");
        if (_this->phy == NULL) {
            _this->phy = findDeviceByNameContains(_this->ctx, "ad9361-phy");
        }
        if (_this->phy == NULL) {
            flog::error("Could not connect to pluto phy");
            iio_context_destroy(_this->ctx);
            _this->ctx = NULL;
            return;
        }
        _this->dev = findRxStreamingDevice(_this->ctx);
        if (_this->dev == NULL) {
            flog::error("Could not connect to pluto dev");
            iio_context_destroy(_this->ctx);
            _this->ctx = NULL;
            return;
        }

        // Get RX channels
        _this->rxChan = iio_device_find_channel(_this->phy, "voltage0", false);
        _this->rxLO = iio_device_find_channel(_this->phy, "altvoltage0", true);
        iio_channel* txLO = iio_device_find_channel(_this->phy, "altvoltage1", true);
        if (_this->rxChan == NULL || _this->rxLO == NULL || txLO == NULL) {
            flog::error("Could not acquire required Pluto/LibreSDR channels");
            iio_context_destroy(_this->ctx);
            _this->ctx = NULL;
            return;
        }

        // Enable RX LO and disable TX
        iio_channel_attr_write_bool(txLO, "powerdown", true);
        iio_channel_attr_write_bool(_this->rxLO, "powerdown", false);

        // Configure RX channel
        iio_channel_attr_write(_this->rxChan, "rf_port_select", "A_BALANCED");
        if (_this->freq <= 0.0) {
            _this->freq = 100000000.0;
        }
        iio_channel_attr_write_longlong(_this->rxLO, "frequency", round(_this->freq));                              // Freq
        iio_channel_attr_write_longlong(_this->rxChan, "sampling_frequency", round(_this->samplerate));             // Sample rate
        iio_channel_attr_write_double(_this->rxChan, "hardwaregain", _this->gain);                                  // Gain
        iio_channel_attr_write(_this->rxChan, "gain_control_mode", _this->gainModes.value(_this->gmId).c_str());    // Gain mode
        _this->setBandwidth(_this->bandwidth);

        // Start worker thread
        _this->running = true;
        _this->workerThread = std::thread(worker, _this);
        flog::info("PlutoSDRSourceModule '{0}': Start!", _this->name);
    }

    static void stop(void* ctx) {
        PlutoSDRSourceModule* _this = (PlutoSDRSourceModule*)ctx;
        if (!_this->running) { return; }

        // Stop worker thread
        _this->running = false;
        _this->stream.stopWriter();
        _this->workerThread.join();
        _this->stream.clearWriteStop();

        // Close device
        if (_this->ctx != NULL) {
            iio_context_destroy(_this->ctx);
            _this->ctx = NULL;
        }

        flog::info("PlutoSDRSourceModule '{0}': Stop!", _this->name);
    }

    static void tune(double freq, void* ctx) {
        PlutoSDRSourceModule* _this = (PlutoSDRSourceModule*)ctx;
        _this->freq = freq;
        if (_this->running) {
            // Tune device
            iio_channel_attr_write_longlong(_this->rxLO, "frequency", round(freq));
        }
        flog::info("PlutoSDRSourceModule '{0}': Tune: {1}!", _this->name, freq);
    }

    static void menuHandler(void* ctx) {
        PlutoSDRSourceModule* _this = (PlutoSDRSourceModule*)ctx;

        if (_this->running) { SmGui::BeginDisabled(); }
        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Combo("##plutosdr_dev_sel", &_this->devId, _this->devices.txt)) {
            _this->select(_this->devices.key(_this->devId));
            core::setInputSampleRate(_this->samplerate);
            config.acquire();
            config.conf["device"] = _this->devices.key(_this->devId);
            config.release(true);
        }

        if (_this->running) { SmGui::EndDisabled(); }

        if (SmGui::Combo(CONCAT("##_pluto_sr_", _this->name), &_this->srId, _this->samplerates.txt)) {
            _this->samplerate = _this->samplerates.value(_this->srId);
            applySampleRateChange(_this);
            if (!_this->devDesc.empty()) {
                config.acquire();
                config.conf["devices"][_this->devDesc]["samplerate"] = _this->samplerate;
                config.release(true);
            }
        }

        // Refresh button
        if (_this->running) { SmGui::BeginDisabled(); }
        SmGui::SameLine();
        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Button(CONCAT("Refresh##_pluto_refr_", _this->name))) {
            _this->refresh();
            _this->select(_this->devDesc);
            core::setInputSampleRate(_this->samplerate);
        }
        if (_this->running) { SmGui::EndDisabled(); }

        SmGui::LeftLabel("Bandwidth");
        SmGui::FillWidth();
        if (SmGui::Combo(CONCAT("##_pluto_bw_", _this->name), &_this->bwId, _this->bandwidths.txt)) {
            _this->bandwidth = _this->bandwidths.value(_this->bwId);
            if (_this->running) {
                _this->setBandwidth(_this->bandwidth);
            }
            if (!_this->devDesc.empty()) {
                config.acquire();
                config.conf["devices"][_this->devDesc]["bandwidth"] = _this->bandwidth;
                config.release(true);
            }
        }

        SmGui::LeftLabel("Gain Mode");
        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Combo(CONCAT("##_pluto_gainmode_select_", _this->name), &_this->gmId, _this->gainModes.txt)) {
            if (_this->running) {
                iio_channel_attr_write(_this->rxChan, "gain_control_mode", _this->gainModes.value(_this->gmId).c_str());
            }
            if (!_this->devDesc.empty()) {
                config.acquire();
                config.conf["devices"][_this->devDesc]["gainMode"] = _this->gainModes.key(_this->gmId);
                config.release(true);
            }
        }

        SmGui::LeftLabel("Gain");
        if (_this->gmId) { SmGui::BeginDisabled(); }
        SmGui::FillWidth();
        if (SmGui::SliderFloatWithSteps(CONCAT("##_pluto_gain__", _this->name), &_this->gain, -1.0f, 73.0f, 1.0f, SmGui::FMT_STR_FLOAT_DB_NO_DECIMAL)) {
            if (_this->running) {
                iio_channel_attr_write_double(_this->rxChan, "hardwaregain", _this->gain);
            }
            if (!_this->devDesc.empty()) {
                config.acquire();
                config.conf["devices"][_this->devDesc]["gain"] = _this->gain;
                config.release(true);
            }
        }
        if (_this->gmId) { SmGui::EndDisabled(); }

        if (_this->running) { SmGui::BeginDisabled(); }
        SmGui::LeftLabel("IQ Mode");
        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Combo(CONCAT("##_pluto_iqmode_select_", _this->name), &_this->iqModeId, _this->iqModeSelect.txt)) {
            if (!_this->devDesc.empty()) {
                config.acquire();
                config.conf["devices"][_this->devDesc]["iqmode"] = _this->iqModeSelect.key(_this->iqModeId);
                config.release(true);
            }
        }
        if (_this->running) { SmGui::EndDisabled(); }
    }

    void setBandwidth(int bw) {
        if (bw > 0) {
            iio_channel_attr_write_longlong(rxChan, "rf_bandwidth", bw);
        }
        else {
            iio_channel_attr_write_longlong(rxChan, "rf_bandwidth", std::min<int>(samplerate, 52000000));
        }
    }

    static void worker(void* ctx) {
        PlutoSDRSourceModule* _this = (PlutoSDRSourceModule*)ctx;
        constexpr size_t MAX_BUFFER_PLUTO = 64000000 / 2;
        size_t bufferSize = (size_t)(_this->samplerate / 20.0f);
        size_t blockSize = std::min<size_t>(STREAM_BUFFER_SIZE, bufferSize);
        size_t kernelBuffers = std::max<size_t>(1, std::min<size_t>(8, MAX_BUFFER_PLUTO / std::max<size_t>(1, blockSize)));

        // Acquire channels
        iio_channel* rx0_i = iio_device_find_channel(_this->dev, "voltage0", 0);
        iio_channel* rx0_q = iio_device_find_channel(_this->dev, "voltage1", 0);
        if (!rx0_i || !rx0_q) {
            flog::error("Failed to acquire RX channels");
            return;
        }

        // Start streaming
        iio_channel_enable(rx0_i);
        if (_this->iqModeId == 0) {
            iio_channel_enable(rx0_q);
        }
        else {
            iio_channel_disable(rx0_q);
        }

        // Allocate buffer
        iio_device_set_kernel_buffers_count(_this->dev, (unsigned int)kernelBuffers);
        flog::info("PlutoSDRSourceModule '{0}': Allocate {1} kernel buffers", _this->name, kernelBuffers);
        flog::info("PlutoSDRSourceModule '{0}': Allocate buffer size {1}", _this->name, blockSize);
        iio_buffer* rxbuf = iio_device_create_buffer(_this->dev, blockSize, false);
        if (!rxbuf) {
            flog::error("Could not create RX buffer");
            iio_channel_disable(rx0_i);
            iio_channel_disable(rx0_q);
            return;
        }

        // Receive loop
        while (true) {
            // Read samples
            ssize_t refillRes = iio_buffer_refill(rxbuf);
            if (refillRes < 0) {
                flog::error("PlutoSDRSourceModule worker refill failed: {}", refillRes);
                break;
            }

            if (_this->iqModeId == 0) {
                int16_t* buf = (int16_t*)iio_buffer_start(rxbuf);
                if (!buf) { break; }
                volk_16i_s32f_convert_32f((float*)_this->stream.writeBuf, buf, 2048.0f, blockSize * 2);
            }
            else {
                int8_t* buf = (int8_t*)iio_buffer_start(rxbuf);
                if (!buf) { break; }
                volk_8i_s32f_convert_32f((float*)_this->stream.writeBuf, buf, 128.0f, blockSize * 2);
            }

            // Send out the samples
            if (!_this->stream.swap(blockSize)) { break; };
        }

        // Stop streaming
        iio_channel_disable(rx0_i);
        iio_channel_disable(rx0_q);

        // Free buffer
        iio_buffer_destroy(rxbuf);
    }

    std::string name;
    bool enabled = true;
    dsp::stream<dsp::complex_t> stream;
    SourceManager::SourceHandler handler;
    std::thread workerThread;
    iio_context* ctx = NULL;
    iio_device* phy = NULL;
    iio_device* dev = NULL;
    iio_channel* rxLO = NULL;
    iio_channel* rxChan = NULL;
    bool running = false;

    std::string devDesc = "";
    std::string uri = "";

    double freq = 100000000.0;
    int samplerate = 4000000;
    int bandwidth = 0;
    float gain = -1;

    int devId = 0;
    int srId = 0;
    int bwId = 0;
    int gmId = 0;
    int iqModeId = 0;

    OptionList<std::string, std::string> devices;
    OptionList<int, double> samplerates;
    OptionList<int, double> bandwidths;
    OptionList<std::string, std::string> gainModes;
    OptionList<std::string, std::string> iqModeSelect;
};

MOD_EXPORT void _INIT_() {
    json defConf = {};
    defConf["device"] = "";
    defConf["devices"] = {};
    config.setPath(std::string(core::getRoot()) + "/plutosdr_source_config.json");
    config.load(defConf);
    config.enableAutoSave();

    // Reset the configuration if the old format is still used
    config.acquire();
    if (!config.conf.contains("device") || !config.conf.contains("devices")) {
        config.conf = defConf;
        config.release(true);
    }
    else {
        config.release();
    }
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new PlutoSDRSourceModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(ModuleManager::Instance* instance) {
    delete (PlutoSDRSourceModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
