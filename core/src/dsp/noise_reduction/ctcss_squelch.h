#pragma once
#include "../channel/rx_vfo.h"
#include "../demod/quadrature.h"
#include "../filter/fir.h"
#include "../taps/high_pass.h"
#include <fftw3.h>
#include <map>
#include <atomic>

#define CTCSS_DECODE_SAMPLERATE 500//250.0
#define CTCSS_DECODE_BANDWIDTH  200.0
#define CTCSS_DECODE_OFFSET     160.55

namespace dsp::noise_reduction {
    enum CTCSSTone {
        /**
         * Indicates that any valid tone will let audio through.
        */
        CTCSS_TONE_ANY = -2,

        /**
         * Indicates that no tone is being received, or to act as a decoder only, letting audio through continuously.
        */
        CTCSS_TONE_NONE = -1,

        /**
         * CTCSS Tone Frequency.
        */
        CTCSS_TONE_67Hz,
        CTCSS_TONE_69_3Hz,
        CTCSS_TONE_71_9Hz,
        CTCSS_TONE_74_4Hz,
        CTCSS_TONE_77Hz,
        CTCSS_TONE_79_7Hz,
        CTCSS_TONE_82_5Hz,
        CTCSS_TONE_85_4Hz,
        CTCSS_TONE_88_5Hz,
        CTCSS_TONE_91_5Hz,
        CTCSS_TONE_94_8Hz,
        CTCSS_TONE_97_4Hz,
        CTCSS_TONE_100Hz,
        CTCSS_TONE_103_5Hz,
        CTCSS_TONE_107_2Hz,
        CTCSS_TONE_110_9Hz,
        CTCSS_TONE_114_8Hz,
        CTCSS_TONE_118_8Hz,
        CTCSS_TONE_123Hz,
        CTCSS_TONE_127_3Hz,
        CTCSS_TONE_131_8Hz,
        CTCSS_TONE_136_5Hz,
        CTCSS_TONE_141_3Hz,
        CTCSS_TONE_146_2Hz,
        CTCSS_TONE_150Hz,
        CTCSS_TONE_151_4Hz,
        CTCSS_TONE_156_7Hz,
        CTCSS_TONE_159_8Hz,
        CTCSS_TONE_162_2Hz,
        CTCSS_TONE_165_5Hz,
        CTCSS_TONE_167_9Hz,
        CTCSS_TONE_171_3Hz,
        CTCSS_TONE_173_8Hz,
        CTCSS_TONE_177_3Hz,
        CTCSS_TONE_179_9Hz,
        CTCSS_TONE_183_5Hz,
        CTCSS_TONE_186_2Hz,
        CTCSS_TONE_189_9Hz,
        CTCSS_TONE_192_8Hz,
        CTCSS_TONE_196_6Hz,
        CTCSS_TONE_199_5Hz,
        CTCSS_TONE_203_5Hz,
        CTCSS_TONE_206_5Hz,
        CTCSS_TONE_210_7Hz,
        CTCSS_TONE_218_1Hz,
        CTCSS_TONE_225_7Hz,
        CTCSS_TONE_229_1Hz,
        CTCSS_TONE_233_6Hz,
        CTCSS_TONE_241_8Hz,
        CTCSS_TONE_250_3Hz,
        CTCSS_TONE_254_1Hz,
        _CTCSS_TONE_COUNT
    };

    const float CTCSS_TONES[_CTCSS_TONE_COUNT] = {
        67.0f,
        69.3f,
        71.9f,
        74.4f,
        77.0f,
        79.7f,
        82.5f,
        85.4f,
        88.5f,
        91.5f,
        94.8f,
        97.4f,
        100.0f,
        103.5f,
        107.2f,
        110.9f,
        114.8f,
        118.8f,
        123.0f,
        127.3f,
        131.8f,
        136.5f,
        141.3f,
        146.2f,
        150.0f,
        151.4f,
        156.7f,
        159.8f,
        162.2f,
        165.5f,
        167.9f,
        171.3f,
        173.8f,
        177.3f,
        179.9f,
        183.5f,
        186.2f,
        189.9f,
        192.8f,
        196.6f,
        199.5f,
        203.5f,
        206.5f,
        210.7f,
        218.1f,
        225.7f,
        229.1f,
        233.6f,
        241.8f,
        250.3f,
        254.1f
    };

    class CTCSSSquelch : public Processor<stereo_t, stereo_t> {
        using base_type = Processor<stereo_t, stereo_t>;
    public:
        CTCSSSquelch() {}

        CTCSSSquelch(stream<stereo_t>* in, double samplerate) { init(in, samplerate); }

        ~CTCSSSquelch() {
            // If not initialized, do nothing
            if (!base_type::_block_init) { return; }

            // Stop the DSP thread
            base_type::stop();
        }

