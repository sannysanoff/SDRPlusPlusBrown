// SPDX-License-Identifier: MIT
// Copyright (C) 2026 DeepFist integration for SDR++ Brown
//
// TinyCW decode engine: takes a wide IQ stream, scans the configured CW range
// for keyed signals, opens one narrow channel per signal (RxVFO + CW demod),
// and runs the CNN+CTC model on sliding 6 s windows.
//
// No GUI code lives here; the module owns menus and printing.

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <complex>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <dsp/channel/rx_vfo.h>
#include <dsp/demod/cw.h>
#include <dsp/routing/splitter.h>
#include <dsp/sink/handler_sink.h>
#include <dsp/stream.h>
#include <dsp/types.h>

#include "tinycw_conditioner.h"
#include "tinycw_markers.h"
#include "tinycw_model.h"
#include "tinycw_spectrogram.h"
#include "tinycw_vote.h"
#include "tinycw_zone.h"

namespace tinycw {

// Keying ratio above which a channel counts as "has a signal" for its lifetime.
constexpr double kSignalRatioForLife = 6.0;

struct EngineConfig {
    // Measured on real contest CW (ground truth from a classical decoder):
    // 6 s context / 1 s hop = 87% CER, 12 s / 4 s = 74%, 15 s / 15 s = 64%.
    // Longer context is worth the latency; 12/4 keeps text within a few seconds.
    double windowSec = 12.0;     // model context
    double tickSec = 1.0;        // decode interval per channel (FastCW: ~2 ms per window)
    int    maxChannels = 32;     // CPU guard rail
    double cwPitchHz = 700.0;    // audio pitch the model expects (400..1200)
    double channelBwHz = 1200.0; // per-signal channel bandwidth (wider = better for keying sidebands)
    double squelch = 0.0;        // optional hard gate, 0 = off (frame gate instead)
    double spotHoldSec = 12.0;   // drop a channel after this much silence
    // Two channels closer than this decode the same signal: one station then
    // shows up as several lines of slightly different text. Keep them apart.
    double minChannelSpacingHz = 250.0;
    // Each window commits only its newest part (the overlap is context). A
    // factor > 1 widens that slice: more coverage, more risk of duplicates.
    double commitTailFactor = 1.0;
    // Tail of the window that stays provisional. One CW character plus its
    // trailing gap is 0.5-2 s (18 WPM: dot 67 ms, dash 200 ms, hand sent
    // spacing longer), so the model needs that much audio after a character
    // before its timing is settled.
    double provisionalSec = 3.0;
    // Python reference scores better on this material without the conditioning
    // stage, so keep it switchable.
    bool   conditioning = true;
    // Frame-level gating: decode every window, then drop characters that land on
    // frames without keying energy. Window-level squelch throws away whole
    // windows and swallows the characters around a pause.
    bool   frameGate = true;
    double frameGateFrac = 0.45;  // threshold between the p10 floor and p95 peak
    double frameGateSec = 0.03;   // one model output frame at time_downsample 2
    int    frameGateDilate = 2;   // grow the mask by N frames each side: the model
                                  // places a character on the element edge
    // Pacing: keep the inference thread below this share of one core-second so
    // CoreAudio always finds a free core. 0 disables pacing.
    double inferCpuPercent = 0.0;
    // Cap windows per model pass. A shorter pass returns the cores sooner, so
    // the audio thread gets scheduled instead of waiting out a 1 s burst.
    int    maxBatchWindows = 16;
    int    batchGapMs = 15;   // pause between passes, yields the CPU to audio
    // 0 or 1 = greedy CTC, >1 = prefix beam search width. Beam search is off by
    // default: on this model it degenerates on silence frames (repeats an
    // alternating pair) and scores worse than greedy.
    int    ctcBeam = 0;
    // Focus decoder: window length (12 s, as FastCW was trained) and the right
    // context of each committed character = how far behind live the text runs.
    double focusWindowSec = 12.0;
    double focusLagSec = kZoneLagSec;
    // Mass decoder: commit zones (tinycw_zone.h) instead of the older
    // newest-slice commit. The lag is the right context of each character;
    // 0 means kZoneLagSec.
    bool   massEnsemble = true;
    // Both decoders: vote per character over all windows that cover it
    // (tinycw_vote.h). The last window length of text stays open.
    bool   voteDecode = true;
    double massLagSec = 0.0;

