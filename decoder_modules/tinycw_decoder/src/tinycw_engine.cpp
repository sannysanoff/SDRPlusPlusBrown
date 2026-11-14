// SPDX-License-Identifier: MIT
// Copyright (C) 2026 DeepFist integration for SDR++ Brown

// Windows.h defines min/max as macros; the DSP code below uses std::min/std::max.
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "tinycw_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/resource.h>
#endif

#if defined(__APPLE__)
#include <pthread/qos.h>
#elif defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include <core.h>
#include <utils/flog.h>

#include "tinycw_conditioner.h"
#include "tinycw_mainband.h"
#include "tinycw_ctc.h"
#include "tinycw_squelch.h"
#include "tinycw_spectrogram.h"

namespace tinycw {

// Worker threads belong to the background: visible work must never outrank the
// audio thread or the UI. On macOS this is a QoS class, on Linux a per-thread
// nice value.
static void lowerThreadPriority() {
#if defined(__APPLE__)
    pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#elif defined(__linux__)
    setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 10);
#endif
}

namespace {
constexpr int    kAudioRate = 3200;         // model rate
constexpr int    kScanFft = 4096;           // scanner FFT
constexpr int    kScanFrames = 30;          // ~3 s of history at 100 ms hop
constexpr int    kScanHopMs = 100;
constexpr double kPeakClusterHz = 150.0;  // dense contest band: do not merge neighbours
constexpr double kChannelMatchHz = 90.0;

int autoThreads() {
    const int hw = (int)std::thread::hardware_concurrency();
    if (hw <= 1) { return 1; }
    return std::min(hw - 1, 4);   // leave one core for the GUI/source
}

double chooseWideRate(double spanHz) {
    double rate = 48000.0;
    while (rate < spanHz * 1.25 && rate < 400000.0) { rate *= 2.0; }
    return rate;
}
} // namespace

Engine::Engine(Model* model) : model_(model) {}

Engine::~Engine() { stop(); }

double Engine::nowSec() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime_).count();
}

bool Engine::start(dsp::stream<dsp::complex_t>* iq, double inSampleRate, std::string& err) {
    const EngineConfig cfg = cfgSnap();
    updateKeepSamples();
    std::lock_guard<std::mutex> life(lifecycleMtx_);
    stopLocked();
    if (iq == nullptr || inSampleRate <= 0.0) {
        err = "invalid IQ stream";
        return false;
    }

    iq_ = iq;
    inRate_ = inSampleRate;
    startTime_ = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lck(scanMtx_);
        spots_.clear();
        scanWrite_ = 0;
        scanCount_ = 0;
        scanTotal_ = 0;
        scanBuf_.assign((size_t)1 << 18, dsp::complex_t{0.0f, 0.0f});
    }

    {
        std::lock_guard<std::mutex> lck(mtx_);
        if (rangeHiHz_ <= rangeLoHz_) {
            rangeLoHz_ = -inSampleRate * 0.5;
            rangeHiHz_ = inSampleRate * 0.5;
            decodeLoHz_ = rangeLoHz_;
            decodeHiHz_ = rangeHiHz_;
        }
        rangeCenter_ = (rangeLoHz_ + rangeHiHz_) * 0.5;
        if (decodeHiHz_ <= decodeLoHz_) {
            decodeLoHz_ = rangeLoHz_;
            decodeHiHz_ = rangeHiHz_;
        }
    }
    rebuildWideChain(inSampleRate);

    threads_ = (cfg.threads > 0) ? cfg.threads : autoThreads();
    threads_ = std::max(1, std::min(threads_, hardwareThreads()));

    conditioners_.clear();
    spectrometers_.clear();
    condBufs_.clear();
    specBufs_.clear();
    workerBusy_.clear();
    workerRound_.clear();
    for (int i = 0; i < threads_; i++) {
        conditioners_.push_back(std::make_unique<Conditioner>());
        spectrometers_.push_back(std::make_unique<Spectrogram>());
        condBufs_.emplace_back();
        specBufs_.emplace_back();
        workerBusy_.push_back(std::make_unique<std::atomic<double>>(0.0));
        workerRound_.push_back(std::make_unique<std::atomic<int64_t>>(0));
    }

    inferBusy_ = std::make_unique<std::atomic<double>>(0.0);

    running_ = true;
    scanWorker_ = std::thread(&Engine::scanThread, this);
    managerWorker_ = std::thread(&Engine::managerThread, this);
    // One thread owns the model: every window that is ready at the same moment
    // goes through a single batched pass.
    inferWorker_ = std::thread(&Engine::inferThread, this);
    for (int i = 0; i < threads_; i++) {
        decodeWorkers_.emplace_back(&Engine::decodeWorker, this, i);
    }
    char msg[256];
    snprintf(msg, sizeof msg, "engine started: in %.0f Hz, wide %.0f Hz, range %.0f..%.0f Hz",
             inSampleRate, wideRate_, rangeLoHz_, rangeHiHz_);
    flog::info("[tinycw] {}", msg);
    return true;
}

void Engine::stop() {
    std::lock_guard<std::mutex> life(lifecycleMtx_);
    stopLocked();
}

void Engine::stopLocked() {
    if (!running_.load() && !wide_) { return; }
    running_ = false;

    stopFocusLocked();
    if (scanWorker_.joinable()) { scanWorker_.join(); }
    if (managerWorker_.joinable()) { managerWorker_.join(); }
    {
        std::lock_guard<std::mutex> lck(inferMtx_);
        inferCv_.notify_all();
    }
    if (inferWorker_.joinable()) { inferWorker_.join(); }
    {
        std::lock_guard<std::mutex> lck(inferMtx_);
        inferQueue_.clear();
    }
    for (auto& t : decodeWorkers_) {
        if (t.joinable()) { t.join(); }
    }
    decodeWorkers_.clear();

    std::vector<std::shared_ptr<Channel>> old;
    {
        std::lock_guard<std::mutex> lck(mtx_);
        old = detachChannelsLocked();
    }
    for (auto& ch : old) { stopChannelDsp(ch); }

    {
        std::lock_guard<std::recursive_mutex> dl(dspMtx_);
        std::lock_guard<std::mutex> lck(mtx_);
        if (split_) { split_->stop(); split_.reset(); }
        if (widePack_) { widePack_->stop(); widePack_.reset(); }
        if (wide_) { wide_->stop(); wide_.reset(); }
        iq_ = nullptr;
        wideRate_ = 0.0;
        lastRoundSec_ = 0.0;
    }

    // A stopped engine holds no CW state: the next start() (SDR++ play, a
    // sample-rate change) begins with no rows, no markers and no focus text.
    // Channels, votes and the focus decoder were cleared above.
    {
        std::lock_guard<std::mutex> lck(textMtx_);
        textByFreq_.clear();
    }
    textRows_ = 0;
    {
        std::lock_guard<std::mutex> lck(markerMtx_);
        markers_.clear();
    }
    {
        std::lock_guard<std::mutex> lck(scanMtx_);
        spots_.clear();
    }
    roundReq_ = 0;
    channelCount_ = 0;
    queueDepth_ = 0;
    queueLagSec_ = 0.0;
}

void Engine::rebuildWideChain(double inRate) {
    std::lock_guard<std::recursive_mutex> dsp(dspMtx_);
    // Copied under the lock: the UI thread rewrites these fields in setWideRange().
    double lo = 0.0, hi = 0.0;
    dsp::stream<dsp::complex_t>* iq = nullptr;
    {
        std::lock_guard<std::mutex> lck(mtx_);
        lo = rangeLoHz_;
        hi = rangeHiHz_;
        iq = iq_;
    }
    // The focus chain is bound to this splitter, so it has to go first.
    stopFocusLocked();
    if (split_) { split_->stop(); split_.reset(); }
    if (widePack_) { widePack_->stop(); widePack_.reset(); }
    if (wide_) { wide_->stop(); wide_.reset(); }

    const double span = (hi > lo) ? (hi - lo) : inRate;
    double wideOffset = 0.0;
    {
        std::lock_guard<std::mutex> lck(mtx_);
        wideRate_ = chooseWideRate(span);
        if (wideRate_ > inRate) { wideRate_ = inRate; }
        wideBw_ = span;
        rangeCenter_ = (hi > lo) ? (lo + hi) * 0.5 : 0.0;
        wideOffset = rangeCenter_ - centerHz_;
        wideRetune_ = false;   // the new chain uses the current centre
    }

    wide_ = std::make_unique<dsp::channel::RxVFO>(iq, inRate, wideRate_, wideBw_, wideOffset);
    constexpr double kWideBlockSec = 0.05;
    const int packSamples = std::max(1, (int)std::lround(wideRate_ * kWideBlockSec));
    widePack_ = std::make_unique<dsp::buffer::Packer<dsp::complex_t>>(&wide_->out, packSamples);
    split_ = std::make_unique<dsp::routing::Splitter<dsp::complex_t>>(&widePack_->out);
    split_->origin = "tinycw.splitter";
    split_->setHook([this](dsp::complex_t* data, int count) {
        this->onWideSamples(data, count);
    });

    wide_->start();
    widePack_->start();
    split_->start();

    {
        std::lock_guard<std::mutex> lck(scanMtx_);
        spots_.clear();
        scanWrite_ = 0;
        scanCount_ = 0;
        scanTotal_ = 0;
    }
    scanGen_.fetch_add(1);

    char msg[256];
    snprintf(msg, sizeof msg, "wide chain: center %.0f Hz (offset %.0f Hz), bw %.0f Hz, rate %.0f Hz",
             rangeCenter_, wideOffset, wideBw_, wideRate_);
    flog::info("[tinycw] {}", msg);
}

void Engine::setWideRange(double centerHz, double rangeLoHz, double rangeHiHz) {
    std::lock_guard<std::mutex> lck(mtx_);
    const bool sameRange = std::fabs(rangeLoHz - rangeLoHz_) < 100.0 &&
                           std::fabs(rangeHiHz - rangeHiHz_) < 100.0;
    if (sameRange && std::fabs(centerHz - centerHz_) < 1.0) { return; }
    if (sameRange) {
        // Only the SDR centre moved: the decoded segment and every channel
        // (tuned relative to the segment centre) stay; the wide VFO is retuned.
        centerHz_ = centerHz;
        wideRetune_ = true;
        return;
    }
    centerHz_ = centerHz;
    rangeLoHz_ = rangeLoHz;
    rangeHiHz_ = rangeHiHz;
    rangeDirty_ = true;
}

void Engine::setRange(double centerHz, double rangeLoHz, double rangeHiHz) {
    std::lock_guard<std::mutex> lck(mtx_);
    decodeLoHz_ = rangeLoHz;
    decodeHiHz_ = rangeHiHz;
    if (centerHz > 0.0 && std::fabs(centerHz - centerHz_) > 1.0) {
        centerHz_ = centerHz;
        wideRetune_ = true;
    }
}

