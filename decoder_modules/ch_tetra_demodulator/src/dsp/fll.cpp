#include "fll.h"

namespace dsp {
    namespace loop {
            FLL::~FLL() {
                taps::free(lbandedgerrcTaps);
                taps::free(hbandedgerrcTaps);
            }

            void FLL::init(stream<complex_t>* in, double bandwidth, int sym_rate, int samp_rate, int filt_size, float filt_a, double initFreq, double minFreq, double maxFreq) {
                _initFreq = initFreq;
                _symbolrate = sym_rate;
                _samplerate = samp_rate;
                _filt_size = filt_size;
                _filt_a = filt_a;

                //Create band-edge and normal filters
                createBandedgeFilters();
                lbandedgerrc.init(NULL, lbandedgerrcTaps);
                hbandedgerrc.init(NULL, hbandedgerrcTaps);

                // Init phase control loop
                float alpha, beta;
                PhaseControlLoop<float>::criticallyDamped(bandwidth, alpha, beta);
                alpha = 0; //we don't need to track phase in FLL
                pcl.init(alpha, beta, 0, -FL_M_PI, FL_M_PI, initFreq, minFreq, maxFreq);
                base_type::init(in);
            }

            void FLL::setSymbolrate(double symbolrate) {
                assert(base_type::_block_init);
                std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
                base_type::tempStop();
                _symbolrate = symbolrate;
                if(_samplerate/_symbolrate > 0.0f) {
                    taps::free(lbandedgerrcTaps);
                    taps::free(hbandedgerrcTaps);
                    createBandedgeFilters();
                    lbandedgerrc.setTaps(lbandedgerrcTaps);
                    hbandedgerrc.setTaps(hbandedgerrcTaps);
                }
                base_type::tempStart();
            }

            void FLL::setSamplerate(double samplerate) {
                assert(base_type::_block_init);
                std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
                base_type::tempStop();
                _samplerate = samplerate;
                if(_samplerate/_symbolrate > 0.0f) {
                    taps::free(lbandedgerrcTaps);
                    taps::free(hbandedgerrcTaps);
                    createBandedgeFilters();
                    lbandedgerrc.setTaps(lbandedgerrcTaps);
                    hbandedgerrc.setTaps(hbandedgerrcTaps);
                }
                base_type::tempStart();
            }

            //STOLEN FROM GNURADIO!
            void FLL::createBandedgeFilters() {
                float sps = _samplerate / _symbolrate;
                // printf("fll new sps: (%f/%f) %f\n", _samplerate, _symbolrate, sps);
                const int M = (_filt_size / sps);
                float power = 0;

                // Create the baseband filter by adding two sincs together
                std::vector<float> bb_taps;
                for (int i = 0; i < _filt_size; i++) {
                    float k = -M + i * 2.0f / sps;
                    float tap = math::sinc(_filt_a * k - 0.5f) + math::sinc(_filt_a * k + 0.5f);
                    power += tap;

                    bb_taps.push_back(tap);
                }

                lbandedgerrcTaps = taps::alloc<complex_t>(_filt_size);
                hbandedgerrcTaps = taps::alloc<complex_t>(_filt_size);

                // Create the band edge filters by spinning the baseband
                // filter up and down to the right places in frequency.
                // Also, normalize the power in the filters
                int N = (bb_taps.size() - 1.0f) / 2.0f;
                for (int i = 0; i < _filt_size; i++) {
                    float tap = bb_taps[i] / power;

                    float k = (-N + (int)i) / (2.0f * sps);

                    complex_t t1 = math::phasor(-2.0f * FL_M_PI * (1.0f + _filt_a) * k) * tap;
                    complex_t t2 = math::phasor( 2.0f * FL_M_PI * (1.0f + _filt_a) * k) * tap;

                    lbandedgerrcTaps.taps[_filt_size - i - 1] = t1;
                    hbandedgerrcTaps.taps[_filt_size - i - 1] = t2;
                }
            }

            void FLL::setBandwidth(double bandwidth) {
                assert(base_type::_block_init);
                std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
                base_type::tempStop();
                float alpha, beta;
                PhaseControlLoop<float>::criticallyDamped(bandwidth, alpha, beta);
                alpha = 0; //we don't need to track phase in FLL
                pcl.setCoefficients(alpha, beta);
                base_type::tempStart();
            }

            void FLL::setInitialFreq(double initFreq) {
                assert(base_type::_block_init);
                std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
                _initFreq = initFreq;
            }

            void FLL::setFrequencyLimits(double minFreq, double maxFreq) {
                assert(base_type::_block_init);
                std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
                pcl.setFreqLimits(minFreq, maxFreq);
            }

            void FLL::reset() {
                assert(base_type::_block_init);
                std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
                base_type::tempStop();
                pcl.phase = 0;
                pcl.freq = _initFreq;
                base_type::tempStart();
            }

            void FLL::force_set_freq(float newf) {
                assert(base_type::_block_init);
                std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
                pcl.freq = newf;
            }

            int FLL::process(int count, complex_t* in, complex_t* out) {
                for (int i = 0; i < count; i++) {
                    complex_t shift = math::phasor(-pcl.phase);
                    complex_t x = in[i] * shift;
                    complex_t lbe_out;
                    complex_t hbe_out;
                    lbandedgerrc.process(1, &(x), &lbe_out);
                    hbandedgerrc.process(1, &(x), &hbe_out);
                    float freqError = hbe_out.fastAmplitude() - lbe_out.fastAmplitude();
                    // dbg_last_err = pcl.freq;
                    pcl.advance(freqError);
                    out[i] = x;
                }
                return count;
            }
    }
}
