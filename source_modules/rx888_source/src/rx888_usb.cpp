// libusb side of the RX888 MkII driver, the firmware image and the gain tables.
//
// Ported from the OnAir receiver (https://github.com/adswill/OnAir, MIT): the libusb, firmware and gain
// sections of core/src/source/source_rx888.cpp plus core/src/source/rx888_usb.h. The only changes are the
// namespace, the logging (OnAir writes to stderr, this uses flog) and the folder list of the firmware,
// which the module passes in.
#include "rx888_usb.h"
#include <libusb.h>
#include <utils/flog.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>

namespace rx888 {

// ---------------------------------------------------------------------------------------------------------------- libusb

namespace {

constexpr int kControlTimeoutMs = 5000;
constexpr int kBulkTimeoutMs = 5000;

// One lock for the context, the device list and every open handle. list() and open() hold it for the whole libusb call, so a rescan
// after the firmware jump cannot destroy the context under them.
std::mutex gUsbMu;
libusb_context* gCtx = nullptr;
int gCtxRc = 1;
int gOpenLinks = 0;

libusb_context* usbContext(std::string& err) {
    // gUsbMu is held by the caller
    if (!gCtx && gCtxRc != 0) {
        gCtxRc = libusb_init(&gCtx);
        if (gCtxRc != 0) {
            gCtx = nullptr;
            err = std::string("libusb_init: ") + libusb_error_name(gCtxRc);
        }
    }
    return gCtx;
}

int statusCode(libusb_transfer_status s) {
    switch (s) {
    case LIBUSB_TRANSFER_TIMED_OUT: return LIBUSB_ERROR_TIMEOUT;
    case LIBUSB_TRANSFER_STALL: return LIBUSB_ERROR_PIPE;
    case LIBUSB_TRANSFER_NO_DEVICE: return LIBUSB_ERROR_NO_DEVICE;
    case LIBUSB_TRANSFER_OVERFLOW: return LIBUSB_ERROR_OVERFLOW;
    default: return LIBUSB_ERROR_IO;
    }
}

class LibusbLink : public Rx888Link {
public:
    LibusbLink(libusb_context* ctx, libusb_device_handle* h, bool claimed, bool ss, uint8_t ep)
        : ctx_(ctx), h_(h), claimed_(claimed), ss_(ss), ep_(ep) {}

    ~LibusbLink() override {
        std::lock_guard<std::mutex> g(gUsbMu);
        if (claimed_ && !gone_) { libusb_release_interface(h_, 0); }
        libusb_close(h_);
        if (gOpenLinks > 0) { gOpenLinks--; }
    }

    void noteDisconnected() override { gone_ = true; }

    int control(bool in, uint8_t request, uint16_t value, uint16_t index, uint8_t* data, uint16_t length) override {
        const uint8_t type = (uint8_t)((in ? LIBUSB_ENDPOINT_IN : LIBUSB_ENDPOINT_OUT) | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE);
        return libusb_control_transfer(h_, type, request, value, index, data, length, kControlTimeoutMs);
    }

    bool superSpeed() const override { return ss_; }

    void interrupt() override { libusb_interrupt_event_handler(ctx_); }

