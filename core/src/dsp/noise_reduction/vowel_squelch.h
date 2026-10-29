#pragma once
#include "../processor.h"
#include <fftw3.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

namespace dsp::noise_reduction {

// Voice (vowel) squelch: passes the demodulated audio only while it sounds like a spoken vowel, and mutes noise,
// carriers and data bursts - what Icom calls "VSC" and the literature a syllabic squelch.
//
// A vowel is a quasi-periodic sound: the glottal pulse train (one person's pitch, 60 to 400 Hz) driven through the
// vocal tract, whose formants put the energy between 300 and 3000 Hz. Its spectrum is a comb of harmonics under a
// slow envelope. Noise has no comb, an unmodulated carrier and a CW note have a single line, FSK/PSK data bursts
// have a few unrelated tones. Three things are measured on the last 1/4 s of audio, every 10 ms:
//   p   the peak of the normalised autocorrelation over the pitch period (how periodic it is),
//   nh  how many harmonics of that pitch stand out of the band (a vowel has several, a tone has one),
//   snr how far the audio is above the noise floor learned while it is quiet.
// All three have to pass. The audio is delayed by 1/4 s and the decision is applied to that buffered audio, so a
// word opens the squelch without losing its first syllable. It stays open for `decay` seconds after the last vowel.
class VowelSquelch : public Processor<stereo_t, stereo_t> {
    using base_type = Processor<stereo_t, stereo_t>;
public:
    static constexpr double kDelaySec = 0.25;    // how much audio is buffered, so a word is not cut
    static constexpr double kHopSec = 0.01;      // a decision every 10 ms
    static constexpr double kAnalysisSec = 0.12; // how much audio a decision looks at (a syllable is 50 to 200 ms)
    static constexpr int kFftSize = 2048;        // 3.9 Hz bins at the 8 kHz analysis rate
    static constexpr double kMinPitchHz = 60.0, kMaxPitchHz = 400.0;
    static constexpr double kBandLowHz = 100.0, kBandHighHz = 3000.0;

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

    void setAnalysisSeconds(double seconds) {
        std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
        _analysisSec = std::clamp(seconds, 0.05, 0.25);
        setup(_fs);
    }

    double getDecay() const { return _decaySec; }
    double getSensitivity() const { return _sensitivity; }

    // state, for the menu and for the tests
    bool isOpen() const { return _open.load(std::memory_order_relaxed); }
    double lastSnrDb() const { return _snrDb; }
    double lastPitchHz() const { return _pitchHz; }
    double lastPeriodicity() const { return _periodicity; }
    double lastHarmonicRatio() const { return _harmonicRatio; }
    int lastHarmonics() const { return _harmonics; }
    double lastLowestHarmonicHz() const { return _lowestHarmonicHz; }
    double lastHighestHarmonicHz() const { return _highestHarmonicHz; }
    double lastHarmonicCv() const { return _harmonicCv; }
    double lastCombContrast() const { return _combContrast; }
    double lastOccupiedSpanHz() const { return _occupiedSpanHz; }
    double lastSpectralFlux() const { return _spectralFlux; }
    double lastPeakFraction() const { return _peakFraction; }
    double lastSpectralFlatness() const { return _spectralFlatness; }
    double lastPeakToMedian() const { return _peakToMedian; }
    double lastEnvelopeSwingDb() const { return _envelopeSwingDb; }

