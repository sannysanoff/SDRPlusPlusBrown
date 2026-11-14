#pragma once
#include "../processor.h"
#include <fftw3.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <cmath>
#include <cstring>
#include <numeric>
#include <vector>

namespace dsp::noise_reduction {

// Voice (vowel) squelch: passes the demodulated audio only while it sounds like a spoken vowel, and mutes noise,
// carriers and data bursts - what Icom calls "VSC".
//
// The detector is a harmonic-motion test. A spoken vowel is a harmonic stack whose pitch glides, so the spacing of
// the partials changes with time and the higher partials move proportionally more than the lower ones. Three other
// explanations of the same spectrum fail that test:
//   - an unmodulated carrier or a CW note is one line (fewer than three partials),
//   - a rigidly drifting comb moves all partials by the same number of Hz (translation, not dilation),
//   - FSK/PSK data bursts and noise switch lines on and off instead of tracking them for 60 ms.
// The block tracks partials one to one, fits the harmonic lattice f_j = order_j * F0
// with one shared tuning offset (SSB shifts the whole comb), and requires the fitted pitch to move while the
// residual of harmonic dilation is at least twice as small as the residual of rigid translation.
//
// The long path uses an 80 ms window and 60 ms of tracking. Faster paths use 80/30 ms and 40/20 ms;
// those require pitch motion >= 3 Hz and motion SNR >= 3. All paths share the sensitivity threshold.
// The audio is delayed by 1/2 s so the first syllable of a word is not cut, and the gate stays open for
// `decay` seconds after the last vowel.
class VowelSquelch : public Processor<stereo_t, stereo_t> {
    using base_type = Processor<stereo_t, stereo_t>;
public:
    // Analysis geometry in physical units, so the numbers do not depend on the sound card rate.
    static constexpr double kAnalysisRate = 8000.0;  // analysis rate after decimation
    static constexpr double kHopSec = 0.01;          // one decision every 10 ms
    static constexpr double kWindowSec = 0.08;       // 80 ms trailing window
    static constexpr int kFftSize = 2048;            // zero padded: 3.9 Hz bins, 12.5 Hz resolution
    static constexpr int kHistory = 7;               // long path: partials across 7 frames (60 ms)
    static constexpr double kPitchMinHz = 70.0, kPitchMaxHz = 360.0, kPitchStepHz = 2.0;
    static constexpr double kPrerollSec = 0.5;       // 1/2 s of audio is buffered so a word is not cut
    static constexpr double kMaxStepHz = 18.0;       // how far a partial may move in one 10 ms hop
    static constexpr double kMatchTolHz = 8.0;       // lattice tolerance around a multiple of the pitch
    static constexpr double kMinSpanHz = 250.0;      // the comb has to cover a voice-like span
    static constexpr double kPeakProminenceDb = 5.0; // a partial has to stand out of its neighbourhood
    static constexpr double kMinContrast = 2.0;      // ... and out of the smoothed spectral envelope
    static constexpr double kBandLowHz = 150.0, kBandHighHz = 1600.0;
    static constexpr double kEnvelopeSigmaHz = 100.0;

    struct BandDiagnostics {
        double rawScore = 0.0, score = 0.0, support = 0.0;
        double pitch = 0.0, motion = 0.0, snr = 0.0, span = 0.0;
        double latticeError = -1.0, motionError = -1.0, translationError = -1.0, roughness = -1.0, strength = 0.0;
        int peaks = 0, tracks = 0, complete = 0, partials = 0, stage = 0;
        int windowMs = 80, trackingMs = 60;
        double offsetHz = 0.0;
        int firstHarmonic = 0, lastHarmonic = 0;
    };
    struct Diagnostics {
        uint64_t sequence = 0;
        int64_t utcMs = 0;
        double inputPeakDb = -300.0, inputRmsDb = -300.0, analysisGainDb = 0.0;
        double sampleRate = 0.0, analysisRate = 0.0, threshold = 0.0, holdLeft = 0.0, detectionAge = -1.0;
        int hop = 0, detections = 0, selectedBand = 0;
        double tunedHz = 0.0;
        int demodId = 0;
        bool detected = false, open = false;
        BandDiagnostics bands[2];
    };

    // Bounded history shared by the menu and its file logger; no file I/O runs on the DSP thread.
    std::vector<Diagnostics> diagnosticsSince(uint64_t sequence) const {
        std::lock_guard<std::mutex> lock(_diagnosticsMutex);
        std::vector<Diagnostics> result;
        for (const auto& item : _diagnostics) { if (item.sequence > sequence) { result.push_back(item); } }
        return result;
    }

    void setDiagnosticTuning(double tunedHz, int demodId) {
        _diagnosticTunedHz.store(tunedHz, std::memory_order_relaxed);
        _diagnosticDemodId.store(demodId, std::memory_order_relaxed);
    }

    static const char* rejectionReason(int stage) {
        switch (stage) {
        case 0: return "too few peaks";
        case 1: return "tracks too short";
        case 2: return "no 3-partial comb/span";
        case 3: return "harmonic fit > 5 Hz";
        case 4: return "movement resembles drift";
        case 5: return "pitch movement < 1.5 Hz";
        case 6: return "score below threshold";
        case 7: return "needs recent support";
        case 8: return "VOWEL DETECTED";
        case 9: return "short path: motion/SNR < 3";
        }
        return "unknown";
    }

    VowelSquelch() {}
    VowelSquelch(stream<stereo_t>* in, double sampleRate) { init(in, sampleRate); }

    ~VowelSquelch() {
        if (!base_type::_block_init) { return; }
        base_type::stop();
        freeAll();
    }

    void init(stream<stereo_t>* in, double sampleRate) {
        setup(sampleRate);
        base_type::init(in);
    }

    // how long the squelch stays open after the last vowel, in seconds
    void setDecay(double seconds) {
        std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
        _decaySec = std::clamp(seconds, 0.0, 10.0);
    }

    // 0 = only strong, clean voices open it; 1 = a weak and noisy voice is enough
    void setSensitivity(double sensitivity) {
        std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
        _sensitivity = std::clamp(sensitivity, 0.0, 1.0);
    }