    int stream(size_t bytes, int queued, const std::function<void(const uint8_t*, size_t)>& onData, const std::function<bool()>& keepGoing,
               const std::function<void()>& onStarted) override {
        if (!ep_) { return LIBUSB_ERROR_NOT_FOUND; }
        struct State {
            const std::function<void(const uint8_t*, size_t)>* onData;
            const std::function<bool()>* keepGoing;
            std::atomic<int> inflight{0};
            std::atomic<int> fail{0};
        } st;
        st.onData = &onData;
        st.keepGoing = &keepGoing;
        auto cb = [](libusb_transfer* t) {
            auto* s = static_cast<State*>(t->user_data);
            if (t->status == LIBUSB_TRANSFER_COMPLETED) {
                if (t->actual_length > 0) { (*s->onData)(t->buffer, (size_t)t->actual_length); }
                if (!s->fail && (*s->keepGoing)()) {
                    const int r = libusb_submit_transfer(t);
                    if (r == 0) { return; }
                    s->fail = r;
                }
            }
            else if (t->status != LIBUSB_TRANSFER_CANCELLED && !s->fail) {
                s->fail = statusCode(t->status);
            }
            s->inflight--;
        };
        std::vector<std::vector<uint8_t>> bufs((size_t)queued, std::vector<uint8_t>(bytes));
        std::vector<libusb_transfer*> xfers((size_t)queued, nullptr);
        for (int i = 0; i < queued; i++) {
            xfers[(size_t)i] = libusb_alloc_transfer(0);
            libusb_fill_bulk_transfer(xfers[(size_t)i], h_, ep_, bufs[(size_t)i].data(), (int)bytes, cb, &st, kBulkTimeoutMs);
        }
        for (int i = 0; i < queued && !st.fail; i++) {
            st.inflight++;
            const int r = libusb_submit_transfer(xfers[(size_t)i]);
            if (r != 0) {
                st.inflight--;
                st.fail = r;
            }
        }
        if (!st.fail) { onStarted(); }
        bool cancelled = false;
        while (st.inflight > 0) {
            if (!cancelled && (st.fail || !keepGoing())) {
                for (auto* t : xfers) { libusb_cancel_transfer(t); }   // (a transfer that is not in flight answers NOT_FOUND: fine)
                cancelled = true;
            }
            timeval tv{0, 50000};
            const int r = libusb_handle_events_timeout_completed(ctx_, &tv, nullptr);
            if (r != 0 && r != LIBUSB_ERROR_TIMEOUT && r != LIBUSB_ERROR_INTERRUPTED && !st.fail) { st.fail = r; }
        }
        for (auto* t : xfers) { libusb_free_transfer(t); }
        return st.fail;
    }

private:
    libusb_context* ctx_;
    libusb_device_handle* h_;
    bool claimed_;
    bool gone_ = false;
    bool ss_;
    uint8_t ep_;
};

std::string stringOf(libusb_device_handle* h, uint8_t idx) {
    unsigned char b[256] = {0};
    if (!idx || libusb_get_string_descriptor_ascii(h, idx, b, sizeof b - 1) <= 0) { return {}; }
    return reinterpret_cast<char*>(b);
}

class LibusbRx888 : public Rx888Usb {
public:
    std::vector<Rx888UsbDevice> list() override {
        std::lock_guard<std::mutex> g(gUsbMu);
        std::vector<Rx888UsbDevice> out;
        std::string err;
        libusb_context* ctx = usbContext(err);
        if (!ctx) {
            logOnce("RX888: " + err);
            return out;
        }
        libusb_device** devs = nullptr;
        const ssize_t n = libusb_get_device_list(ctx, &devs);
        if (n < 0) { return out; }
        for (ssize_t i = 0; i < n; i++) {
            libusb_device_descriptor d{};
            if (libusb_get_device_descriptor(devs[i], &d) != 0 || d.idVendor != kRx888Vid) { continue; }
            if (d.idProduct != kRx888PidApp && d.idProduct != kRx888PidBoot) { continue; }
            Rx888UsbDevice u;
            u.vid = d.idVendor;
            u.pid = d.idProduct;
            u.bus = libusb_get_bus_number(devs[i]);
            u.address = libusb_get_device_address(devs[i]);
            if (!u.bootloader()) {
                // 04b4:00f1 is also what a LimeSDR-USB shows (LimeSuite's): the strings say which it is. Read once per device: a radio that is
                // open (Windows: one handle at a time) could not be asked again. A failed or empty read is not cached: the name often appears
                // only after Windows has finished binding the driver of a radio that just re-enumerated.
                const int key = u.bus * 256 + u.address;
                auto it = known_.find(key);
                if (it != known_.end() && it->second.pid == u.pid && it->second.nameError == 0 && !it->second.product.empty()) {
                    u = it->second;
                }
                else {
                    libusb_device_handle* h = nullptr;
                    const int r = libusb_open(devs[i], &h);
                    if (r != 0) {
                        u.nameError = r;
                        logOnce("RX888: a Cypress FX3 (04b4:00f1) could not be opened to read its name: " + usbErrorText(r));
                    }
                    else {
                        u.manufacturer = stringOf(h, d.iManufacturer);
                        u.product = stringOf(h, d.iProduct);
                        u.serial = stringOf(h, d.iSerialNumber);
                        libusb_close(h);
                        if (!u.product.empty()) { known_[key] = u; }
                    }
                }
                if (u.nameError == 0 && !rx888ProductIsMk2(u.product)) { continue; }
            }
            out.push_back(u);
        }
        libusb_free_device_list(devs, 1);
        return out;
    }