    int process(int count, const stereo_t* in, stereo_t* out) {
        const int delayLen = (int)_delay.size();
        for (int i = 0; i < count; i++) {
            // the delayed sample goes out, gated and ramped so that opening and closing do not click
            const stereo_t d = _delay[(size_t)_dpos];
            _delay[(size_t)_dpos] = in[i];
            _dpos = (_dpos + 1) % delayLen;

            const bool open = (_openLeft > 0);
            if (_detected) {
                _openLeft = delayLen + (int)std::lround(_decaySec * _fs);
                _detected = false;
                _open.store(true, std::memory_order_relaxed);
            }
            const double target = (_openLeft > 0) ? 1.0 : 0.0;
            if (_gain < target) { _gain = std::min(target, _gain + _gainStep); }
            else if (_gain > target) { _gain = std::max(target, _gain - _gainStep); }
            const float g = (float)_gain;
            out[i].l = d.l * g;
            out[i].r = d.r * g;
            (void)open;

            // the detector runs on the mono sum, one sample in every `decim` after the anti-alias filter
            feed((in[i].l + in[i].r) * 0.5f);

            if (_openLeft > 0) {
                _openLeft--;
                if (_openLeft == 0) { _open.store(false, std::memory_order_relaxed); }
            }
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
    void setup(double fs) {
        _fs = fs;
        _decim = std::max(1, (int)std::lround(fs / 8000.0));
        _fsa = fs / _decim;
        _nw = (int)std::lround(_analysisSec * _fsa);
        _hopLen = std::max(1, (int)std::lround(kHopSec * _fsa));

        _delay.assign((size_t)std::lround(kDelaySec * fs), stereo_t{ 0.0f, 0.0f });
        _dpos = 0;
        _gain = 0.0;
        _gainStep = 1.0 / std::max(1.0, 0.005 * fs);   // 5 ms ramp
        _openLeft = 0;
        _detected = false;
        _open.store(false, std::memory_order_relaxed);

        _win.assign((size_t)_nw, 0.0f);
        _lin.assign((size_t)_nw, 0.0f);
        _ana.assign((size_t)_nw, 0.0f);
        _anaPos = 0;
        _anaCount = 0;
        _hopCounter = 0;

        // anti-alias filter for the decimation to the analysis rate, and the window used for the comb
        _taps = 63;
        _fir.assign((size_t)_taps, 0.0f);
        _firHist.assign((size_t)_taps, 0.0f);
        _firPos = 0;
        _decCount = 0;
        const double cut = 0.40 * _fsa;               // 3.2 kHz at 8 kHz: the whole voice band
        const int mid = (_taps - 1) / 2;
        double sum = 0.0;
        for (int i = 0; i < _taps; i++) {
            const double t = i - mid;
            const double sinc = (std::fabs(t) < 1e-9) ? (2.0 * cut / _fs) : (std::sin(2.0 * M_PI * cut * t / _fs) / (M_PI * t));
            const double w = 0.42 - 0.5 * std::cos(2.0 * M_PI * i / (_taps - 1)) + 0.08 * std::cos(4.0 * M_PI * i / (_taps - 1));
            _fir[(size_t)i] = (float)(sinc * w);
            sum += _fir[(size_t)i];
        }
        for (auto& h : _fir) { h = (float)(h / sum); }

        for (int i = 0; i < _nw; i++) { _win[(size_t)i] = (float)(0.5 - 0.5 * std::cos(2.0 * M_PI * i / (_nw - 1))); }

        _fftIn = (float*)fftwf_malloc(sizeof(float) * kFftSize);
        _fftOut = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * (kFftSize / 2 + 1));
        _plan = fftwf_plan_dft_r2c_1d(kFftSize, _fftIn, _fftOut, FFTW_ESTIMATE);
        _power.assign((size_t)(kFftSize / 2 + 1), 0.0);
        _fluxPrev.assign((size_t)(kFftSize / 2 + 1), 0.0);
        _fluxCur.assign((size_t)(kFftSize / 2 + 1), 0.0);
        const double binHz = _fsa / (double)kFftSize;
        _fluxB0 = std::max(1, (int)std::ceil(200.0 / binHz));
        _fluxB1 = std::min(kFftSize / 2 - 1, (int)std::floor(3200.0 / binHz));
        _subWin.assign((size_t)std::max(32, (int)std::lround(0.025 * _fsa)), 0.0f);
        for (size_t i = 0; i < _subWin.size(); i++) { _subWin[i] = (float)(0.5 - 0.5 * std::cos(2.0 * M_PI * i / (_subWin.size() - 1))); }

        // the chain's own start-up transient (resampler, buffers) looks like a click, not like a voice: learn the
        // noise floor from it and stay shut for the first half second
        _warmupHops = (int)std::lround(0.5 / kHopSec);
        _floorDb = NAN;
        _snrDb = 0.0;
        _pitchHz = 0.0;
        _periodicity = 0.0;
        _harmonicRatio = 0.0;
        _harmonics = 0;
    }

    void freeAll() {
        if (_plan) { fftwf_destroy_plan(_plan); _plan = nullptr; }
        if (_fftIn) { fftwf_free(_fftIn); _fftIn = nullptr; }
        if (_fftOut) { fftwf_free(_fftOut); _fftOut = nullptr; }
    }

    void feed(float x) {
        _firHist[(size_t)_firPos] = x;
        _firPos = (_firPos + 1) % _taps;
        if (++_decCount < _decim) { return; }
        _decCount = 0;

        float y = 0.0f;   // y = sum_k h[k] * x[n - k]
        for (int k = 0; k < _taps; k++) { y += _fir[(size_t)k] * _firHist[(size_t)((_firPos + k) % _taps)]; }

        _ana[(size_t)_anaPos] = y;
        _anaPos = (_anaPos + 1) % _nw;
        if (_anaCount < _nw) { _anaCount++; }
        if (_anaCount < _nw) { return; }
        if (++_hopCounter < _hopLen) { return; }
        _hopCounter = 0;
        analyse();
    }

    void analyse() {
        for (int i = 0; i < _nw; i++) { _lin[(size_t)i] = _ana[(size_t)((_anaPos + i) % _nw)]; }

        double mean = 0.0;
        for (int i = 0; i < _nw; i++) { mean += _lin[(size_t)i]; }
        mean /= _nw;
        double energy = 0.0;
        for (int i = 0; i < _nw; i++) {
            _lin[(size_t)i] -= (float)mean;
            energy += (double)_lin[(size_t)i] * _lin[(size_t)i];
        }

        // noise floor: falls at once, rises slowly, so a voice does not become the floor
        // a window of (near) digital silence says nothing: leave the floor where it is and stay shut
        const double rmsDb = 10.0 * std::log10(energy / _nw + 1e-20);
        const bool audible = (rmsDb > -110.0);
        if (_warmupHops > 0) {
            _warmupHops--;
            _floorDb = std::isnan(_floorDb) ? rmsDb : std::min(_floorDb, rmsDb);
            _voicedHits = 0;
            return;
        }
        if (std::isnan(_floorDb)) { _floorDb = std::min(rmsDb, -60.0); }
        // The floor follows the quiet parts of the last ~20 s: down at once, up very slowly. Speech can occupy
        // most of a transmission (>70 % is normal) and the floor still has to sit on the noise, not on the voice.
        if (audible) { _floorDb += (rmsDb < _floorDb ? 0.30 : 0.0005) * (rmsDb - _floorDb); }
        _floorDb = std::max(_floorDb, -100.0);
        _snrDb = rmsDb - _floorDb;

        // pitch: the strongest autocorrelation peak over one period of a plausible voice
        _periodicity = 0.0;
        _pitchHz = 0.0;
        if (audible && energy > 0.0) {
            const int lmin = std::max(2, (int)(_fsa / kMaxPitchHz));
            const int lmax = std::min(_nw - 2, (int)(_fsa / kMinPitchHz));
            const float* x = _lin.data();
            double best = -1.0;
            int bestLag = 0;
            for (int lag = lmin; lag <= lmax; lag++) {
                const int n = _nw - lag;
                double r = 0.0, e0 = 0.0, e1 = 0.0;
                for (int i = 0; i < n; i++) {
                    r += (double)x[i] * x[i + lag];
                    e0 += (double)x[i] * x[i];
                    e1 += (double)x[i + lag] * x[i + lag];
                }
                const double den = std::sqrt(e0 * e1);
                const double rn = (den > 0.0) ? (r / den) : 0.0;   // bounded to [-1, 1] whatever the level is
                if (rn > best) { best = rn; bestLag = lag; }
            }
            if (bestLag > 0) {
                _periodicity = best;
                _pitchHz = _fsa / bestLag;
            }
        }

        // harmonics: how many lines of that comb stand out of the band
        int harmonics = 0;
        double harmonicRatio = 0.0, lowestHz = 0.0, highestHz = 0.0, cv = 0.0, combContrast = 0.0, occupiedSpanHz = 0.0, peakFraction = 0.0;
        double spectralFlatness = 1.0, peakToMedian = 0.0;
        if (_periodicity > 0.15 && _pitchHz >= kMinPitchHz) {
            std::memset(_fftIn, 0, sizeof(float) * kFftSize);
            for (int i = 0; i < _nw; i++) { _fftIn[i] = _lin[(size_t)i] * _win[(size_t)i]; }
            fftwf_execute(_plan);

            // envelope burstiness: the level of 20 ms blocks across the window. Speech has syllables (10-20 dB swings
        // at 2-8 Hz), static is flat, and a keyed carrier swings all the way to zero (handled by the comb test).
        double envMean = 0.0, envVar = 0.0;
        int envCount = 0;
        {
            const int blk = std::max(16, (int)std::lround(0.02 * _fsa));
            std::vector<double> levels;
            for (int start = 0; start + blk <= _nw; start += blk) {
                double e = 0.0;
                for (int i = 0; i < blk; i++) { const double v = _lin[(size_t)(start + i)]; e += v * v; }
                levels.push_back(10.0 * std::log10(e / blk + 1e-20));
            }
            if (!levels.empty()) {
                for (double v : levels) { envMean += v; }
                envMean /= levels.size();
                for (double v : levels) { envVar += (v - envMean) * (v - envMean); }
                envVar = std::sqrt(envVar / levels.size());
                envCount = (int)levels.size();
            }
        }
        _envelopeSwingDb = envCount ? envVar : 0.0;

        // spectral flux: how much the shape of the spectrum moves inside the window (formants move, a carrier only
        // changes its level, noise has no structure at all - and no harmonics either)
        _spectralFlux = 0.0;
        if (audible) {
            const int sub = (int)_subWin.size();
            const int step = sub / 2;
            int frames = 0;
            for (int start = 0; start + sub <= _nw; start += step) {
                std::memset(_fftIn, 0, sizeof(float) * kFftSize);
                for (int i = 0; i < sub; i++) { _fftIn[i] = _lin[(size_t)(start + i)] * _subWin[(size_t)i]; }
                fftwf_execute(_plan);
                double norm = 0.0;
                for (int b = _fluxB0; b <= _fluxB1; b++) {
                    const double re = _fftOut[b][0], im = _fftOut[b][1];
                    _fluxCur[(size_t)b] = std::sqrt(re * re + im * im);
                    norm += _fluxCur[(size_t)b] * _fluxCur[(size_t)b];
                }
                norm = std::sqrt(norm);
                if (norm <= 0.0) { continue; }
                if (frames > 0) {
                    double d = 0.0;
                    for (int b = _fluxB0; b <= _fluxB1; b++) {
                        const double a = _fluxCur[(size_t)b] / norm - _fluxPrev[(size_t)b];
                        d += a * a;
                    }
                    _spectralFlux += std::sqrt(d);
                }
                for (int b = _fluxB0; b <= _fluxB1; b++) { _fluxPrev[(size_t)b] = _fluxCur[(size_t)b] / norm; }
                frames++;
            }
            if (frames > 1) { _spectralFlux /= (frames - 1); }
        }

        const double binHz = _fsa / (double)kFftSize;
            const int b0 = std::max(1, (int)std::ceil(kBandLowHz / binHz));
            const int b1 = std::min(kFftSize / 2 - 1, (int)std::floor(kBandHighHz / binHz));
            double total = 0.0, peak = 0.0;
            for (int b = b0; b <= b1; b++) {
                const double re = _fftOut[b][0], im = _fftOut[b][1];
                _power[(size_t)b] = re * re + im * im;
                total += _power[(size_t)b];
                peak = std::max(peak, _power[(size_t)b]);
            }
            if (total > 0.0) {
                // The span 90 % of the band's energy occupies: a keyed carrier and a two-tone FSK burst keep it in
                // a couple of hundred Hz, a vowel spreads it over its formants, a kilohertz and more.
                _order.resize((size_t)(b1 - b0 + 1));
                for (int b = b0; b <= b1; b++) { _order[(size_t)(b - b0)] = b; }
                const double* pw = _power.data();
                std::sort(_order.begin(), _order.end(), [pw](int x, int y) { return pw[x] > pw[y]; });
                double acc = 0.0;
                int loBin = b1, hiBin = b0;
                for (int idx : _order) {
                    acc += _power[(size_t)idx];
                    loBin = std::min(loBin, idx);
                    hiBin = std::max(hiBin, idx);
                    if (acc >= 0.90 * total) { break; }
                }
                occupiedSpanHz = (hiBin - loBin) * binHz;

                // How much of the band sits within +-150 Hz of its strongest line: a carrier (even one that the AGC
                // has surrounded with boosted noise, which is what makes its span look wide) keeps nearly all of it
                // there, a voice spreads it over its formants.
                int peakBin = b0;
                for (int b = b0; b <= b1; b++) { if (_power[(size_t)b] > _power[(size_t)peakBin]) { peakBin = b; } }
                const int wide = std::max(1, (int)std::lround(150.0 / binHz));
                double around = 0.0;
                for (int b = std::max(b0, peakBin - wide); b <= std::min(b1, peakBin + wide); b++) { around += _power[(size_t)b]; }
                peakFraction = around / total;

                // spectrum shape: geometric over arithmetic mean (flat static ~0.3-0.6, a voice comb 0.02-0.10) and
                // how far the loudest bin stands above the median one (a comb is peaky, static is not)
                double logSum = 0.0;
                std::vector<double>& tmp = _powerScratch;
                tmp.resize((size_t)(b1 - b0 + 1));
                for (int b = b0; b <= b1; b++) {
                    tmp[(size_t)(b - b0)] = _power[(size_t)b];
                    logSum += std::log(_power[(size_t)b] + 1e-30);
                }
                const int cnt = b1 - b0 + 1;
                const double geo = std::exp(logSum / cnt);
                const double arith = total / cnt;
                spectralFlatness = (arith > 0.0) ? (geo / arith) : 0.0;
                std::nth_element(tmp.begin(), tmp.begin() + cnt / 2, tmp.end());
                const double med = tmp[(size_t)(cnt / 2)];
                peakToMedian = (med > 0.0) ? (peak / med) : 0.0;
                _bandMedian = med;
                // Each line of the comb is judged against the bins between the lines, never against the loudest
                // bin in the band: a strong carrier in the passband lowers the noise floor relative to the peak and
                // then noise bins pass for harmonics (that is how a busy RTTY band used to open this squelch).
                const double tol = std::max(2.0 * binHz, 0.03 * _pitchHz);
                auto energyAround = [&](double f) {
                    int lo = (int)std::floor((f - tol) / binHz), hi = (int)std::ceil((f + tol) / binHz);
                    lo = std::max(lo, b0);
                    hi = std::min(hi, b1);
                    double e = 0.0;
                    for (int b = lo; b <= hi; b++) { e += _power[(size_t)b]; }
                    return e;
                };
                double comb = 0.0, between = 0.0;
                std::vector<double> lines; lines.reserve(32);
                // A line counts when it stands above the *typical bin of the band*, not above its neighbour: on top of
                // static the neighbouring bins are noise too, so a data tone would otherwise pass for a comb. A voice
                // has many such lines, a data signal or a carrier one or two.
                const double lineFloor = 6.0 * _bandMedian * (2.0 * tol / binHz + 1.0);
                for (int k = 1; k * _pitchHz <= kBandHighHz; k++) {
                    const double f = k * _pitchHz;
                    if (f < kBandLowHz) { continue; }
                    const double lineEnergy = energyAround(f);
                    const double gapEnergy = energyAround(f + 0.5 * _pitchHz);
                    comb += lineEnergy;
                    between += gapEnergy;
                    if (lineEnergy > lineFloor) {
                        harmonics++;
                        if (lowestHz == 0.0) { lowestHz = f; }
                        highestHz = f;
                        lines.push_back(std::sqrt(lineEnergy));
                    }
                }
                harmonicRatio = comb / total;
                combContrast = (between > 0.0) ? (comb / between) : 0.0;
                if (lines.size() >= 2) {
                    double m = 0.0;
                    for (double v : lines) { m += v; }
                    m /= lines.size();
                    double var = 0.0;
                    for (double v : lines) { var += (v - m) * (v - m); }
                    cv = (m > 0.0) ? std::sqrt(var / lines.size()) / m : 0.0;
                }
            }
        }
        _harmonics = harmonics;
        _harmonicRatio = harmonicRatio;
        _lowestHarmonicHz = lowestHz;
        _highestHarmonicHz = highestHz;
        _harmonicCv = cv;
        _combContrast = combContrast;
        _occupiedSpanHz = occupiedSpanHz;
        _peakFraction = peakFraction;
        _spectralFlatness = spectralFlatness;
        _peakToMedian = peakToMedian;

        const double pThr = 0.72 - 0.34 * _sensitivity;      // 0.38 .. 0.72
        const double snrThr = 10.0 - 10.0 * _sensitivity;    // 0 .. 10 dB above the learned noise floor
        // A vowel is more than "some comb": it has a low fundamental, several harmonics, and energy in the
        // formant region above 1 kHz. A CW note, a pair of RTTY tones or a beat between carriers fails these.
        const bool voiced = (_periodicity >= pThr) && (harmonics >= 4) && (harmonicRatio >= 0.10) && (_snrDb >= snrThr) &&
                            (_lowestHarmonicHz > 0.0) && (_lowestHarmonicHz <= 350.0) && (_highestHarmonicHz >= 900.0) &&
                            (combContrast >= 40.0) &&      // the lines are far above what lies between them: not mush
                            (occupiedSpanHz >= 700.0);     // and the sound is wide: not a keyed carrier or FSK
        // A syllable lasts 50 ms or more, a keying click or a data transient does not: ask for a few hops in a row.
        _voicedHits = voiced ? (_voicedHits + 1) : 0;
        if (_voicedHits >= 8) { _detected = true; }
    }

    double _fs = 48000.0, _fsa = 8000.0;
    int _decim = 6, _nw = 2000, _hopLen = 80, _taps = 63;
    int _firPos = 0, _decCount = 0;
    int _anaPos = 0, _anaCount = 0, _hopCounter = 0;
    std::vector<float> _fir, _firHist, _win, _lin, _ana;
    std::vector<stereo_t> _delay;
    int _dpos = 0;

    double _gain = 0.0, _gainStep = 0.002;
    int _openLeft = 0;
    bool _detected = false;

    double _decaySec = 1.0, _sensitivity = 0.5;
    double _floorDb = NAN, _snrDb = 0.0, _pitchHz = 0.0, _periodicity = 0.0, _harmonicRatio = 0.0;
    int _harmonics = 0, _voicedHits = 0, _warmupHops = 50;
    double _lowestHarmonicHz = 0.0, _highestHarmonicHz = 0.0, _harmonicCv = 0.0, _combContrast = 0.0, _occupiedSpanHz = 0.0;
    double _spectralFlux = 0.0, _peakFraction = 0.0, _spectralFlatness = 1.0, _peakToMedian = 0.0, _envelopeSwingDb = 0.0;
    double _bandMedian = 0.0;
    int _fluxB0 = 1, _fluxB1 = 1;
    std::vector<float> _subWin;
    std::vector<double> _fluxPrev, _fluxCur;
    std::vector<int> _order;
    std::vector<double> _powerScratch;
    double _analysisSec = kAnalysisSec;

    fftwf_plan _plan = nullptr;
    float* _fftIn = nullptr;
    fftwf_complex* _fftOut = nullptr;
    std::vector<double> _power;
    std::atomic<bool> _open{ false };
};

}