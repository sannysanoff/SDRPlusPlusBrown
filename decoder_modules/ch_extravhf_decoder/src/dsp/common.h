#pragma once
#include <dsp/processor.h>
#include <fftw3.h>

namespace dsp {

    class Int16ToFloat : public Processor<short, float> {
        using base_type = Processor<short, float>;
    public:
        Int16ToFloat() {}

        Int16ToFloat(stream<int16_t>* in) { base_type::init(in); }

        int run() {
            int count = base_type::_in->read();
            if (count < 0) { return -1; }

            volk_16i_s32f_convert_32f(base_type::out.writeBuf, base_type::_in->readBuf, 32768.0f, count);

            base_type::_in->flush();
            if (!base_type::out.swap(count)) { return -1; }
            return count;
        }
    };

    class FloatToInt16 : public Processor<float, short> {
        using base_type = Processor<float, short>;
    public:
        FloatToInt16() {}

        FloatToInt16(stream<float>* in) { base_type::init(in); }

        int run() {
            int count = base_type::_in->read();
            if (count < 0) { return -1; }

            volk_32f_s32f_convert_16i(base_type::out.writeBuf, base_type::_in->readBuf, 32768.0f, count);

            base_type::_in->flush();
            if (!base_type::out.swap(count)) { return -1; }
            return count;
        }
    };

}
