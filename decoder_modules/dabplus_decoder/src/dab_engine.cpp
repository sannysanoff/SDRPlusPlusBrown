#include "dab_engine.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>

#include "basic_radio/basic_audio_channel.h"
#include "basic_radio/basic_radio.h"
#include "dab/constants/subchannel_protection_tables.h"
#include "dab/database/dab_database.h"
#include "ofdm/dab_mapper_ref.h"
#include "ofdm/dab_ofdm_params_ref.h"
#include "ofdm/dab_prs_ref.h"
#include "ofdm/ofdm_demodulator.h"

namespace dabplus {

    struct Engine::Impl {
        // One second of decoded audio at the highest rate a DAB service uses,
        // interleaved int16, same capacity the SDR# reference bridge used.
        static constexpr size_t AUDIO_RING_SAMPLES = 192000;

        std::unique_ptr<BasicRadio> radio;
        std::unique_ptr<OFDM_Demod> ofdm;
        OFDM_Demod* demod = nullptr;      // stable view for the frame callback
        BasicRadio* radioRaw = nullptr;   // stable while the demod's threads run

        // Metadata published by the OFDM frame callback and read by the GUI.
        mutable std::mutex metaMtx;
        Snapshot meta;
        std::vector<Service> services;

        std::atomic<int> selected{-1};
        std::atomic<int> requested{-1};

        // Decoded PCM, written by the MSC threads and drained by the audio worker.
        std::mutex audioMtx;
        std::vector<int16_t> ring = std::vector<int16_t>(AUDIO_RING_SAMPLES);
        size_t ringRead = 0;
        size_t ringWrite = 0;
        size_t ringCount = 0;
        uint32_t audioRate = 48000;
        bool audioStereo = false;
        std::atomic<int> audioOverflows{0};

        // Constellation snapshot, published per frame.
        mutable std::mutex constMtx;
        std::array<std::complex<float>, 1024> constellation;
        size_t constellationCount = 0;

        Impl(int transmission_mode, int ofdm_threads) {
            radio = std::make_unique<BasicRadio>(get_dab_parameters(transmission_mode), size_t(ofdm_threads));
            radioRaw = radio.get();

            radio->On_Audio_Channel().Attach([this](subchannel_id_t id, Basic_Audio_Channel& channel) {
                const int channelId = int(id);
                channel.GetControls().StopAll();
                channel.OnAudioData().Attach([this, channelId](BasicAudioParams params, tcb::span<const uint8_t> bytes) {
                    OnAudioData(channelId, params, bytes);
                });
                channel.OnDynamicLabel().Attach([this, channelId](std::string_view label) {
                    if (channelId != selected.load()) { return; }
                    std::lock_guard<std::mutex> lck(metaMtx);
                    meta.dynamic_label.assign(label);
                });
            });

            const OFDM_Params params = get_DAB_OFDM_params(transmission_mode);
            std::vector<std::complex<float>> prs(params.nb_fft);
            std::vector<int> mapper(params.nb_data_carriers);
            get_DAB_PRS_reference(transmission_mode, prs);
            get_DAB_mapper_ref(mapper, params.nb_fft);

            ofdm = std::make_unique<OFDM_Demod>(params, prs, mapper, ofdm_threads);
            demod = ofdm.get();

            // Shallow nulls (low SNR, short recordings) still need a PRS
            // correlation to start; same threshold as the SDR# reference.
            ofdm->GetConfig().null_l1_search.thresh_null_start = 0.65f;

            // The frame callback runs on the demodulator's coordinator thread and
            // reaches the objects through these stable pointers, never through the
            // owning unique_ptrs: the destructor releases ofdm first, which joins
            // that thread, and only then radio.
            BasicRadio* radioStable = radioRaw;
            OFDM_Demod* demodStable = demod;
            ofdm->On_OFDM_Frame().Attach([this, radioStable, demodStable](tcb::span<const viterbi_bit_t> bits) {
                OnFrame(radioStable, demodStable, bits);
            });
        }

