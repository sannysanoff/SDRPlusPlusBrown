#pragma once
#include <dsp/block.h>
#include <volk/volk.h>
#include <utils/flog.h>
#include <dsp/types.h>
#include <dsp/processor.h>
#include "utils/arrays.h"

namespace dsp {

    using namespace dsp::arrays;

    template <class T>
    class FrequencyCarving : public Processor<T, T> {
        using base = Processor<T, T>;

    public:
        FrequencyCarving() {}

        void init(stream<complex_t>* in, float inSampleRate, float outSampleRate) {
            base::init(in);
            setSampleRates(inSampleRate, outSampleRate);
        }

        std::mutex mtx;
        multirate::RationalResampler<complex_t> resamp;

        void setSampleRates(float inSampleRate, float outSampleRate) {
            flog::info("carving::Set sample rate: {0} {0}", inSampleRate, outSampleRate);
            std::lock_guard g(mtx);
            _inSampleRate = (int)(inSampleRate);
            _outSampleRate = (int)(outSampleRate);
            resamp.init(nullptr, inSampleRate, outSampleRate);
            inputBuffer.clear();
            _inArr = dsp::npzeros_c(_inSampleRate);
        }

        std::vector<dsp::complex_t> inputBuffer;
        ComplexArray part;



        int process(dsp::complex_t *in, int count, dsp::complex_t *out){
            auto nwritten = resamp.process(count, in, out);
            return nwritten;
        }


        int run() {
            int count = this->_in->read();
            if (count < 0) {
                return -1;
            }
            auto in = this->_in->readBuf;
            std::lock_guard g(mtx);
            flog::info("carving enter");
            // TODO: Do float xlation
            if constexpr (std::is_same_v<T, float>) {
                flog::error("NOT IMPLEMENTED FOR FLOAT");
            }
            int nwritten = 0;
            if constexpr (std::is_same_v<T, complex_t>) {
                nwritten = process(in, count, this->out.writeBuf);
            }

            this->_in->flush();
            if (nwritten > 0){
                flog::info("carving swap: {0}", nwritten);
                if (!this->out.swap(nwritten)) { return -1; }
            }
            flog::info("carving out: {0}", count);
            return count;
        }


    private:
        int _inSampleRate;
        int _outSampleRate;
        ComplexArray _inArr;
    };

}
