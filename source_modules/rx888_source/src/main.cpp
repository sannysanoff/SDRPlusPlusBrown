// RX888 MkII source module for SDR++.
//
// Native driver for the RX888 MkII (a Cypress FX3 USB 3 board: an LTC2208-class 16-bit converter that samples the HF input directly, up to
// 130 Msps, and an R828D tuner behind the VHF input). No vendor library is used: the radio is driven over USB (libusb) with the protocol of the
// SDDC firmware, written from the MIT-licensed renardspark/SDDC_Driver (Interface.h, Core/usb/*, Core/radio/RX888R2Radio.cpp), which is ik1xpv's
// ExtIO_sddc / libsddc. See rx888_usb.h for the requests.
//
// Ported from the OnAir receiver (https://github.com/adswill/OnAir, MIT), core/src/source/source_rx888.cpp: the driver logic, the ADC clock and
// decimation plan, the HF/VHF switching order, the gain mapping and the Windows failure messages are OnAir's. What SDR++ supplies is the module
// wrapper, the menu and the sample stream (dsp::stream<dsp::complex_t>, the same convention as the other source modules).
//
// - The FX3 has no firmware of its own: it enumerates as 04b4:00f3 until the image of the SDDC project (SDDC_FX3.img) is written into its
//   RAM, then as 04b4:00f1 "RX888mk2". The image is installed next to the module (share/sdrpp/firmware, and Resources on macOS);
//   SDRPP_RX888_FIRMWARE points at a file instead. Loading the image makes the chip drop off the bus and come back at a new address. On
//   Windows the Cypress driver from the official RX-888 setup (HDSDR / ExtIO) binds 04b4:00f1; this module uses libusb and needs WinUSB for
//   that device. No driver is shipped.
// - The converter delivers REAL 16-bit samples at the ADC clock (64 Msps typically, 128 at most 130). HF (below half the clock): the wanted
//   frequency is shifted to zero, filtered and decimated by RealDdc (realddc.h). The rates are the ones OnAir offers (kRates): 0.25 to
//   61.44 Msps, delivered as the ADC clock divided by a whole number. VHF (the R828D tuner, from half the clock up to 1.75 GHz):
//   the tuner puts the channel on an IF of 4.57 MHz, spectrum inverted (SDDC's "sideband"), so the DDC is centred on the IF and its output
//   conjugated.
// - A real input has no sideband to reject: once the tuning frequency is mixed down, the conjugate of the wanted band sits at minus that
//   frequency. While the tuning frequency is below half the output rate - the whole HF band at 32 Msps, 3.15 MHz in the report - that ghost
//   falls inside the passband and shows as a mirrored copy at negative offsets. OnAir's design does the same; a tuning frequency above half
//   the output rate, or a lower rate, keeps it off the screen. The rates here are OnAir's (kRates): 0.25 to 61.44 Msps, delivered as the ADC
//   clock divided by a whole number, so an entry can come out slightly different (the "Match the rate" ADC clock hits it exactly).
// - The firmware starts the sample stream with STARTFX3 after the host has queued its bulk transfers, and stops it with STOPFX3.
#include <imgui.h>
#include <module.h>
#include <gui/gui.h>
#include <gui/smgui.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <utils/flog.h>
#include <utils/optionlist.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <complex>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <libusb.h>
#include "realddc.h"
#include "rx888_usb.h"

#ifdef _WIN32
#include <setupapi.h>
#endif

SDRPP_MOD_INFO{
    /* Name:            */ "rx888_source",
    /* Description:     */ "RX888 MkII source module for SDR++",
    /* Author:          */ "SDR++ Brown fork (port of the OnAir native RX888 driver)",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ -1
};

ConfigManager config;

#define CONCAT(a, b) ((std::string(a) + b).c_str())