        ~Impl() {
            ofdm.reset();
            radio.reset();
        }

        void OnAudioData(int channelId, const BasicAudioParams& params, tcb::span<const uint8_t> bytes) {
            if (params.bytes_per_sample != 2) { return; }
            const size_t channels = params.is_stereo ? 2 : 1;
            const size_t samples = bytes.size() / sizeof(int16_t);
            const size_t frames = samples / channels;
            if (!frames) { return; }

            const int16_t* src = reinterpret_cast<const int16_t*>(bytes.data());
            std::lock_guard<std::mutex> lck(audioMtx);
            if (channelId != selected.load()) { return; }
            if (params.frequency != audioRate || params.is_stereo != audioStereo) {
                audioRate = params.frequency;
                audioStereo = params.is_stereo;
                ringRead = ringWrite = ringCount = 0;
            }
            const size_t incoming = frames * channels;
            if (incoming > ring.size() - ringCount) {
                audioOverflows.fetch_add(1, std::memory_order_relaxed);
                ringRead = ringWrite = ringCount = 0;
                return;
            }
            for (size_t i = 0; i < incoming; i++) {
                ring[ringWrite] = src[i];
                ringWrite = (ringWrite + 1 == ring.size()) ? 0 : ringWrite + 1;
            }
            ringCount += incoming;
        }

        void OnFrame(BasicRadio* radioStable, OFDM_Demod* demodStable, tcb::span<const viterbi_bit_t> bits) {
            // Constellation: unit-magnitude data carriers of this frame's first
            // data symbol, scaled by the frame's mean carrier amplitude.
            {
                const auto symbols = demodStable->GetFrameDataVec();
                const size_t carriers = demodStable->GetOFDMParams().nb_data_carriers;
                float sum = 0.0f;
                for (size_t i = 0; i < carriers; i++) {
                    const float magnitude = std::abs(symbols[i]);
                    if (std::isfinite(magnitude)) { sum += magnitude; }
                }
                const float mean = carriers ? sum / float(carriers) : 0.0f;
                std::lock_guard<std::mutex> lck(constMtx);
                constellationCount = 0;
                if (std::isfinite(mean) && mean > 0.0f) {
                    const size_t count = std::min(carriers, constellation.size());
                    for (size_t i = 0; i < count; i++) {
                        const auto value = symbols[i * carriers / count];
                        if (std::isfinite(value.real()) && std::isfinite(value.imag())) {
                            constellation[constellationCount++] = value / mean;
                        }
                    }
                }
            }

            // Apply a pending service selection before the frame is decoded.
            const int desired = requested.load();
            if (desired != selected.load()) {
                const int previous = selected.exchange(desired);
                if (previous >= 0) {
                    if (auto* channel = radioStable->Get_Audio_Channel(subchannel_id_t(previous))) {
                        channel->GetControls().StopAll();
                    }
                }
                std::lock_guard<std::mutex> lck(metaMtx);
                meta.dynamic_label.clear();
                std::lock_guard<std::mutex> audioLck(audioMtx);
                ringRead = ringWrite = ringCount = 0;
            }
            if (desired >= 0) {
                if (auto* channel = radioStable->Get_Audio_Channel(subchannel_id_t(desired))) {
                    channel->GetControls().RunAll();
                }
            }

            radioStable->Process(bits);

            // Publish ensemble, services and sync state for the GUI.
            const OFDM_Demod::State state = demodStable->GetState();
            const int frames = demodStable->GetTotalFramesRead();
            const int desyncs = demodStable->GetTotalFramesDesync();
            const float coarse = demodStable->GetCoarseFrequencyOffset() * 2048000.0f;
            const float fine = demodStable->GetFineFrequencySnapshot() * 2048000.0f;
            const float timeUs = float(demodStable->GetFineTimeOffset()) * 1000000.0f / 2048000.0f;
            const float signal = demodStable->GetSignalAverage();

            std::lock_guard<std::mutex> dbLock(radioStable->GetMutex());
            auto& db = radioStable->GetDatabase();

            std::lock_guard<std::mutex> lck(metaMtx);
            meta.ensemble = db.ensemble.label;
            meta.ensemble_short = db.ensemble.short_label;
            meta.ensemble_id = db.ensemble.id.value;
            meta.ecc = db.ensemble.extended_country_code;
            meta.sync_state = int(state);
            meta.frames = frames;
            meta.desyncs = desyncs;
            meta.coarse_hz = coarse;
            meta.fine_hz = fine;
            meta.time_us = timeUs;
            meta.signal = signal;
            meta.selected = selected.load();

            services.clear();
            for (const auto& component : db.service_components) {
                if (!component.is_complete) { continue; }
                if (component.transport_mode != TransportMode::STREAM_MODE_AUDIO) { continue; }
                Service service;
                service.subchannel = int(component.subchannel_id);
                service.component = int(component.component_id);
                service.service_id = component.service_id.value;
                service.type = int(component.audio_service_type);
                const auto uid = component.service_id.get_unique_identifier();
                for (const auto& entry : db.services) {
                    if (entry.id.get_unique_identifier() != uid) { continue; }
                    service.label = entry.label;
                    service.short_label = entry.short_label;
                    break;
                }
                services.push_back(std::move(service));
            }
            std::sort(services.begin(), services.end(), [](const Service& a, const Service& b) {
                if (a.label != b.label) { return a.label < b.label; }
                return a.subchannel < b.subchannel;
            });
            meta.services = services;

            // Keep the audio rate the GUI reports in step with the running decoder.
            std::lock_guard<std::mutex> audioLck(audioMtx);
            meta.audio_rate = audioRate;
            meta.audio_stereo = audioStereo;
            meta.audio_overflows = audioOverflows.load(std::memory_order_relaxed);
        }
    };

