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
        //ONLY 1 SAMPLE/SYMBOL INPUT!!
        class PI4DQPSK_COSTAS : public PLL {
            using base_type = PLL;
        public:
            int process(int count, complex_t* in, complex_t* out);

        protected:
            float errorFunction(complex_t val);
            float ph2 = 0;

        };
    }
}