namespace {

constexpr double kHfMinHz = 10e3, kVhfMaxHz = 1.75e9;
constexpr double kAdcMaxHz = 130e6, kAdcLowHz = 64e6, kAdcHighHz = 128e6;
constexpr double kIfHz = 4.57e6, kR828dRefHz = 16e6;   // the IF carrier of the R828D path and its reference clock (R828D_IF_CARRIER, R828D_FREQ)
constexpr double kMinOutHz = 0.25e6, kMaxOutHz = 64e6;
constexpr int kMaxDecim = 4096;
constexpr size_t kTransferBytes = 131072;   // a multiple of 16 bursts of 1 KiB, as SDDC
constexpr int kQueued = 32;
constexpr size_t kMaxBlocks = 256;          // what the processing thread may fall behind by (32 MiB) before the oldest blocks are dropped

int reenumWaitMs() { return rx888::kOnWindows ? 15000 : 8000; }
int reenumPollMs() { return 200; }

std::string mhzText(double hz) {
    char b[32];
    snprintf(b, sizeof b, "%.6g MHz", hz / 1e6);
    return b;
}

// The rates OnAir offers for this radio, in its order: its core/src/engine/rate_choice.cpp asks for the rates SDRs are usually
// given (0.25 to 20 Msps) and, for a radio that runs above 20 Msps, the wide captures up to 61.44, then the driver rounds each to
// the ADC clock divided by a whole number (plan()). The two programs now offer the same choice at the same figures.
struct RateOption { double hz; const char* label; };
constexpr RateOption kRates[] = {
    {0.25e6, "250 kHz"}, {0.5e6, "500 kHz"}, {1e6, "1 MHz"}, {1.024e6, "1.024 MHz"}, {2e6, "2 MHz"},
    {2.048e6, "2.048 MHz"}, {2.4e6, "2.4 MHz"}, {2.56e6, "2.56 MHz"}, {3.2e6, "3.2 MHz"}, {4e6, "4 MHz"},
    {5e6, "5 MHz"}, {6e6, "6 MHz"}, {8e6, "8 MHz"}, {10e6, "10 MHz"}, {12.5e6, "12.5 MHz"}, {16e6, "16 MHz"},
    {20e6, "20 MHz"}, {25e6, "25 MHz"}, {30.72e6, "30.72 MHz"}, {40e6, "40 MHz"}, {50e6, "50 MHz"},
    {56e6, "56 MHz"}, {61.44e6, "61.44 MHz"},
    // OnAir's ladder stops at 61.44; the radio itself is good to 64 (the HF input is specified to 64 MHz, and 64 Msps complex is an ADC
    // clock of 128 MHz halved, which the firmware takes as it takes any clock: STARTADC only retunes the Si5351). Hence one more entry.
    {64e6, "64 MHz"}};

// OnAir's TuneSettings, reduced to the settings this radio has
struct Settings {
    double sampleRate = 4e6;
    double centerHz = 100e6;
    double gainDb = 0;
    double hfAttenuatorDb = 0;
    double ppm = 0;
    std::string adc = "auto";     // auto, 64, 128, exact
    std::string input = "auto";   // auto, hf, vhf
    bool dither = false, rand = false, pga = false, biasTee = false;
};

inline double ppmCorrectedHz(double hz, double ppm) { return ppm == 0 ? hz : hz / (1.0 + ppm * 1e-6); }

void addNote(std::string& err, const std::string& note) {
    if (note.empty()) { return; }
    err += err.empty() ? note : "; " + note;
}

// The Windows driver bound to a USB device, or "" when it is not there. libusb only sees WinUSB / libusbK / libusb0, so a radio that
// re-enumerated onto the Cypress driver (the official RX-888 installer) is missing from list() even though Windows has it.
// Loaded by name: the Windows build does not link setupapi.
std::string windowsUsbService(uint16_t vid, uint16_t pid) {
#ifndef _WIN32
    (void)vid;
    (void)pid;
    return {};
#else
    HMODULE mod = LoadLibraryW(L"setupapi.dll");
    if (!mod) { return {}; }
    struct Guard {
        HMODULE m;
        ~Guard() { if (m) { FreeLibrary(m); } }
    } guard{mod};
    using Get_t = HDEVINFO(WINAPI*)(const GUID*, PCWSTR, HWND, DWORD);
    using Enum_t = BOOL(WINAPI*)(HDEVINFO, DWORD, PSP_DEVINFO_DATA);
    using Id_t = BOOL(WINAPI*)(HDEVINFO, PSP_DEVINFO_DATA, PWSTR, DWORD, PDWORD);
    using Prop_t = BOOL(WINAPI*)(HDEVINFO, PSP_DEVINFO_DATA, DWORD, PDWORD, PBYTE, DWORD, PDWORD);
    using Destroy_t = BOOL(WINAPI*)(HDEVINFO);
    auto pGet = (Get_t)GetProcAddress(mod, "SetupDiGetClassDevsW");
    auto pEnum = (Enum_t)GetProcAddress(mod, "SetupDiEnumDeviceInfo");
    auto pId = (Id_t)GetProcAddress(mod, "SetupDiGetDeviceInstanceIdW");
    auto pProp = (Prop_t)GetProcAddress(mod, "SetupDiGetDeviceRegistryPropertyW");
    auto pDestroy = (Destroy_t)GetProcAddress(mod, "SetupDiDestroyDeviceInfoList");
    if (!pGet || !pEnum || !pId || !pProp || !pDestroy) { return {}; }
    HDEVINFO info = pGet(nullptr, L"USB", nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (info == INVALID_HANDLE_VALUE) { return {}; }
    char needle[32];
    snprintf(needle, sizeof needle, "VID_%04X&PID_%04X", vid, pid);
    std::string found;
    SP_DEVINFO_DATA dev{};
    dev.cbSize = sizeof dev;
    for (DWORD i = 0; pEnum(info, i, &dev); i++) {
        WCHAR id[512];
        if (!pId(info, &dev, id, 512, nullptr)) { continue; }
        char ascii[512];
        size_t n = 0;
        for (; n + 1 < sizeof ascii && id[n]; n++) { ascii[n] = (char)toupper((unsigned char)id[n]); }
        ascii[n] = 0;
        if (!strstr(ascii, needle)) { continue; }
        WCHAR svc[128] = {};
        if (!pProp(info, &dev, SPDRP_SERVICE, nullptr, (PBYTE)svc, sizeof svc, nullptr)) {
            found = "present";
            continue;
        }
        std::string s;
        for (int k = 0; svc[k] && k < 127; k++) { s.push_back((char)tolower((unsigned char)svc[k])); }
        if (s.empty()) {
            found = "present";
            continue;
        }
        found = s;
        if (s.find("cyusb") != std::string::npos || s.find("cypress") != std::string::npos) { break; }
    }
    pDestroy(info);
    return found;
#endif
}

bool windowsLibusbDriverBound() {
#ifndef _WIN32
    return false;
#else
    const std::string svc = windowsUsbService(rx888::kRx888Vid, rx888::kRx888PidApp);
    return svc == "winusb" || svc == "libusbk" || svc == "libusb0" || svc.find("libusb") != std::string::npos;
#endif
}

// What start() shows when the firmware was written and the new device never became an RX888 MkII we can name.
// Kept free of the words start() treats as "try again" ("not found", "unplugged"): a retry looks up the boot loader's old
// address, which has gone, and would replace this with "RX888 MkII not found".
std::string reenumTimeoutMessage() {
    const std::string svc = windowsUsbService(rx888::kRx888Vid, rx888::kRx888PidApp);
    const bool cypress = svc.find("cyusb") != std::string::npos || svc.find("cypress") != std::string::npos;
    if (cypress) {
        return "RX888 MkII: the firmware was loaded and the radio left the bus, but SDR++ cannot see it come back. Windows has bound USB\\VID_04B4&PID_00F1 to the "
               "Cypress driver, which SDR++ cannot use. SDR++ does not ship a driver. Install the WinUSB driver for 04B4:00F1 with Zadig (zadig.akeo.ie); the "
               "Cypress driver stays what HDSDR and ExtIO use.";
    }
    const bool libusbDriver = svc == "winusb" || svc == "libusbk" || svc == "libusb0" || svc == "libusb-win32";
    if (!svc.empty() && svc != "present" && !libusbDriver) {
        return "RX888 MkII: the firmware was loaded and the radio left the bus, but SDR++ cannot see it come back. Windows has bound USB\\VID_04B4&PID_00F1 to "
               "the driver \"" + svc + "\". SDR++ uses libusb, needs the WinUSB driver for that device (Zadig, zadig.akeo.ie), and does not ship a driver.";
    }
    std::string e = "RX888 MkII: the firmware was loaded and the radio left the bus, but it did not come back in time. Unplug it, plug it in again and try once more.";
    if (rx888::kOnWindows && !libusbDriver) {
        e += " On Windows the USB package from the official RX-888 setup binds USB\\VID_04B4&PID_00F1 and hides the radio from SDR++. SDR++ needs the WinUSB "
             "driver for that device (Zadig, zadig.akeo.ie) and does not ship a driver.";
    }
    return e;
}

} // namespace

class RX888SourceModule : public ModuleManager::Instance {
public:
    RX888SourceModule(std::string name) {
        this->name = name;

        for (const RateOption& r : kRates) { sampleRates.define(r.hz, r.label, r.hz); }

        adcModes.define("auto", "Auto", "auto");
        adcModes.define("64", "64 Msps", "64");
        adcModes.define("128", "128 Msps", "128");
        adcModes.define("exact", "Match the rate (experimental)", "exact");

        inputs.define("auto", "Auto (by frequency)", "auto");
        inputs.define("hf", "HF", "hf");
        inputs.define("vhf", "VHF", "vhf");

        attenuations.define("0", "0 dB", 0.0);
        attenuations.define("6", "6 dB", 6.0);
        attenuations.define("12", "12 dB", 12.0);
        attenuations.define("18", "18 dB", 18.0);
        attenuations.define("24", "24 dB", 24.0);
        attenuations.define("31.5", "31.5 dB", 31.5);

        handler.ctx = this;
        handler.selectHandler = menuSelected;
        handler.deselectHandler = menuDeselected;
        handler.menuHandler = menuHandler;
        handler.startHandler = start;
        handler.stopHandler = stop;
        handler.tuneHandler = tune;
        handler.stream = &stream;

        refresh();

        config.acquire();
        std::string device = config.conf["device"];
        config.release();
        select(device);

        sigpath::sourceManager.registerSource("RX888", &handler);
    }