    double markerHoldSec = 5.0;  // how long a waterfall marker stays visible
    int    threads = 0;          // decode workers; 0 = auto (hardware based)
};

struct BatchLine {
    double freqHz = 0.0;
    std::string text;
    double strength = 0.0;
};

// One decoded CW channel.
struct Channel {
    double freqHz = 0.0;         // absolute audio frequency
    double ratio = 0.0;          // keying ratio of the last detection
    bool   hasSignal = false;

    std::unique_ptr<dsp::stream<dsp::complex_t>> in;
    std::unique_ptr<dsp::channel::RxVFO> vfo;
    std::unique_ptr<dsp::demod::CW<float>> cw;
    std::unique_ptr<dsp::sink::Handler<float>> sink;

    std::mutex mtx;
    std::vector<float> buf;      // 3200 Hz audio ring
    int64_t totalSamples = 0;    // samples appended so far
    int64_t consumedSamples = 0; // start of the next window to decode
    // Decode rounds: the manager bumps a counter once per tick and every worker
    // decodes the newest window of all its channels, so one round fills one
    // batch instead of trickling one window into the model at a time.
    int64_t roundSeen = 0;

    size_t keepSamples = 32000;  // audio ring length for this channel
    uint64_t id = 0;             // stable id: selects the decode worker
    std::string pendingText;
    std::string rollingText;     // last characters, kept for the decodes window
    std::string freshText;       // characters committed in the last few seconds
    double freshAtSec = 0.0;
    // The last character(s) of a window can be cut in half by the window edge.
    // They are re-decoded by the next window, so they stay "provisional": the
    // next commit removes them and writes the better version.
    int    provisionalTail = 0;
    bool   gapPending = false;   // silence since the last commit -> mark with "  "
    double stableCommitSec = 0.0;
    double lastTextSec = 0.0;
    double lastCommitSec = 0.0;
    double lastSignalSec = 0.0;
    bool   started = false;

    ZoneState zone;                     // commit zone (tinycw_zone.h)
    VoteText vote;                      // statistical text (tinycw_vote.h)
    ConfText voteFrozen;                // frozen part, with confidence
    double  voteLastSec = 0.0;          // render state of the frozen part
    bool    voteLastSpace = false;
    double  lastCharSec = 0.0;          // time of the last committed character
};

class Engine {
public:
    explicit Engine(Model* model);
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Builds the DSP graph. `iq` must stay alive until stop().
    bool start(dsp::stream<dsp::complex_t>* iq, double inSampleRate, std::string& err);
    void stop();
    bool running() const { return running_.load(); }

    // Absolute range the wide channelizer covers (rarely changes: band/source).
    void setWideRange(double centerHz, double rangeLoHz, double rangeHiHz);
    // Absolute range that gets decoded right now (changes with the width policy).
    void setRange(double centerHz, double rangeLoHz, double rangeHiHz);
    void setMaxChannels(int n);
    void setPaused(bool paused) { paused_ = paused; }
    bool paused() const { return paused_.load(); }
    void setSquelch(double thresh) { cfg_.squelch = thresh; }
    void setChannelBandwidth(double hz) { cfg_.channelBwHz = hz; }
    void setMinChannelSpacing(double hz) { cfg_.minChannelSpacingHz = hz; }
    void setCtcBeam(int n) { cfg_.ctcBeam = n; }
    void setFocusLagSec(double s) { cfg_.focusLagSec = std::max(0.0, s); }
    void setMassEnsemble(bool on) { cfg_.massEnsemble = on; }
    void setVoteDecode(bool on) { cfg_.voteDecode = on; }
    void setCommitTailFactor(double f) { cfg_.commitTailFactor = f; }
    void setProvisionalSec(double sec) { cfg_.provisionalSec = std::max(0.0, sec); }
    void setConditioning(bool on) { cfg_.conditioning = on; }
    void setFrameGate(bool on) { cfg_.frameGate = on; }
    void setFrameGateFrac(double f) { if (f > 0.0 && f < 1.0) { cfg_.frameGateFrac = f; } }
    void setFrameGateDilate(int n) { cfg_.frameGateDilate = std::max(0, n); }
    void setInferCpuPercent(double p) { cfg_.inferCpuPercent = std::max(0.0, std::min(100.0, p)); }
    void setMaxBatchWindows(int n) { cfg_.maxBatchWindows = std::max(1, std::min(64, n)); }
    void setCwPitch(double hz) { cfg_.cwPitchHz = hz; }
    // Buffer kept per channel: window plus slack, so a long context window is
    // never trimmed before it can be decoded.
    void setWindowSec(double v);
    void setTickSec(double v) {
        if (v > 0.1) { cfg_.tickSec = v; }
        updateKeepSamples();
    }
    // Number of decode worker threads (1 = single core). Safe to call while the
    // engine runs: the worker set is resized in place, channels are kept.
    void setDecodeThreads(int n);
    int decodeThreads() const { return threads_; }
    static int hardwareThreads();

