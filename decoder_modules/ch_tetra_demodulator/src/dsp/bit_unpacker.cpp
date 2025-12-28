#include "bit_unpacker.h" 

namespace dsp {
    int BitUnpacker::process(int count, const uint8_t* in, uint8_t* out) {
        for(int i = 0; i < count; i++) {
            out[(i * 2) + 1] = in[i] & 0b01;
            out[(i * 2)] = (in[i] & 0b10) >> 1;
        }
        return count*2;
    }
}