    ~RX888SourceModule() {
        stop(this);
        sigpath::sourceManager.unregisterSource("RX888");
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
    // ------------------------------------------------------------------------------------------------ device list and settings

    void refresh() {
        devices.clear();
        entries.clear();

        for (const auto& device : rx888::rx888Usb().list()) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%d:%d", device.bus, device.address);
            std::string key = device.serial.empty() ? std::string(buf) : device.serial;
            if (devices.keyExists(key)) { continue; }

            std::string label = "RX888 MkII";
            if (!device.serial.empty()) { label += " " + device.serial.substr(device.serial.size() > 8 ? device.serial.size() - 8 : 0); }
            if (device.bootloader()) { label += " (bootloader, firmware is loaded on start)"; }

            devices.define(key, label, key);
            entries[key] = device;
        }
    }

    void select(const std::string& serial) {
        if (devices.empty()) {
            selectedSerial.clear();
            return;
        }
        if (!devices.keyExists(serial)) {
            select(devices.key(0));
            return;
        }

        devId = devices.keyId(serial);
        selectedSerial = serial;
        deviceSerial_ = entries[serial].serial;
        deviceArg_ = std::to_string(entries[serial].bus) + ":" + std::to_string(entries[serial].address);

        // defaults, then whatever the config holds for this radio
        settings.sampleRate = 4e6;
        settings.gainDb = 0;
        settings.hfAttenuatorDb = 0;
        settings.ppm = 0;
        settings.adc = "auto";
        settings.input = "auto";
        settings.dither = false;
        settings.rand = false;
        settings.pga = false;
        settings.biasTee = false;
        adcId = adcModes.keyId("auto");
        inputId = inputs.keyId("auto");
        attId = attenuations.keyId("0");

        config.acquire();
        bool created = false;
        if (!config.conf["devices"].contains(selectedSerial)) {
            created = true;
            config.conf["devices"][selectedSerial]["sampleRate"] = 4e6;
            config.conf["devices"][selectedSerial]["gainDb"] = 0.0;
            config.conf["devices"][selectedSerial]["hfAttenuator"] = "0";
            config.conf["devices"][selectedSerial]["ppm"] = 0.0;
            config.conf["devices"][selectedSerial]["adc"] = "auto";
            config.conf["devices"][selectedSerial]["input"] = "auto";
            config.conf["devices"][selectedSerial]["dither"] = false;
            config.conf["devices"][selectedSerial]["randomizer"] = false;
            config.conf["devices"][selectedSerial]["pga"] = false;
            config.conf["devices"][selectedSerial]["biasTee"] = false;
        }
        json& cfg = config.conf["devices"][selectedSerial];
        if (cfg.contains("sampleRate")) {
            const double rate = cfg["sampleRate"];
            if (sampleRates.valueExists(rate)) { settings.sampleRate = rate; }
        }
        if (cfg.contains("gainDb")) { settings.gainDb = std::clamp<double>(cfg["gainDb"], 0, rx888::kRx888GainMaxDb); }
        if (cfg.contains("hfAttenuator")) {
            const std::string key = cfg["hfAttenuator"];
            if (attenuations.keyExists(key)) {
                attId = attenuations.keyId(key);
                settings.hfAttenuatorDb = attenuations[attId];
            }
        }
        if (cfg.contains("ppm")) { settings.ppm = std::clamp<double>(cfg["ppm"], -100, 100); }
        if (cfg.contains("adc")) {
            const std::string key = cfg["adc"];
            if (adcModes.keyExists(key)) {
                adcId = adcModes.keyId(key);
                settings.adc = adcModes[adcId];
            }
        }
        if (cfg.contains("input")) {
            const std::string key = cfg["input"];
            if (inputs.keyExists(key)) {
                inputId = inputs.keyId(key);
                settings.input = inputs[inputId];
            }
        }
        if (cfg.contains("dither")) { settings.dither = cfg["dither"]; }
        if (cfg.contains("randomizer")) { settings.rand = cfg["randomizer"]; }
        if (cfg.contains("pga")) { settings.pga = cfg["pga"]; }
        if (cfg.contains("biasTee")) { settings.biasTee = cfg["biasTee"]; }
        config.release(created);

        srId = sampleRates.valueId(settings.sampleRate);

        // The rate the module reports is the one the radio actually delivers (the ADC clock divided by a whole number)
        core::setInputSampleRate(outputRate());
    }

    void saveSetting(const std::string& key, const json& value) {
        if (selectedSerial.empty()) { return; }
        config.acquire();
        config.conf["devices"][selectedSerial][key] = value;
        config.release(true);
    }

    double outputRate() {
        if (!link_) { return settings.sampleRate; }
        return rate_ > 0 ? rate_ : settings.sampleRate;
    }

    // The firmware image: SDRPP_RX888_FIRMWARE, then the copy installed with the module
    std::string firmwarePath() {
        core::configManager.acquire();
        std::string resourcesDir = core::configManager.conf["resourcesDirectory"];
        core::configManager.release();
        const std::vector<std::string> paths = rx888::rx888FirmwarePaths({resourcesDir + "/firmware"});
        for (const auto& path : paths) {
            std::ifstream f(path, std::ios::binary);
            if (f) { return path; }
        }
        return {};
    }

    // ------------------------------------------------------------------------------------------------ the radio (OnAir's Rx888Source)