    // Lines decoded since the previous call (module prints them in batches).
    std::vector<BatchLine> takeBatch();

    // Focus decoder: only the tuned VFO. One 12 s window -> one model pass with
    // batch 1, once per second, no squelch and no keying gate.
    void setFocusFreq(double absHz);
    void disableFocus();
    bool focusActive() const { return focusActive_.load(); }
    double focusFreq() const { return focusFreq_.load(); }
    std::string focusText();
    std::string focusFresh();
    // Focus text with one confidence (0..1) per byte; conf is empty without votes.
    void focusTextConf(std::string& text, std::vector<float>& conf);

    // Latest text per live channel, sorted by frequency, for the decodes window.
    struct DecodeLine {
        double freqHz = 0.0;
        std::string text;
        std::string fresh;   // newest characters, drawn in a different colour
        std::vector<float> conf;   // per byte of text (vote confidence), empty = all 1
        double lastSec = 0.0;
    };
    std::vector<DecodeLine> decodeLines(double maxAgeSec);

    // Markers still inside their hold window, for the waterfall overlay.
    std::vector<Marker> activeMarkers();

    int liveChannels() const;
    // Marker counts from the last collect: carriers drawn yellow, decodes cyan.
    int carrierMarkers() const { return lastCarrier_.load(); }
    int decodeMarkers() const { return lastDecode_.load(); }
    int spotCount();
    long scanFrameCount() const { return scanFrames_.load(); }
    // Audio seconds still waiting to be decoded (max over channels).
    double backlogSeconds() const;
    int activeSignals() const;
    // Real process CPU (all threads, getrusage delta): N busy cores report N * 100.
    double busyPercent() const;

    // Rolling (since the previous call) timing of one channel pass and of the
    // single batched model pass. Resets the accumulators.
    struct PerfStats {
        double feMsPerChannel = 0.0;      // DSP front-end (conditioner + STFT)
        double inferMsPerPass = 0.0;      // whole batched model run
        double channelsPerPass = 0.0;     // channels inside that run
        double inferMsPerChannel = 0.0;   // inferMsPerPass / channelsPerPass
        double ctcMsPerChannel = 0.0;     // greedy CTC + text commit
        double detectMsPerTick = 0.0;     // scanner -> channel bookkeeping
        double detectMsPerChannel = 0.0;  // detectMsPerTick / channels scanned
        long   passes = 0;
        // Duty cycle against wall clock since the previous call.
        double windowSec = 0.0;
        double detectDutyPct = 0.0;       // manager channel sync, per wall second
        double modelDutyPct = 0.0;        // single model thread occupancy
        double feCpuPct = 0.0;            // front-end CPU summed over workers
        double decodeDutyPct = 0.0;       // (front-end + model + ctc) / wall
        double roundsPerSec = 0.0;
        double openMsPerTick = 0.0;       // part of detect spent opening channels
    };
    PerfStats takePerfStats();
    // Load relative to the allowed thread budget, 0..100.
    double cpuLoadPercent() const;
    double wideRate() const { return wideRate_; }
    const EngineConfig& config() const { return cfg_; }

private:
    void scanThread();
    void focusThread();
    void stopFocusLocked();
    static void focusAudioHandler(float* data, int count, void* ctx);
    void managerThread();
    void decodeWorker(int stripe);
    void inferThread();