    std::unique_ptr<Rx888Link> open(const Rx888UsbDevice& d, int& code) override {
        std::lock_guard<std::mutex> g(gUsbMu);
        code = 0;
        std::string err;
        libusb_context* ctx = usbContext(err);
        if (!ctx) {
            code = LIBUSB_ERROR_OTHER;
            return nullptr;
        }
        libusb_device** devs = nullptr;
        const ssize_t n = libusb_get_device_list(ctx, &devs);
        if (n < 0) {
            code = (int)n;
            return nullptr;
        }
        libusb_device* dev = nullptr;
        for (ssize_t i = 0; i < n; i++) {
            libusb_device_descriptor dd{};
            if (libusb_get_device_descriptor(devs[i], &dd) == 0 && dd.idVendor == d.vid && dd.idProduct == d.pid &&
                libusb_get_bus_number(devs[i]) == d.bus && libusb_get_device_address(devs[i]) == d.address) {
                dev = devs[i];
                break;
            }
        }
        if (!dev) {
            libusb_free_device_list(devs, 1);
            code = LIBUSB_ERROR_NO_DEVICE;
            return nullptr;
        }
        libusb_device_handle* h = nullptr;
        int r = libusb_open(dev, &h);
        if (r != 0) {
            libusb_free_device_list(devs, 1);
            code = r;
            return nullptr;
        }
#ifndef _WIN32
        if (libusb_kernel_driver_active(h, 0) == 1) { libusb_detach_kernel_driver(h, 0); }
#endif
        r = libusb_claim_interface(h, 0);
        if (r != 0) {
            libusb_close(h);
            libusb_free_device_list(devs, 1);
            code = r;
            return nullptr;
        }
        // the speed (unknown counts as fine), and the bulk IN endpoint of the application firmware
        const int speed = libusb_get_device_speed(dev);
        const bool ss = speed == LIBUSB_SPEED_UNKNOWN || speed >= LIBUSB_SPEED_SUPER;
        uint8_t ep = 0;
        libusb_config_descriptor* cfg = nullptr;
        if (!d.bootloader() && libusb_get_active_config_descriptor(dev, &cfg) == 0) {
            for (int i = 0; i < cfg->bNumInterfaces && !ep; i++) {
                for (int a = 0; a < cfg->interface[i].num_altsetting && !ep; a++) {
                    for (int e = 0; e < cfg->interface[i].altsetting[a].bNumEndpoints; e++) {
                        const libusb_endpoint_descriptor& x = cfg->interface[i].altsetting[a].endpoint[e];
                        if ((x.bmAttributes & 0x03) == LIBUSB_TRANSFER_TYPE_BULK && (x.bEndpointAddress & 0x80)) {
                            ep = x.bEndpointAddress;
                            break;
                        }
                    }
                }
            }
            libusb_free_config_descriptor(cfg);
        }
        libusb_free_device_list(devs, 1);
        gOpenLinks++;
        return std::make_unique<LibusbLink>(ctx, h, true, ss, ep);
    }