    struct Plan {
        bool vhf = false;
        uint32_t adc = 0;      // the ADC clock
        int decim = 2;
        double outHz = 0;
    };

    Plan plan(const Settings& s) const {
        Plan p;
        const double want = std::clamp(s.sampleRate > 0 ? s.sampleRate : 8e6, kMinOutHz, kMaxOutHz);
        const std::string mode = s.adc;
        double adc;
        if (mode == "exact") {
            int d = (std::max)(2, (int)std::ceil(kAdcLowHz / want * (1 - 1e-9)));   // (64 MHz / 9.142857 MHz is 7 and a bit of rounding)
            while (want * d > kAdcMaxHz && d > 2) { d--; }
            p.decim = (std::min)(d, kMaxDecim);
            adc = want * p.decim;
        }
        else {
            adc = mode == "64" ? kAdcLowHz : mode == "128" ? kAdcHighHz : (want <= 32e6 * 1.001 ? kAdcLowHz : kAdcHighHz);
            p.decim = (int)std::clamp(std::floor(adc / want * 1.001), 2.0, (double)kMaxDecim);
        }
        p.adc = (uint32_t)std::lround((std::min)(adc, kAdcMaxHz));
        p.outHz = (double)p.adc / p.decim;
        const std::string in = s.input;
        p.vhf = in == "vhf" || (in == "auto" && s.centerHz >= p.adc / 2.0);
        return p;
    }

    bool resolve(rx888::Rx888UsbDevice& out) {
        const std::vector<rx888::Rx888UsbDevice> all = rx888::rx888Usb().list();
        int bus = -1, addr = -1;
        if (sscanf(deviceArg_.c_str(), "%d:%d", &bus, &addr) != 2) { bus = addr = -1; }
        const rx888::Rx888UsbDevice* hit = nullptr;
        if (!deviceSerial_.empty()) {
            for (const auto& d : all) {
                if (d.serial == deviceSerial_) {
                    hit = &d;
                    break;
                }
            }
        }
        if (!hit) {
            for (const auto& d : all) {
                if (d.bus == bus && d.address == addr) {
                    hit = &d;
                    break;
                }
            }
        }
        if (!hit && all.size() == 1 && (deviceSerial_.empty() || all[0].bootloader())) { hit = &all[0]; }   // (the bootloader has no serial and a new address after unplugging)
        if (!hit) { return false; }
        out = *hit;
        return true;
    }

    // The FX3 is in its boot loader: write the image, wait for the radio to come back as 04b4:00f1
    bool uploadFirmware(rx888::Rx888UsbDevice& dev, std::string& err) {
        std::vector<uint8_t> image;
        const std::string path = firmwarePath();
        if (path.empty()) {
            err = "RX888 MkII: the FX3 firmware image (SDDC_FX3.img) was not found. The radio needs it each time it is plugged in; put the file in "
                  "SDR++'s firmware folder (share/sdrpp/firmware), or point SDRPP_RX888_FIRMWARE at it";
            return false;
        }
        {
            std::ifstream f(path, std::ios::binary);
            image.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        }
        const std::string bad = rx888::rx888FirmwareProblem(image);
        if (!bad.empty()) {
            err = "RX888 MkII: the FX3 firmware image (SDDC_FX3.img, " + path + ") is not usable: " + bad;
            return false;
        }

        setBefore_.clear();
        for (const auto& d : rx888::rx888Usb().list()) {
            if (!d.bootloader()) { setBefore_.push_back(d.bus * 256 + d.address); }
        }
        {
            int code = 0;
            std::unique_ptr<rx888::Rx888Link> l = rx888::rx888Usb().open(dev, code);
            if (!l) {
                err = "RX888 MkII: cannot open the radio's boot loader: " + rx888::usbErrorText(code);
                return false;
            }
            std::string e;
            if (!rx888::rx888LoadFirmware(*l, image, e)) {
                err = "RX888 MkII: " + e;
                return false;
            }
            flog::info("RX888 MkII: firmware loaded from {}", path);
            l->noteDisconnected();   // the jump drops the chip off the bus; releasing the interface on it can stall
        }
        rx888::rx888Usb().rescan();

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(reenumWaitMs());
        int pendingKey = -1;
        auto pendingSince = std::chrono::steady_clock::now();
        auto take = [&](const rx888::Rx888UsbDevice& d) {
            dev = d;
            deviceArg_ = std::to_string(d.bus) + ":" + std::to_string(d.address);   // the boot loader's address is gone
            if (!d.serial.empty()) { deviceSerial_ = d.serial; }
            flog::info("RX888 MkII: radio back at {}:{}", d.bus, d.address);
            return true;
        };
        // The new device has a new USB address. Keep looking until it is an RX888 MkII, or until Windows has had time to bind its
        // driver (several seconds; the old 6 s cap, then a retry of the boot loader address, reported "not found").
        for (;;) {
            const auto now = std::chrono::steady_clock::now();
            const std::vector<rx888::Rx888UsbDevice> nowList = rx888::rx888Usb().list();
            const rx888::Rx888UsbDevice* pick = nullptr;
            const rx888::Rx888UsbDevice* unread = nullptr;
            for (const auto& d : nowList) {
                const int key = d.bus * 256 + d.address;
                if (d.bootloader()) { continue; }
                // An app-mode radio that was already there is not the one that just left the bus. The boot loader's own address is not in
                // this set, so the running radio is taken even when Windows gives it that address again.
                if (std::find(setBefore_.begin(), setBefore_.end(), key) != setBefore_.end()) { continue; }
                if (d.nameError == 0 && rx888::rx888ProductIsMk2(d.product)) {
                    pick = &d;
                    break;
                }
                if (d.nameError != 0 && !unread) { unread = &d; }
            }
            if (pick) { return take(*pick); }
            if (unread) {
                const int key = unread->bus * 256 + unread->address;
                if (key != pendingKey) {
                    pendingKey = key;
                    pendingSince = now;
                }
                const bool driver = rx888::usbDriverMissing(unread->nameError);
                const bool graceOver = now - pendingSince >= std::chrono::seconds(1);
                if (now >= deadline || (driver && graceOver)) { return take(*unread); }   // open() reports the driver, or the name arrives on a retry
            }
            if (now >= deadline) { break; }
            auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
            auto step = std::chrono::milliseconds(reenumPollMs());
            if (step > left) { step = left; }
            if (step.count() > 0) { std::this_thread::sleep_for(step); }
        }
        err = reenumTimeoutMessage();
        return false;
    }