    void rebuildWideChain(double inRate);
    void updateKeepSamples();
    void stopLocked();
    void updateRangeLocked();
    Channel* addChannel(double freqHz, double spotRatio = 0.0);
    void removeChannel(Channel* ch);
    void destroyChannelsLocked();
    // Clears focus audio, votes and text; caller holds focusMtx_.
    void resetFocusDecodeLocked();
    bool decodeChannel(const std::shared_ptr<Channel>& ch, int worker, double nowSec);
    bool decodeWindow(const std::shared_ptr<Channel>& ch, int worker, const std::vector<float>& window,
                      double t0, double now, double lastCommit);
    // CTC decode + text commit for one window, shared by the batch path.
    std::vector<uint8_t> frameActivity(const std::vector<float>& window) const;
    void applyModelOutput(const std::shared_ptr<Channel>& ch, const float* logProbs,
                          int outFrames, double t0, double now, double lastCommit,
                          const std::vector<uint8_t>& active);
    // Commit-zone path: decode this window and commit only the characters in
    // its zone (tinycw_zone.h), so each character comes from one window.
    // Vote path: add this window's characters to the channel vote, then render
    // frozen + open text with per-character confidence.
    void commitChannelVote(const std::shared_ptr<Channel>& ch, const float* logProbs,
                           int outFrames, double t0,
                           const std::vector<uint8_t>& active, double now);
    void commitChannelZone(const std::shared_ptr<Channel>& ch, const float* logProbs,
                           int outFrames, double t0,
                           const std::vector<uint8_t>& active, double now);
    static void audioHandler(float* data, int count, void* ctx);
    void onWideSamples(dsp::complex_t* data, int count);

    Model* model_ = nullptr;
    EngineConfig cfg_;

    std::atomic_bool running_{ false };
    std::atomic_bool paused_{ false };
    std::atomic_bool workersPaused_{ false };
    // Serializes start()/stop()/setDecodeThreads(): the GUI and the module tick
    // thread both call into the engine.
    std::mutex lifecycleMtx_;

    dsp::stream<dsp::complex_t>* iq_ = nullptr;
    std::unique_ptr<dsp::channel::RxVFO> wide_;
    std::unique_ptr<dsp::routing::Splitter<dsp::complex_t>> split_;
    double inRate_ = 0.0;
    double wideRate_ = 0.0;
    double wideBw_ = 0.0;

    mutable std::mutex mtx_;
    double centerHz_ = 0.0;      // IQ center (absolute)
    double rangeLoHz_ = 0.0;     // wide channelizer range (absolute)
    double rangeHiHz_ = 0.0;
    double rangeCenter_ = 0.0;   // wide channelizer center (absolute)
    double decodeLoHz_ = 0.0;    // current decode range (absolute)
    double decodeHiHz_ = 0.0;
    bool   rangeDirty_ = false;
    // shared_ptr: decode workers snapshot channels and may outlive a removal.
    std::vector<std::shared_ptr<Channel>> channels_;
    int maxChannels_ = 32;

    // Scanner state.
    std::mutex scanMtx_;
    std::vector<dsp::complex_t> scanBuf_;
    size_t scanWrite_ = 0;
    size_t scanCount_ = 0;
    int64_t scanTotal_ = 0;
    std::atomic<long> scanFrames_{ 0 };
    std::atomic<int64_t> wideTotal_{ 0 };
    std::atomic<int64_t> roundReq_{ 0 };
    double lastRoundSec_ = 0.0;

    struct Spot {
        double freqHz = 0.0;
        double ratio = 0.0;
        double lastSeenSec = 0.0;
    };
    std::vector<Spot> spots_;

    std::atomic<int> scanGen_{ 0 };
    mutable std::mutex markerMtx_;
    MarkerTracker markers_;

    // Windows waiting for one batched model pass. Pushed by the decode workers,
    // drained by the single inference thread: one Run() per batch, never one
    // Run() per channel.
    struct InferJob {
        std::shared_ptr<Channel> ch;
        std::vector<float> spec;   // kFreqBins * frames, spectrogram of one window
        int frames = 0;
        double t0 = 0.0;
        double now = 0.0;
        double lastCommit = 0.0;
        double feMs = 0.0;         // front-end time spent on this window
        std::vector<uint8_t> active;  // per model frame: is there keying energy
    };
    std::mutex inferMtx_;
    std::condition_variable inferCv_;
    std::vector<InferJob> inferQueue_;
    std::atomic<int> lastBatchSize_{ 0 };
    std::atomic<int> lastCarrier_{ 0 };
    std::atomic<int> lastDecode_{ 0 };
    std::atomic<int> channelCount_{ 0 };
    std::atomic<int> queueDepth_{ 0 };
    std::atomic<double> queueLagSec_{ 0.0 };