    // kept for compatibility with the earlier analysis-window setting; the window is fixed by the algorithm now
    void setAnalysisSeconds(double) {}

    double getDecay() const { return _decaySec; }
    double getSensitivity() const { return _sensitivity; }
    double getThreshold() const { return thresholdFor(_sensitivity); }

    // state, for the menu, the procfs endpoints and the tests
    bool isOpen() const { return _open.load(std::memory_order_relaxed); }
    double lastScore() const { return _score.load(std::memory_order_relaxed); }
    double lastPitchHz() const { return _pitchHz.load(std::memory_order_relaxed); }
    double lastPitchMotionHz() const { return _pitchMotion.load(std::memory_order_relaxed); }
    double lastMotionSnr() const { return _motionSnr.load(std::memory_order_relaxed); }
    int lastPartials() const { return _partials.load(std::memory_order_relaxed); }
    double lastSpanHz() const { return _spanHz.load(std::memory_order_relaxed); }
    // A three-partial lattice was found; this diagnostic alone does not establish voice or extend the gate.
    bool lastLattice() const { return _lattice.load(std::memory_order_relaxed); }
    int detectionCount() const { return _detections.load(std::memory_order_relaxed); }
    // A vowel was detected in the last `windowMs` of *input* audio. This is the raw per-hop decision, so it is not
    // delayed by the preroll buffer and not stretched by the decay: it is what the panel light shows.
    bool detectedRecently(double windowMs = 100.0) const {
        const int last = _lastDetectionHop.load(std::memory_order_relaxed);
        if (last < 0) { return false; }
        const int hops = _hops.load(std::memory_order_relaxed);
        return (hops - last) * kHopSec * 1000.0 <= windowMs;
    }
    int hopCount() const { return _hops.load(std::memory_order_relaxed); }

    // 0 = strict, 1 = sensitive; 0.5 corresponds to a score of 1.8
    static double thresholdFor(double sensitivity) { return 3.6 * std::pow(0.25, std::clamp(sensitivity, 0.0, 1.0)); }

    int process(int count, const stereo_t* in, stereo_t* out) {
        const int delayLen = (int)_delay.size();
        for (int i = 0; i < count; i++) {
            // ---- output: the delayed audio, gated with a short ramp so that opening and closing do not click
            const stereo_t d = _delay[(size_t)_dpos];
            _delay[(size_t)_dpos] = in[i];
            _dpos = (_dpos + 1) % delayLen;

            if (_pendingDetection) {
                _pendingDetection = false;
                // the output is the audio from kPrerollSec ago, so the gate covers [t - preroll, t + decay) of the
                // original timeline, which is what the decay slider promises
                _openLeft = (int)std::lround((_decaySec + kPrerollSec) * _fs);
                _open.store(true, std::memory_order_relaxed);
            }
            const double target = (_openLeft > 0) ? 1.0 : 0.0;
            if (_gain < target) { _gain = (std::min)(target, _gain + _rampStep); }
            else if (_gain > target) { _gain = (std::max)(target, _gain - _rampStep); }
            const float g = (float)_gain;
            out[i].l = d.l * g;
            out[i].r = d.r * g;
            if (_openLeft > 0) {
                _openLeft--;
                if (_openLeft == 0) { _open.store(false, std::memory_order_relaxed); }
            }

            // ---- detection: the mono sum, decimated to the analysis rate
            feedMono((in[i].l + in[i].r) * 0.5f);
        }
        return count;
    }

    int run() {
        int count = base_type::_in->read();
        if (count < 0) { return -1; }
        process(count, base_type::_in->readBuf, base_type::out.writeBuf);
        base_type::_in->flush();
        if (!base_type::out.swap(count)) { return -1; }
        return count;
    }

private:
    struct Partial {
        double freq = 0.0;
        double contrast = 0.0;
    };
    struct Track {
        std::vector<Partial> items;
    };
    struct BandState {
        std::vector<Track> tracks;
        double previousScore1 = 0.0, previousScore2 = 0.0;
    };
    struct Evidence {
        double score = 0.0, pitch = 0.0, motion = 0.0, snr = 0.0, span = 0.0;
        int partials = 0;
        bool lattice = false;
        BandDiagnostics diagnostic;
    };

    static constexpr int kBins = kFftSize / 2 + 1;

    // ---------------------------------------------------------------- setup

    // modified Bessel function of the first kind, order zero (libc++ does not provide std::cyl_bessel_i)
    static double besselI0(double x) {
        double sum = 1.0, term = 1.0;
        const double q = x * x * 0.25;
        for (int k = 1; k < 32; k++) {
            term *= q / ((double)k * k);
            sum += term;
            if (term < sum * 1e-16) { break; }
        }
        return sum;
    }

    static std::vector<float> designLowpass(int taps, double cutoffNorm, double beta) {
        std::vector<float> h((size_t)taps, 0.0f);
        const int mid = (taps - 1) / 2;
        const double denom = besselI0(beta);
        double sum = 0.0;
        for (int i = 0; i < taps; i++) {
            const int n = i - mid;
            const double sinc = (n == 0) ? (2.0 * cutoffNorm) : (std::sin(2.0 * M_PI * cutoffNorm * n) / (M_PI * n));
            const double r = (double)n / mid;
            const double w = (std::fabs(r) >= 1.0) ? 0.0 : (besselI0(beta * std::sqrt(1.0 - r * r)) / denom);
            h[(size_t)i] = (float)(sinc * w);
            sum += h[(size_t)i];
        }
        if (sum != 0.0) { for (auto& v : h) { v = (float)(v / sum); } }
        return h;
    }

