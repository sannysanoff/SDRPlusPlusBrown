#pragma once
#include "../processor.h"
#include <vector>
#include <utils/arrays.h>
#include <sstream>
#include <type_traits>

namespace dsp::detector {

    template<typename T>
    class ArrayView {
    private:
        const T* ptr;
        size_t length;

    public:
        ArrayView(const T* p, size_t len) : ptr(p), length(len) {}

        // Constructor from const vector reference
        ArrayView(const std::vector<T>& vec) : ptr(vec.data()), length(vec.size()) {}

        size_t size() const { return length; }
        const T& operator[](size_t idx) const { return ptr[idx]; }
        const T* data() const { return ptr; }

        const T* begin() const { return ptr; }
        const T* end() const { return ptr + length; }
        
        // Dump the content as a C++ initializer string (only for float type)
        std::string dump() const {
            // Return empty string for non-float types
            if constexpr (!std::is_same_v<T, float>) {
                return "";
            } else {
                std::stringstream ss;
                ss << "{";
                for (size_t i = 0; i < length; ++i) {
                    ss << ptr[i];
                    if (i < length - 1) {
                        ss << "f, ";
                    } else {
                        ss << "f";
                    }
                }
                ss << "}";
                return ss.str();
            }
        }
    };


    class SignalDetector : public Processor<complex_t, complex_t> {
        using base_type = Processor<complex_t, complex_t>;
    public:
        SignalDetector();
        ~SignalDetector();

        void init(stream<complex_t>* in);
        void setSampleRate(double sampleRate);
        void setCenterFrequency(double centerFrequency);

        int run();

    private:
        static constexpr double TIME_SLICE = 1 / 10.0; // don't change, code currently has magic numbers implicitly depending on it.

        static constexpr int N_FFT_ROWS = (int)(1 / TIME_SLICE * 10); // 10 seconds
        static constexpr int MIN_DETECT_FFT_ROWS = (int)(1 / TIME_SLICE * 2); // 2 seconds


        double sampleRate = 0.0;
        double centerFrequency = 0.0;
        int fftSize = 0;
        int bufferPos = 0;

        std::vector<complex_t> buffer;
        float* fftWindowBuf = nullptr;
        dsp::arrays::ComplexArray fftInArray;
        dsp::arrays::Arg<dsp::arrays::FFTPlan> fftPlan;

        std::vector<std::shared_ptr<std::vector<float>>> suppressedCarrierCandidates;

        void updateFFTSize();
        void generateWindow();
        void aggregateAndDetect();
        void addSingleFFTRow(const ArrayView<float> &rowView);
        void clear();
    };
}
