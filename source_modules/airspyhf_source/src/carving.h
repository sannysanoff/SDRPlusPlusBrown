#pragma once
#include <dsp/block.h>
#include <volk/volk.h>
#include <spdlog/spdlog.h>
#include <dsp/types.h>
#include <dsp/processor.h>
#include <arrays.h>

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

        void setSampleRates(float inSampleRate, float outSampleRate) {

            std::lock_guard g(mtx);
            _inSampleRate = (int)(inSampleRate / 100);
            _outSampleRate = (int)(outSampleRate / 100);
            forward = allocateFFTWPlan(false, _inSampleRate);
            backward = allocateFFTWPlan(true, _outSampleRate);
            inputBuffer.clear();
            _inArr = dsp::npzeros_c(_inSampleRate);
        }

        std::vector<dsp::complex_t> inputBuffer;

        int run() {
            int count = this->_in->read();
            if (count < 0) { return -1; }
            std::lock_guard g(mtx);

            // TODO: Do float xlation
            if constexpr (std::is_same_v<T, float>) {
                spdlog::error("NOT IMPLEMENTED FOR FLOAT");
            }
            int nwritten = 0;
            if constexpr (std::is_same_v<T, complex_t>) {
                auto currentInputSize = inputBuffer.size();
                inputBuffer.resize(currentInputSize + count);
                memcpy(inputBuffer.data() + currentInputSize, this->_in->readBuf, count * sizeof(dsp::complex_t));
                auto diff = (_inSampleRate - _outSampleRate);
                int cutPlace = _inSampleRate / 2 - diff / 2;
                while (inputBuffer.size() >= _inSampleRate) {
                    std::copy(inputBuffer.begin(), inputBuffer.begin() + _inSampleRate, _inArr->begin());
                    ComplexArray buckets = npfftfft(_inArr, forward);
                    inputBuffer.erase(inputBuffer.begin(), inputBuffer.begin() + _inSampleRate);
                    //                dumpArr_(buckets);
                    buckets->erase(buckets->begin() + cutPlace, buckets->begin() + cutPlace + diff);
                    //                dumpArr_(buckets);
                    auto stream = npfftfft(buckets, backward);
                    memcpy(this->out.writeBuf + nwritten, stream->data(), stream->size() * sizeof(dsp::complex_t));
                    nwritten += stream->size();
                }
            }

            this->_in->flush();
            if (!this->out.swap(nwritten)) { return -1; }
            return count;
        }


    private:
        int _inSampleRate;
        int _outSampleRate;
        Arg<fftwPlan> forward;
        Arg<fftwPlan> backward;
        ComplexArray _inArr;
        ComplexArray _outArr;
    };

}