    bool openDevice(std::string& err) {
        std::lock_guard<std::mutex> lk(api_);
        if (link_) { return false; }
        rx888::Rx888UsbDevice dev;
        if (!resolve(dev)) {
            err = "RX888 MkII" + (deviceSerial_.empty() ? std::string() : " " + deviceSerial_) + " not found (unplugged?)";
            return false;
        }
        if (dev.bootloader() && !uploadFirmware(dev, err)) { return false; }
        int code = 0;
        link_ = rx888::rx888Usb().open(dev, code);
        if (!link_) {
            err = "RX888 MkII: cannot open the radio: " + rx888::usbErrorText(code);
            if (rx888::kOnWindows && code == LIBUSB_ERROR_NOT_SUPPORTED) {
                err += " The Cypress driver from the official RX-888 setup is not usable by SDR++, and SDR++ does not ship a driver. After the firmware loads, "
                       "the radio is USB\\VID_04B4&PID_00F1 and needs the WinUSB driver: install it once with Zadig (zadig.akeo.ie), choosing that device and "
                       "the driver WinUSB.";
            }
            return false;
        }
        if (!link_->superSpeed()) {
            link_.reset();
            err = "RX888 MkII: the radio is on a USB 2 connection; it needs a USB 3 (SuperSpeed) port and cable";
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(rx888::kOnWindows ? 25 : 5));
        setU8(rx888::kStopFx3, 0);   // as the SDDC driver: a stream left running by another program is stopped
        uint8_t info[4] = {0};
        const int r = link_->control(true, rx888::kTestFx3, 0, 0, info, sizeof info);
        if (r != (int)sizeof info && r != LIBUSB_ERROR_PIPE) {
            err = "RX888 MkII: the radio did not answer (" + std::string(rx888::usbErrorName(r)) + ")";
            link_.reset();
            return false;
        }
        if (info[0] != rx888::kModelRx888Mk2) {
            err = "RX888: the radio reports SDDC model " + std::to_string(info[0]) + " (not an RX888 MkII, which is 4): SDR++ drives the RX888 MkII only";
            link_.reset();
            return false;
        }
        fwMajor_ = info[1];
        fwMinor_ = info[2];
        flog::info("RX888 MkII: opened {}, SDDC firmware {}.{}", dev.serial, (int)fwMajor_, (int)fwMinor_);
        gpios_ = 0;
        vhf_ = -1;
        adc_ = 0;
        dat31_ = ad8340_ = lna_ = vga_ = -1;
        tuneHz_ = 0;
        streamed_ = false;
        return apply(settings, err, false);
    }

    void closeDevice() {
        std::lock_guard<std::mutex> lk(api_);
        if (!link_) { return; }
        if (streamed_) { setU8(rx888::kStopFx3, 0); }
        link_.reset();
    }

    std::string firmwareTag() const {
        if (fwMajor_ < 0) { return {}; }
        return ", SDDC firmware " + std::to_string((int)fwMajor_) + "." + std::to_string((int)fwMinor_);
    }

    std::string usbStepErr(const std::string& step) const {
        std::string e = "RX888 MkII: " + step + " (" + rx888::usbErrorText(lastUsbErr_) + ")" + firmwareTag();
        if (!rx888::kOnWindows) { return e; }
        const bool pipeIo = lastUsbErr_ == LIBUSB_ERROR_PIPE || lastUsbErr_ == LIBUSB_ERROR_IO;
        if (windowsLibusbDriverBound()) {
            if (pipeIo) {
                e += " WinUSB is already bound to this radio. Close HDSDR, ExtIO or any other program that may have the RX888 open, unplug the USB cable and plug it "
                     "in again, then retry. If it keeps failing, replace SDDC_FX3.img in SDR++'s firmware folder with the image from SDDC_Driver v2.0.0.";
            }
            else if (lastUsbErr_ == LIBUSB_ERROR_NOT_SUPPORTED) {
                e += " WinUSB is bound but this request was refused: the RAM firmware may be too old — load SDDC_FX3.img from SDDC_Driver v2.0.0 (unplug and replug "
                     "so the boot loader at 04b4:00f3 appears, or use the official RX-888 tools once).";
            }
        }
        else if (pipeIo || lastUsbErr_ == LIBUSB_ERROR_NOT_SUPPORTED) {
            e += " If another SDR program has the radio open, close it. Otherwise install WinUSB for USB\\VID_04B4&PID_00F1 with Zadig (zadig.akeo.ie); the Cypress "
                 "driver from the official RX-888 setup cannot be used by SDR++.";
        }
        return e;
    }

    // Vendor control transfers: retry PIPE/IO/TIMEOUT/BUSY (common on Windows WinUSB right after open).
    bool ctl(bool in, uint8_t request, uint16_t val, uint16_t idx, uint8_t* data, uint16_t len, int want, int tries = 0, int delayMs = 0) {
        if (!link_) {
            lastUsbErr_ = LIBUSB_ERROR_NO_DEVICE;
            return false;
        }
        const int n = tries > 0 ? tries : (rx888::kOnWindows ? 5 : 3);
        const int delay = delayMs > 0 ? delayMs : (rx888::kOnWindows ? 20 : 8);
        for (int a = 0; a < n; a++) {
            if (a) { std::this_thread::sleep_for(std::chrono::milliseconds(delay)); }
            const int r = link_->control(in, request, val, idx, data, len);
            if (r == want) {
                lastUsbErr_ = 0;
                return true;
            }
            if (in && request == rx888::kTestFx3 && want == 4 && r == LIBUSB_ERROR_PIPE) {
                lastUsbErr_ = 0;
                return true;
            }
            lastUsbErr_ = r;
            if (r != LIBUSB_ERROR_PIPE && r != LIBUSB_ERROR_IO && r != LIBUSB_ERROR_TIMEOUT && r != LIBUSB_ERROR_BUSY) { break; }
        }
        return false;
    }

    // RX888R2Radio::SetRFMode(HFMODE): Control(TUNERSTDBY) then VHF off. WinUSB often stalls the first EP0 write after open.
    bool setTunerStandby() {
        if (rx888::kOnWindows) { std::this_thread::sleep_for(std::chrono::milliseconds(15)); }
        uint8_t z = 0;
        if (ctl(false, rx888::kTunerStandby, 0, 0, &z, 1, 1, rx888::kOnWindows ? 12 : 6, rx888::kOnWindows ? 35 : 12)) { return true; }
        // Cold start (tuner never brought up this session): some Windows stacks keep returning PIPE even though standby is already the default.
        if (vhf_ < 0 && lastUsbErr_ == LIBUSB_ERROR_PIPE) {
            flog::warn("RX888 MkII: tuner standby stalled with PIPE on a cold start (continuing{})", firmwareTag());
            lastUsbErr_ = 0;
            return true;
        }
        return false;
    }