int Engine::hardwareThreads() {
    const int hw = (int)std::thread::hardware_concurrency();
    return (hw > 0) ? hw : 4;
}

void Engine::setDecodeThreads(int n) {
    std::lock_guard<std::mutex> life(lifecycleMtx_);
    const int hw = hardwareThreads();
    const int want = (n <= 0) ? 0 : std::max(1, std::min(n, hw));
    {
        std::lock_guard<std::mutex> cl(cfgMtx_);
        cfg_.threads = want;
    }

    if (!running_.load()) { return; }

    const int target = std::max(1, std::min((want > 0) ? want : autoThreads(), hw));
    if (target == threads_) { return; }

    // Stop the current workers, resize the per-worker state, start the new set.
    workersPaused_ = true;
    for (auto& t : decodeWorkers_) {
        if (t.joinable()) { t.join(); }
    }
    decodeWorkers_.clear();

    conditioners_.clear();
    spectrometers_.clear();
    condBufs_.clear();
    specBufs_.clear();
    workerBusy_.clear();
    {
        // inferThread() reads this vector while the engine runs.
        std::lock_guard<std::mutex> rl(roundMtx_);
        workerRound_.clear();
        for (int i = 0; i < target; i++) {
            workerRound_.push_back(std::make_unique<std::atomic<int64_t>>(0));
        }
    }
    for (int i = 0; i < target; i++) {
        conditioners_.push_back(std::make_unique<Conditioner>());
        spectrometers_.push_back(std::make_unique<Spectrogram>());
        condBufs_.emplace_back();
        specBufs_.emplace_back();
        workerBusy_.push_back(std::make_unique<std::atomic<double>>(0.0));
    }
    threads_ = target;
    for (int i = 0; i < threads_; i++) {
        decodeWorkers_.emplace_back(&Engine::decodeWorker, this, i);
    }
    workersPaused_ = false;
    flog::info("[tinycw] decode workers resized");
}

void Engine::updateKeepSamples() {
    const EngineConfig cfg = cfgSnap();
    const double sec = std::max(12.0, cfg.windowSec * 2.0 + cfg.tickSec);
    keepSamples_ = (size_t)(kAudioRate * sec);
}

void Engine::setWindowSec(double v) {
    if (v <= 1.0) { return; }
    {
        std::lock_guard<std::mutex> cl(cfgMtx_);
        cfg_.windowSec = v;
    }
    updateKeepSamples();
}

void Engine::setMaxChannels(int n) {
    std::lock_guard<std::mutex> lck(mtx_);
    maxChannels_ = std::max(1, n);
}

void Engine::onWideSamples(dsp::complex_t* data, int count) {
    if (count <= 0) { return; }
    wideTotal_.fetch_add(count);
    std::lock_guard<std::mutex> lck(scanMtx_);
    if (scanBuf_.empty()) { return; }
    const size_t cap = scanBuf_.size();
    for (int i = 0; i < count; i++) {
        scanBuf_[scanWrite_] = data[i];
        scanWrite_ = (scanWrite_ + 1) % cap;
    }
    scanCount_ = std::min(cap, scanCount_ + (size_t)count);
    scanTotal_ += count;
}


// ---------------------------------------------------------------- focus decode
void Engine::focusAudioHandler(float* data, int count, void* ctx) {
    Engine* e = (Engine*)ctx;
    if (e == nullptr || count <= 0) { return; }
    std::lock_guard<std::mutex> lck(e->focusMtx_);
    e->focusBuf_.insert(e->focusBuf_.end(), data, data + count);
    e->focusTotal_ += count;
    const size_t keep = (size_t)(kAudioRate * 60);
    if (e->focusBuf_.size() > keep) {
        e->focusBuf_.erase(e->focusBuf_.begin(),
                           e->focusBuf_.begin() + (long)(e->focusBuf_.size() - keep));
    }
}

void Engine::stopFocusLocked() {
    focusActive_ = false;
    if (focusWorker_.joinable()) { focusWorker_.join(); }
    // Unbind first: the splitter must stop writing into focusIn_ before the
    // VFO that reads it stops, or the splitter can block on a dead reader.
    if (split_ && focusIn_) {
        try { split_->unbindStream(focusIn_.get()); } catch (...) {}
    }
    if (focusVfo_) { focusVfo_->stop(); }
    if (focusCw_) { focusCw_->stop(); }
    if (focusSink_) { focusSink_->stop(); }
    focusSink_.reset();
    focusCw_.reset();
    focusVfo_.reset();
    focusIn_.reset();
    std::lock_guard<std::mutex> lck(focusMtx_);
    resetFocusDecodeLocked();
}

void Engine::resetFocusDecodeLocked() {
    focusGen_++;
    focusBuf_.clear();
    focusTotal_ = 0;
    focusText_.clear();
    focusFresh_.clear();
    focusCommitSec_ = 0.0;
    focusLastCharSec_ = 0.0;
    focusZone_ = ZoneState();
    focusVote_ = VoteText();
    focusFrozen_ = ConfText();
    focusConf_.clear();
    focusRender_ = RenderState();
    focusCalls_ = CallLexicon();
    focusGaps_.clear();
}

void Engine::disableFocus() {
    std::lock_guard<std::mutex> life(lifecycleMtx_);
    // dspMtx_: rebuildWideChain() (manager thread) also stops the focus chain.
    std::lock_guard<std::recursive_mutex> dl(dspMtx_);
    stopFocusLocked();
}

void Engine::setFocusFreq(double absHz) {
    const EngineConfig cfg = cfgSnap();
    std::lock_guard<std::mutex> life(lifecycleMtx_);
    // dspMtx_: rebuildWideChain() (manager thread) replaces split_ and stops
    // the focus chain; both must not run at the same time.
    std::lock_guard<std::recursive_mutex> dl(dspMtx_);
    double rangeCenter = 0.0, wideBw = 0.0, wideRate = 0.0;
    {
        std::lock_guard<std::mutex> lck(mtx_);
        rangeCenter = rangeCenter_;
        wideBw = wideBw_;
        wideRate = wideRate_;
    }
    if (!running_.load() || !split_ || wideRate <= 0.0) { return; }

    const double offset = absHz - rangeCenter;
    if (std::fabs(offset) > wideBw * 0.5 - 300.0) {   // outside the band
        if (focusActive_.load()) { stopFocusLocked(); }
        return;
    }

    if (focusActive_.load()) {
        if (std::fabs(absHz - focusFreq_.load()) < 25.0) { return; }
        focusFreq_.store(absHz);
        if (focusVfo_) { focusVfo_->setOffset(offset); }
        // New frequency: the old audio, votes and text belong to another station.
        std::lock_guard<std::mutex> lck(focusMtx_);
        resetFocusDecodeLocked();
        return;
    }

    focusFreq_.store(absHz);
    focusIn_ = std::make_unique<dsp::stream<dsp::complex_t>>("tinycw.focus");
    focusVfo_ = std::make_unique<dsp::channel::RxVFO>();
    focusCw_ = std::make_unique<dsp::demod::CW<float>>();
    focusSink_ = std::make_unique<dsp::sink::Handler<float>>();
    focusVfo_->init(focusIn_.get(), wideRate, (double)kAudioRate, cfg.channelBwHz, offset);
    focusCw_->init(&focusVfo_->out, cfg.cwPitchHz, 0.0, 0.0, (double)kAudioRate);
    focusSink_->init(&focusCw_->out, &Engine::focusAudioHandler, this);
    focusSink_->start();
    focusCw_->start();
    focusVfo_->start();
    split_->bindStream(focusIn_.get());
    {
        std::lock_guard<std::mutex> lck(focusMtx_);
        resetFocusDecodeLocked();
    }
    focusActive_ = true;
    focusWorker_ = std::thread(&Engine::focusThread, this);
    flog::info("[tinycw] focus decoder on");
}

std::string Engine::focusText() {
    std::lock_guard<std::mutex> lck(focusMtx_);
    return focusText_;
}

void Engine::focusTextConf(std::string& text, std::vector<float>& conf) {
    std::lock_guard<std::mutex> lck(focusMtx_);
    text = focusText_;
    conf = focusConf_;
}

std::string Engine::focusFresh() {
    std::lock_guard<std::mutex> lck(focusMtx_);
    if (focusFresh_.empty()) { return std::string(); }
    if ((nowSec() - focusFreshAt_) > 2.0) { return std::string(); }
    return focusFresh_;
}

