#include "dqpsk_sym_extr.h"

namespace dsp {
    int DQPSKSymbolExtractor::process(int count, const complex_t* in, uint8_t* out) {
        for(int i = 0; i < count; i++) {
            complex_t sym_c = in[i];
            bool a = sym_c.im<0;
            bool b = sym_c.re<0;
            complex_t ideal_sym = {b?-0.7071f : 0.7071f, a?-0.7071f : 0.7071f};
            // float dist = fabsf(sqrtf(((ideal_sym.re-sym_c.re)*(ideal_sym.re-sym_c.re))+((ideal_sym.im-sym_c.im)*(ideal_sym.im-sym_c.im))));
            float dist = std::abs(ideal_sym.phase() - sym_c.phase());
            errorbuf[errorptr] = dist;
            errorptr++;
            if(errorptr >= SYNC_DETECT_BUF) {
                errorptr = 0;
            }
            errordisplayptr++;
            if(errordisplayptr >= SYNC_DETECT_DISPLAY) {
                float xerr = 0;
                for(int i = 0; i < SYNC_DETECT_BUF; i++) {
                    xerr+=errorbuf[i];
                }
                xerr /= (float)SYNC_DETECT_BUF;
                standarderr = xerr;
                if(xerr >= 0.35f) {
                    sync = false;
                } else {
                    sync = true;
                }
                errordisplayptr = 0;
            }
            uint8_t sym = ((a)<<1) | (a!=b); //This mapping is required to make substraction differential decoder work properly
            uint8_t phaseDiff = (sym - prev + 4) % 4;
            switch(phaseDiff) { //Remap phase diffs to actual tetra symbols(swap 0b10 and 0b11)
                case 0b00:
                    //0
                    out[i] = 0b00;
                    break;
                case 0b01:
                    //pi/2
                    out[i]=0b01;
                    break;
                case 0b10:
                    //pi
                    out[i]=0b11;
                    break;
                case 0b11:
                    //-pi/2
                    out[i]=0b10;
                    break;
            }
            prev = sym;
        }
        return count;
    }
}