    Engine::Engine(int transmission_mode, int ofdm_threads) {
        impl = std::make_unique<Impl>(transmission_mode, ofdm_threads);
    }

    Engine::~Engine() {
        impl.reset();
    }

    void Engine::Process(const std::complex<float>* samples, size_t count) {
        impl->ofdm->Process({samples, count});
    }

    void Engine::Select(int subchannel) {
        impl->requested.store(subchannel);
    }

    int Engine::GetSelected() const {
        return impl->selected.load();
    }

    void Engine::SetCoherentSecondPass(bool enabled) {
        impl->ofdm->SetCoherentSecondPass(enabled);
    }

    Snapshot Engine::GetSnapshot() const {
        std::lock_guard<std::mutex> lck(impl->metaMtx);
        return impl->meta;
    }

    size_t Engine::AudioPendingFrames() const {
        std::lock_guard<std::mutex> lck(impl->audioMtx);
        const size_t channels = impl->audioStereo ? 2 : 1;
        return impl->ringCount / channels;
    }

    size_t Engine::ReadAudio(int16_t* dst, size_t max_frames, uint32_t& rate, bool& stereo) {
        std::lock_guard<std::mutex> lck(impl->audioMtx);
        rate = impl->audioRate;
        stereo = impl->audioStereo;
        const size_t channels = impl->audioStereo ? 2 : 1;
        const size_t frames = std::min(max_frames, impl->ringCount / channels);
        const size_t samples = frames * channels;
        for (size_t i = 0; i < samples; i++) {
            dst[i] = impl->ring[impl->ringRead];
            impl->ringRead = (impl->ringRead + 1 == impl->ring.size()) ? 0 : impl->ringRead + 1;
        }
        impl->ringCount -= samples;
        return frames;
    }

    void Engine::ClearAudio() {
        std::lock_guard<std::mutex> lck(impl->audioMtx);
        impl->ringRead = impl->ringWrite = impl->ringCount = 0;
    }

    size_t Engine::GetConstellation(std::complex<float>* dst, size_t capacity) const {
        std::lock_guard<std::mutex> lck(impl->constMtx);
        const size_t count = std::min(capacity, impl->constellationCount);
        std::memcpy(dst, impl->constellation.data(), count * sizeof(std::complex<float>));
        return count;
    }

}