        void init(stream<stereo_t>* in, double samplerate) {
            // Save settings
            _samplerate = samplerate;

            // Create dummy taps just for initialization
            float dummy[1] = { 1.0f };
            auto dummyTaps = dsp::taps::fromArray(1, dummy);

            // Initialize the DDC and FM demod
            ddc.init(NULL, samplerate, CTCSS_DECODE_SAMPLERATE, CTCSS_DECODE_BANDWIDTH, CTCSS_DECODE_OFFSET);
            fm.init(NULL, 1.0, CTCSS_DECODE_SAMPLERATE);

            // Initilize the base block class
            base_type::init(in);
        }

        void setSamplerate(double samplerate) {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            base_type::tempStop();
            _samplerate = samplerate;
            ddc.setInSamplerate(samplerate);
            base_type::tempStart();
        }

        void setRequiredTone(CTCSSTone tone) {
            assert(base_type::_block_init);
            requiredTone = tone;
        }

        CTCSSTone getCurrentTone() {
            assert(base_type::_block_init);
            return currentTone;
        }

        inline int process(int count, const stereo_t* in, stereo_t* out) {
            // Shift and resample to the correct samplerate
            int ddcOutCount = ddc.process(count, (complex_t*)in, ddc.out.writeBuf);
            
            // FM Demod the CTCSS tone
            fm.process(ddcOutCount, ddc.out.writeBuf, fm.out.writeBuf);

            // Get the required tone
            const CTCSSTone rtone = requiredTone;

            // Detect the tone frequency
            for (int i = 0; i < ddcOutCount; i++) {
                // Compute the running mean
                const float val = fm.out.writeBuf[i];
                mean = 0.95f*mean + 0.05f*val;

                // Compute the running variance
                const float err = val - mean;
                var = 0.95f*var + 0.05f*err*err;

                // Run a schmitt trigger on the variance
                bool nvarOk = varOk ? (var < 1100.0f) : (var < 1000.0f);

                // Check if the tone has to be rematched
                if (nvarOk && (!varOk || mean < minFreq || mean > maxFreq)) {
                    // Compute the absolute frequency
                    float freq = mean + CTCSS_DECODE_OFFSET;

                    // Check it against the known tones
                    if (freq < CTCSS_TONES[0] - 2.5) {
                        currentTone = CTCSS_TONE_NONE;
                    }
                    else if (freq > CTCSS_TONES[_CTCSS_TONE_COUNT-1] + 2.5) {
                        currentTone = CTCSS_TONE_NONE;
                    }
                    else if (freq < CTCSS_TONES[0]) {
                        currentTone = (CTCSSTone)0;
                    }
                    else if (freq > CTCSS_TONES[_CTCSS_TONE_COUNT-1]) {
                        currentTone = (CTCSSTone)(_CTCSS_TONE_COUNT-1);
                    }
                    else {
                        int a = 0;
                        int b = _CTCSS_TONE_COUNT-1;
                        while (b - a > 1) {
                            int c = (a + b) >> 1;
                            ((CTCSS_TONES[c] < freq) ? a : b) = c;
                        }
                        currentTone = (CTCSSTone)((freq - CTCSS_TONES[a] < CTCSS_TONES[b] - freq) ? a : b);
                    }

                    // Update the mute status
                    mute = !(currentTone == rtone || (currentTone != CTCSS_TONE_NONE && rtone == CTCSS_TONE_ANY));

                    // Unmuted the audio if needed
                    // TODO

                    // Recompute min and max freq if a valid tone is detected
                    if (currentTone != CTCSS_TONE_NONE) {
                        float c = CTCSS_TONES[currentTone];
                        float l = (currentTone > CTCSS_TONE_67Hz) ? CTCSS_TONES[currentTone - 1] : c - 2.5f;
                        float r = (currentTone < CTCSS_TONE_254_1Hz) ? CTCSS_TONES[currentTone + 1] : c + 2.5f;
                        minFreq = (l+c) / 2.0f;
                        maxFreq = (r+c) / 2.0f;
                    }
                }

                // Check for a rising edge on the variance
                if (!nvarOk && varOk) {
                    // Mute the audio
                    // TODO
                    mute = true;
                    currentTone = CTCSS_TONE_NONE;
                }

                // Save the new variance state
                varOk = nvarOk;
            }

            // DEBUG ONLY
            if ((rtone != CTCSS_TONE_NONE) && mute) {
                memset(out, 0, count * sizeof(stereo_t));
            }
            else {
                memcpy(out, in, count * sizeof(stereo_t));
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
        double _samplerate;
        std::atomic<CTCSSTone> requiredTone = CTCSS_TONE_ANY;

        float mean = 0.0f;
        float var = 0.0f;
        bool varOk = false;
        float minFreq = 0.0f;
        float maxFreq = 0.0f;
        bool mute = true;
        std::atomic<CTCSSTone> currentTone = CTCSS_TONE_NONE;
        
        channel::RxVFO ddc;
        demod::Quadrature fm;
    };
}