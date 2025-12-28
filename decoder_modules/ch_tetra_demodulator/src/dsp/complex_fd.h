#pragma once
#include <dsp/processor.h>

#include <fstream>
#include <iomanip>
#include <sstream>

#include <dsp/loop/phase_control_loop.h>
#include <dsp/taps/windowed_sinc.h>
#include <dsp/multirate/polyphase_bank.h>
#include <dsp/math/step.h>
#include <dsp/loop/costas.h>
#include <dsp/clock_recovery/mm.h>
#include <dsp/taps/root_raised_cosine.h>
#include <dsp/filter/fir.h>
#include <dsp/loop/fast_agc.h>
#include <dsp/loop/costas.h>
#include <dsp/clock_recovery/mm.h>
#include <math.h>

namespace dsp {

    namespace clock_recovery {
        class COMPLEX_FD : public Processor<complex_t, complex_t> {
            using base_type = Processor<complex_t, complex_t> ;
        public:
            COMPLEX_FD() {}

            COMPLEX_FD(stream<complex_t>* in, double omega, double omegaGain, double muGain, double omegaRelLimit, int outSps = 1, int interpPhaseCount = 128, int interpTapCount = 8) { init(in, omega, omegaGain, muGain, omegaRelLimit, outSps, interpPhaseCount, interpTapCount); }

            ~COMPLEX_FD();

            void init(stream<complex_t>* in, double omega, double omegaGain, double muGain, double omegaRelLimit, int outSps = 1, int interpPhaseCount = 128, int interpTapCount = 8);
            void setOmega(double omega);
            void setOmegaGain(double omegaGain);
            void setMuGain(double muGain);
            void setOmegaRelLimit(double omegaRelLimit);
            void setInterpParams(int interpPhaseCount, int interpTapCount);
            void reset();

            int process(int count, const complex_t* in, complex_t* out);

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

            loop::PhaseControlLoop<float, false> pcl;
        protected:
            void generateInterpTaps();

            dsp::multirate::PolyphaseBank<float> interpBank;

            double _omega;
            int _outSps;
            int _spsctr = 0;
            double _omegaGain;
            double _muGain;
            double _omegaRelLimit;
            int _interpPhaseCount;
            int _interpTapCount;

            int offset = 0;
            complex_t* buffer;
            complex_t* bufStart;
        };
    }
}