    bool setU8(uint8_t request, uint8_t v) { return ctl(false, request, 0, 0, &v, 1, 1); }

    bool setU32(uint8_t request, uint32_t v) {
        uint8_t b[4];
        memcpy(b, &v, 4);
        return ctl(false, request, 0, 0, b, 4, 4);
    }

    bool setU64(uint8_t request, uint64_t v) {
        uint8_t b[8];
        memcpy(b, &v, 8);
        return ctl(false, request, 0, 0, b, 8, 8);
    }

    bool setArg(uint16_t arg, uint16_t value) {
        uint8_t z = 0;
        return ctl(false, rx888::kSetArg, value, arg, &z, 1, 1);
    }

    bool setGpios(uint32_t w) {
        if (w == gpios_) { return true; }
        if (!setU32(rx888::kGpioFx3, w)) { return false; }
        gpios_ = w;
        return true;
    }

    uint32_t gpioWord(const Settings& s, bool vhf) const {
        uint32_t w = 0;
        if (s.dither) { w |= rx888::kGpioDither; }
        if (s.rand) { w |= rx888::kGpioRand; }
        if (s.pga) { w |= rx888::kGpioPgaEn; }
        if (s.biasTee) { w |= vhf ? rx888::kGpioBiasVhf : rx888::kGpioBiasHf; }
        if (vhf) { w |= rx888::kGpioVhfEn; }
        return w;
    }

    // (under api_) the HF <-> VHF switch, in the order of the SDDC driver (RX888R2Radio::SetRFMode)
    bool switchInput(const Settings& s, bool vhf, std::string& err) {
        if (vhf) {
            // the HF path is shut (maximum attenuation, lowest gain), the VHF antenna switched in, the tuner started
            if (!setArg(rx888::kArgDat31, 63)) {
                err = usbStepErr("the VHF input could not be switched on: HF attenuator");
                return false;
            }
            if (!setArg(rx888::kArgAd8340, 1)) {
                err = usbStepErr("the VHF input could not be switched on: HF IF gain");
                return false;
            }
            if (!setGpios(gpioWord(s, true))) {
                err = usbStepErr("the VHF input could not be switched on: antenna switch");
                return false;
            }
            if (!setArg(rx888::kArgAd8340, 0x80 | 3)) {
                err = usbStepErr("the VHF input could not be switched on: HF VGA");
                return false;
            }
            if (!setU32(rx888::kTunerInit, (uint32_t)kR828dRefHz)) {
                err = usbStepErr("the VHF input could not be switched on: R828D reference clock");
                return false;
            }
            dat31_ = 63;
            ad8340_ = 0x83;
            lna_ = vga_ = -1;
            tuneHz_ = 0;
        }
        else {
            if (!setTunerStandby()) {
                err = usbStepErr("the HF input could not be switched on: tuner standby");
                return false;
            }
            if (!setGpios(gpioWord(s, false))) {
                err = usbStepErr("the HF input could not be switched on: antenna switch");
                return false;
            }
            dat31_ = ad8340_ = -1;
        }
        vhf_ = vhf ? 1 : 0;
        flog::info("RX888 MkII: {} input", vhf ? "VHF (R828D tuner)" : "HF");
        return true;
    }

    // (under api_) live: the stream is running, only the frequency, the gains and the switches may change
    bool apply(const Settings& s, std::string& err, bool live) {
        if (!link_) { return false; }
        const Plan p = plan(s);
        if (!live) {
            // SDDC RadioHandler::Open: HF path (tuner standby, VHF off) before STARTADC. Starting the ADC first breaks RF switching on hardware.
            if (vhf_ < 0 && !switchInput(s, false, err)) { return false; }
            if (!setU32(rx888::kStartAdc, p.adc)) {
                err = usbStepErr("the ADC clock of " + mhzText(p.adc) + " was refused");
                return false;
            }
            std::lock_guard<std::mutex> dl(ddcMu_);
            adc_ = p.adc;
            decim_ = p.decim;
            ddc_.design(decim_);
            rate_ = p.outHz;
            requested_ = s.sampleRate;
            if (std::fabs(rate_ - requested_) > 1.0) {
                flog::info("RX888 MkII: the ADC clock of {} divided by {} gives {}, not the {} asked for", (uint32_t)adc_, decim_, rate_, requested_);
            }
        }
        if (vhf_ != (p.vhf ? 1 : 0) && !switchInput(s, p.vhf, err)) { return false; }
        const bool vhf = vhf_ == 1;
        if (!setGpios(gpioWord(s, vhf))) {
            err = "RX888 MkII: the switches (dither, randomizer, PGA, bias tee) could not be set (USB transfer failed)";
            return false;
        }
        const double f = (std::max)(s.centerHz, 0.0);
        const double tuneHz = ppmCorrectedHz(f, s.ppm);
        {
            std::lock_guard<std::mutex> dl(ddcMu_);
            vhfSpectrum_ = vhf;
            rand_ = s.rand;
            if (vhf) { ddc_.tune(adc_, kIfHz, 1.0 / 32768.0); }
            else { ddc_.tune(adc_, (std::max)(tuneHz, 0.0), 1.0 / 32768.0); }   // a mixer: above half the ADC clock it aliases, as the hardware would
        }
        if (vhf) {
            const uint64_t hz = (uint64_t)std::llround((std::max)(tuneHz, 0.0));
            if (hz != tuneHz_) {
                if (!setU64(rx888::kTunerTune, hz)) {
                    err = "RX888 MkII: the tuner was not told " + mhzText(f) + " (USB transfer failed)";
                    return false;
                }
                tuneHz_ = hz;
            }
            if (f < 24e6 || f > kVhfMaxHz) {
                addNote(err, "the VHF input (R828D tuner) of this RX888 MkII is specified for 24 MHz to 1750 MHz; the driver may not reach " + mhzText(f) +
                                 " or the signal may be weak or absent");
            }
        }
        else if (f < kHfMinHz || f > adc_ / 2.0) {
            addNote(err, "the HF input of this RX888 MkII is specified for " + mhzText(kHfMinHz) + " to " + mhzText(adc_ / 2.0) +
                             " (half the ADC clock), the VHF input starts there; the driver may not reach " + mhzText(f) +
                             " or the signal may be weak or absent");
        }
        const rx888::Rx888Gains g = rx888::rx888Gains(s.gainDb, s.hfAttenuatorDb);
        bool ok = true;
        if (vhf) {
            if (g.lna != lna_) {
                if (setArg(rx888::kArgR82xxLna, (uint16_t)g.lna)) { lna_ = g.lna; }
                else { ok = false; }
            }
            if (g.vga != vga_) {
                if (setArg(rx888::kArgR82xxVga, (uint16_t)g.vga)) { vga_ = g.vga; }
                else { ok = false; }
            }
        }
        else {
            if (g.dat31 != dat31_) {
                if (setArg(rx888::kArgDat31, (uint16_t)g.dat31)) { dat31_ = g.dat31; }
                else { ok = false; }
            }
            if (g.ad8340 != ad8340_) {
                if (setArg(rx888::kArgAd8340, (uint16_t)g.ad8340)) { ad8340_ = g.ad8340; }
                else { ok = false; }
            }
        }
        if (!ok && err.empty()) { err = "RX888 MkII: a gain setting was not accepted (USB transfer failed)"; }
        return true;
    }

