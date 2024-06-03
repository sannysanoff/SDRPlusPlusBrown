#pragma once
#include <dsp/hier_block.h>
#include <dsp/multirate/rational_resampler.h>
#include <dsp/correction/dc_blocker.h>
#include <dsp/routing/splitter.h>
#include <dsp/convert/stereo_to_mono.h>
#include <dsp/buffer/reshaper.h>
#include <dsp/convert/real_to_complex.h>
#include <dsp/convert/mono_to_stereo.h>
#include <dsp/convert/complex_to_real.h>
#include <dsp/taps/high_pass.h>
#include <dsp/block.h>
#include <fftw3.h>
#include "common.h"

#define CTCSS_SENSITIVITY 17

namespace dsp {

    //Input sample rate = 600 S/s
    class CTCSSFreqMeasure : public Sink<float> {
        using base_type =  Sink<float>;
    public:
        CTCSSFreqMeasure() {}

        CTCSSFreqMeasure(stream<float>* in, int nsamp, float squelchFreq) { init(in, nsamp, squelchFreq); }

        ~CTCSSFreqMeasure() {
            if (!base_type::_block_init) { return; }
            base_type::stop();
            delete[] in_data;
        }

        void init(stream<float>* in, int nsamp, float squelchFreq) {
            _in = in;
            nSamp = nsamp * oversamp;
            nr = nSamp / 2 + 1;
            in_data = new float[nSamp];
            for(int i = nsamp; i < nSamp; i++) {
                in_data[i] = 0.0;
            }
            fftw_out = (fftwf_complex*) fftwf_malloc(sizeof(fftwf_complex) * nr);
            p = fftwf_plan_dft_r2c_1d(nSamp, in_data, fftw_out, FFTW_ESTIMATE);
            squelchFrequency = squelchFreq;
            base_type::init(in);
        }

        void setInput(stream<float>* in) {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            base_type::tempStop();
            base_type::unregisterInput(_in);
            _in = in;
            base_type::registerInput(_in);
            base_type::tempStart();
        }

        void setSquelchFreq(float squelchFreq) {
            assert(base_type::_block_init);
            squelchFrequency = squelchFreq;
        }

        inline int process(int count, const float* in) {
            float stddev,mean,nf;
            float fft_pwr_points[nr];
            uint32_t maxFreq = 1;
            float maxPwr;
            int beginIndex = 33 * nSamp / 600; //begin from 33 Hz
            int stopIndex = 255 * nSamp / 600; //end at 255 Hz
            if(count != nSamp / oversamp) { return 0; }

            volk_32f_stddev_and_mean_32f_x2(&stddev, &mean, in, count);

            if(mean == 0.0f) {
                mean = 1.0f;
            }

            volk_32f_s32f_multiply_32f(in_data, in, (1.0f/mean), count);
            volk_32f_stddev_and_mean_32f_x2(&stddev, &mean, in_data, count);
            fftwf_execute(p);
            volk_32fc_s32f_power_spectrum_32f(fft_pwr_points, (lv_32fc_t*) fftw_out, nr, nr);
            volk_32f_s32f_calc_spectral_noise_floor_32f(&nf, (fft_pwr_points+beginIndex), 20, (stopIndex-beginIndex));
            volk_32f_index_max_32u(&maxFreq, (fft_pwr_points+beginIndex), (stopIndex-beginIndex));
            maxFreq+=beginIndex;
            maxPwr = fft_pwr_points[maxFreq];
            float realFreq = (600.0f * (float)maxFreq) / (float)(nSamp);
            if(maxFreq != 1 && (maxPwr - nf) >= CTCSS_SENSITIVITY ) { //PWR - noise floor >= CTCSS_SENSITIVITY dB
                    freq = realFreq;
                    if(fabs(freq - squelchFrequency) < 0.9f) {
                        frequencyMatch = true;
                    } else {
                        frequencyMatch = false;
                    }
                    isActive = true;
                    realFreq = -1;
                if(realFreq != -1) {
                    frequencyMatch = false;
                    isActive = false;
                }
            } else {
                frequencyMatch = false;
                isActive = false;
            }
            return 0;
        }