void Engine::focusThread() {
    EngineConfig cfg = cfgSnap();
    lowerThreadPriority();
    SetThreadName("tinycw_focus");

    const int classes = classCount();
    const std::vector<std::string>& tok = tokens();
    const int frameSamples = 2 * kHop;                     // 30 ms output frame
    const double framePeriod = (double)frameSamples / (double)kAudioRate;
    const int windowSamples = (int)(cfg.focusWindowSec * (double)kAudioRate);
    const int tickSamples = kAudioRate;                    // one pass per second
    int64_t lastDecoded = 0;
    uint64_t gen = 0;

    while (running_.load() && focusActive_.load()) {
        cfg = cfgSnap();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!running_.load() || !focusActive_.load()) { break; }

        std::vector<float> window;
        int64_t endAbs = 0;
        {
            std::lock_guard<std::mutex> lck(focusMtx_);
            if (gen != focusGen_) { gen = focusGen_; lastDecoded = 0; }   // retuned: start over
            if (focusTotal_ < windowSamples) { continue; }
            if (focusTotal_ - lastDecoded < tickSamples) { continue; }
            endAbs = focusTotal_;
            window.assign(focusBuf_.end() - windowSamples, focusBuf_.end());
        }
        lastDecoded = endAbs;
        if (!model_) { continue; }

        const auto tPass0 = std::chrono::steady_clock::now();
        // Single band: the main tone by the screening rules the model was trained on,
        // searched only within this channel (pitch +- half the channel spacing).
        const MainTone mt = findMainTone(window.data(), (int)window.size(),
                                           cfg.cwPitchHz - cfg.minChannelSpacingHz / 2,
                                           cfg.cwPitchHz + cfg.minChannelSpacingHz / 2);
        focusCond_.process(window.data(), (int)window.size(), focusCondBuf_, kAudioRate,
                           mt.found ? mt.toneHz : cfg.cwPitchHz);
        int frames = 0;
        if (!focusSpec_.compute(focusCondBuf_.data(), (int)focusCondBuf_.size(),
                                focusSpecBuf_, frames) || frames <= 0) {
            continue;
        }

        std::vector<float> logProbs;
        int outFrames = 0;
        std::string err;
        if (!model_->inferBatch(focusSpecBuf_.data(), 1, kFreqBins, frames, logProbs, outFrames, err)) {
            continue;
        }
        if (outFrames <= 0 || (int)logProbs.size() < outFrames * classes) { continue; }

        const int64_t start = endAbs - windowSamples;
        const double startSec = (double)start / (double)kAudioRate;

        std::lock_guard<std::mutex> lck(focusMtx_);
        if (gen != focusGen_) { continue; }   // retuned during this pass: old audio

        if (cfg.voteDecode) {
            // Statistical text (tinycw_vote.h): every window votes; the last
            // window length stays open and is re-rendered on every pass.
            std::vector<int> vIds, vIdx;
            greedyCtc(logProbs.data(), outFrames, classes, vIds, vIdx);
            focusVote_.add(vIds, vIdx, startSec, framePeriod, cfg.focusWindowSec);
            const size_t before = focusFrozen_.text.size();
            const std::vector<VoteText::Slot> focusSlots = focusVote_.takeFrozen();
            for (const VoteText::Slot& sl : focusSlots) {
                if (sl.id > 0 && sl.id < (int)tok.size()) { focusCalls_.add(tok[(size_t)sl.id], sl.sec); }
            }
            focusCalls_.update(startSec);
            renderSlots(focusSlots, tok, focusFrozen_, focusRender_);
            focusFrozen_.keepLast(1000);
            ConfText open;
            RenderState openRender = focusRender_;
            renderSlots(focusVote_.open(), tok, open, openRender);
            focusText_ = focusFrozen_.text + open.text;
            focusConf_ = focusFrozen_.conf;
            focusConf_.insert(focusConf_.end(), open.conf.begin(), open.conf.end());
            callPass(focusText_, focusConf_, focusCalls_.calls());
            fixWords(focusText_, focusConf_);
            focusFresh_ = open.text;
            focusFreshAt_ = nowSec();
            static double lastVoteLog = 0.0;
            if (focusFreshAt_ - lastVoteLog > 5.0) {
                lastVoteLog = focusFreshAt_;
                flog::info("[tinycw] focus vote: win {} s, frozen +{}, open {}: {}", (int)startSec,
                           (int)(focusFrozen_.text.size() - std::min(before, focusFrozen_.text.size())),
                           (int)open.text.size(), open.text);
            }
            continue;
        }

        // Commit zone (tinycw_zone.h): this window commits only the characters
        // between the previous zone end and focusLagSec before its own end.
        std::vector<int> ids, idx;
        greedyCtc(logProbs.data(), outFrames, classes, ids, idx);
        const std::vector<ZoneChar> chars =
            commitZone(focusZone_, ids, idx, startSec, framePeriod, cfg.focusWindowSec,
                       cfg.focusLagSec, startSec + kZoneEdgeSec);

        int added = 0;
        for (const ZoneChar& c : chars) {
            const int id = c.id;
            if (id <= 0 || id >= (int)tok.size()) { continue; }
            const std::string& sym = tok[(size_t)id];
            const double charSec = c.sec;

            // A really long pause is worth showing, even though the model has
            // no silence token. Two seconds is far longer than any word gap.
            if (!focusText_.empty() && focusLastCharSec_ > 0.0 &&
                sym != " " && focusText_.back() != ' ') {
                if (charSec - focusLastCharSec_ > 2.0) { focusText_ += "  "; focusFresh_ += "  "; }
            }

            if (sym == " " && (focusText_.empty() || focusText_.back() == ' ')) {
                continue;
            }
            focusText_ += sym;
            focusFresh_ += sym;
            if (sym != " ") { focusLastCharSec_ = charSec; }
            added++;
        }

        if (focusText_.size() > 1000) { focusText_.erase(0, focusText_.size() - 1000); }
        if (focusFresh_.size() > 200) { focusFresh_.erase(0, focusFresh_.size() - 200); }
        if (added > 0) { focusFreshAt_ = nowSec(); }

        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - tPass0).count();
        static double lastLog = 0.0;
        const double tNow = nowSec();
        if (tNow - lastLog > 5.0) {
            lastLog = tNow;
            char msg[256];
            snprintf(msg, sizeof msg,
                     "focus pass %.1f ms, win %.0f s, lag %.1f s, frames %d chars %d, "
                     "+%d chars, text %d: %s",
                     ms, startSec, cfg.focusLagSec, outFrames, (int)chars.size(), added,
                     (int)focusText_.size(),
                     focusText_.size() > 44 ? focusText_.substr(focusText_.size() - 44).c_str()
                                            : focusText_.c_str());
            flog::info("[tinycw] {}", msg);
        }
    }
}

void Engine::scanThread() {
    lowerThreadPriority();
    SetThreadName("tinycw_scan");

    Fft fft(kScanFft);
    const std::vector<double> win = hannPeriodic(kScanFft);
    std::vector<std::complex<double>> buf((size_t)kScanFft);
    // Full, fft-shifted spectrum: index i maps to (i - N/2) * binHz, so both
    // halves of the CW segment (negative and positive offsets) are scanned.
    std::vector<float> mags((size_t)kScanFft);

    std::vector<std::vector<float>> frames;
    size_t frameIdx = 0;
    int64_t lastTotal = 0;
    int lastGen = scanGen_.load();
    int ratioTick = 0;

    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kScanHopMs));
        if (!running_.load()) { break; }

        if (scanGen_.load() != lastGen) {
            lastGen = scanGen_.load();
            frames.clear();
            frameIdx = 0;
            lastTotal = 0;
            ratioTick = 0;
        }

        std::vector<dsp::complex_t> snap;
        size_t wideRate = 0;
        {
            std::lock_guard<std::mutex> lck(mtx_);
            wideRate = (size_t)wideRate_;
        }
        if (wideRate == 0) { continue; }

        {
            std::lock_guard<std::mutex> lck(scanMtx_);
            if (scanTotal_ == lastTotal || scanCount_ < (size_t)kScanFft) { continue; }
            lastTotal = scanTotal_;
            const size_t cap = scanBuf_.size();
            const size_t start = (scanWrite_ + cap - (size_t)kScanFft) % cap;
            snap.resize((size_t)kScanFft);
            for (int i = 0; i < kScanFft; i++) { snap[(size_t)i] = scanBuf_[(start + (size_t)i) % cap]; }
        }

        scanFrames_++;
        for (int i = 0; i < kScanFft; i++) {
            buf[(size_t)i] = std::complex<double>((double)snap[(size_t)i].re * win[i],
                                                  (double)snap[(size_t)i].im * win[i]);
        }
        fft.forward(buf.data());
        for (int k = 0; k < kScanFft; k++) {
            const double re = buf[(size_t)k].real();
            const double im = buf[(size_t)k].imag();
            const size_t idx = (size_t)((k + kScanFft / 2) % kScanFft);
            mags[idx] = (float)std::sqrt(re * re + im * im);
        }

        if (frames.size() < (size_t)kScanFrames) { frames.resize((size_t)kScanFrames); }
        frames[frameIdx] = mags;
        frameIdx = (frameIdx + 1) % (size_t)kScanFrames;

        // Keying-ratio map is expensive; refresh it every ~300 ms.
        if (++ratioTick < 3) { continue; }
        ratioTick = 0;

        const int nBins = kScanFft;
        const double binHz = (double)wideRate / (double)kScanFft;
        const int half = kScanFft / 2;
        std::vector<double> ratio((size_t)nBins, 0.0);
        std::vector<double> col;
        col.reserve((size_t)kScanFrames);

        double rangeCenter = 0.0;
        double loHz = 0.0;
        double hiHz = 0.0;
        double sdrCenter = 0.0;
        {
            std::lock_guard<std::mutex> lck(mtx_);
            rangeCenter = rangeCenter_;
            loHz = rangeLoHz_;
            hiHz = rangeHiHz_;
            sdrCenter = centerHz_;
        }
        std::vector<double> p10s((size_t)nBins, 0.0), p90s((size_t)nBins, 0.0), floors;

        // Only bins inside the wide (segment) range need a probability map.
        for (int b = 0; b < nBins; b++) {
            const double absHz = rangeCenter + ((double)b - (double)half) * binHz;
            if (absHz < loHz - binHz || absHz > hiHz + binHz) { continue; }
            col.clear();
            for (size_t f = 0; f < frames.size(); f++) {
                if (frames[f].empty()) { continue; }
                col.push_back((double)frames[f][(size_t)b]);
            }
            if (col.size() < 8) { continue; }
            std::sort(col.begin(), col.end());
            p10s[(size_t)b] = percentile(col.data(), (int)col.size(), 0.10);
            p90s[(size_t)b] = percentile(col.data(), (int)col.size(), 0.90);
            floors.push_back(p10s[(size_t)b]);
        }
        // Division guard relative to the noise floor of the range, so the
        // ratio does not depend on the SDR level (raw IQ, no AGC).
        double eps = 1e-12;
        if (!floors.empty()) {
            std::nth_element(floors.begin(), floors.begin() + (long)(floors.size() / 2), floors.end());
            eps = std::max(eps, 1e-3 * floors[floors.size() / 2]);
        }
        for (int b = 0; b < nBins; b++) {
            if (p90s[(size_t)b] > 0.0) { ratio[(size_t)b] = p90s[(size_t)b] / (p10s[(size_t)b] + eps); }
        }

        const int guard = std::max(1, (int)std::lround(60.0 / binHz));
        std::vector<Spot> found;
        const double t = nowSec();

        for (int b = guard; b < nBins - guard; b++) {
            const double r = ratio[(size_t)b];
            if (ratioDb(r) < kSpotFloorDb) { continue; }   // tracked below the gate too
            bool isPeak = true;
            for (int k = b - guard; k <= b + guard; k++) {
                if (k == b) { continue; }
                if (ratio[(size_t)k] > r) { isPeak = false; break; }
            }
            if (!isPeak) { continue; }

            const double offsetHz = ((double)b - (double)half) * binHz;
            const double absHz = rangeCenter + offsetHz;
            if (absHz < loHz || absHz > hiHz) { continue; }
            // The SDR LO (DC spike) is at the tuned centre, not at the
            // centre of the wide channel (that one is just the remix point).
            if (sdrCenter > 0.0 && std::fabs(absHz - sdrCenter) < 120.0) { continue; }

            if (!found.empty() && std::fabs(absHz - found.back().freqHz) < kPeakClusterHz) {
                Spot& prev = found.back();
                if (r > prev.ratio) { prev.freqHz = absHz; prev.ratio = r; }
                continue;
            }
            Spot s;
            s.freqHz = absHz;
            s.ratio = r;
            s.lastSeenSec = t;
            found.push_back(s);
        }

        const double scoreWindowSec = cfgSnap().channelIdleSec;
        {
            std::lock_guard<std::mutex> lck(scanMtx_);
            for (const Spot& s : found) {
                // The nearest old spot takes the measurement (the latest one
                // wins: a historical peak pinned the spot to an old position).
                // Two peaks of this round on one spot: the stronger one wins.
                Spot* best = nullptr;
                for (Spot& old : spots_) {
                    const double d = std::fabs(old.freqHz - s.freqHz);
                    if (d < kPeakClusterHz && (!best || d < std::fabs(best->freqHz - s.freqHz))) { best = &old; }
                }
                if (!best) { spots_.push_back(s); continue; }
                if (best->lastSeenSec == t && s.ratio <= best->ratio) { continue; }
                best->freqHz = s.freqHz;
                best->ratio = s.ratio;
                best->lastSeenSec = t;
            }
            // Score every spot from this round's ratio map at its frequency
            // (the strongest bin within the peak guard), also when it was not
            // found as a peak: the score is the detection criterion.
            for (Spot& sp : spots_) {
                const int b0 = half + (int)std::lround((sp.freqHz - rangeCenter) / binHz);
                double r = 0.0;
                for (int b = std::max(0, b0 - guard); b <= std::min(nBins - 1, b0 + guard); b++) {
                    r = std::max(r, ratio[(size_t)b]);
                }
                sp.score.add(t, ratioDb(r), scoreWindowSec);
                sp.scoreDb = sp.score.max();
            }
            spots_.erase(std::remove_if(spots_.begin(), spots_.end(),
                                        [t](const Spot& s) {
                                            return (t - s.lastSeenSec) > 8.0 || s.scoreDb < kSpotFloorDb;
                                        }),
                         spots_.end());
            if (spots_.size() > 128) {
                std::sort(spots_.begin(), spots_.end(),
                          [](const Spot& a, const Spot& b) { return a.scoreDb > b.scoreDb; });
                spots_.resize(128);
            }
        }
    }
}

