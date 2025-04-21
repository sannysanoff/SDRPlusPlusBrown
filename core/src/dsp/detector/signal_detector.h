#pragma once
#include "../processor.h"
#include <vector>
#include <utils/arrays.h>
#include <sstream>
#include <type_traits>
#include <stdexcept>

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
        const T& operator[](size_t idx) const { 
            #ifndef NDEBUG
            if (idx >= length) {
                throw std::out_of_range("ArrayView index out of range");
            }
            #endif
            return ptr[idx]; 
        }
        
        const T& at(size_t idx) const {
            if (idx >= length) {
                throw std::out_of_range("ArrayView index out of range");
            }
            return ptr[idx];
        }
        const T* data() const { return ptr; }

        const T* begin() const { return ptr; }
        const T* end() const { return ptr + length; }
        
        // Dump the content as a C++ initializer string (for float and int types)
        std::string dump() const {
            std::stringstream ss;
            ss << "{";
            
            if constexpr (std::is_same_v<T, float>) {
                // Float type
                for (size_t i = 0; i < length; ++i) {
                    ss << ptr[i];
                    if (i < length - 1) {
                        ss << "f, ";
                    } else {
                        ss << "f";
                    }
                }
            } 
            else if constexpr (std::is_same_v<T, int>) {
                // Int type
                for (size_t i = 0; i < length; ++i) {
                    ss << ptr[i];
                    if (i < length - 1) {
                        ss << ", ";
                    }
                }
            }
            else {
                // Other types - return empty braces
                return "{}";
            }
            
            ss << "}";
            return ss.str();
        }
    };


    class SignalDetector : public Processor<complex_t, complex_t> {
        using base_type = Processor<complex_t, complex_t>;
    public:
        std::vector<float> sigs_smoothed; // Smoothed signal vector for detection

        SignalDetector();
        ~SignalDetector();

        void init(stream<complex_t>* in);
        void setSampleRate(double sampleRate);
        void setCenterFrequency(double centerFrequency);
        

        int run();

    public:
        // Enable/disable the detector
        void setEnabled(bool enabled) { this->enabled = enabled; }
        bool isEnabled() const { return enabled; }

    private:
        static constexpr double TIME_SLICE = 1 / 10.0; // don't change, code currently has magic numbers implicitly depending on it.

        static constexpr int N_FFT_ROWS = (int)(1 / TIME_SLICE * 10); // 10 seconds
        static constexpr int MIN_DETECT_FFT_ROWS = (int)(1 / TIME_SLICE * 2); // 2 seconds
        static constexpr int FREQ_WINDOW_SIZE = 50; // Window size for frequency bins in candidate selection
        static constexpr int DETECT_INTERVAL_FRAMES = (int)(1 / TIME_SLICE); // Once per second

        bool enabled = true; // Whether the detector is enabled


        double sampleRate = 0.0;
        double centerFrequency = 0.0;
        int fftSize = 0;
        int bufferPos = 0;

        std::vector<complex_t> buffer;
        float* fftWindowBuf = nullptr;
        dsp::arrays::ComplexArray fftInArray;
        dsp::arrays::Arg<dsp::arrays::FFTPlan> fftPlan;

        std::vector<std::shared_ptr<std::vector<float>>> suppressedCarrierCandidates;
        int framesSinceLastDetect = 0; // Counter for frames since last detection

        void updateFFTSize();
        void generateWindow();
        void aggregateAndDetect();
        void addSingleFFTRow(const ArrayView<float> &rowView);
        void clear();
    };
}
