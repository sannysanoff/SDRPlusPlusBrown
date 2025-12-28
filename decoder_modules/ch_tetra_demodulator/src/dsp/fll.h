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
    namespace loop {
        class FLL : public Processor<complex_t, complex_t>  {
            using base_type = Processor<complex_t, complex_t>;
        public:
            FLL() {}

            FLL(stream<complex_t>* in, double bandwidth, int sym_rate, int samp_rate, int filt_size, float filt_a, double initFreq = 0.0, double minFreq = -FL_M_PI, double maxFreq = FL_M_PI) { init(in, bandwidth, sym_rate, samp_rate, filt_size, filt_a, initFreq, minFreq, maxFreq); }

            ~FLL();

            void init(stream<complex_t>* in, double bandwidth, int sym_rate, int samp_rate, int filt_size, float filt_a, double initFreq = 0.0, double minFreq = -FL_M_PI, double maxFreq = FL_M_PI);
            void setSymbolrate(double symbolrate);
            void setSamplerate(double samplerate);
            void createBandedgeFilters();
            void setBandwidth(double bandwidth);
            void setInitialFreq(double initFreq);
            void setFrequencyLimits(double minFreq, double maxFreq);
            void reset();
            void force_set_freq(float newf);

            int process(int count, complex_t* in, complex_t* out);

            int run() {
                int count = base_type::_in->read();
                if (count < 0) { return -1; }

                process(count, base_type::_in->readBuf, base_type::out.writeBuf);

                base_type::_in->flush();
                if (!base_type::out.swap(count)) { return -1; }
                return count;
            }

            // float dbg_last_err = 0;
        protected:
            PhaseControlLoop<float> pcl;
            tap<complex_t> lbandedgerrcTaps;
            filter::FIR<complex_t, complex_t> lbandedgerrc;
            tap<complex_t> hbandedgerrcTaps;
            filter::FIR<complex_t, complex_t> hbandedgerrc;
            float _initFreq;
            double _symbolrate;
            double _samplerate;
            int _filt_size;
            float _filt_a;
            complex_t lastVCO = { 1.0f, 0.0f };
        };
    }
}