std::shared_ptr<Channel> Engine::startChannel(double freqHz, double spotRatio) {
    const EngineConfig cfg = cfgSnap();
    const auto addStart = std::chrono::steady_clock::now();
    dsp::routing::Splitter<dsp::complex_t>* split = nullptr;
    double rate = 0.0;
    {
        std::lock_guard<std::mutex> lck(mtx_);
        split = split_.get();
        rate = wideRate_;
    }
    if (!split || rate <= 0.0) { return nullptr; }
    if (!wide_) { return nullptr; }

    auto ch = std::make_shared<Channel>();
    ch->freqHz = freqHz;
    // A new channel counts as live from its start; otherwise the idle and
    // spot-hold timeouts (measured from 0) kill it on the next manager tick.
    ch->lastSignalSec = nowSec();
    ch->keepSamples = keepSamples_;
    ch->id = nextChannelId_++;
    ch->roundSeen = roundReq_.load();
    ch->in = std::make_unique<dsp::stream<dsp::complex_t>>("tinycw.channel");
    ch->vfo = std::make_unique<dsp::channel::RxVFO>();
    ch->cw = std::make_unique<dsp::demod::CW<float>>();
    ch->sink = std::make_unique<dsp::sink::Handler<float>>();

    const double offset = freqHz - rangeCenter_;
    ch->vfo->init(ch->in.get(), rate, (double)kAudioRate, cfg.channelBwHz, offset);
    ch->cw->init(&ch->vfo->out, cfg.cwPitchHz, 0.0, 0.0, (double)kAudioRate);
    ch->sink->init(&ch->cw->out, &Engine::audioHandler, ch.get());

    ch->sink->start();
    ch->cw->start();
    ch->vfo->start();
    split->bindStream(ch->in.get());
    ch->started = true;

    const double addMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - addStart).count();
{ std::lock_guard<std::mutex> lck(perfMtx_); perfOpenMs_ += addMs; }
    (void)addMs;

    return ch;
}

void Engine::stopChannelDsp(const std::shared_ptr<Channel>& ch) {
    if (!ch) { return; }
    if (ch->started) {
        dsp::routing::Splitter<dsp::complex_t>* split = nullptr;
        {
            std::lock_guard<std::mutex> lck(mtx_);
            split = split_.get();
        }
        // Unbind first: a stopped VFO no longer reads ch->in, and the splitter
        // would block on it and then drop a block for every other channel.
        if (split && ch->in) {
            try { split->unbindStream(ch->in.get()); }
            catch (...) {}
        }
        if (ch->vfo) { ch->vfo->stop(); }
        if (ch->cw) { ch->cw->stop(); }
        if (ch->sink) { ch->sink->stop(); }
        ch->started = false;
    }
}

std::vector<std::shared_ptr<Channel>> Engine::detachChannelsLocked() {
    std::vector<std::shared_ptr<Channel>> old;
    old.swap(channels_);
    channelCount_.store(0);
    return old;
}

void Engine::audioHandler(float* data, int count, void* ctx) {
    Channel* ch = (Channel*)ctx;
    if (ch == nullptr || count <= 0) { return; }
    std::lock_guard<std::mutex> lck(ch->mtx);
    ch->buf.insert(ch->buf.end(), data, data + count);
    ch->totalSamples += count;
    // Keep enough audio for the model window plus one tick of slack, so a long
    // context window is never trimmed before it can be decoded.
    // Trim only when the buffer is twice the size: an erase per audio block
    // moved the whole buffer every time (8 % of the CPU with 60 channels).
    // Readers use totalSamples - buf.size(), so a longer buffer is fine.
    const size_t keep = ch->keepSamples;
    if (ch->buf.size() > 2 * keep) {
        const size_t drop = ch->buf.size() - keep;
        ch->buf.erase(ch->buf.begin(), ch->buf.begin() + (long)drop);
    }
}

// One flag per model output frame: does this 30 ms slice carry keying energy?
std::vector<uint8_t> Engine::frameActivity(const std::vector<float>& window) const {
    const EngineConfig cfg = cfgSnap();
    std::vector<uint8_t> active;
    if (!cfg.frameGate) { return active; }
    const double frameSec = cfg.frameGateSec;
    const int frameSamples = std::max(1, (int)std::lround(frameSec * (double)kAudioRate));
    // Model frame j is centred at sample j * frameSamples (centred STFT), and
    // there are n / frameSamples + 1 of them (401 for 12 s).
    const int n = (int)window.size();
    const int nFrames = n / frameSamples + 1;
    if (nFrames < 4) { return active; }

    active.assign((size_t)nFrames, 0);
    std::vector<double> rms((size_t)nFrames, 0.0);
    for (int i = 0; i < nFrames; i++) {
        const int a = std::max(0, i * frameSamples - frameSamples / 2);
        const int b = std::min(n, i * frameSamples + (frameSamples + 1) / 2);
        double acc = 0.0;
        for (int k = a; k < b; k++) { acc += (double)window[(size_t)k] * (double)window[(size_t)k]; }
        rms[(size_t)i] = (b > a) ? std::sqrt(acc / (double)(b - a)) : 0.0;
    }
    std::vector<double> sorted = rms;
    std::sort(sorted.begin(), sorted.end());
    const double floor = sorted[(size_t)(0.10 * (double)(sorted.size() - 1))];
    const double peak = sorted[(size_t)(0.95 * (double)(sorted.size() - 1))];
    if (peak < floor * 1.5) { return active; }   // no keying in this window
    const double thr = floor + (peak - floor) * cfg.frameGateFrac;
    for (int i = 0; i < nFrames; i++) { active[(size_t)i] = rms[(size_t)i] > thr ? 1 : 0; }

    // Dilate: a character is often placed on the rising or falling edge of an
    // element, where the frame itself still looks quiet.
    const int d = cfg.frameGateDilate;
    if (d > 0) {
        std::vector<uint8_t> grown = active;
        for (int i = 0; i < nFrames; i++) {
            if (!active[(size_t)i]) { continue; }
            for (int k = std::max(0, i - d); k <= std::min(nFrames - 1, i + d); k++) {
                grown[(size_t)k] = 1;
            }
        }
        active.swap(grown);
    }
    return active;
}

bool Engine::decodeWindow(const std::shared_ptr<Channel>& chp, int worker,
                          const std::vector<float>& window,
                          double t0, double now, double lastCommit) {
    const EngineConfig cfg = cfgSnap();
    Channel* ch = chp.get();
    const double ratio = keyingRatio(window.data(), (int)window.size(), kAudioRate, cfg.cwPitchHz);
    {
        std::lock_guard<std::mutex> lck(ch->mtx);
        ch->ratio = ratio;
        if (ratio >= kSignalRatioForLife) {
            ch->hasSignal = true;
            ch->lastSignalSec = now;
        }
        else {
            ch->hasSignal = false;
        }
    }
    // Optional hard gate. Off by default: the frame gate below keeps quiet
    // stretches silent without discarding the characters around them.
    if (cfg.squelch > 0.0 && ratio < cfg.squelch) {
        {
            std::lock_guard<std::mutex> lck(ch->mtx);
            ch->gapPending = true;
        }
        enqueueSilentWindow(chp, t0, now);
        return false;
    }

    // Cheap first check on the raw channel audio: no keying at all in the
    // window means dead air, and the conditioner is not run.
    auto anyActive = [](const std::vector<uint8_t>& a) {
        for (uint8_t v : a) { if (v) { return true; } }
        return false;
    };
    if (cfg.frameGate && !anyActive(frameActivity(window))) {
        {
            std::lock_guard<std::mutex> lck(ch->mtx);
            ch->gapPending = true;
        }
        enqueueSilentWindow(chp, t0, now);
        return false;
    }

    // Per-worker scratch: the DSP stages must not be shared between threads.
    Conditioner& cond = *conditioners_[(size_t)worker];
    Spectrogram& spec = *spectrometers_[(size_t)worker];
    std::vector<float>& conditioned = condBufs_[(size_t)worker];
    std::vector<float>& specData = specBufs_[(size_t)worker];

    const auto feStart = std::chrono::steady_clock::now();
    if (cfg.conditioning) {
        // Single band: the main tone by the screening rules the model was trained on,
        // searched only within this channel (pitch +- half the channel spacing).
        const MainTone mt = findMainTone(window.data(), (int)window.size(),
                                           cfg.cwPitchHz - cfg.minChannelSpacingHz / 2,
                                           cfg.cwPitchHz + cfg.minChannelSpacingHz / 2);
        cond.process(window.data(), (int)window.size(), conditioned, kAudioRate,
                     mt.found ? mt.toneHz : cfg.cwPitchHz);
    }
    else {
        conditioned.assign(window.begin(), window.end());
    }

    // Keying gate on the conditioned audio: the 90 Hz filter around the main
    // tone keeps a neighbour station (inside the 1200 Hz channel) out of it.
    const std::vector<uint8_t> active = frameActivity(conditioned);
    if (cfg.frameGate) {
        if (!anyActive(active)) {   // only a neighbour keys: nothing to decode
            {
                std::lock_guard<std::mutex> lck(ch->mtx);
                ch->gapPending = true;
            }
            enqueueSilentWindow(chp, t0, now);
            return false;
        }
    }

    int frames = 0;
    if (!spec.compute(conditioned.data(), (int)conditioned.size(), specData, frames)) {
        return false;
    }
    const double feMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - feStart).count();

    // Hand the spectrogram to the inference thread. Windows that are ready at
    // the same time leave as ONE batched model pass, not one pass per channel.
    {
        std::lock_guard<std::mutex> lck(inferMtx_);
        if (inferQueue_.size() >= 512) { return false; }   // never queue without bound
        InferJob job;
        job.ch = chp;
        job.spec.assign(specData.begin(), specData.begin() + (long)(frames * kFreqBins));
        job.frames = frames;
        job.t0 = t0;
        job.now = now;
        job.lastCommit = lastCommit;
        job.feMs = feMs;
        job.active = active;
        inferQueue_.push_back(std::move(job));
    }
    inferCv_.notify_one();
    return true;
}

