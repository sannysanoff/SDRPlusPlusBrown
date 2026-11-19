#pragma once
#include <complex>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Adapter between SDR++ and the DAB-Radio engine (OFDM demodulator + BasicRadio).
// Deliberately free of SDR++ headers so the decoder side can be driven and tested
// without the GUI.
namespace dabplus {

    // One audio service of the ensemble, as advertised by the FIC.
    struct Service {
        int subchannel = -1;
        int component = -1;
        int service_id = -1;
        int type = 0;
        std::string label;
        std::string short_label;
    };

    struct Snapshot {
        std::string ensemble;
        std::string ensemble_short;
        std::string dynamic_label;
        int ensemble_id = -1;
        int ecc = -1;
        int selected = -1;
        uint32_t audio_rate = 0;
        bool audio_stereo = false;
        int sync_state = 0;
        int frames = 0;
        int desyncs = 0;
        int audio_overflows = 0;
        float coarse_hz = 0.0f;
        float fine_hz = 0.0f;
        float time_us = 0.0f;
        float signal = 0.0f;
        std::vector<Service> services;
    };

    class Engine {
    public:
        // transmission_mode: DAB Mode I..IV (1 = Band III)
        Engine(int transmission_mode, int ofdm_threads = 1);
        ~Engine();

        // Feed 2048 kS/s complex baseband. Safe to call from a DSP worker thread;
        // frames are decoded on the demodulator's own coordinator thread.
        void Process(const std::complex<float>* samples, size_t count);

        // Request an audio service by its subchannel id, -1 to stop audio.
        void Select(int subchannel);
        int GetSelected() const;

        void SetCoherentSecondPass(bool enabled);

        Snapshot GetSnapshot() const;

        // Drain decoded PCM. Returns whole frames copied; the engine's native rate
        // and channel count are reported so the caller can resample.
        size_t ReadAudio(int16_t* dst, size_t max_frames, uint32_t& rate, bool& stereo);
        void ClearAudio();

        // Whole frames waiting in the audio ring. The demodulator uses it to stop
        // pulling samples when decoded audio is running ahead of the device.
        size_t AudioPendingFrames() const;

        // Unit-magnitude data carriers of the most recent frame, for the GUI.
        size_t GetConstellation(std::complex<float>* dst, size_t capacity) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

}