    void setup(double sampleRate) {
        std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
        _fs = (sampleRate > 1000.0) ? sampleRate : 48000.0;
        _decim = (std::max)(1, (int)std::lround(_fs / kAnalysisRate));
        _fa = _fs / _decim;
        _nw = (int)std::lround(kWindowSec * _fa);
        _hop = (std::max)(1, (int)std::lround(kHopSec * _fa));

        _ana.assign((size_t)_nw, 0.0f);
        _anaPos = 0;
        _anaFilled = 0;
        _hopCount = 0;

        // anti-alias before decimation
        const int taps = (std::max)(21, 20 * _decim + 1);
        _aaTaps = designLowpass(taps, 0.5 / _decim, 5.0);
        _aaHist.assign((size_t)taps, 0.0f);
        _aaPos = 0;
        _decimPhase = _decim - 1;
        _aaWarmup = (taps - 1) / 2;

        _fastNw = (int)std::lround(0.04 * _fa);
        _fastWin.assign((size_t)_fastNw, 0.0f);
        for (int i = 0; i < _fastNw; i++) { _fastWin[(size_t)i] = (float)(0.5 - 0.5 * std::cos(2.0 * M_PI * i / (_fastNw - 1))); }
        _frame.assign((size_t)_nw, 0.0f);
        _win.assign((size_t)_nw, 0.0f);
        for (int i = 0; i < _nw; i++) { _win[(size_t)i] = (float)(0.5 - 0.5 * std::cos(2.0 * M_PI * i / (_nw - 1))); }

        _delay.assign((size_t)(std::max)(1, (int)std::lround(kPrerollSec * _fs)), stereo_t{ 0.0f, 0.0f });
        _dpos = 0;
        _gain = 0.0;
        _rampStep = 1.0 / (std::max)(1.0, 0.005 * _fs);   // 5 ms ramp
        _openLeft = 0;
        _pendingDetection = false;
        _open.store(false, std::memory_order_relaxed);

        freeFft();
        _fftIn = (float*)fftwf_malloc(sizeof(float) * kFftSize);
        _fftOut = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * kBins);
        _plan = fftwf_plan_dft_r2c_1d(kFftSize, _fftIn, _fftOut, FFTW_ESTIMATE);
        _power.assign((size_t)kBins, 0.0);
        _logPow.assign((size_t)kBins, 0.0);

        _binHz = _fa / (double)kFftSize;
        _envSigmaBins = kEnvelopeSigmaHz / _binHz;
        _envRadius = (int)std::lround(4.0 * _envSigmaBins);
        _envKernel.assign((size_t)(2 * _envRadius + 1), 0.0);
        double ksum = 0.0;
        for (int i = -_envRadius; i <= _envRadius; i++) {
            const double v = std::exp(-0.5 * (i / _envSigmaBins) * (i / _envSigmaBins));
            _envKernel[(size_t)(i + _envRadius)] = v;
            ksum += v;
        }
        for (auto& v : _envKernel) { v /= ksum; }

        _peakDistance = (std::max)(2, (int)std::lround(35.0 / _binHz));
        _binsLow = (int)std::ceil(kBandLowHz / _binHz);
        _binsHigh = (std::min)(kBins - 2, (int)std::floor(kBandHighHz / _binHz));

        _pitches.clear();
        for (double p = kPitchMinHz; p <= kPitchMaxHz + 1e-9; p += kPitchStepHz) { _pitches.push_back(p); }

        for (auto& profile : _bands) { for (auto& band : profile) { band = BandState{}; } }
        _score.store(0.0, std::memory_order_relaxed);
        _pitchHz.store(0.0, std::memory_order_relaxed);
        _pitchMotion.store(0.0, std::memory_order_relaxed);
        _motionSnr.store(0.0, std::memory_order_relaxed);
        _partials.store(0, std::memory_order_relaxed);
        _spanHz.store(0.0, std::memory_order_relaxed);
        _detections.store(0, std::memory_order_relaxed);
        _lastDetectionHop.store(-1, std::memory_order_relaxed);
        _hops.store(0, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(_diagnosticsMutex);
            _diagnostics.clear();
        }
    }

    void freeFft() {
        if (_plan) { fftwf_destroy_plan(_plan); _plan = nullptr; }
        if (_fftIn) { fftwf_free(_fftIn); _fftIn = nullptr; }
        if (_fftOut) { fftwf_free(_fftOut); _fftOut = nullptr; }
    }

    void freeAll() {
        freeFft();
        base_type::_block_init = false;
    }

    // ---------------------------------------------------------------- audio path

    void feedMono(float x) {
        // decimating FIR
        const int taps = (int)_aaTaps.size();
        _aaHist[(size_t)_aaPos] = x;
        _aaPos = (_aaPos + 1) % taps;
        if (++_decimPhase < _decim) { return; }
        _decimPhase = 0;
        double acc = 0.0;
        int idx = _aaPos;                       // oldest sample, in circular order
        for (int i = 0; i < taps; i++) {
            acc += (double)_aaTaps[(size_t)(taps - 1 - i)] * _aaHist[(size_t)idx];
            idx = (idx + 1) % taps;
        }
        // Discard the causal FIR startup delay; the first analysed sample is aligned to input sample zero.
        if (_aaWarmup > 0) { _aaWarmup -= _decim; return; }
        _ana[(size_t)_anaPos] = (float)acc;
        _anaPos = (_anaPos + 1) % _nw;
        if (_anaFilled < _nw) { _anaFilled++; }
        if (_anaFilled < _fastNw) { return; }
        if (_anaFilled == _fastNw) { _hopCount = 0; analyse(); return; }
        if (++_hopCount < _hop) { return; }
        _hopCount = 0;
        analyse();
    }

    // ---------------------------------------------------------------- detector

    // mirrored index for the gaussian envelope, as scipy's 'reflect' boundary
    int mirrorIndex(int i) const {
        const int last = kBins - 1;
        while (i < 0 || i > last) {
            if (i < 0) { i = -i - 1; }
            if (i > last) { i = 2 * last + 1 - i; }
        }
        return i;
    }

    double envelopeAt(int bin) const {
        double acc = 0.0;
        for (int i = -_envRadius; i <= _envRadius; i++) {
            acc += _envKernel[(size_t)(i + _envRadius)] * _power[(size_t)mirrorIndex(bin + i)];
        }
        return acc;
    }