void Engine::enqueueSilentWindow(const std::shared_ptr<Channel>& chp, double t0, double now) {
    const EngineConfig cfg = cfgSnap();
    if (!cfg.voteDecode) { return; }
    {
        std::lock_guard<std::mutex> lck(inferMtx_);
        if (inferQueue_.size() >= 512) { return; }
        InferJob job;
        job.ch = chp;
        job.t0 = t0;
        job.now = now;
        job.silent = true;
        inferQueue_.push_back(std::move(job));
    }
    inferCv_.notify_one();
}

// Runs on the single model thread. Collects every queued window, groups them by
// spectrogram length, and runs one model pass per group.
void Engine::inferThread() {
    EngineConfig cfg = cfgSnap();
    lowerThreadPriority();
    SetThreadName("tinycw_infer");

    double busyAccum = 0.0;
    auto lastStat = std::chrono::steady_clock::now();
    int64_t lastRound = 0;

    while (running_.load()) {
        cfg = cfgSnap();
        // Wait for the next decode round, then for every worker to publish it.
        const int64_t wantRound = roundReq_.load();
        if (wantRound == lastRound) {
            std::unique_lock<std::mutex> lck(inferMtx_);
            inferCv_.wait_for(lck, std::chrono::milliseconds(20),
                              [this, lastRound] { return !running_.load() || roundReq_.load() != lastRound; });
            continue;
        }
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
            while (running_.load() && std::chrono::steady_clock::now() < deadline) {
                bool all = true;
                {
                    std::lock_guard<std::mutex> rl(roundMtx_);
                    for (auto& wr : workerRound_) {
                        if (wr && wr->load() < wantRound) { all = false; break; }
                    }
                }
                if (all) { break; }
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }
            lastRound = wantRound;
        }

        // Drain the whole queue, one bounded chunk at a time. A round enqueues
        // far more windows than one chunk holds.
        const size_t kMaxBatch = (size_t)std::max(1, std::min(64, cfg.maxBatchWindows));
        const double maxLagSec = std::max(6.0, cfg.tickSec * 3.0);
        const int classes = classCount();

        for (;;) {
            if (!running_.load()) { break; }
            std::vector<InferJob> jobs;
            {
                std::lock_guard<std::mutex> lck(inferMtx_);
                queueDepth_.store((int)inferQueue_.size());
                if (inferQueue_.empty()) { break; }
                if (inferQueue_.size() <= kMaxBatch) {
                    jobs.swap(inferQueue_);
                }
                else {
                    jobs.assign(std::make_move_iterator(inferQueue_.begin()),
                                std::make_move_iterator(inferQueue_.begin() + (long)kMaxBatch));
                    inferQueue_.erase(inferQueue_.begin(), inferQueue_.begin() + (long)kMaxBatch);
                }
            }
            if (jobs.empty()) { break; }

            const auto tChunk0 = std::chrono::steady_clock::now();

            // Drop windows that already waited too long: their text would land
            // behind live audio anyway.
            {
                const double t = nowSec();
                const size_t before = jobs.size();
                jobs.erase(std::remove_if(jobs.begin(), jobs.end(),
                                          [t, maxLagSec](const InferJob& j) {
                                              return (t - j.now) > maxLagSec;
                                          }),
                           jobs.end());
                if (jobs.size() != before) {
                    static double lastWarn = 0.0;
                    if (t - lastWarn > 10.0) {
                        lastWarn = t;
                        char m[128];
                        snprintf(m, sizeof m, "dropped %d stale windows (lag > %.0f s)",
                                 (int)(before - jobs.size()), maxLagSec);
                        flog::warn("[tinycw] {}", m);
                    }
                }
                if (jobs.empty()) { continue; }
                double lag = 0.0;
                for (auto& j : jobs) { lag = std::max(lag, t - j.now); }
                queueLagSec_.store(lag);
            }

            // Group by frame count: the window length is fixed, so this is one
            // group in practice.
            std::map<int, std::vector<size_t>> groups;
            for (size_t i = 0; i < jobs.size(); i++) {
                if (!jobs[i].silent) { groups[jobs[i].frames].push_back(i); }
            }

            std::vector<float> batchSpec;
            std::vector<float> logProbs;

            for (auto& [frames, idx] : groups) {
                if (!model_ || frames <= 0) { continue; }
                const size_t n = idx.size();
                batchSpec.resize(n * (size_t)kFreqBins * (size_t)frames);
                for (size_t k = 0; k < n; k++) {
                    std::memcpy(batchSpec.data() + k * (size_t)kFreqBins * (size_t)frames,
                                jobs[idx[k]].spec.data(),
                                sizeof(float) * (size_t)kFreqBins * (size_t)frames);
                }

                const double tIn = nowSec();
                {
                    char m[160];
                    snprintf(m, sizeof m, "model call begin t=%.3f s, ch=%d, frames=%d",
                             tIn, (int)n, frames);
                    flog::info("[tinycw] {}", m);
                }

                int outFrames = 0;
                std::string err;
                const auto tModel0 = std::chrono::steady_clock::now();
                const bool ok = model_->inferBatch(batchSpec.data(), (int)n, kFreqBins, frames,
                                                   logProbs, outFrames, err);
                const double passMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - tModel0).count();
                {
                    char m[160];
                    snprintf(m, sizeof m, "model call end   t=%.3f s, ch=%d, %.1f ms",
                             nowSec(), (int)n, passMs);
                    flog::info("[tinycw] {}", m);
                }
                if (!ok) {
                    static bool warned = false;
                    if (!warned) {
                        flog::error("[tinycw] inference failed: {}", err);
                        warned = true;
                    }
                    continue;
                }
                if (outFrames <= 0 ||
                    logProbs.size() < (size_t)outFrames * n * (size_t)classes) { continue; }

                // Output layout is [frames, batch, classes]; slice per channel.
                std::vector<float> slice((size_t)outFrames * (size_t)classes);
                for (size_t k = 0; k < n; k++) {
                    for (int t = 0; t < outFrames; t++) {
                        std::memcpy(slice.data() + (size_t)t * (size_t)classes,
                                    logProbs.data() + ((size_t)t * n + k) * (size_t)classes,
                                    sizeof(float) * (size_t)classes);
                    }
                    const auto tCtc0 = std::chrono::steady_clock::now();
                    if (cfg.voteDecode) {
                        commitChannelVote(jobs[idx[k]].ch, slice.data(), outFrames,
                                          jobs[idx[k]].t0, jobs[idx[k]].active,
                                          jobs[idx[k]].now);
                    }
                    else if (cfg.massEnsemble) {
                        commitChannelZone(jobs[idx[k]].ch, slice.data(), outFrames,
                                          jobs[idx[k]].t0, jobs[idx[k]].active,
                                          jobs[idx[k]].now);
                    }
                    else {
                        applyModelOutput(jobs[idx[k]].ch, slice.data(), outFrames,
                                         jobs[idx[k]].t0, jobs[idx[k]].now,
                                         jobs[idx[k]].lastCommit, jobs[idx[k]].active);
                    }
                    const double ctcMs = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - tCtc0).count();
                    std::lock_guard<std::mutex> lck(perfMtx_);
                    perfCtcMs_ += ctcMs;
                }

                double fe = 0.0;
                for (size_t k = 0; k < n; k++) { fe += jobs[idx[k]].feMs; }
                {
                    std::lock_guard<std::mutex> lck(perfMtx_);
                    perfFeMs_ += fe;
                    perfInferMs_ += passMs;
                    perfChannels_ += (double)n;
                    perfPasses_ += 1;
                }
                lastBatchSize_ = (int)n;
            }

            // Silent windows after the decoded ones: the queue is in time order
            // per channel, and a silent window must not freeze slots that an
            // earlier window of this chunk has not voted on yet.
            for (auto& j : jobs) {
                if (j.silent && cfg.voteDecode) {
                    commitChannelVote(j.ch, nullptr, 0, j.t0, j.active, j.now);
                }
            }

            const double chunkSec = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - tChunk0).count();
            busyAccum += chunkSec;

            // Hand the CPU back between chunks so CoreAudio can run.
            if (cfg.batchGapMs > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(cfg.batchGapMs));
            }
            if (cfg.inferCpuPercent > 1.0) {
                const double budget = chunkSec * (100.0 / cfg.inferCpuPercent - 1.0);
                if (budget > 0.002) {
                    std::this_thread::sleep_for(std::chrono::milliseconds((long)(budget * 1000.0)));
                }
            }
        }

        const auto tNow = std::chrono::steady_clock::now();
        const double wall = std::chrono::duration<double>(tNow - lastStat).count();
        if (wall >= 1.0) {
            if (inferBusy_) { inferBusy_->store(100.0 * (busyAccum / wall)); }
            busyAccum = 0.0;
            lastStat = tNow;
        }
    }

    if (inferBusy_) { inferBusy_->store(0.0); }
}