    void rescan() override {
        std::lock_guard<std::mutex> g(gUsbMu);
        known_.clear();
        // The Windows backend keeps the device list of this context. After the FX3 re-enumerates, that list still describes the boot
        // loader that has gone, and the new 04b4:00f1 does not show up until the context is created again. No handle is open here: the
        // boot loader's was closed before this runs.
        if (gOpenLinks == 0 && gCtx) {
            libusb_exit(gCtx);
            gCtx = nullptr;
            gCtxRc = 1;
        }
    }

private:
    void logOnce(const std::string& m) {
        if (m == lastLog_) { return; }
        lastLog_ = m;
        flog::warn("{}", m);
    }
    std::map<int, Rx888UsbDevice> known_;
    std::string lastLog_;
};

} // namespace

Rx888Usb& rx888Usb() {
    static LibusbRx888 real;
    return real;
}

// ---------------------------------------------------------------------------------------------------------------- firmware image

std::vector<std::string> rx888FirmwarePaths(const std::vector<std::string>& folders) {
    std::vector<std::string> v;
    if (const char* e = getenv("SDRPP_RX888_FIRMWARE")) {
        if (*e) { v.push_back(e); }
    }
    for (const auto& dir : folders) {
        std::string d = dir;
        if (!d.empty() && d.back() != '/' && d.back() != '\\') { d += '/'; }
        v.push_back(d + "SDDC_FX3.img");
    }
    return v;
}

namespace {
uint32_t word(const std::vector<uint8_t>& b, size_t at) {
    uint32_t w;
    memcpy(&w, b.data() + at, 4);
    return w;   // (little endian hosts)
}
} // namespace

std::string rx888FirmwareProblem(const std::vector<uint8_t>& im) {
    if (im.size() < 16) { return "the file is too short to be a firmware image"; }
    if (im[0] != 'C' || im[1] != 'Y') { return "it has no Cypress \"CY\" signature"; }
    if (im[3] == 0xB1) { return "security binary images are not supported"; }
    if (im[3] == 0xB2) { return "VID:PID images are not supported"; }
    if (im[3] != 0xB0) { return "it is not a normal firmware binary image (type " + std::to_string(im[3]) + ")"; }
    size_t at = 4;
    uint32_t sum = 0;
    for (;;) {
        if (at + 8 > im.size()) { return "the section list ends inside the file"; }
        const uint32_t len = word(im, at);
        at += 8;
        if (len == 0) { break; }
        if ((uint64_t)len * 4 > im.size() - at) { return "a section runs past the end of the file"; }
        for (uint32_t i = 0; i < len; i++) { sum += word(im, at + (size_t)i * 4); }
        at += (size_t)len * 4;
    }
    if (at + 4 > im.size()) { return "the checksum is missing"; }
    if (word(im, at) != sum) { return "the checksum does not match (damaged download?)"; }
    return "";
}

bool rx888LoadFirmware(Rx888Link& link, const std::vector<uint8_t>& im, std::string& err) {
    const std::string bad = rx888FirmwareProblem(im);
    if (!bad.empty()) {
        err = "the firmware image is not usable: " + bad;
        return false;
    }
    size_t at = 4;
    uint32_t entry = 0;
    for (;;) {
        const uint32_t len = word(im, at);
        uint32_t addr = word(im, at + 4);
        at += 8;
        if (len == 0) {
            entry = addr;
            break;
        }
        const uint8_t* p = im.data() + at;
        size_t left = (size_t)len * 4;
        at += left;
        while (left > 0) {
            const size_t n = std::min<size_t>(4096, left);
            uint8_t back[4096];
            if (link.control(false, kEzUsbRw, (uint16_t)(addr & 0xFFFF), (uint16_t)(addr >> 16), const_cast<uint8_t*>(p), (uint16_t)n) != (int)n ||
                link.control(true, kEzUsbRw, (uint16_t)(addr & 0xFFFF), (uint16_t)(addr >> 16), back, (uint16_t)n) != (int)n) {
                err = "the firmware could not be written to the FX3's RAM (USB transfer failed)";
                return false;
            }
            if (memcmp(back, p, n) != 0) {
                err = "the firmware read back from the FX3's RAM differs from what was written";
                return false;
            }
            p += n;
            addr += (uint32_t)n;
            left -= n;
        }
    }
    const int r = link.control(false, kEzUsbRw, (uint16_t)(entry & 0xFFFF), (uint16_t)(entry >> 16), nullptr, 0);
    if (r != 0 && r != LIBUSB_ERROR_IO) {   // (the radio drops off the bus as it starts: an I/O error here is the normal case)
        err = "the FX3 did not start the firmware (" + std::string(usbErrorName(r)) + ")";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------- gains

namespace {
// the SDDC tables (RX888R2Radio.cpp): R828D LNA + mixer and VGA gains in dB per step; the AD8340 follows a formula
const double kVhfRfDb[29] = {0.0, 0.9, 1.4, 2.7, 3.7, 7.7, 8.7, 12.5, 14.4, 15.7, 16.6, 19.7, 20.7, 22.9, 25.4, 28.0, 29.7, 32.8,
                             33.8, 36.4, 37.2, 38.6, 40.2, 42.1, 43.4, 43.9, 44.5, 48.0, 49.6};
const double kVhfIfDb[16] = {-4.7, -2.1, 0.5, 3.5, 7.7, 11.2, 13.6, 14.9, 16.3, 19.5, 23.1, 26.5, 30.0, 33.7, 37.2, 40.8};
constexpr int kAd8340Steps = 127, kGainSweetPoint = 18;

int nearest(const double* t, int n, double want) {
    int best = 0;
    for (int i = 1; i < n; i++) {
        if (std::fabs(t[i] - want) < std::fabs(t[best] - want)) { best = i; }
    }
    return best;
}
} // namespace

double rx888HfIfGainDb(int index) {
    index = std::clamp(index, 0, kAd8340Steps - 1);
    return index > kGainSweetPoint ? 20.0 * std::log10(0.409 * (index - kGainSweetPoint + 3)) : 20.0 * std::log10(0.059 * (index + 1));
}

Rx888Gains rx888Gains(double slider, double hfAtt) {
    Rx888Gains g;
    slider = std::clamp(slider, 0.0, kRx888GainMaxDb);
    g.dat31 = (int)std::clamp(std::lround(hfAtt * 2), 0L, 63L);
    // HF: the attenuator is a setting of its own; the slider is the AD8340 IF gain, from its lowest step upwards
    double best = 1e9;
    const double target = rx888HfIfGainDb(0) + slider;
    for (int i = 0; i < kAd8340Steps; i++) {
        const double d = std::fabs(rx888HfIfGainDb(i) - target);
        if (d < best) {
            best = d;
            g.ifIndex = i;
        }
    }
    g.ad8340 = g.ifIndex > kGainSweetPoint ? (0x80 | (g.ifIndex - kGainSweetPoint + 3)) : (g.ifIndex + 1);
    g.hfIfDb = rx888HfIfGainDb(g.ifIndex);
    // VHF: the tuner's LNA and mixer first (the lowest noise), the VGA with what is left
    g.lna = nearest(kVhfRfDb, 29, (std::min)(slider, kVhfRfDb[28]));
    g.vga = nearest(kVhfIfDb, 16, kVhfIfDb[0] + (std::max)(0.0, slider - kVhfRfDb[28]));
    g.vhfRfDb = kVhfRfDb[g.lna];
    g.vhfIfDb = kVhfIfDb[g.vga];
    return g;
}

} // namespace rx888