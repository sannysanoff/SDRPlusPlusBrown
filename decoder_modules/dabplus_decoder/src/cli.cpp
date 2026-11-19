// Headless DAB/DAB+ decode of a recording, for e2e tests and offline checks.
//
// Input is 2048 kS/s complex baseband: either a raw file of unsigned 8-bit
// interleaved I/Q (the rtl_sdr / SDR# dump format, extension .raw/.bin/.iq) or
// a WAV holding 8-bit unsigned or 16-bit signed interleaved I/Q.
//
// Output is one JSON object on stdout: ensemble, services, sync state, the
// decoded audio format and its measured level. --audio writes the decoded
// audio to a WAV for listening.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <complex>
#include <chrono>
#include <thread>

#include "dab_engine.h"
#include "dabplus_fft.h"

namespace {

    constexpr double SAMPLE_RATE = 2048000.0;
    constexpr size_t BLOCK_SAMPLES = 16384;

    struct Options {
        std::string input;
        std::string audioOut;
        std::string selectLabel;
        std::string dumpSnapshot;   // write the final snapshot JSON here as well
        int mode = 1;
        int subchannel = -1;
        double seconds = 0.0;       // 0 = whole file
        int passes = 2;
        bool acceleratedFft = false;  // macOS: run the transforms on vDSP
    };

    bool readFile(const std::string& path, std::vector<uint8_t>& data) {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) { return false; }
        fseek(f, 0, SEEK_END);
        const long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (size <= 0) { fclose(f); return false; }
        data.resize(size_t(size));
        const size_t read = fread(data.data(), 1, data.size(), f);
        fclose(f);
        return read == data.size();
    }

    bool hasExtension(const std::string& path, const char* ext) {
        const std::string suffix = std::string(".") + ext;
        return path.size() > suffix.size() &&
               path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    // Minimal RIFF/WAVE reader: returns interleaved samples as float complex.
    bool loadWav(const std::vector<uint8_t>& file, std::vector<std::complex<float>>& out) {
        if (file.size() < 44) { return false; }
        if (memcmp(file.data(), "RIFF", 4) || memcmp(file.data() + 8, "WAVE", 4)) { return false; }
        uint16_t format = 0;
        uint16_t channels = 0;
        uint16_t bits = 0;
        size_t dataOffset = 0;
        size_t dataSize = 0;
        size_t pos = 12;
        while (pos + 8 <= file.size()) {
            const char* id = (const char*)file.data() + pos;
            uint32_t chunkSize = 0;
            memcpy(&chunkSize, file.data() + pos + 4, 4);
            const size_t body = pos + 8;
            if (!memcmp(id, "fmt ", 4) && body + 16 <= file.size()) {
                memcpy(&format, file.data() + body + 0, 2);
                memcpy(&channels, file.data() + body + 2, 2);
                memcpy(&bits, file.data() + body + 14, 2);
            }
            else if (!memcmp(id, "data", 4)) {
                dataOffset = body;
                dataSize = std::min<size_t>(chunkSize, file.size() - body);
            }
            pos = body + chunkSize + (chunkSize & 1);
        }
        if (format != 1 || channels != 2 || (bits != 8 && bits != 16)) { return false; }
        if (!dataOffset || !dataSize) { return false; }
        out.clear();
        out.reserve(dataSize / (bits / 8) / 2);
        if (bits == 16) {
            const int16_t* samples = (const int16_t*)(file.data() + dataOffset);
            for (size_t i = 0; i + 1 < dataSize / 2; i += 2) {
                out.push_back({ samples[i] / 32768.0f, samples[i + 1] / 32768.0f });
            }
        }
        else {
            const uint8_t* samples = file.data() + dataOffset;
            for (size_t i = 0; i + 1 < dataSize; i += 2) {
                out.push_back({ (float(samples[i]) - 128.0f) / 128.0f, (float(samples[i + 1]) - 128.0f) / 128.0f });
            }
        }
        return true;
    }

    void writeWav16(const std::string& path, const std::vector<int16_t>& pcm, uint32_t rate, bool stereo) {
        const uint16_t channels = stereo ? 2 : 1;
        const uint32_t dataBytes = uint32_t(pcm.size() * sizeof(int16_t));
        const uint32_t byteRate = rate * channels * 2;
        uint8_t header[44] = { 0 };
        memcpy(header, "RIFF", 4);
        uint32_t riffSize = 36 + dataBytes;
        memcpy(header + 4, &riffSize, 4);
        memcpy(header + 8, "WAVEfmt ", 8);
        uint32_t fmtSize = 16;
        memcpy(header + 16, &fmtSize, 4);
        uint16_t pcmFormat = 1;
        memcpy(header + 20, &pcmFormat, 2);
        memcpy(header + 22, &channels, 2);
        memcpy(header + 24, &rate, 4);
        memcpy(header + 28, &byteRate, 4);
        uint16_t blockAlign = channels * 2;
        memcpy(header + 32, &blockAlign, 2);
        uint16_t bits = 16;
        memcpy(header + 34, &bits, 2);
        memcpy(header + 36, "data", 4);
        memcpy(header + 40, &dataBytes, 4);
        FILE* f = fopen(path.c_str(), "wb");
        if (!f) { return; }
        fwrite(header, 1, sizeof(header), f);
        fwrite(pcm.data(), 1, dataBytes, f);
        fclose(f);
    }

    std::string jsonEscape(const std::string& text) {
        std::string out;
        for (char c : text) {
            if (c == '"' || c == '\\') { out += '\\'; out += c; }
            else if ((unsigned char)c < 0x20) { out += ' '; }
            else { out += c; }
        }
        return out;
    }

    std::string formatSnapshot(const dabplus::Snapshot& snap, int decodedFrames, double peak, double rms) {
        char buf[512];
        std::string json = "{";
        snprintf(buf, sizeof(buf), "\"sync_state\":%d,\"frames\":%d,\"desyncs\":%d,",
                 snap.sync_state, snap.frames, snap.desyncs);
        json += buf;
        snprintf(buf, sizeof(buf), "\"coarse_hz\":%.0f,\"fine_hz\":%.0f,\"signal\":%.3f,",
                 double(snap.coarse_hz), double(snap.fine_hz), double(snap.signal));
        json += buf;
        json += "\"ensemble\":\"" + jsonEscape(snap.ensemble) + "\",";
        snprintf(buf, sizeof(buf), "\"ensemble_id\":%d,\"ecc\":%d,", snap.ensemble_id, snap.ecc);
        json += buf;
        json += "\"dynamic_label\":\"" + jsonEscape(snap.dynamic_label) + "\",";
        snprintf(buf, sizeof(buf), "\"selected\":%d,\"audio_rate\":%u,\"audio_stereo\":%s,",
                 snap.selected, unsigned(snap.audio_rate), snap.audio_stereo ? "true" : "false");
        json += buf;
        snprintf(buf, sizeof(buf), "\"audio_frames\":%d,\"audio_peak\":%.4f,\"audio_rms\":%.4f,",
                 decodedFrames, peak, rms);
        json += buf;
        json += "\"services\":[";
        for (size_t i = 0; i < snap.services.size(); i++) {
            const auto& service = snap.services[i];
            if (i) { json += ","; }
            snprintf(buf, sizeof(buf), "{\"subchannel\":%d,\"component\":%d,\"service_id\":%d,\"type\":%d,\"label\":\"",
                     service.subchannel, service.component, service.service_id, service.type);
            json += buf;
            json += jsonEscape(service.label) + "\",\"short_label\":\"" + jsonEscape(service.short_label) + "\"}";
        }
        json += "]}";
        return json;
    }

}