void Engine::applyModelOutput(const std::shared_ptr<Channel>& chp, const float* logProbs,
                              int outFrames, double t0, double now, double lastCommit,
                              const std::vector<uint8_t>& active) {
    const EngineConfig cfg = cfgSnap();
    Channel* ch = chp.get();
    const int classes = classCount();
    std::vector<int> ids;
    std::vector<int> framesIdx;
    // Prefix beam search keeps several hypotheses per window: fewer single
    // character substitutions than greedy, no extra model work.
    const int beam = (cfg.ctcBeam > 1) ? cfg.ctcBeam : 0;
    if (beam > 1) { beamCtc(logProbs, outFrames, classes, beam, ids, framesIdx); }
    else { greedyCtc(logProbs, outFrames, classes, ids, framesIdx); }

    // Model output frame period: hop * time_downsample.
    const double framePeriod = 2.0 * (double)kHop / (double)kAudioRate;

    // Overlapping windows re-decode most of their audio. Relying on the model's
    // own frame timing to drop the duplicates leaks insertions, so each window
    // commits only its newest slice (the last tick) and the overlap is used as
    // context only.
    const double windowSec = cfg.windowSec;
    // The provisional tail can only be re-decoded if the next window commits
    // further back than this one did. Without the overlap the rolled back
    // characters fall outside the next commit zone and are lost.
    double tailFactor = cfg.commitTailFactor;
    if (cfg.provisionalSec > 0.0 && cfg.tickSec > 0.0) {
        const double needed = 1.0 + cfg.provisionalSec / cfg.tickSec;
        if (tailFactor < needed) { tailFactor = needed; }
    }
    double keepFrom = t0 + windowSec - cfg.tickSec * tailFactor - 0.10;
    if (keepFrom < t0) { keepFrom = t0; }

    const std::vector<std::string>& tok = tokens();
    std::lock_guard<std::mutex> lck(ch->mtx);

    // Drop the provisional tail of the previous window: those characters sat on
    // the window edge, so the current window has the full context for them.
    auto trimBack = [](std::string& t, int n) {
        while (n > 0 && !t.empty()) { t.pop_back(); n--; }
    };
    auto trimBackForFreq = trimBack;
    if (ch->provisionalTail > 0) {
        // The same characters were already published for this frequency.
        const int64_t key = (int64_t)std::llround(ch->freqHz / 50.0);
        std::lock_guard<std::mutex> tlck(textMtx_);
        auto it = textByFreq_.find(key);
        if (it != textByFreq_.end()) {
            trimBackForFreq(it->second.text, ch->provisionalTail);
            trimBackForFreq(it->second.fresh, ch->provisionalTail);
        }
    }
    double commit = lastCommit;
    if (ch->provisionalTail > 0) {
        trimBack(ch->rollingText, ch->provisionalTail);
        trimBack(ch->freshText, ch->provisionalTail);
        trimBack(ch->pendingText, ch->provisionalTail);
        commit = ch->stableCommitSec;
        ch->provisionalTail = 0;
    }
    // The tail of the window stays provisional: it is re-decoded by the next
    // window, which has the full timing context for it.
    double provisionalSec = cfg.provisionalSec;
    const double committedSpan = cfg.tickSec * cfg.commitTailFactor;
    if (provisionalSec > committedSpan * 0.5) { provisionalSec = committedSpan * 0.5; }
    const double provisionalFrom = t0 + windowSec - provisionalSec;

    auto appendText = [ch](const char* what) {
        ch->pendingText += what;
        ch->rollingText += what;
        ch->freshText += what;
    };
    // Silence before this window: mark the break between two transmissions.
    if (ch->gapPending) {
        ch->gapPending = false;
        if (!ch->rollingText.empty() && ch->rollingText.back() != ' ') {
            appendText("  ");
        }
    }

    for (size_t i = 0; i < ids.size(); i++) {
        const double charTime = t0 + (double)framesIdx[i] * framePeriod;
        if (charTime < keepFrom) { continue; }
        // One output frame of guard: the same character shifted by a frame or
        // two in the next window must not be appended twice.
        if (charTime <= commit + framePeriod) { continue; }
        commit = charTime;
        const int id = ids[i];
        if (id <= 0 || id >= (int)tok.size()) { continue; }
        const std::string& s = tok[(size_t)id];
        // Gaps between characters are silent by definition, so the keying gate
        // must not test word spaces and prosigns: it would delete every one.
        const bool gapToken = (s == " " || s == "<SK>" || s == "<KN>");
        if (!gapToken && !active.empty()) {
            const size_t idx = (size_t)std::lround((charTime - t0) / cfg.frameGateSec);
            if (idx >= active.size() || !active[idx]) { continue; }
        }
        if (s == " " && (ch->pendingText.empty() || ch->pendingText.back() == ' ')) { continue; }
        ch->pendingText += s;
        ch->rollingText += s;
        if (ch->freshText.size() > 0 && (nowSec() - ch->freshAtSec) > 2.0) {
            ch->freshText.clear();   // the previous burst is no longer "new"
        }
        ch->freshText += s;
        ch->freshAtSec = nowSec();
        {
            const int64_t key = (int64_t)std::llround(ch->freqHz / 50.0);
            std::lock_guard<std::mutex> tlck(textMtx_);
            FreqText& e = textByFreq_[key];
            e.freqHz = ch->freqHz;
            e.text = ch->rollingText;
            e.fresh = ch->freshText;
            e.freshAtSec = ch->freshAtSec;
            e.lastSec = ch->lastTextSec;
        }
        if (charTime >= provisionalFrom) {
            ch->provisionalTail += 1;
        }
        else {
            ch->stableCommitSec = charTime;
            ch->provisionalTail = 0;
        }
        if (ch->rollingText.size() > 1000) { ch->rollingText.erase(0, ch->rollingText.size() - 1000); }
        ch->lastTextSec = nowSec();
    }
    ch->lastCommitSec = commit;
    if (commit > lastCommit) {
        std::lock_guard<std::mutex> lck2(markerMtx_);
        markers_.noteDecode(ch->freqHz, nowSec());
    }
}

void Engine::commitChannelVote(const std::shared_ptr<Channel>& chp,
                               const float* logProbs, int outFrames, double t0,
                               const std::vector<uint8_t>& active, double now) {
    const EngineConfig cfg = cfgSnap();
    Channel* ch = chp.get();
    const int classes = classCount();
    const std::vector<std::string>& tok = tokens();
    const double framePeriod = 2.0 * (double)kHop / (double)kAudioRate;   // 30 ms

    std::vector<int> ids, idx;
    greedyCtc(logProbs, outFrames, classes, ids, idx);
    // Keying gate: a character on a frame without keying energy gets no vote.
    // Word spaces and prosigns sit on silent frames by definition.
    std::vector<int> gIds, gIdx;
    for (size_t i = 0; i < ids.size(); i++) {
        const int id = ids[i];
        if (id <= 0 || id >= (int)tok.size()) { continue; }
        const std::string& s = tok[(size_t)id];
        const bool gapToken = (s == " " || s == "<SK>" || s == "<KN>");
        if (!gapToken && !active.empty() &&
            (idx[i] >= (int)active.size() || !active[(size_t)idx[i]])) { continue; }
        gIds.push_back(id);
        gIdx.push_back(idx[i]);
    }

    std::lock_guard<std::mutex> lck(ch->mtx);
    ch->vote.add(gIds, gIdx, t0, framePeriod, cfg.windowSec);
    const size_t before = ch->voteFrozen.text.size();
    const std::vector<VoteText::Slot> frozenSlots = ch->vote.takeFrozen();
    if (slotSink_ && !frozenSlots.empty()) { slotSink_(ch->freqHz, frozenSlots); }
    for (const VoteText::Slot& sl : frozenSlots) {
        if (sl.id > 0 && sl.id < (int)tok.size()) { ch->calls.add(tok[(size_t)sl.id], sl.sec); }
    }
    ch->calls.update(t0);
    renderSlots(frozenSlots, tok, ch->voteFrozen, ch->voteRender);
    // Batch output (CLI) gets only the final text.
    ch->pendingText += ch->voteFrozen.text.substr(before);
    ch->voteFrozen.keepLast(1000);

    ConfText open;
    RenderState openRender = ch->voteRender;
    renderSlots(ch->vote.open(), tok, open, openRender);

    ch->rollingText = ch->voteFrozen.text + open.text;
    ch->freshText = open.text;
    ch->freshAtSec = now;
    // A silent window (logProbs == nullptr) only ages the votes. The open tail
    // still holds the text of earlier windows for ~12 s, so it must not count
    // as a new decode: the row age and the waterfall marker would stay fresh
    // long after the keying stopped. The same holds for the tail that freezes
    // during the silence: it is old text.
    const bool silent = (logProbs == nullptr);
    const bool decoded = !silent && (!open.text.empty() || ch->voteFrozen.text.size() != before);
    const bool any = !ch->rollingText.empty();
    if (any) {
        const int64_t textKey = (int64_t)std::llround(ch->freqHz / 50.0);
        std::lock_guard<std::mutex> tlck(textMtx_);
        FreqText& e = textByFreq_[textKey];
        e.freqHz = ch->freqHz;
        e.text = ch->rollingText;
        e.conf = ch->voteFrozen.conf;
        e.conf.insert(e.conf.end(), open.conf.begin(), open.conf.end());
        callPass(e.text, e.conf, ch->calls.calls());
        fixWords(e.text, e.conf);
        e.fresh = open.text;
        e.freshAtSec = now;
        if (decoded) {
            e.lastSec = now;
            ch->lastTextSec = now;
        }
    }
    ch->lastCommitSec = ch->vote.frozenSec();
    ch->provisionalTail = 0;
    if (decoded) {
        std::lock_guard<std::mutex> lck2(markerMtx_);
        markers_.noteDecode(ch->freqHz, nowSec());
    }
}