    void convert(const int16_t* x, size_t n) {
        std::lock_guard<std::mutex> dl(ddcMu_);
        conv_.clear();
        ddc_.processS16(x, n, rand_, conv_);
        if (vhfSpectrum_) {
            for (auto& c : conv_) { c = std::conj(c); }   // the R828D path delivers the spectrum inverted
        }
        push(conv_.data(), conv_.size());
    }

    void push(const rx888::cf32* data, size_t n) {
        static_assert(sizeof(rx888::cf32) == sizeof(dsp::complex_t), "the DDC output must match the stream type");
        while (n) {
            const size_t chunk = (std::min)(n, (size_t)STREAM_BUFFER_SIZE);
            memcpy(stream.writeBuf, data, chunk * sizeof(rx888::cf32));
            if (!stream.swap((int)chunk)) { return; }
            data += chunk;
            n -= chunk;
        }
    }

    // The libusb callback only copies the block: the conversion (the DDC) runs on a thread of its own, so a slow block does not keep
    // the transfers from being queued again.
    void streamLoop() {
        rx888::Rx888Link* link;
        {
            std::lock_guard<std::mutex> lk(api_);
            link = link_.get();
        }
        if (!link || !run_) { return; }

        std::mutex qMu;
        std::condition_variable qCv;
        std::deque<std::vector<int16_t>> q;
        std::vector<std::vector<int16_t>> pool;
        bool done = false;
        size_t dropped = 0;
        std::thread worker([&] {
            SetThreadName("rx888_source");
            for (;;) {
                std::vector<int16_t> b;
                {
                    std::unique_lock<std::mutex> lk(qMu);
                    qCv.wait(lk, [&] { return !q.empty() || done; });
                    if (q.empty()) { return; }
                    b = std::move(q.front());
                    q.pop_front();
                }
                convert(b.data(), b.size());
                std::lock_guard<std::mutex> lk(qMu);
                pool.push_back(std::move(b));
            }
        });
        std::atomic<bool> startFailed{false};
        const int r = link->stream(
            kTransferBytes, kQueued,
            [&](const uint8_t* p, size_t n) {
                n &= ~(size_t)1;
                std::vector<int16_t> b;
                {
                    std::lock_guard<std::mutex> lk(qMu);
                    if (!pool.empty()) {
                        b = std::move(pool.back());
                        pool.pop_back();
                    }
                }
                b.resize(n / 2);
                memcpy(b.data(), p, n);   // (little endian hosts)
                {
                    std::lock_guard<std::mutex> lk(qMu);
                    if (q.size() >= kMaxBlocks) {
                        pool.push_back(std::move(q.front()));
                        q.pop_front();
                        dropped++;
                    }
                    q.push_back(std::move(b));
                }
                qCv.notify_one();
            },
            [&] { return run_.load() && !startFailed.load(); },
            [&] {
                std::lock_guard<std::mutex> lk(api_);
                streamed_ = true;
                if (!setU8(rx888::kStartFx3, 0)) { startFailed = true; }
            });
        {
            std::lock_guard<std::mutex> lk(qMu);
            done = true;
        }
        qCv.notify_all();
        worker.join();
        {
            std::lock_guard<std::mutex> lk(api_);
            if (link_) { setU8(rx888::kStopFx3, 0); }
        }
        if (dropped) { flog::warn("RX888 MkII: the computer fell behind: {0} blocks of samples were dropped (a lower ADC clock helps)", std::to_string(dropped)); }
        if (startFailed) { flog::error("RX888 MkII: the radio did not take the start command"); }
        else if (run_) { flog::warn("RX888 MkII: the stream ended ({})", r ? rx888::usbErrorName(r) : "stopped"); }
    }

    // ------------------------------------------------------------------------------------------------ module handlers

    static void menuSelected(void* ctx) {
        RX888SourceModule* _this = (RX888SourceModule*)ctx;
        core::setInputSampleRate(_this->outputRate());
        flog::info("RX888SourceModule '{0}': Menu Select!", _this->name);
    }

    static void menuDeselected(void* ctx) {
        RX888SourceModule* _this = (RX888SourceModule*)ctx;
        flog::info("RX888SourceModule '{0}': Menu Deselect!", _this->name);
    }

    static void start(void* ctx) {
        RX888SourceModule* _this = (RX888SourceModule*)ctx;
        if (_this->running) { return; }

        std::string err;
        if (!_this->openDevice(err)) {
            flog::error("{}", err);
            return;
        }

        _this->run_ = true;
        _this->usbThread = std::thread(&RX888SourceModule::streamLoop, _this);
        _this->running = true;
        core::setInputSampleRate(_this->outputRate());
        flog::info("RX888SourceModule '{0}': Start at {1} ({2})", _this->name, _this->outputRate(), _this->settings.adc);
    }

    static void stop(void* ctx) {
        RX888SourceModule* _this = (RX888SourceModule*)ctx;
        if (!_this->running) { return; }
        _this->running = false;

        _this->run_ = false;
        {
            std::lock_guard<std::mutex> lk(_this->api_);
            if (_this->link_) { _this->link_->interrupt(); }
        }
        if (_this->usbThread.joinable()) { _this->usbThread.join(); }
        _this->closeDevice();

        flog::info("RX888SourceModule '{0}': Stop!", _this->name);
    }