    static double wrapError(double diff, double pitch) {
        const double q = std::round(diff / pitch) * pitch;
        return std::fabs(diff - q);
    }

    // Minimum-cost one-to-one assignment for an n x m matrix (n rows <= m columns).
    // Hungarian algorithm with potentials, O(n^2 m).
    void assign(const std::vector<double>& cost, int n, int m, std::vector<int>& result) const {
        result.assign((size_t)n, -1);
        if (n <= 0 || m <= 0) { return; }
        const double inf = (std::numeric_limits<double>::max)() / 4.0;
        std::vector<double> u((size_t)n + 1, 0.0), v((size_t)m + 1, 0.0);
        std::vector<int> p((size_t)m + 1, 0), way((size_t)m + 1, 0);
        for (int i = 1; i <= n; i++) {
            p[0] = i;
            int j0 = 0;
            std::vector<double> minv((size_t)m + 1, inf);
            std::vector<char> used((size_t)m + 1, 0);
            do {
                used[(size_t)j0] = 1;
                const int i0 = p[(size_t)j0];
                double delta = inf;
                int j1 = 0;
                for (int j = 1; j <= m; j++) {
                    if (used[(size_t)j]) { continue; }
                    const double cur = cost[(size_t)(i0 - 1) * m + (size_t)(j - 1)] - u[(size_t)i0] - v[(size_t)j];
                    if (cur < minv[(size_t)j]) { minv[(size_t)j] = cur; way[(size_t)j] = j0; }
                    if (minv[(size_t)j] < delta) { delta = minv[(size_t)j]; j1 = j; }
                }
                for (int j = 0; j <= m; j++) {
                    if (used[(size_t)j]) { u[(size_t)p[(size_t)j]] += delta; v[(size_t)j] -= delta; }
                    else { minv[(size_t)j] -= delta; }
                }
                j0 = j1;
            } while (p[(size_t)j0] != 0);
            do {
                const int j1 = way[(size_t)j0];
                p[(size_t)j0] = p[(size_t)j1];
                j0 = j1;
            } while (j0 != 0);
        }
        for (int j = 1; j <= m; j++) {
            if (p[(size_t)j] > 0 && p[(size_t)j] <= n) { result[(size_t)(p[(size_t)j] - 1)] = j - 1; }
        }
    }

    void analyse() {
        // ---- 1. the window, hann weighted and zero padded to kFftSize
        const int start = _anaPos;              // the ring holds the oldest sample here after a full window
        for (int i = 0; i < _nw; i++) { _frame[(size_t)i] = _ana[(size_t)((start + i) % _nw)]; }
        // A constant gain per analysis window preserves the spectrum and keeps numerical floors level independent.
        double framePeak = 0.0;
        for (int i = 0; i < _nw; i++) { framePeak = (std::max)(framePeak, std::fabs((double)_frame[(size_t)i])); }
        const double analysisGain = (framePeak > 0.0) ? (0.5 / framePeak) : 0.0;
        auto spectrum = [&](int size, const std::vector<float>& window, double gain) {
            std::memset(_fftIn, 0, sizeof(float) * kFftSize);
            for (int i = 0; i < size; i++) { _fftIn[i] = (float)(_frame[(size_t)(_nw - size + i)] * gain * window[(size_t)i]); }
            fftwf_execute(_plan);
            for (int i = 0; i < kBins; i++) {
                const double re = _fftOut[i][0], im = _fftOut[i][1];
                _power[(size_t)i] = re * re + im * im;
                _logPow[(size_t)i] = 10.0 * std::log10(_power[(size_t)i] + 1e-15);
            }
        };
        auto prefer = [](const Evidence& a, const Evidence& b) {
            if (b.score > a.score || (b.score == a.score && b.diagnostic.stage > a.diagnostic.stage)) { return b; }
            return a;
        };
        Evidence high, low;
        if (_anaFilled >= _nw) {
            spectrum(_nw, _win, analysisGain);
            high = analyseBand(_bands[0][0], kBandLowHz, kMinSpanHz, kHistory, false, 80);
            low = analyseBand(_bands[0][1], 70.0, 150.0, kHistory, false, 80);
            // A shorter track needs clear pitch dilation, never just a weak supported score.
            high = prefer(high, analyseBand(_bands[1][0], kBandLowHz, kMinSpanHz, 4, true, 80));
            low = prefer(low, analyseBand(_bands[1][1], 70.0, 150.0, 4, true, 80));
        }
        double fastPeak = 0.0;
        for (int i = _nw - _fastNw; i < _nw; i++) { fastPeak = (std::max)(fastPeak, std::fabs((double)_frame[(size_t)i])); }
        const double fastGain = (fastPeak > 0.0) ? 0.5 / fastPeak : 0.0;
        spectrum(_fastNw, _fastWin, fastGain);
        high = prefer(high, analyseBand(_bands[2][0], kBandLowHz, kMinSpanHz, 3, true, 40));
        low = prefer(low, analyseBand(_bands[2][1], 70.0, 150.0, 3, true, 40));
        const Evidence& best = (low.score > high.score) ? low : high;
        _lattice.store(high.lattice || low.lattice, std::memory_order_relaxed);
        publish(best.score, best.pitch, best.motion, best.snr, best.span, best.partials);
        Diagnostics diagnostic;
        diagnostic.utcMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        double frameSq = 0.0;
        for (float value : _frame) { frameSq += (double)value * value; }
        diagnostic.inputPeakDb = 20.0 * std::log10((std::max)(framePeak, 1e-15));
        diagnostic.inputRmsDb = 10.0 * std::log10((std::max)(frameSq / _anaFilled, 1e-30));
        const double selectedGain = best.diagnostic.windowMs == 40 ? fastGain : analysisGain;
        diagnostic.analysisGainDb = (selectedGain > 0.0) ? 20.0 * std::log10(selectedGain) : 0.0;
        diagnostic.sampleRate = _fs;
        diagnostic.analysisRate = _fa;
        diagnostic.threshold = thresholdFor(_sensitivity);
        diagnostic.tunedHz = _diagnosticTunedHz.load(std::memory_order_relaxed);
        diagnostic.demodId = _diagnosticDemodId.load(std::memory_order_relaxed);
        diagnostic.hop = hopCount();
        diagnostic.detections = detectionCount();
        diagnostic.selectedBand = (low.score > high.score) ? 1 : 0;
        diagnostic.detected = best.score >= diagnostic.threshold;
        diagnostic.open = isOpen() || diagnostic.detected;
        diagnostic.holdLeft = diagnostic.detected ? _decaySec + kPrerollSec : _openLeft / _fs;
        const int lastDetection = _lastDetectionHop.load(std::memory_order_relaxed);
        diagnostic.detectionAge = (lastDetection >= 0) ? (diagnostic.hop - lastDetection) * kHopSec : -1.0;
        diagnostic.bands[0] = high.diagnostic;
        diagnostic.bands[1] = low.diagnostic;
        {
            std::lock_guard<std::mutex> lock(_diagnosticsMutex);
            diagnostic.sequence = ++_diagnosticsSequence;
            _diagnostics.push_back(diagnostic);
            if (_diagnostics.size() > 1000) { _diagnostics.pop_front(); }
        }
    }