void Engine::commitChannelZone(const std::shared_ptr<Channel>& chp,
                               const float* logProbs, int outFrames, double t0,
                               const std::vector<uint8_t>& active, double now) {
    const EngineConfig cfg = cfgSnap();
    Channel* ch = chp.get();
    const int classes = classCount();
    const std::vector<std::string>& tok = tokens();
    const double framePeriod = 2.0 * (double)kHop / (double)kAudioRate;   // 30 ms
    const double lagSec = (cfg.massLagSec > 0.0) ? cfg.massLagSec : kZoneLagSec;

    std::vector<int> ids, idx;
    greedyCtc(logProbs, outFrames, classes, ids, idx);

    std::lock_guard<std::mutex> lck(ch->mtx);
    const std::vector<ZoneChar> chars =
        commitZone(ch->zone, ids, idx, t0, framePeriod, cfg.windowSec, lagSec, t0 + kZoneEdgeSec);

    if (ch->gapPending) {
        ch->gapPending = false;
        if (!ch->rollingText.empty() && ch->rollingText.back() != ' ') {
            ch->pendingText += "  ";
            ch->rollingText += "  ";
            ch->freshText += "  ";
        }
    }

    const int64_t textKey = (int64_t)std::llround(ch->freqHz / 50.0);
    bool appended = false;
    for (const ZoneChar& c : chars) {
        if (c.id <= 0 || c.id >= (int)tok.size()) { continue; }
        const std::string& sym = tok[(size_t)c.id];
        // Word spaces are silent by definition, so the keying gate must skip
        // them; testing them would delete every word break.
        const bool gapToken = (sym == " " || sym == "<SK>" || sym == "<KN>");
        if (!gapToken && !active.empty() &&
            (c.frame >= (int)active.size() || !active[(size_t)c.frame])) { continue; }
        if (sym == " " && (ch->pendingText.empty() || ch->pendingText.back() == ' ')) { continue; }

        // A pause longer than any word gap is the break between transmissions.
        // The model has no silence token, so render it as a double space instead
        // of gluing the next "CQ" onto the previous callsign.
        if (sym != " " && ch->lastCharSec > 0.0 && (c.sec - ch->lastCharSec) > 2.0 &&
            !ch->pendingText.empty() && ch->pendingText.back() != ' ') {
            ch->pendingText += "  ";
            ch->rollingText += "  ";
            ch->freshText += "  ";
        }
        if (sym != " ") { ch->lastCharSec = c.sec; }

        ch->pendingText += sym;
        ch->rollingText += sym;
        if (!ch->freshText.empty() && (now - ch->freshAtSec) > 2.0) { ch->freshText.clear(); }
        ch->freshText += sym;
        ch->freshAtSec = now;
        appended = true;
    }

    if (appended) {
        std::lock_guard<std::mutex> tlck(textMtx_);
        FreqText& e = textByFreq_[textKey];
        e.freqHz = ch->freqHz;
        e.text = ch->rollingText;
        e.fresh = ch->freshText;
        e.freshAtSec = ch->freshAtSec;
        e.lastSec = now;
        ch->lastTextSec = now;
    }
    if (ch->rollingText.size() > 1000) {
        ch->rollingText.erase(0, ch->rollingText.size() - 1000);
    }

    ch->lastCommitSec = ch->zone.zoneEndSec;
    ch->provisionalTail = 0;
    if (appended) {
        std::lock_guard<std::mutex> lck2(markerMtx_);
        markers_.noteDecode(ch->freqHz, nowSec());
    }
}

Engine::PerfStats Engine::takePerfStats() {
    PerfStats st;
    std::lock_guard<std::mutex> lck(perfMtx_);
    const double now = nowSec();
    const double window = (perfWindowStart_ > 0.0) ? (now - perfWindowStart_) : 0.0;
    perfWindowStart_ = now;
    if (window > 0.0) {
        const double wallMs = window * 1000.0;
        st.windowSec = window;
        st.detectDutyPct = 100.0 * perfDetectMs_ / wallMs;
        st.modelDutyPct = 100.0 * perfInferMs_ / wallMs;
        st.feCpuPct = 100.0 * perfFeMs_ / wallMs;
        st.decodeDutyPct = 100.0 * (perfInferMs_ + perfCtcMs_) / wallMs;
        st.roundsPerSec = (double)perfPasses_ / window;
    }
    if (perfPasses_ > 0 && perfChannels_ > 0.0) {
        st.passes = perfPasses_;
        st.feMsPerChannel = perfFeMs_ / perfChannels_;
        st.inferMsPerPass = perfInferMs_ / (double)perfPasses_;
        st.channelsPerPass = perfChannels_ / (double)perfPasses_;
        st.inferMsPerChannel = perfInferMs_ / perfChannels_;
        st.ctcMsPerChannel = perfCtcMs_ / perfChannels_;
        st.detectMsPerTick = (perfDetectTicks_ > 0)
                                 ? perfDetectMs_ / (double)perfDetectTicks_ : 0.0;
        st.detectMsPerChannel = (perfDetectCh_ > 0.0) ? perfDetectMs_ / perfDetectCh_ : 0.0;
        st.openMsPerTick = (perfDetectTicks_ > 0)
                               ? perfOpenMs_ / (double)perfDetectTicks_ : 0.0;
    }
    perfFeMs_ = perfInferMs_ = perfChannels_ = perfCtcMs_ = perfDetectMs_ = perfOpenMs_ = 0.0;
    perfDetectCh_ = 0.0;
    perfPasses_ = 0;
    perfDetectTicks_ = 0;
    return st;
}

std::vector<Marker> Engine::activeMarkers() {
    const EngineConfig cfg = cfgSnap();
    std::lock_guard<std::mutex> lck(markerMtx_);
    // Every channel decodes in the same round, so all decode markers get the
    // same timestamp and expire together. Hold must span at least two rounds,
    // otherwise a single skipped round clears the whole band at once.
    const double hold = std::max(cfg.markerHoldSec, cfg.tickSec * 2.0 + 1.0);
    markers_.setHold(hold);
    std::vector<Marker> out;
    markers_.collect(nowSec(), out);
    int carriers = 0, decodes = 0;
    for (auto& m : out) { if (m.carrier) { carriers++; } if (m.decode) { decodes++; } }
    lastCarrier_.store(carriers);
    lastDecode_.store(decodes);
    static double lastLog = 0.0;
    const double now = nowSec();
    if (now - lastLog >= 5.0) {
        lastLog = now;
        char m[192];
        snprintf(m, sizeof m,
                 "status: channels %d, carrier %d, decode %d, rows %d, queue %d, "
                 "lag %.1f s, last batch %d, hold %.0f s",
                 channelCount_.load(), carriers, decodes, textRows_.load(),
                 queueDepth_.load(), queueLagSec_.load(), lastBatchSize_.load(), hold);
        flog::info("[tinycw] {}", m);
    }
    return out;
}

bool Engine::decodeChannel(const std::shared_ptr<Channel>& chp, int worker, double now) {
    const EngineConfig cfg = cfgSnap();
    Channel* ch = chp.get();
    const int windowSamples = (int)(cfg.windowSec * kAudioRate);
    const int tickSamples = std::max(1, (int)(cfg.tickSec * kAudioRate));

    std::vector<float> window;
    double t0 = 0.0;
    double lastCommit = 0.0;
    {
        std::lock_guard<std::mutex> lck(ch->mtx);
        // Rounds arrive every tickSec of wall clock, audio arrives at its own
        // rate, so a channel can be a few percent short of a full tick and get
        // skipped for a whole round. Allow a small tolerance.
        if (ch->totalSamples - ch->consumedSamples < (int64_t)(tickSamples * 0.85)) {
            return false;                                   // no new audio yet
        }
        const int64_t bufStartAbs = ch->totalSamples - (int64_t)ch->buf.size();
        const int64_t endCh = ch->totalSamples;             // newest audio
        int64_t startCh = endCh - windowSamples;
        if (startCh < bufStartAbs) { startCh = bufStartAbs; }
        if (endCh - startCh < (int64_t)windowSamples) { return false; }
        const size_t off = (size_t)(startCh - bufStartAbs);
        window.assign(ch->buf.begin() + (long)off,
                      ch->buf.begin() + (long)off + windowSamples);
        t0 = (double)startCh / (double)kAudioRate;
        ch->consumedSamples = endCh;
        lastCommit = ch->lastCommitSec;
    }
    return decodeWindow(chp, worker, window, t0, now, lastCommit);
}

