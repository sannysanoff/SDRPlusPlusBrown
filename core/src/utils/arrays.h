#pragma once
#include <memory>
#include <iostream>

#ifdef WIN32
#define _USE_MATH_DEFINES
#include <math.h>
#endif

#include "dsp/block.h"

#define FL_M_PI 3.1415926535f

extern long long fftCumulativeTime;
extern bool enableAcceleratedFFT;

namespace dsp {

    /**
     * this implements python-style nparray interface
     */

    namespace math {

        std::vector<float> sma(int smawindow, std::vector<float>& src);
        std::vector<float> maxeach(int maxwindow, std::vector<float>& src);
        double sinc(double omega, double x, double norm);
        float expn(float q);
        bool linearInterpolateHoles(float *arr, int narr);
    }

    namespace arrays {

        template <class X>
        using Arg = std::shared_ptr<X>;
        typedef std::shared_ptr<std::vector<float>> FloatArray;
        typedef std::shared_ptr<std::vector<dsp::complex_t>> ComplexArray;

        std::string dumpArr(const float *x, int limit);
        std::string dumpArr(const ComplexArray& x);
        std::string dumpArr(const FloatArray &x);
        std::string dumpArr(const dsp::complex_t *x, int limit);

        void dumpArr_(const FloatArray& x) ;
        void dumpArr_(const std::vector<float> &x);
        void dumpArr_(const ComplexArray& x);
        void dumpArr_(const std::vector<dsp::complex_t> &x);
        void dumpArr_(dsp::complex_t *ptr, int len);
        void dumpArr_(float *ptr, int len);

        // hanning window
        FloatArray nphanning(int len);

        // add all items together
        float npsum(const FloatArray& v);

        // multiply by scalar
        FloatArray mul(const FloatArray& v, float e);
        // add scalar to all items
        FloatArray add(const FloatArray& v, float e);

        // add two arrays
        FloatArray addeach(const FloatArray& v, const FloatArray& w);

        // subtract two array
        FloatArray subeach(const FloatArray& v, const FloatArray& w);
        // add two arrays
        ComplexArray addeach(const ComplexArray& v, const ComplexArray& w);

        // multiply two arrays
        FloatArray muleach(const FloatArray& v, const FloatArray& w);
        // multiply two arrays
        ComplexArray muleach(const FloatArray& v, const ComplexArray& w);
        FloatArray diveach(const FloatArray& v, const FloatArray& w);

        bool npall(const FloatArray& v);
        FloatArray div(const FloatArray& v, float e);
        void div_(const FloatArray& v, float e);
        FloatArray npminimum(const FloatArray& v, const FloatArray& w);
        FloatArray npminimum(const FloatArray& v, float lim);
        FloatArray npminimum_(const FloatArray& v, float lim);
        ComplexArray div(const ComplexArray& v, float val);
        void div_(ComplexArray& v, float val);
        float npmax(const FloatArray& v);
        float npmin(const FloatArray& v);
        FloatArray npmaximum(const FloatArray& v, float lim);
        FloatArray npmaximum_(const FloatArray& v, float lim);
        // array range
        FloatArray nparange(const FloatArray& v, int begin, int end);
        ComplexArray nparange(const Arg<std::vector<dsp::complex_t>>& v, int begin, int end);
        void nparangeset(const FloatArray& v, int begin, const FloatArray& part);
        void nparangeset(const ComplexArray& v, int begin, const ComplexArray& part);
        FloatArray neg(const FloatArray& v);
        FloatArray npexp(const FloatArray& v);
        FloatArray npsqrt(const FloatArray& v);
        FloatArray nplog(const FloatArray& v);
        ComplexArray tocomplex(const FloatArray& v);

        // only even window sizes
        FloatArray npmavg(const FloatArray& v, int windowSize);
        FloatArray npreal(const ComplexArray& v);
        FloatArray npzeros(int size);
        FloatArray hamming(int N);
        FloatArray linspace(float start, float stop, int num);
        ComplexArray npzeros_c(int size);
        void swapfft(const ComplexArray &arr);
        ComplexArray resize(const ComplexArray& in, int nsize);
        FloatArray scipyspecialexpn(const FloatArray& in);
        FloatArray maximum(const FloatArray& in, float value);
        FloatArray clone(const FloatArray& in);
        ComplexArray clone(const ComplexArray & in);
        FloatArray npabsolute(const ComplexArray& in);
        FloatArray centeredSma(FloatArray in, int winsize);
        FloatArray movingVariance(FloatArray in, int winsize);


        struct FFTPlan {
            virtual ComplexArray getInput() = 0;
            virtual ComplexArray getOutput() = 0;
            virtual ComplexArray npfftfft(const ComplexArray& in) = 0;
            virtual ~FFTPlan() {

            }
        };

        Arg<FFTPlan> allocateFFTWPlan(bool backward, int buckets);

        void npfftfft(const ComplexArray& in, const Arg<FFTPlan>& plan);
        std::string ftos(float x);
        std::string sampleArr(const FloatArray& x);
        std::string sampleArr(const ComplexArray& x);
        // For FloatArray
        FloatArray tile(const FloatArray& arr, size_t repeat);


        // For ComplexArray
        ComplexArray tile(const ComplexArray& arr, size_t repeat);

        // For FloatArray
        float amin(const FloatArray& arr);

        // For ComplexArray
        complex_t amin(const ComplexArray& arr);
        // For float values
        float power(float base, float exponent);
        // For complex_t values
        complex_t power(const complex_t& base, float exponent);
        // For FloatArray
        FloatArray concatenate(const FloatArray& arr1, const FloatArray& arr2);

        // For ComplexArray
        ComplexArray concatenate(const ComplexArray& arr1, const ComplexArray& arr2);

        ComplexArray conj(const ComplexArray& arr);
        FloatArray exp(const FloatArray& arr);
        FloatArray real(const ComplexArray& arr);
        FloatArray convolve(const FloatArray& arr1, const FloatArray& arr2);
        ComplexArray operator*(const ComplexArray& a, const FloatArray& b);
        FloatArray operator*(const FloatArray& a, const FloatArray& b);

        FloatArray operator*(const float a, const FloatArray& b);

        FloatArray operator/(const FloatArray& a, const FloatArray& b);

        FloatArray operator/(const FloatArray& a, float b);

        FloatArray operator-(const FloatArray& a, float b);

        FloatArray operator+(float a, const FloatArray& b);

        FloatArray operator+(const FloatArray& a, const FloatArray& b);

    }
}