    // Focus decoder state.
    std::thread focusWorker_;
    std::atomic_bool focusActive_{ false };
    std::atomic<double> focusFreq_{ 0.0 };
    std::unique_ptr<dsp::stream<dsp::complex_t>> focusIn_;
    std::unique_ptr<dsp::channel::RxVFO> focusVfo_;
    std::unique_ptr<dsp::demod::CW<float>> focusCw_;
    std::unique_ptr<dsp::sink::Handler<float>> focusSink_;
    mutable std::mutex focusMtx_;
    std::vector<float> focusBuf_;
    int64_t focusTotal_ = 0;
    uint64_t focusGen_ = 0;   // bumped by every reset (retune): stale passes are dropped
    std::string focusText_, focusFresh_;
    double focusFreshAt_ = 0.0;
    double focusCommitSec_ = 0.0;

    double focusLastCharSec_ = 0.0;

    // Commit zone of the focus decoder (tinycw_zone.h). Averaging the frames
    // of overlapping windows doubled FastCW characters (their spikes move by a
    // frame or two between windows).
    ZoneState focusZone_;
    VoteText focusVote_;
    ConfText focusFrozen_;
    std::vector<float> focusConf_;      // per byte of focusText_ (vote path)
    double focusVoteLastSec_ = 0.0;
    bool focusVoteLastSpace_ = false;
    std::vector<double> focusGaps_;   // recent inter-character gaps, seconds
    Conditioner focusCond_;
    Spectrogram focusSpec_;
    std::vector<float> focusCondBuf_, focusSpecBuf_;

    std::thread scanWorker_;
    std::thread managerWorker_;
    std::thread inferWorker_;
    std::unique_ptr<std::atomic<double>> inferBusy_;
    // Process CPU sampling (all threads), for the menu readout and the policy.
    mutable std::mutex cpuMtx_;
    mutable double cpuPrevWall_ = 0.0;
    mutable double cpuPrevCpu_ = 0.0;
    mutable double cpuPercent_ = 0.0;

    // Timing accumulators for PerfStats (windowed, reset on read).
    // Decode text keyed by frequency, not by channel: a channel closes and
    // reopens as spots jitter, and the text must not blink with it.
    struct FreqText {
        double freqHz = 0.0;
        std::string text;
        std::string fresh;
        std::vector<float> conf;   // per byte of text, empty = all confident
        double freshAtSec = 0.0;
        double lastSec = 0.0;
    };
    mutable std::mutex textMtx_;
    std::map<int64_t, FreqText> textByFreq_;
    std::atomic<int> textRows_{ 0 };

    std::mutex perfMtx_;
    double perfFeMs_ = 0.0;
    double perfInferMs_ = 0.0;
    double perfChannels_ = 0.0;
    double perfCtcMs_ = 0.0;
    double perfDetectMs_ = 0.0;
    double perfOpenMs_ = 0.0;
    double perfDetectCh_ = 0.0;
    long   perfDetectTicks_ = 0;
    double perfWindowStart_ = 0.0;
    long   perfPasses_ = 0;
    std::vector<std::thread> decodeWorkers_;
    int threads_ = 1;

    // Per worker, so no locking is needed in the hot path.
    std::vector<std::unique_ptr<Conditioner>> conditioners_;
    std::vector<std::unique_ptr<Spectrogram>> spectrometers_;
    std::vector<std::vector<float>> condBufs_;
    std::vector<std::vector<float>> specBufs_;
    std::vector<std::unique_ptr<std::atomic<double>>> workerBusy_;
    // Last round each worker finished: the model thread waits for all of them,
    // so one round is exactly one batched pass.
    std::vector<std::unique_ptr<std::atomic<int64_t>>> workerRound_;
    uint64_t nextChannelId_ = 0;
    size_t keepSamples_ = 32000;   // per-channel audio ring length

    std::chrono::steady_clock::time_point startTime_;
    double nowSec() const;
};

} // namespace tinycw