int main(int argc, char** argv) {
    Options opts;
    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        auto next = [&](const char* what) -> std::string {
            if (i + 1 >= argc) { fprintf(stderr, "missing value for %s\n", what); exit(2); }
            return argv[++i];
        };
        if (arg == "-m" || arg == "--mode") { opts.mode = std::stoi(next("--mode")); }
        else if (arg == "-t" || arg == "--seconds") { opts.seconds = std::stod(next("--seconds")); }
        else if (arg == "--passes") { opts.passes = std::stoi(next("--passes")); }
        else if (arg == "--service") { opts.selectLabel = next("--service"); }
        else if (arg == "--subchannel") { opts.subchannel = std::stoi(next("--subchannel")); }
        else if (arg == "--audio") { opts.audioOut = next("--audio"); }
        else if (arg == "--accelerated-fft") { opts.acceleratedFft = true; }
        else if (arg == "-h" || arg == "--help") {
            printf("usage: %s <recording.raw|recording.wav> [--mode N] [--seconds S] [--passes N]\n"
                   "          [--service LABEL] [--subchannel N] [--audio out.wav] [--accelerated-fft]\n", argv[0]);
            return 0;
        }
        else if (!arg.empty() && arg[0] == '-') { fprintf(stderr, "unknown option %s\n", arg.c_str()); return 2; }
        else { opts.input = arg; }
    }
    if (opts.input.empty()) {
        fprintf(stderr, "usage: %s <recording.raw|recording.wav> [options]\n", argv[0]);
        return 2;
    }
    if (opts.mode < 1 || opts.mode > 4) { fprintf(stderr, "mode must be 1..4\n"); return 2; }

    std::vector<uint8_t> file;
    if (!readFile(opts.input, file)) { fprintf(stderr, "cannot read %s\n", opts.input.c_str()); return 1; }

    std::vector<std::complex<float>> samples;
    if (hasExtension(opts.input, "wav")) {
        if (!loadWav(file, samples)) { fprintf(stderr, "%s is not 2-channel 8/16-bit PCM WAV\n", opts.input.c_str()); return 1; }
    }
    else {
        samples.reserve(file.size() / 2);
        for (size_t i = 0; i + 1 < file.size(); i += 2) {
            samples.push_back({ (float(file[i]) - 128.0f) / 128.0f, (float(file[i + 1]) - 128.0f) / 128.0f });
        }
    }
    if (opts.seconds > 0.0) {
        const size_t limit = std::min(samples.size(), size_t(opts.seconds * SAMPLE_RATE));
        samples.resize(limit);
    }
    if (samples.empty()) { fprintf(stderr, "no samples in %s\n", opts.input.c_str()); return 1; }

    fprintf(stderr, "input: %.3f s at %.0f kS/s, mode %d\n",
            double(samples.size()) / SAMPLE_RATE, SAMPLE_RATE / 1000.0, opts.mode);

    // The transforms are the app's own FFT; on macOS this selects the vDSP
    // backend the display menu calls "accelerated FFT".
    dabplus::SetAcceleratedFFT(opts.acceleratedFft);

    dabplus::Engine engine(opts.mode, 1);
    std::vector<int16_t> pcm(4096 * 2);
    std::vector<int16_t> captured;
    int decodedFrames = 0;
    double peak = 0.0;
    double sumSquares = 0.0;
    uint32_t audioRate = 0;
    bool audioStereo = false;
    bool selectionApplied = false;

    for (int pass = 0; pass < std::max(1, opts.passes); pass++) {
        for (size_t offset = 0; offset < samples.size(); offset += BLOCK_SAMPLES) {
            const size_t count = std::min(BLOCK_SAMPLES, samples.size() - offset);
            engine.Process(samples.data() + offset, count);

            uint32_t rate = 0;
            bool stereo = false;
            size_t frames = 0;
            while ((frames = engine.ReadAudio(pcm.data(), 4096, rate, stereo)) > 0) {
                audioRate = rate;
                audioStereo = stereo;
                const size_t channels = stereo ? 2 : 1;
                for (size_t i = 0; i < frames * channels; i++) {
                    const float value = float(pcm[i]) / 32768.0f;
                    peak = std::max(peak, double(std::fabs(value)));
                    sumSquares += double(value) * double(value);
                    if (!opts.audioOut.empty()) { captured.push_back(pcm[i]); }
                }
                decodedFrames += int(frames);
            }

            if (!selectionApplied) {
                const auto snap = engine.GetSnapshot();
                if (!snap.services.empty()) {
                    int subchannel = opts.subchannel;
                    if (subchannel < 0 && !opts.selectLabel.empty()) {
                        for (const auto& service : snap.services) {
                            if (service.label.find(opts.selectLabel) != std::string::npos) {
                                subchannel = service.subchannel;
                                break;
                            }
                        }
                    }
                    if (subchannel < 0) { subchannel = snap.services.front().subchannel; }
                    engine.Select(subchannel);
                    selectionApplied = true;
                    fprintf(stderr, "selected subchannel %d\n", subchannel);
                }
            }
        }
    }

    // Let the pipeline publish the last frames and the audio ring drain.
    for (int i = 0; i < 3; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        uint32_t rate = 0;
        bool stereo = false;
        size_t frames = 0;
        while ((frames = engine.ReadAudio(pcm.data(), 4096, rate, stereo)) > 0) {
            audioRate = rate;
            audioStereo = stereo;
            decodedFrames += int(frames);
            const size_t channels = stereo ? 2 : 1;
            for (size_t i = 0; i < frames * channels; i++) {
                const float value = float(pcm[i]) / 32768.0f;
                peak = std::max(peak, double(std::fabs(value)));
                sumSquares += double(value) * double(value);
                if (!opts.audioOut.empty()) { captured.push_back(pcm[i]); }
            }
        }
    }

    if (!opts.audioOut.empty() && !captured.empty()) {
        writeWav16(opts.audioOut, captured, audioRate ? audioRate : 48000, audioStereo);
        fprintf(stderr, "wrote %s (%d frames)\n", opts.audioOut.c_str(), decodedFrames);
    }

    const auto snap = engine.GetSnapshot();
    const double rms = decodedFrames ? std::sqrt(sumSquares / double(decodedFrames * (audioStereo ? 2 : 1))) : 0.0;
    printf("%s\n", formatSnapshot(snap, decodedFrames, peak, rms).c_str());

    // The core library keeps graphical globals (textures and other GL objects)
    // that are torn down at exit and, with no GL context in a headless tool,
    // take the process down with them. The decode is finished and the result is
    // on stdout, so leave without running those destructors.
    fflush(stdout);
    fflush(stderr);
    std::_Exit(0);
}