    Evidence finishBand(BandState& state, Evidence evidence, bool rapid) {
        const double support = (std::max)(state.previousScore1, state.previousScore2);
        const double rawScore = evidence.score;
        const bool clearMotion = evidence.motion >= 3.0 && evidence.snr >= 3.0;
        if (!clearMotion) { evidence.score = rapid ? 0.0 : (std::min)(rawScore, 2.0 * support); }
        auto& d = evidence.diagnostic;
        d.rawScore = rawScore;
        d.score = evidence.score;
        d.support = support;
        if (rawScore > 0.0) {
            d.stage = (rapid && !clearMotion) ? 9 : ((evidence.score >= thresholdFor(_sensitivity)) ? 8 :
                      ((rawScore >= thresholdFor(_sensitivity)) ? 7 : 6));
        }
        state.previousScore2 = state.previousScore1;
        state.previousScore1 = rawScore;
        return evidence;
    }

    Evidence analyseBand(BandState& state, double lowHz, double minSpanHz, int history, bool rapid, int windowMs) {
        auto& _tracks = state.tracks;
        const int binsLow = (int)std::floor(lowHz / _binHz);
        // ---- 2. spectral peaks: local maxima with 5 dB prominence, at least 35 Hz apart, band limited,
        //         and standing 3 dB above the gaussian-smoothed envelope
        std::vector<int> maxima;
        maxima.reserve(64);
        for (int i = 1; i < kBins - 1; i++) {
            const double v = _logPow[(size_t)i];
            if (v > _logPow[(size_t)(i - 1)] && v >= _logPow[(size_t)(i + 1)]) { maxima.push_back(i); }
        }
        std::vector<double> prom(maxima.size(), 0.0);
        for (size_t k = 0; k < maxima.size(); k++) {
            const int i = maxima[k];
            const double v = _logPow[(size_t)i];
            double leftMin = v, rightMin = v;
            for (int j = i - 1; j >= 0; j--) {
                if (_logPow[(size_t)j] > v) { break; }
                leftMin = (std::min)(leftMin, _logPow[(size_t)j]);
            }
            for (int j = i + 1; j < kBins; j++) {
                if (_logPow[(size_t)j] > v) { break; }
                rightMin = (std::min)(rightMin, _logPow[(size_t)j]);
            }
            prom[k] = v - (std::max)(leftMin, rightMin);
        }
        std::vector<int> order(maxima.size());
        for (size_t k = 0; k < order.size(); k++) { order[k] = (int)k; }
        std::sort(order.begin(), order.end(), [&](int a, int b) { return _logPow[(size_t)maxima[(size_t)a]] > _logPow[(size_t)maxima[(size_t)b]]; });
        std::vector<int> accepted;
        accepted.reserve(maxima.size());
        for (int k : order) {
            bool tooClose = false;
            for (int a : accepted) {
                if (std::abs(a - maxima[(size_t)k]) < _peakDistance) { tooClose = true; break; }
            }
            if (!tooClose) { accepted.push_back(maxima[(size_t)k]); }
        }
        std::sort(accepted.begin(), accepted.end());

        std::vector<double> freqs, contrasts;
        for (int i : accepted) {
            const auto at = std::lower_bound(maxima.begin(), maxima.end(), i);
            if (prom[(size_t)(at - maxima.begin())] < kPeakProminenceDb) { continue; }
            if (i < binsLow || i > _binsHigh) { continue; }
            const double env = envelopeAt(i);
            const double contrast = (env > 0.0) ? (_power[(size_t)i] / env) : 0.0;
            if (contrast < kMinContrast) { continue; }
            const double a = _logPow[(size_t)(i - 1)], b = _logPow[(size_t)i], c = _logPow[(size_t)(i + 1)];
            const double denom = a - 2.0 * b + c;
            const double delta = (std::fabs(denom) > 1e-12) ? (0.5 * (a - c) / denom) : 0.0;
            freqs.push_back((i + delta) * _binHz);
            contrasts.push_back(contrast);
        }

        Evidence evidence;
        evidence.diagnostic.windowMs = windowMs;
        evidence.diagnostic.trackingMs = (history - 1) * 10;
        evidence.diagnostic.peaks = (int)freqs.size();
        evidence.diagnostic.stage = (freqs.size() >= 3) ? 1 : 0;

        // ---- 3. bridge at most one missing 10 ms peak, and only use a track when it is observed again
        std::vector<Track> next;
        next.reserve(_tracks.size() + freqs.size());
        std::vector<char> used(freqs.size(), 0), matched(_tracks.size(), 0);
        if (!_tracks.empty() && !freqs.empty()) {
            const int n = (int)_tracks.size(), m = (int)freqs.size();
            // The assignment uses the raw distances, exactly like a minimum-cost matching; the 18 Hz limit is
            // applied afterwards, so a hopeless pair does not push another pair out of the matching.
            std::vector<double> cost((size_t)n * (size_t)m, 0.0);
            for (int i = 0; i < n; i++) {
                const double last = _tracks[(size_t)i].items.back().freq;
                for (int j = 0; j < m; j++) { cost[(size_t)i * m + (size_t)j] = std::fabs(last - freqs[(size_t)j]); }
            }
            std::vector<int> match;
            if (n <= m) { assign(cost, n, m, match); }
            else {
                // transpose, so the smaller dimension is the row count
                std::vector<double> transposed((size_t)m * (size_t)n, 0.0);
                for (int i = 0; i < n; i++) { for (int j = 0; j < m; j++) { transposed[(size_t)j * n + i] = cost[(size_t)i * m + j]; } }
                std::vector<int> reverse;
                assign(transposed, m, n, reverse);
                match.assign((size_t)n, -1);
                for (int j = 0; j < m; j++) { if (reverse[(size_t)j] >= 0) { match[(size_t)reverse[(size_t)j]] = j; } }
            }
            for (int i = 0; i < n; i++) {
                const int j = match[(size_t)i];
                if (j < 0 || cost[(size_t)i * m + (size_t)j] >= kMaxStepHz) { continue; }
                Track t = _tracks[(size_t)i];
                if (t.items.size() > 1 && t.items.back().contrast == 0.0) {
                    t.items.back().freq = 0.5 * (t.items[t.items.size() - 2].freq + freqs[(size_t)j]);
                }
                t.items.push_back(Partial{ freqs[(size_t)j], contrasts[(size_t)j] });
                if ((int)t.items.size() > history) { t.items.erase(t.items.begin()); }
                next.push_back(std::move(t));
                used[(size_t)j] = 1;
                matched[(size_t)i] = 1;
            }
        }
        for (size_t i = 0; i < _tracks.size(); i++) {
            const auto& items = _tracks[i].items;
            if (matched[i] || items.size() < 2 || items.back().contrast == 0.0) { continue; }
            bool missing = false;
            for (const auto& item : items) { if (item.contrast == 0.0) { missing = true; break; } }
            if (missing) { continue; }
            Track t = _tracks[i];
            t.items.push_back(Partial{ items.back().freq, 0.0 });
            if ((int)t.items.size() > history) { t.items.erase(t.items.begin()); }
            next.push_back(std::move(t));
        }
        for (size_t j = 0; j < freqs.size(); j++) {
            if (used[j]) { continue; }
            Track t;
            t.items.push_back(Partial{ freqs[j], contrasts[j] });
            next.push_back(std::move(t));
        }
        _tracks = std::move(next);

        evidence.diagnostic.tracks = (int)_tracks.size();
        if ((int)_tracks.size() < 3) { return finishBand(state, evidence, rapid); }

        // ---- 4. complete tracks only
        std::vector<int> complete;
        for (size_t i = 0; i < _tracks.size(); i++) {
            if ((int)_tracks[i].items.size() == history && _tracks[i].items.back().contrast > 0.0) { complete.push_back((int)i); }
        }
        evidence.diagnostic.complete = (int)complete.size();
        if ((int)complete.size() < 3) { return finishBand(state, evidence, rapid); }
        evidence.diagnostic.stage = 2;

        const int t_count = (int)complete.size();
        const int h = history;
        std::vector<std::vector<double>> traj((size_t)t_count, std::vector<double>((size_t)h, 0.0));
        std::vector<double> strength((size_t)t_count, 0.0), meanFreq((size_t)t_count, 0.0);
        for (int i = 0; i < t_count; i++) {
            const auto& items = _tracks[(size_t)complete[(size_t)i]].items;
            double sf = 0.0, sc = 0.0;
            int observed = 0;
            for (int k = 0; k < h; k++) {
                traj[(size_t)i][(size_t)k] = items[(size_t)k].freq;
                sf += items[(size_t)k].freq;
                sc += items[(size_t)k].contrast;
                if (items[(size_t)k].contrast > 0.0) { observed++; }
            }
            meanFreq[(size_t)i] = sf / h;
            strength[(size_t)i] = sc / observed;
        }
        std::vector<double> difference((size_t)t_count * (size_t)t_count, 0.0);
        for (int i = 0; i < t_count; i++) {
            for (int j = 0; j < t_count; j++) { difference[(size_t)i * t_count + j] = meanFreq[(size_t)i] - meanFreq[(size_t)j]; }
        }

        // ---- 5. harmonic lattice search
        double bestScore = 0.0, bestPitch = 0.0, bestMotion = 0.0, bestSnr = 0.0, bestSpan = 0.0;
        int bestPartials = 0;
        bool latticeFound = false;
        std::vector<char> use((size_t)t_count, 0);
        std::vector<int> orders((size_t)t_count, 0);
        std::vector<double> pitchCurve((size_t)h, 0.0), offsetCurve((size_t)h, 0.0);
        std::vector<double> movement((size_t)t_count * (size_t)h, 0.0), dilation((size_t)h, 0.0);
        for (double pitch : _pitches) {
            for (int anchor = 0; anchor < t_count; anchor++) {
                int count = 0;
                for (int j = 0; j < t_count; j++) {
                    use[(size_t)j] = (wrapError(difference[(size_t)anchor * t_count + j], pitch) < kMatchTolHz) ? 1 : 0;
                    count += use[(size_t)j];
                }
                if (count < 3) { continue; }
                double lo = 1e30, hi = -1e30;
                for (int j = 0; j < t_count; j++) {
                    if (!use[(size_t)j]) { continue; }
                    lo = (std::min)(lo, meanFreq[(size_t)j]);
                    hi = (std::max)(hi, meanFreq[(size_t)j]);
                }
                const double span = hi - lo;
                if (span < minSpanHz) { continue; }

                // least squares f_j(t) = order_j * F0(t) + offset(t), two parameters, h time columns
                double so = 0.0, soo = 0.0;
                double soy[16] = { 0 }, sy[16] = { 0 };
                int m = 0;
                for (int j = 0; j < t_count; j++) {
                    if (!use[(size_t)j]) { continue; }
                    const int o = (int)std::lround((meanFreq[(size_t)j] - meanFreq[(size_t)anchor]) / pitch);
                    orders[(size_t)j] = o;
                    so += o;
                    soo += (double)o * o;
                    for (int t = 0; t < h; t++) {
                        soy[t] += (double)o * traj[(size_t)j][(size_t)t];
                        sy[t] += traj[(size_t)j][(size_t)t];
                    }
                    m++;
                }
                const double det = (double)m * soo - so * so;
                if (m < 3 || std::fabs(det) < 1e-9) { continue; }
                for (int t = 0; t < h; t++) {
                    pitchCurve[(size_t)t] = ((double)m * soy[t] - so * sy[t]) / det;
                    offsetCurve[(size_t)t] = (sy[t] * soo - so * soy[t]) / det;
                }
                double pmean = 0.0;
                for (int t = 0; t < h; t++) { pmean += pitchCurve[(size_t)t]; }
                pmean /= h;
                if (pmean < 60.0 || pmean > 400.0) { continue; }

                double latticeSq = 0.0;
                for (int j = 0; j < t_count; j++) {
                    if (!use[(size_t)j]) { continue; }
                    for (int t = 0; t < h; t++) {
                        const double model = orders[(size_t)j] * pitchCurve[(size_t)t] + offsetCurve[(size_t)t];
                        const double e = traj[(size_t)j][(size_t)t] - model;
                        latticeSq += e * e;
                    }
                }
                const double latticeError = std::sqrt(latticeSq / (m * h));
                // Keep the furthest-progressing rejected candidate, so zeros have an explanation in live diagnostics.
                auto candidate = evidence.diagnostic;
                candidate.pitch = pitchCurve[(size_t)(h - 1)];
                candidate.partials = m;
                candidate.span = span;
                candidate.latticeError = latticeError;
                auto remember = [&](int stage) {
                    candidate.stage = stage;
                    if (stage > evidence.diagnostic.stage ||
                        (stage == evidence.diagnostic.stage && candidate.latticeError < evidence.diagnostic.latticeError)) {
                        evidence.diagnostic = candidate;
                    }
                };
                remember(3);
                if (latticeError > 5.0) { continue; }
                latticeFound = true;

                // Nominal harmonic anchor; a clear motion fit below can infer a larger SSB offset.
                double offsetMean = 0.0;
                for (int t = 0; t < h; t++) { offsetMean += offsetCurve[(size_t)t]; }
                offsetMean /= h;
                const double shift = std::round(offsetMean / pmean);

                // Physical harmonic numbers are inferred from motion, independently of the SSB tuning offset.
                int minOrder = 100000, maxOrder = -100000;
                for (int j = 0; j < t_count; j++) {
                    if (!use[(size_t)j]) { continue; }
                    minOrder = (std::min)(minOrder, orders[(size_t)j]);
                    maxOrder = (std::max)(maxOrder, orders[(size_t)j]);
                    double rowMean = 0.0;
                    for (int t = 0; t < h; t++) { rowMean += traj[(size_t)j][(size_t)t]; }
                    rowMean /= h;
                    for (int t = 0; t < h; t++) { movement[(size_t)j * h + t] = traj[(size_t)j][(size_t)t] - rowMean; }
                }
                double translationSq = 0.0;
                for (int t = 0; t < h; t++) {
                    double columnMean = 0.0;
                    for (int j = 0; j < t_count; j++) { if (use[(size_t)j]) { columnMean += movement[(size_t)j * h + t]; } }
                    columnMean /= m;
                    for (int j = 0; j < t_count; j++) {
                        if (!use[(size_t)j]) { continue; }
                        const double error = movement[(size_t)j * h + t] - columnMean;
                        translationSq += error * error;
                    }
                }
                // Sufficient statistics make the integer anchor search O(h * partials + 32).
                std::vector<double> weighted((size_t)h, 0.0), total((size_t)h, 0.0);
                for (int j = 0; j < t_count; j++) {
                    if (!use[(size_t)j]) { continue; }
                    for (int t = 0; t < h; t++) {
                        const double value = movement[(size_t)j * h + t];
                        weighted[(size_t)t] += orders[(size_t)j] * value;
                        total[(size_t)t] += value;
                    }
                }
                double d = 0.0, e = 0.0, f = 0.0;
                for (int t = 0; t < h; t++) { d += weighted[(size_t)t] * weighted[(size_t)t]; e += weighted[(size_t)t] * total[(size_t)t]; f += total[(size_t)t] * total[(size_t)t]; }
                int chosenShift = (int)shift;
                double bestExplained = -1.0;
                for (int anchorShift = 1 - minOrder; anchorShift <= 32 - maxOrder; anchorShift++) {
                    const double a = anchorShift;
                    const double explained = (d + 2.0 * a * e + a * a * f) / (soo + 2.0 * a * so + a * a * m);
                    if (explained > bestExplained) { bestExplained = explained; chosenShift = anchorShift; }
                }
                auto motionFor = [&](int anchorShift) {
                    const double a = anchorShift;
                    const double ho2 = soo + 2.0 * a * so + a * a * m;
                    if (ho2 < 1e-9) { return 1e30; }
                    for (int t = 0; t < h; t++) { dilation[(size_t)t] = (weighted[(size_t)t] + a * total[(size_t)t]) / ho2; }
                    double sq = 0.0;
                    for (int j = 0; j < t_count; j++) {
                        if (!use[(size_t)j]) { continue; }
                        for (int t = 0; t < h; t++) {
                            const double error = movement[(size_t)j * h + t] - (orders[(size_t)j] + a) * dilation[(size_t)t];
                            sq += error * error;
                        }
                    }
                    return sq;
                };
                double bestMotionSq = motionFor(chosenShift);
                const auto inferredRange = std::minmax_element(dilation.begin(), dilation.end());
                const double inferredSnr = (*inferredRange.second - *inferredRange.first) / (std::sqrt(bestMotionSq / (m * h)) + 0.5);
                // Weak motion cannot reliably identify absolute harmonic orders: retain the nominal hypothesis.
                if (chosenShift != (int)shift && inferredSnr < 3.0) { chosenShift = (int)shift; bestMotionSq = motionFor(chosenShift); }
                const double motionError = std::sqrt(bestMotionSq / (m * h));
                const double translationError = std::sqrt(translationSq / (m * h));
                candidate.offsetHz = offsetMean - chosenShift * pmean;
                candidate.firstHarmonic = minOrder + chosenShift;
                candidate.lastHarmonic = maxOrder + chosenShift;
                candidate.motionError = motionError;
                candidate.translationError = translationError;
                const auto motionRange = std::minmax_element(dilation.begin(), dilation.end());
                candidate.motion = *motionRange.second - *motionRange.first;
                candidate.snr = candidate.motion / (motionError + 0.5);
                remember(4);
                if (motionError >= 0.5 * translationError) { continue; }

                double dmin = 1e30, dmax = -1e30;
                for (int t = 0; t < h; t++) {
                    dmin = (std::min)(dmin, dilation[(size_t)t]);
                    dmax = (std::max)(dmax, dilation[(size_t)t]);
                }
                const double pitchMotion = dmax - dmin;
                candidate.motion = pitchMotion;
                candidate.snr = pitchMotion / (motionError + 0.5);
                remember(5);
                if (pitchMotion < 1.5) { continue; }

                double roughSq = 0.0;
                int roughCount = 0;
                for (int t = 0; t + 2 < h; t++) {
                    const double d2 = dilation[(size_t)(t + 2)] - 2.0 * dilation[(size_t)(t + 1)] + dilation[(size_t)t];
                    roughSq += d2 * d2;
                    roughCount++;
                }
                const double roughness = std::sqrt(roughSq / (std::max)(1, roughCount));
                const double motionSnr = pitchMotion / (motionError + 0.5);

                // the strongest partial is excluded, so a carrier cannot carry the score
                std::vector<double> chosen;
                chosen.reserve((size_t)m);
                for (int j = 0; j < t_count; j++) { if (use[(size_t)j]) { chosen.push_back(strength[(size_t)j]); } }
                std::sort(chosen.begin(), chosen.end());
                const double s = 0.5 * (chosen[(size_t)(m - 2)] + chosen[(size_t)(m - 3)]);

                // Compensate the shorter Hann window's broader peaks using its duration ratio.
                const double score = (80.0 / windowMs) * s * (std::min)(pitchMotion / 3.0, 1.0) * std::exp(-latticeError / 3.0) *
                                     std::exp(-motionError / 1.5) * std::exp(-roughness / 1.5);
                if (score > bestScore) {
                    bestScore = score;
                    bestPitch = pitchCurve[(size_t)(h - 1)];
                    bestMotion = pitchMotion;
                    bestSnr = motionSnr;
                    bestSpan = span;
                    bestPartials = m;
                    candidate.stage = 6;
                    candidate.roughness = roughness;
                    candidate.strength = s;
                    evidence.diagnostic = candidate;
                }
            }
        }

        evidence.score = bestScore;
        evidence.pitch = bestPitch;
        evidence.motion = bestMotion;
        evidence.snr = bestSnr;
        evidence.span = bestSpan;
        evidence.partials = bestPartials;
        evidence.lattice = latticeFound;
        return finishBand(state, evidence, rapid);
    }

