#pragma once
#include "../processor.h"

// TODO: Rewrite better!!!!!
namespace dsp::noise_reduction {
    template <typename T>
    class PowerSquelch : public Processor<T, T> {
        using base_type = Processor<T, T>;
    public:
        PowerSquelch() {}

        PowerSquelch(stream<T>* in, double level) {}

        ~PowerSquelch() {
            if (!base_type::_block_init) { return; }
            base_type::stop();
            buffer::free(normBuffer);
        }

        void init(stream<T>* in, double level) {
            _level = level;

            normBuffer = buffer::alloc<float>(STREAM_BUFFER_SIZE);

            base_type::init(in);
        }

        void setLevel(double level) {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            _level = level;
        }

        inline int process(int count, const T* in, T* out) {
            float sum = 0.0f;
            
            if constexpr (std::is_same_v<T, complex_t>) {
                // For complex_t (IQ data): Use VOLK for magnitude
                volk_32fc_magnitude_32f(normBuffer, (lv_32fc_t*)in, count);
                volk_32f_accumulator_s32f(&sum, normBuffer, count);
                sum /= (float)count;
            }
            else if constexpr (std::is_same_v<T, stereo_t>) {
                // For stereo_t (audio): Calculate power manually
                for (int i = 0; i < count; i++) {
                    sum += in[i].l * in[i].l + in[i].r * in[i].r;
                }
                sum /= (float)(count * 2);
            }
            else {
                // Generic case: assume scalar samples
                for (int i = 0; i < count; i++) {
                    sum += in[i] * in[i];
                }
                sum /= (float)count;
            }

            if (10.0f * log10f(sum + 1e-30f) >= _level) {
                memcpy(out, in, count * sizeof(T));
            }
            else {
                memset(out, 0, count * sizeof(T));
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
        float* normBuffer = nullptr;
        float _level = -50.0f;
                
    };
}
