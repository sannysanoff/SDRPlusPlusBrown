#pragma once
#include <dsp/processor.h>

#include <fstream>
#include <iomanip>
#include <sstream>

#include <dsp/loop/phase_control_loop.h>
#include <dsp/taps/windowed_sinc.h>
#include <dsp/multirate/polyphase_bank.h>
#include <dsp/math/step.h>
#include <math.h>

namespace dsp {
    //Unpack 2bits/byte to 1, because tetra-rx wants it
    class BitUnpacker : public Processor<uint8_t, uint8_t> {
        using base_type = Processor<uint8_t, uint8_t>;
    public:
        BitUnpacker() {}

        BitUnpacker(stream<uint8_t>* in) { init(in); }

        void init(stream<uint8_t>* in) {
            base_type::init(in);
            base_type::out.setBufferSize(STREAM_BUFFER_SIZE * 2);
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

        int process(int count, const uint8_t* in, uint8_t* out);
    };
}
