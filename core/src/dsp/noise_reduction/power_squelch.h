#pragma once
#include "../processor.h"

// TODO: Rewrite better!!!!!
namespace dsp::noise_reduction {
    class PowerSquelch : public Processor<complex_t, complex_t> {
        using base_type = Processor<complex_t, complex_t>;
    public:
        PowerSquelch() {}

        PowerSquelch(stream<complex_t>* in, double level) {}

        ~PowerSquelch() {
            if (!base_type::_block_init) { return; }
            base_type::stop();
            buffer::free(normBuffer);
        }

        void init(stream<complex_t>* in, double level) {
            _level = level;

            normBuffer = buffer::alloc<float>(STREAM_BUFFER_SIZE);

            base_type::init(in);
        }

        void setLevel(double level) {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            _level = level;
        }

        inline int process(int count, const stereo_t* in, stereo_t* out) {
            if (count == 0) {
                return 0;
            }
            float sum = 0;
            for(int i=0; i<count; i++) {
                sum += in[i].l * in[i].l;
                sum += in[i].r * in[i].r;
            }
            float power = 20.0 * log10f(sqrt(sum / (count*2)));

//            flog::info("db = {}", power);
            if (power >= _level) {
                memcpy(out, in, count * sizeof(stereo_t));
            }
            else {
                memset(out, 0, count * sizeof(stereo_t));
            }

            return count;
        }

        inline int process(int count, const complex_t* in, complex_t* out) {
            // Compute the amplitude of each sample
            volk_32fc_magnitude_32f(normBuffer, (lv_32fc_t*)in, count);
            
            // Compute the mean amplitude
            float sum = 0.0f;
            volk_32f_accumulator_s32f(&sum, normBuffer, count);
            sum /= (float)count;

            if (10.0f * log10f(sum) >= _level) {
                memcpy(out, in, count * sizeof(complex_t));
            }
            else {
                memset(out, 0, count * sizeof(complex_t));
            }

            return count;
        }

        int run() {
            int count = base_type::_in->read();
            if (count < 0) { return -1; }
            process(count, base_type::_in->readBuf, base_type::out.writeBuf);
            base_type::_in->flush();
            if (!base_type::out.swap(count)) { return -1; }
            return count;
        }

    private:
        float* normBuffer;
        float _level = -50.0f;
                
    };
}