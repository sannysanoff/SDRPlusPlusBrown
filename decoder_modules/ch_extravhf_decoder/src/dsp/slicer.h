#pragma once
#include <dsp/processor.h>
#include <dsp/multirate/polyphase_bank.h>
#include <dsp/loop/phase_control_loop.h>
#include <dsp/taps/windowed_sinc.h>
#include <dsp/filter/fir.h>
#include <dsp/loop/agc.h>
#include <dsp/loop/costas.h>
#include <dsp/loop/fast_agc.h>
#include <dsp/taps/root_raised_cosine.h>

namespace dsp {

    #define LVL_BUFF 1024

    class FourFSKExtractor : public Processor<float, uint8_t> {
        using base_type = Processor<float, uint8_t>;
    public:
        FourFSKExtractor() {}

        FourFSKExtractor(stream<float>* in) {init(in);}

        inline int process(int count, const float* in, uint8_t* out) {
            for(int i = 0; i < count; i++) {
                float sym_c = in[i];
                if(sym_c >= umid) {
                    out[i] = 0b01;
                } else if(sym_c >= center) {
                    out[i] = 0b00;
                } else if(sym_c >= lmid) {
                    out[i] = 0b10;
                } else {
                    out[i] = 0b11;
                }
                lbuf2[lbuf2idx] = sym_c;
                lbuf2idx = (lbuf2idx+1) % LVL_BUFF;
                float lmax = 0;
                float lmin = 0;
                for(int i = 0; i < 24; i++) {
                    if(lbuf2[i] > lmax) {
                        lmax = lbuf2[i];
                    } else if(lbuf2[i] < lmin) {
                        lmin = lbuf2[i];
                    }
                }
                //WORKS BETTER WITHOUT AFC
//                max += (lmax - max) * 0.01f;
//                min += (lmin - min) * 0.01f;
                if(max >= 1.3f) {
                    max = 1.3f;
                } else if(max <= -1.3f) {
                    max = -1.3f;
                }
                if(min >= 1.3f) {
                    min = 1.3f;
                } else if(min <= -1.3f) {
                    min = -1.3f;
                }
                center = ((max) + (min)) * 0.5f;
                umid = (((max) - center) * mid) + center;
                lmid = (((min) - center) * mid) + center;
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

        float max = 1.0f;
        float min = -1.0f;
        float center = 0.0f;
        float umid = 0.625f;
        float lmid = -0.625f;
        float mid = 0.6f;

    private:
        int lbuf2idx = 0;
        float lbuf2[LVL_BUFF];

    };
}