        int run() {
            int count = base_type::_in->read();
            if (count < 0) { return -1; }

            process(count, base_type::_in->readBuf);
            //no output
            base_type::_in->flush();
            return 0;
        }

        float freq = 0.0f;
        bool isActive = false;
        bool frequencyMatch = false;

    private:
        int nSamp;
        int nr;
        int oversamp = 50;
        float squelchFrequency = 88.5f;
        float* in_data = NULL;
        fftwf_plan p;
        fftwf_complex *fftw_out;

    };
    
    class CTCSSSquelch : public Processor<stereo_t, stereo_t> {
        using base_type = Processor<stereo_t, stereo_t>;
    public:
        CTCSSSquelch() {}

        CTCSSSquelch(stream<stereo_t>* in, float inputSr) { init(in, inputSr); }

        ~CTCSSSquelch() {
            if (!base_type::_block_init) { return; }
            base_type::stop();
            // taps::free(outFilterTaps);
        }

        void init(stream<stereo_t>* in, float inputSr) {
            sTM.init(&measureStream);
            resamp.init(&sTM.out, inputSr, 600.0f);
            dcblock.init(&resamp.out, 0.01);
            reshape.init(&dcblock.out, 120, 0); //0.2sec
            freqMeasure.init(&reshape.out, 120, squelchFreq);

            // taps::free(outFilterTaps);
            // outFilterTaps = taps::highPass(300.0, 100.0, inputSr);
            // outFilter.init(NULL, outFilterTaps);

            //DIRTY HACK!!! I just don't know how to make it in better way

            base_type::init(in);
        }

        void start() {
            sTM.start();
            resamp.start();
            dcblock.start();
            reshape.start();
            freqMeasure.start();
            base_type::start();
        }

        void stop() {
            sTM.stop();
            resamp.stop();
            dcblock.stop();
            reshape.stop();
            freqMeasure.stop();
            base_type::stop();
        }

        void setInputSr(float inputSr) {
            assert(_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            base_type::tempStop();
            resamp.setInSamplerate(inputSr);
            // taps::free(outFilterTaps);
            // outFilterTaps = taps::highPass(300.0, 100.0, inputSr);
            // outFilter.setTaps(outFilterTaps);
            base_type::tempStart();
        }

        void setSquelchFrequency(float squelchFrequency) {
            assert(base_type::_block_init);
            squelchFreq = squelchFrequency;
            freqMeasure.setSquelchFreq(squelchFreq);
        }

        void setSquelchEnabled(bool newSquelchEnabled) {
            assert(base_type::_block_init);
            //Bypass filter when squelch is disabled
            squelchEnabled = newSquelchEnabled;
        }

        float getCurrentCTCSSFreq() {
            assert(base_type::_block_init);
            return freqMeasure.freq;
        }

        bool getCurrentCTCSSActive() {
            assert(base_type::_block_init);
            return freqMeasure.isActive;
        }

        inline int process(int count, const stereo_t* in, stereo_t* out) {
            memcpy(measureStream.writeBuf, in, count*sizeof(stereo_t));
            measureStream.swap(count);
            if (!squelchEnabled) {
                memcpy(out, in, count * sizeof(stereo_t));
            } else if(freqMeasure.frequencyMatch) {
                // outFilter.process(count, in, out);
                memcpy(out, in, count * sizeof(stereo_t));
            } else {
                memset(out, 0, count * sizeof(stereo_t));
            }
            return count;
        }

        int run() {
            int count = base_type::_in->read();
            if (count < 0) { return -1; }

            int outCount = process(count, base_type::_in->readBuf, base_type::out.writeBuf);
            // Swap if some data was generated
            base_type::_in->flush();
            if (outCount) {
                if (!base_type::out.swap(outCount)) { return -1; }
            }
            return outCount;
        }

    private:
        //Measure chain
        stream<stereo_t> measureStream;
        convert::StereoToMono sTM;
        multirate::RationalResampler<float> resamp;
        correction::DCBlocker<float> dcblock;
        buffer::Reshaper<float> reshape;
        CTCSSFreqMeasure freqMeasure;

        //Squelch chain
        // tap<float> outFilterTaps;
        // filter::FIR<stereo_t, float> outFilter;

        bool squelchEnabled = false;
        float squelchFreq = 88.5;
    };

}
