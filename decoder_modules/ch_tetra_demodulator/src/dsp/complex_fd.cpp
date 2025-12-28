#include "complex_fd.h"

namespace dsp {
    namespace clock_recovery {
        COMPLEX_FD::~COMPLEX_FD() {
            if (!base_type::_block_init) { return; }
            base_type::stop();
            dsp::multirate::freePolyphaseBank(interpBank);
            buffer::free(buffer);
        }

        void COMPLEX_FD::init(stream<complex_t>* in, double omega, double omegaGain, double muGain, double omegaRelLimit, int outSps, int interpPhaseCount, int interpTapCount) {
            _omega = omega;
            _outSps = outSps;
            _spsctr = 0;
            _omegaGain = omegaGain;
            _muGain = muGain;
            _omegaRelLimit = omegaRelLimit;
            _interpPhaseCount = interpPhaseCount;
            _interpTapCount = interpTapCount;

            pcl.init(_muGain, _omegaGain, 0.0, 0.0, 1.0, _omega, _omega * (1.0 - omegaRelLimit), _omega * (1.0 + omegaRelLimit));
            generateInterpTaps();
            buffer = buffer::alloc<complex_t>(STREAM_BUFFER_SIZE + _interpTapCount);
            bufStart = &buffer[_interpTapCount - 1];

            base_type::init(in);
        }

        void COMPLEX_FD::setOmega(double omega) {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            base_type::tempStop();
            _omega = omega;
            offset = 0;
            pcl.phase = 0.0f;
            _spsctr = 0;
            pcl.freq = _omega;
            pcl.setFreqLimits(_omega * (1.0 - _omegaRelLimit), _omega * (1.0 + _omegaRelLimit));
            base_type::tempStart();
        }

        void COMPLEX_FD::setOmegaGain(double omegaGain) {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            _omegaGain = omegaGain;
            pcl.setCoefficients(_muGain, _omegaGain);
        }

        void COMPLEX_FD::setMuGain(double muGain) {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            _muGain = muGain;
            pcl.setCoefficients(_muGain, _omegaGain);
        }

        void COMPLEX_FD::setOmegaRelLimit(double omegaRelLimit) {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            _omegaRelLimit = omegaRelLimit;
            pcl.setFreqLimits(_omega * (1.0 - _omegaRelLimit), _omega * (1.0 + _omegaRelLimit));
        }

        void COMPLEX_FD::setInterpParams(int interpPhaseCount, int interpTapCount) {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            base_type::tempStop();
            _interpPhaseCount = interpPhaseCount;
            _interpTapCount = interpTapCount;
            dsp::multirate::freePolyphaseBank(interpBank);
            buffer::free(buffer);
            generateInterpTaps();
            buffer = buffer::alloc<complex_t>(STREAM_BUFFER_SIZE + _interpTapCount);
            bufStart = &buffer[_interpTapCount - 1];
            base_type::tempStart();
        }

        void COMPLEX_FD::reset() {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            base_type::tempStop();
            offset = 0;
            pcl.phase = 0.0f;
            _spsctr = 0;
            pcl.freq = _omega;
            base_type::tempStart();
        }

        int COMPLEX_FD::process(int count, const complex_t* in, complex_t* out) {
            // Copy data to work buffer
            memcpy(bufStart, in, count * sizeof(complex_t));

            // Process all samples
            int outCount = 0;
            while (offset < count) {
                float error;
                complex_t outVal;
                complex_t dfdt;

                // Calculate new output value
                int phase = std::clamp<int>(floorf(pcl.phase * (float)_interpPhaseCount), 0, _interpPhaseCount - 1);
                volk_32fc_32f_dot_prod_32fc((lv_32fc_t*)&outVal, (lv_32fc_t*)&buffer[offset], interpBank.phases[phase], _interpTapCount);
                out[outCount++] = outVal;

                if(_spsctr == 0) {
                    // Calculate derivative of the signal
                    if (phase == 0) {
                        complex_t fT1;
                        volk_32fc_32f_dot_prod_32fc((lv_32fc_t*)&fT1, (lv_32fc_t*)&buffer[offset], interpBank.phases[phase+1], _interpTapCount);
                        dfdt = fT1 - outVal;
                    }
                    else if (phase == _interpPhaseCount - 1) {
                        complex_t fT_1;
                        volk_32fc_32f_dot_prod_32fc((lv_32fc_t*)&fT_1, (lv_32fc_t*)&buffer[offset], interpBank.phases[phase-1], _interpTapCount);
                        dfdt = outVal - fT_1;
                    }
                    else {
                        complex_t fT_1;
                        complex_t fT1;
                        volk_32fc_32f_dot_prod_32fc((lv_32fc_t*)&fT1, (lv_32fc_t*)&buffer[offset], interpBank.phases[phase+1], _interpTapCount);
                        volk_32fc_32f_dot_prod_32fc((lv_32fc_t*)&fT_1, (lv_32fc_t*)&buffer[offset], interpBank.phases[phase-1], _interpTapCount);
                        dfdt = (fT1 - fT_1) * 0.5f;
                    }
                    // Calculate error
                    // error = ((outVal.re * dfdt.re) + (outVal.im * dfdt.im));
                    error = (((outVal.re > 0 ? 1.0f : -1.0f) * dfdt.re) + ((outVal.im > 0 ? 1.0f : -1.0f) * dfdt.im));
                } else {
                    error = 0;
                }
                _spsctr++;
                if(_spsctr >= _outSps) {
                    _spsctr = 0;
                }

                // Clamp symbol phase error
                if (error > 1.0f) { error = 1.0f; }
                if (error < -1.0f) { error = -1.0f; }

                // Advance symbol offset and phase
                pcl.advance(error);
                float delta = floorf(pcl.phase);
                offset += delta;
                pcl.phase -= delta;
            }
            offset -= count;

            // Update delay buffer
            memmove(buffer, &buffer[count], (_interpTapCount - 1) * sizeof(complex_t));

            return outCount;
        }

        void COMPLEX_FD::generateInterpTaps() {
            double bw = 0.5 / (double)_interpPhaseCount;
            dsp::tap<float> lp = dsp::taps::windowedSinc<float>(_interpPhaseCount * _interpTapCount, dsp::math::hzToRads(bw, 1.0), dsp::window::nuttall, _interpPhaseCount);
            interpBank = dsp::multirate::buildPolyphaseBank<float>(_interpPhaseCount, lp);
            taps::free(lp);
        }
    }
}
