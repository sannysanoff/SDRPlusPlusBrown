#include "pi4dqpsk_costas.h"

namespace dsp {
    namespace loop {
        int PI4DQPSK_COSTAS::process(int count, complex_t* in, complex_t* out) {
            for (int i = 0; i < count; i++) {
                complex_t shift = math::phasor(-pcl.phase);
                complex_t x = in[i] * shift;
                //perform pi/4 shift on every symbol
                ph2 += -FL_M_PI/4.0f;
                if(ph2 >= 2*FL_M_PI) {
                    ph2-=2*FL_M_PI;
                } else if(ph2 <= -2*FL_M_PI) {
                    ph2+=2*FL_M_PI;
                }
                x = x * math::phasor(ph2);
                pcl.advance(errorFunction(x));
                out[i] = x;
            }
            return count;
        }

        float PI4DQPSK_COSTAS::errorFunction(complex_t val) {
            float err = 0;
            //default qpsk error function
            err = (math::step(val.re) * val.im) - (math::step(val.im) * val.re);
            return std::clamp<float>(err, -1.0f, 1.0f);
        }
    }
}