void Engine::managerThread() {
    EngineConfig cfg = cfgSnap();
    lowerThreadPriority();
    SetThreadName("tinycw_manager");

    while (running_.load()) {
        cfg = cfgSnap();
        bool wantRebuild = false;
        double rebuildRate = 0.0;
        bool wantRetune = false;
        double retuneOffset = 0.0;
        std::vector<std::shared_ptr<Channel>> deadChannels;
        std::vector<std::pair<double, double>> toAdd;
        std::vector<std::pair<double, double>> carriers;
        std::vector<double> gated;   // channels closed by the detection gate
        const auto detectStart = std::chrono::steady_clock::now();

        // Only bookkeeping happens under mtx_. Starting or stopping a DSP chain
        // joins threads and costs tens of milliseconds per channel while the UI
        // thread needs mtx_ on every frame: doing that under the lock is what
        // stalled the window for a second on every tick.
        {
            std::lock_guard<std::mutex> lck(mtx_);
            if (!running_.load()) { break; }

            if (rangeDirty_) {
                rangeDirty_ = false;
                wantRebuild = true;
                rebuildRate = inRate_;
            }
            if (wideRetune_ && !wantRebuild) {
                wideRetune_ = false;
                wantRetune = true;
                retuneOffset = rangeCenter_ - centerHz_;
            }

            const double now = nowSec();
            if (!paused_.load() && now - lastRoundSec_ >= cfg.tickSec) {
                lastRoundSec_ = now;
                roundReq_.fetch_add(1);
            }

            // Sync the channel set with the scanner's spot list, limited to the
            // range the width policy currently allows.
            std::vector<Spot> spots;
            {
                std::lock_guard<std::mutex> lck2(scanMtx_);
                for (const Spot& s : spots_) {
                    if (s.freqHz < decodeLoHz_ || s.freqHz > decodeHiHz_) { continue; }
                    Spot v;   // without the score window: this copy runs 20 times a second
                    v.freqHz = s.freqHz;
                    v.ratio = s.ratio;
                    v.lastSeenSec = s.lastSeenSec;
                    v.scoreDb = s.scoreDb;
                    spots.push_back(v);
                }
            }
            std::sort(spots.begin(), spots.end(),
                      [](const Spot& a, const Spot& b) { return a.ratio > b.ratio; });

            std::vector<char> used(spots.size(), 0);
            for (auto& ch : channels_) {
                double best = 1e18;
                int bestIdx = -1;
                for (size_t i = 0; i < spots.size(); i++) {
                    const double d = std::fabs(spots[i].freqHz - ch->freqHz);
                    if (d < best) { best = d; bestIdx = (int)i; }
                }
                // The channel keeps its frequency: its VFO is not retuned, and
                // a moving label split one station into several text rows.
                if (bestIdx >= 0 && best < kChannelMatchHz) { used[(size_t)bestIdx] = 1; }
            }

            // Collect additions; they are started below, outside the lock. The
            // pending list also keeps two spots of the same tick from landing on
            // one station.
            std::vector<double> pending;
            for (size_t i = 0; i < spots.size(); i++) {
                if ((int)(channels_.size() + toAdd.size()) >= maxChannels_) { break; }
                if (used[i]) { continue; }
                if (spots[i].scoreDb < cfg.spotGateDb) { continue; }   // below the detection gate
                bool tooClose = false;
                for (auto& ch : channels_) {
                    if (std::fabs(ch->freqHz - spots[i].freqHz) < cfg.minChannelSpacingHz) {
                        tooClose = true;
                        break;
                    }
                }
                if (!tooClose) {
                    for (double f : pending) {
                        if (std::fabs(f - spots[i].freqHz) < cfg.minChannelSpacingHz) { tooClose = true; break; }
                    }
                }
                if (tooClose) { continue; }
                pending.push_back(spots[i].freqHz);
                toAdd.emplace_back(spots[i].freqHz, spots[i].ratio);
            }

            std::vector<std::shared_ptr<Channel>> dead;
            for (auto& ch : channels_) {
                const bool inRange = (ch->freqHz >= decodeLoHz_ - kChannelMatchHz) &&
                                     (ch->freqHz <= decodeHiHz_ + kChannelMatchHz);
                bool matched = false;
                double nearest = 1e18, scoreDb = 0.0;
                for (const Spot& s : spots) {
                    const double d = std::fabs(s.freqHz - ch->freqHz);
                    if (d < kChannelMatchHz) { matched = true; }
                    if (d < kChannelMatchHz && d < nearest) { nearest = d; scoreDb = s.scoreDb; }
                }
                // The detection gate closes a channel at once: the slider
                // was moved down past its score.
                if (matched && scoreDb < cfg.spotGateDb) {
                    dead.push_back(ch);
                    gated.push_back(ch->freqHz);
                    continue;
                }
                // Three ways to die: out of the decode range, silent for the
                // idle timeout (whatever the scanner says), or no spot anywhere
                // near it for the shorter spot hold.
                const bool idle = (now - ch->lastSignalSec) > cfg.channelIdleSec;
                if (!inRange || idle ||
                    (!matched && (now - ch->lastSignalSec) > cfg.spotHoldSec)) {
                    dead.push_back(ch);
                }
            }
            if (!dead.empty()) {
                channels_.erase(
                    std::remove_if(channels_.begin(), channels_.end(),
                                   [&dead](const std::shared_ptr<Channel>& p) {
                                       return std::find(dead.begin(), dead.end(), p) != dead.end();
                                   }),
                    channels_.end());
                deadChannels = std::move(dead);
            }

            // Carrier markers follow the channel list, not the decode rounds.
            // Refreshing them only when a window is decoded makes every dot on
            // the band expire together whenever a round is skipped.
            for (auto& ch : channels_) { carriers.emplace_back(ch->freqHz, now); }
        }

        // Heavy part one: tear down the chains that went silent.
        for (auto& ch : deadChannels) { stopChannelDsp(ch); }

        // The SDR centre moved inside the same segment: retune, no rebuild.
        if (wantRetune) {
            std::lock_guard<std::recursive_mutex> dl(dspMtx_);
            if (wide_) { wide_->setOffset(retuneOffset); }
        }

        // Heavy part two: rebuild the wide chain when the band or span moved.
        if (wantRebuild) {
            std::vector<std::shared_ptr<Channel>> old;
            {
                std::lock_guard<std::mutex> lck(mtx_);
                old = detachChannelsLocked();
            }
            for (auto& ch : old) { stopChannelDsp(ch); }
            rebuildWideChain(rebuildRate);
            carriers.clear();
        }

        // Heavy part three: open the chains for the new spots.
        if (!toAdd.empty()) {
            std::vector<std::shared_ptr<Channel>> created;
            for (auto& p : toAdd) {
                auto ch = startChannel(p.first, p.second);
                if (ch) { created.push_back(ch); }
            }
            if (!created.empty()) {
                std::lock_guard<std::mutex> lck(mtx_);
                const double now = nowSec();
                for (auto& ch : created) {
                    channels_.push_back(ch);
                    carriers.emplace_back(ch->freqHz, now);
                }
            }
        }

        {
            std::lock_guard<std::mutex> lck(markerMtx_);
            const double now = nowSec();
            for (double f : gated) { markers_.dropCarrier(f, now, 2.0 * cfg.tickSec + 1.0); }
            for (auto& c : carriers) { markers_.noteCarrier(c.first, c.second); }
        }

        size_t channelCount = 0;
        {
            std::lock_guard<std::mutex> lck(mtx_);
            channelCount = channels_.size();
        }
        channelCount_.store((int)channelCount);

        const double detectMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - detectStart).count();
        {
            std::lock_guard<std::mutex> lck(perfMtx_);
            perfDetectMs_ += detectMs;
            perfDetectCh_ += (double)std::max<size_t>(1, channelCount);
            perfDetectTicks_ += 1;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void Engine::decodeWorker(int stripe) {
    lowerThreadPriority();
    char name[32];
    snprintf(name, sizeof name, "tinycw_dec%d", stripe);
    SetThreadName(name);

    double busyAccum = 0.0;
    auto lastStat = std::chrono::steady_clock::now();
    int64_t roundSeen = 0;

    while (running_.load() && !workersPaused_.load()) {
        const auto t0 = std::chrono::steady_clock::now();
        const int64_t round = roundReq_.load();
        if (!paused_.load() && round != roundSeen) {
            roundSeen = round;
            std::vector<std::shared_ptr<Channel>> mine;
            {
                std::lock_guard<std::mutex> lck(mtx_);
                for (auto& ch : channels_) {
                    if ((int)(ch->id % (uint64_t)std::max(1, threads_)) == stripe) { mine.push_back(ch); }
                }
            }
            for (auto& ch : mine) {
                if (!running_.load() || workersPaused_.load()) { break; }
                decodeChannel(ch, stripe, nowSec());
            }
            if (stripe < (int)workerRound_.size() && workerRound_[(size_t)stripe]) {
                workerRound_[(size_t)stripe]->store(round);
            }
        }

        const auto t1 = std::chrono::steady_clock::now();
        busyAccum += std::chrono::duration<double>(t1 - t0).count();
        const double wall = std::chrono::duration<double>(t1 - lastStat).count();
        if (wall >= 1.0) {
            if (stripe < (int)workerBusy_.size() && workerBusy_[(size_t)stripe]) {
                workerBusy_[(size_t)stripe]->store(100.0 * (busyAccum / wall));
            }
            busyAccum = 0.0;
            lastStat = t1;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (stripe < (int)workerBusy_.size() && workerBusy_[(size_t)stripe]) {
        workerBusy_[(size_t)stripe]->store(0.0);
    }
}

double Engine::busyPercent() const {
    // Real process CPU: the model pass runs on its own thread, so summed
    // per-worker busy time no longer describes what the process spends.
    std::lock_guard<std::mutex> lck(cpuMtx_);
    const double wall = nowSec();
    double cpu = 0.0;
#if defined(_WIN32)
    FILETIME created, exited, kernelTime, userTime;
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernelTime, &userTime) != 0) {
        ULARGE_INTEGER k, u;
        k.LowPart = kernelTime.dwLowDateTime; k.HighPart = kernelTime.dwHighDateTime;
        u.LowPart = userTime.dwLowDateTime;   u.HighPart = userTime.dwHighDateTime;
        cpu = (double)(k.QuadPart + u.QuadPart) * 1e-7;   // 100 ns ticks
    }
#else
    struct rusage ru{};
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
        cpu = (double)ru.ru_utime.tv_sec + (double)ru.ru_utime.tv_usec * 1e-6 +
              (double)ru.ru_stime.tv_sec + (double)ru.ru_stime.tv_usec * 1e-6;
    }
#endif
    if (cpuPrevWall_ > 0.0 && wall > cpuPrevWall_ + 0.05 && cpu > cpuPrevCpu_) {
        cpuPercent_ = 100.0 * (cpu - cpuPrevCpu_) / (wall - cpuPrevWall_);
    }
    cpuPrevWall_ = wall;
    cpuPrevCpu_ = cpu;
    return cpuPercent_;
}

double Engine::cpuLoadPercent() const {
    // Load relative to the whole machine, or of the single model thread when
    // that is higher: a saturated model thread is the real limit, and on 8
    // cores it shows as only 12.5 % of the process.
    const double t = (double)std::max(1, hardwareThreads());
    double load = busyPercent() / t;
    if (inferBusy_) { load = std::max(load, inferBusy_->load()); }
    return std::max(0.0, std::min(100.0, load));
}

std::vector<BatchLine> Engine::takeBatch() {
    std::vector<BatchLine> out;
    std::lock_guard<std::mutex> lck(mtx_);
    for (auto& ch : channels_) {
        std::lock_guard<std::mutex> lck2(ch->mtx);
        if (ch->pendingText.empty()) { continue; }
        BatchLine line;
        line.freqHz = ch->freqHz;
        line.text = fixWords(callPass(ch->pendingText, ch->calls.calls()));
        line.strength = ch->ratio;
        ch->pendingText.clear();
        out.push_back(std::move(line));
    }
    return out;
}

std::vector<Engine::DecodeLine> Engine::decodeLines(double maxAgeSec) {
    std::vector<DecodeLine> out;
    const double now = nowSec();
    std::lock_guard<std::mutex> lck(textMtx_);
    for (auto it = textByFreq_.begin(); it != textByFreq_.end();) {
        FreqText& e = it->second;
        if (e.text.empty() || (maxAgeSec > 0.0 && (now - e.lastSec) > maxAgeSec)) {
            it = textByFreq_.erase(it);
            continue;
        }
        DecodeLine l;
        l.freqHz = e.freqHz;
        l.text = e.text;
        l.conf = e.conf;
        if (!e.fresh.empty() && (now - e.freshAtSec) <= 2.0) { l.fresh = e.fresh; }
        l.lastSec = e.lastSec;
        out.push_back(std::move(l));
        ++it;
    }
    std::sort(out.begin(), out.end(),
              [](const DecodeLine& a, const DecodeLine& b) { return a.freqHz < b.freqHz; });
    textRows_.store((int)out.size());
    return out;
}

double Engine::backlogSeconds() const {
    const EngineConfig cfg = cfgSnap();
    std::lock_guard<std::mutex> lck(mtx_);
    double worst = 0.0;
    const int windowSamples = (int)(cfg.windowSec * kAudioRate);
    for (auto& ch : channels_) {
        std::lock_guard<std::mutex> lck2(ch->mtx);
        const int64_t ready = ch->totalSamples - ch->consumedSamples - windowSamples;
        if (ready > 0) {
            const double s = (double)ready / (double)kAudioRate;
            if (s > worst) { worst = s; }
        }
    }
    return worst;
}

int Engine::spotCount() {
    std::lock_guard<std::mutex> lck(scanMtx_);
    return (int)spots_.size();
}

int Engine::liveChannels() const {
    std::lock_guard<std::mutex> lck(mtx_);
    return (int)channels_.size();
}

int Engine::activeSignals() const {
    std::lock_guard<std::mutex> lck(mtx_);
    int n = 0;
    for (auto& ch : channels_) {
        std::lock_guard<std::mutex> clck(ch->mtx);
        if (!ch->hasSignal) { continue; }
        n++;
    }
    return n;
}

} // namespace tinycw