    static void tune(double freq, void* ctx) {
        RX888SourceModule* _this = (RX888SourceModule*)ctx;
        _this->settings.centerHz = freq;
        if (_this->running) {
            std::lock_guard<std::mutex> lk(_this->api_);
            std::string err;
            if (!_this->apply(_this->settings, err, true) && !err.empty()) { flog::error("{}", err); }
        }
        flog::info("RX888SourceModule '{0}': Tune: {1}!", _this->name, freq);
    }

    static void menuHandler(void* ctx) {
        RX888SourceModule* _this = (RX888SourceModule*)ctx;

        if (_this->running) { SmGui::BeginDisabled(); }

        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Combo(CONCAT("##_rx888_dev_", _this->name), &_this->devId, _this->devices.txt)) {
            _this->select(_this->devices.key(_this->devId));
            config.acquire();
            config.conf["device"] = _this->selectedSerial;
            config.release(true);
        }

        if (SmGui::Combo(CONCAT("##_rx888_sr_", _this->name), &_this->srId, _this->sampleRates.txt)) {
            _this->settings.sampleRate = _this->sampleRates.value(_this->srId);
            core::setInputSampleRate(_this->outputRate());
            _this->saveSetting("sampleRate", _this->settings.sampleRate);
        }

        SmGui::SameLine();
        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Button(CONCAT("Refresh##_rx888_refr_", _this->name))) {
            _this->refresh();
            _this->select(_this->selectedSerial);
        }

        SmGui::LeftLabel("ADC clock");
        SmGui::FillWidth();
        if (SmGui::Combo(CONCAT("##_rx888_adc_", _this->name), &_this->adcId, _this->adcModes.txt)) {
            _this->settings.adc = _this->adcModes.value(_this->adcId);
            _this->saveSetting("adc", _this->settings.adc);
        }

        SmGui::LeftLabel("Input");
        SmGui::FillWidth();
        if (SmGui::Combo(CONCAT("##_rx888_input_", _this->name), &_this->inputId, _this->inputs.txt)) {
            _this->settings.input = _this->inputs.value(_this->inputId);
            _this->saveSetting("input", _this->settings.input);
            if (_this->running) { _this->applyLive(); }
        }

        SmGui::LeftLabel("HF attenuator");
        SmGui::FillWidth();
        if (SmGui::Combo(CONCAT("##_rx888_att_", _this->name), &_this->attId, _this->attenuations.txt)) {
            _this->settings.hfAttenuatorDb = _this->attenuations.value(_this->attId);
            _this->saveSetting("hfAttenuator", _this->attenuations.key(_this->attId));
            if (_this->running) { _this->applyLive(); }
        }

        if (_this->running) { SmGui::EndDisabled(); }

        float gain = (float)_this->settings.gainDb;
        SmGui::LeftLabel("Gain");
        SmGui::FillWidth();
        if (SmGui::SliderFloat(CONCAT("##_rx888_gain_", _this->name), &gain, 0, (float)rx888::kRx888GainMaxDb)) {
            _this->settings.gainDb = gain;
            _this->saveSetting("gainDb", _this->settings.gainDb);
            if (_this->running) { _this->applyLive(); }
        }

        float ppm = (float)_this->settings.ppm;
        SmGui::LeftLabel("PPM");
        SmGui::FillWidth();
        if (SmGui::SliderFloatWithSteps(CONCAT("##_rx888_ppm_", _this->name), &ppm, -100, 100, 0.1f)) {
            _this->settings.ppm = ppm;
            _this->saveSetting("ppm", _this->settings.ppm);
            if (_this->running) { _this->applyLive(); }
        }

        SmGui::ForceSync();
        if (SmGui::Checkbox(CONCAT("Dither##_rx888_", _this->name), &_this->settings.dither)) {
            _this->saveSetting("dither", _this->settings.dither);
            if (_this->running) { _this->applyLive(); }
        }

        SmGui::ForceSync();
        if (SmGui::Checkbox(CONCAT("Randomizer##_rx888_", _this->name), &_this->settings.rand)) {
            _this->saveSetting("randomizer", _this->settings.rand);
            if (_this->running) { _this->applyLive(); }
        }

        SmGui::ForceSync();
        if (SmGui::Checkbox(CONCAT("PGA##_rx888_", _this->name), &_this->settings.pga)) {
            _this->saveSetting("pga", _this->settings.pga);
            if (_this->running) { _this->applyLive(); }
        }

        SmGui::ForceSync();
        if (SmGui::Checkbox(CONCAT("Bias tee##_rx888_", _this->name), &_this->settings.biasTee)) {
            _this->saveSetting("biasTee", _this->settings.biasTee);
            if (_this->running) { _this->applyLive(); }
        }
    }

    void applyLive() {
        std::lock_guard<std::mutex> lk(api_);
        std::string err;
        if (!apply(settings, err, true) && !err.empty()) { flog::error("{}", err); }
    }

    std::string name;
    bool enabled = true;
    bool running = false;
    std::atomic<bool> run_{false};

    SourceManager::SourceHandler handler;
    dsp::stream<dsp::complex_t> stream;

    std::unique_ptr<rx888::Rx888Link> link_;
    std::mutex api_;   // the control calls of this radio (retune, open, close, start, stop); the stream thread is in stream() meanwhile
    std::thread usbThread;

    Settings settings;

    OptionList<std::string, std::string> devices;
    OptionList<double, double> sampleRates;
    OptionList<std::string, std::string> adcModes;
    OptionList<std::string, std::string> inputs;
    OptionList<std::string, double> attenuations;
    std::map<std::string, rx888::Rx888UsbDevice> entries;

    std::string selectedSerial;
    std::string deviceArg_;
    std::string deviceSerial_;
    int devId = 0;
    int srId = 4;
    int adcId = 0;
    int inputId = 0;
    int attId = 0;

    uint32_t gpios_ = 0, adc_ = 0;
    int vhf_ = -1, decim_ = 2;
    int dat31_ = -1, ad8340_ = -1, lna_ = -1, vga_ = -1;   // what the radio is set to (-1: not yet)
    int lastUsbErr_ = 0;
    int fwMajor_ = -1, fwMinor_ = -1;
    uint64_t tuneHz_ = 0;
    bool streamed_ = false;
    double rate_ = 0, requested_ = 0;
    std::vector<int> setBefore_;   // (uploadFirmware) the app-mode radios there were before it

    std::mutex ddcMu_;   // the conversion state below: the stream against a retune
    rx888::RealDdc ddc_;
    bool vhfSpectrum_ = false, rand_ = false;
    std::vector<rx888::cf32> conv_;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    def["devices"] = json({});
    def["device"] = "";
    config.setPath(core::args["root"].s() + "/rx888_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new RX888SourceModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (RX888SourceModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}