    void publish(double score, double pitch = 0.0, double motion = 0.0, double snr = 0.0, double span = 0.0, int partials = 0) {
        _score.store(score, std::memory_order_relaxed);
        _pitchHz.store(pitch, std::memory_order_relaxed);
        _pitchMotion.store(motion, std::memory_order_relaxed);
        _motionSnr.store(snr, std::memory_order_relaxed);
        _spanHz.store(span, std::memory_order_relaxed);
        _partials.store(partials, std::memory_order_relaxed);
        _hops.fetch_add(1, std::memory_order_relaxed);
        // Each band's acquisition score already includes its own confirmation history.
        const bool detected = score >= thresholdFor(_sensitivity);
        if (detected) {
            _pendingDetection = true;
            _detections.fetch_add(1, std::memory_order_relaxed);
            _lastDetectionHop.store(_hops.load(std::memory_order_relaxed), std::memory_order_relaxed);
        }
    }

    std::atomic<double> _diagnosticTunedHz{ 0.0 };
    std::atomic<int> _diagnosticDemodId{ 0 };
    mutable std::mutex _diagnosticsMutex;
    std::deque<Diagnostics> _diagnostics;
    uint64_t _diagnosticsSequence = 0;

    // ---------------------------------------------------------------- state

    double _fs = 48000.0, _fa = 8000.0, _binHz = 3.90625;
    int _decim = 6, _nw = 640, _hop = 80, _anaPos = 0, _anaFilled = 0, _hopCount = 0;
    int _fastNw = 320;
    std::vector<float> _fastWin;
    int _decimPhase = 0, _aaPos = 0, _aaWarmup = 0;
    int _binsLow = 38, _binsHigh = 410, _peakDistance = 9, _envRadius = 102;
    double _envSigmaBins = 25.6, _decaySec = 1.0, _sensitivity = 0.6;

    std::vector<float> _aaTaps, _aaHist, _ana, _frame, _win;
    std::vector<double> _power, _logPow, _envKernel, _pitches;
    BandState _bands[3][2];
    std::vector<stereo_t> _delay;
    int _dpos = 0, _openLeft = 0;
    double _gain = 0.0, _rampStep = 0.0042;
    bool _pendingDetection = false;

    fftwf_plan _plan = nullptr;
    float* _fftIn = nullptr;
    fftwf_complex* _fftOut = nullptr;

    std::atomic<bool> _open{ false };
    std::atomic<double> _score{ 0.0 }, _pitchHz{ 0.0 }, _pitchMotion{ 0.0 }, _motionSnr{ 0.0 }, _spanHz{ 0.0 };
    std::atomic<int> _partials{ 0 };
    std::atomic<int> _detections{ 0 }, _hops{ 0 }, _lastDetectionHop{ -1 };
    std::atomic<bool> _lattice{ false };
};

} // namespace dsp::noise_reduction