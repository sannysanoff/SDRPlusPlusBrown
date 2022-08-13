#pragma once
#include <memory>
#include <iostream>

#ifdef WIN32
#define _USE_MATH_DEFINES
#include <math.h>
#endif

#include <dsp/block.h>
#include "logmmse_math.h"
#include <fftw3.h>
#include <volk/volk.h>

namespace dsp {

    /**
     * this implements python-style nparray interface
     */

    namespace arrays {

        template <class X>
        using Arg = std::shared_ptr<X>;
        typedef std::shared_ptr<std::vector<float>> FloatArray;
        typedef std::shared_ptr<std::vector<dsp::complex_t>> ComplexArray;

        inline std::string dumpArr(const FloatArray& x) {
            std::string s;
            auto minn = x->at(0);
            auto maxx = x->at(0);
            int lim = 10;
            for (int q = 0; q < x->size(); q++) {
                auto v = x->at(q);
                if (q < lim) {
                    s.append(" ");
                    s.append(std::to_string(v));
                }
                if (v > maxx) {
                    maxx = v;
                }
                if (v < minn) {
                    minn = v;
                }
            }
            std::string pre = "min/max=";
            pre.append(std::to_string(minn));
            pre += "/";
            pre.append(std::to_string(maxx));
            pre.append(" ");
            return pre + s;
        }

        inline std::string dumpArr(const ComplexArray& x) {
            std::string s;
            auto minn = x->at(0).re;
            auto maxx = x->at(0).re;
            for (int q = 0; q < x->size(); q++) {
                s.append(" ");
                auto v = x->at(q).amplitude();
                s.append(std::to_string(v));
                if (v > maxx) {
                    maxx = v;
                }
                if (v < minn) {
                    minn = v;
                }
            }
            std::string pre = "min/max=";
            pre.append(std::to_string(minn));
            pre += "/";
            pre.append(std::to_string(maxx));
            pre.append(" ");
            return pre + s;
        }

        inline void dumpArr_(const FloatArray& x) {
            std::cout << dumpArr(x) << std::endl;
        }

        inline void dumpArr_(const ComplexArray& x) {
            std::cout << dumpArr(x) << std::endl;
        }

        inline FloatArray nphanning(int len) {
            auto retval = std::make_shared<std::vector<float>>();
            for (int i = 0; i < len; i++) {
                retval->emplace_back(0.5 - 0.5 * cos(2.0 * M_PI * i / (len - 1)));
            }
            return retval;
        }

        inline float npsum(const Arg<std::vector<float>>& v) {
            float f = 0;
            for (auto d : *v) {
                f += d;
            }
            return f;
        }

        inline FloatArray mul(const Arg<std::vector<float>>& v, float e) {
            auto retval = std::make_shared<std::vector<float>>();
            if (false) {
                retval->reserve(v->size());
                for (auto d : *v) {
                    retval->emplace_back(d * e);
                }
            }
            else {
                retval->resize(v->size());
                volk_32f_s32f_multiply_32f(retval->data(), v->data(), e, v->size());
            }
            return retval;
        }

        inline FloatArray add(const Arg<std::vector<float>>& v, float e) {
            auto retval = std::make_shared<std::vector<float>>();
            if (true) {
                retval->reserve(v->size());
                for (auto d : *v) {
                    retval->emplace_back(d + e);
                }
            }
            else {
                //                retval->resize(v->size());
                //                volk_32f_s32f_add_32f(retval->data(), v->data(), e, v->size());
            }
            return retval;
        }

        inline FloatArray addeach(const Arg<std::vector<float>>& v, const Arg<std::vector<float>>& w) {
            auto retval = std::make_shared<std::vector<float>>();
            if (false) {
                retval->reserve(v->size());
                for (int q = 0; q < v->size(); q++) {
                    retval->emplace_back(v->at(q) + w->at(q));
                }
            }
            else {
                retval->resize(v->size());
                volk_32f_x2_add_32f(retval->data(), v->data(), w->data(), v->size());
            }
            return retval;
        }

        inline FloatArray subeach(const Arg<std::vector<float>>& v, const Arg<std::vector<float>>& w) {
            auto retval = std::make_shared<std::vector<float>>();
            if (false) {
                retval->reserve(v->size());
                for (int q = 0; q < v->size(); q++) {
                    retval->emplace_back(v->at(q) + w->at(q));
                }
            }
            else {
                retval->resize(v->size());
                volk_32f_x2_subtract_32f(retval->data(), v->data(), w->data(), v->size());
            }
            return retval;
        }


        inline ComplexArray addeach(const ComplexArray& v, const ComplexArray& w) {
            auto retval = std::make_shared<std::vector<dsp::complex_t>>();
#ifndef  VOLK_VERSION
            retval->reserve(v->size());
            for (int q = 0; q < v->size(); q++) {
                retval->emplace_back(v->at(q) + w->at(q));
            }
#else
            retval->resize(v->size());
            volk_32fc_x2_add_32fc((lv_32fc_t*)retval->data(), (lv_32fc_t*)v->data(), (lv_32fc_t*)w->data(), v->size());
#endif
            return retval;
        }

        inline FloatArray muleach(const Arg<std::vector<float>>& v, const Arg<std::vector<float>>& w) {
            auto retval = std::make_shared<std::vector<float>>();
            if (false) {
                retval->reserve(v->size());
                for (int q = 0; q < v->size(); q++) {
                    auto vv = v->at(q);
                    auto ww = w->at(q);
                    auto mul = vv * ww;
                    if (mul == 0 && vv != 0 && ww != 0) {
                        mul = 1.175494351e-37;
                    }
                    retval->emplace_back(mul);
                }
            }
            else {
                retval->resize(v->size());
                volk_32f_x2_multiply_32f(retval->data(), v->data(), w->data(), v->size());
            }
            return retval;
        }

        inline ComplexArray muleach(const FloatArray& v, const ComplexArray& w) {
            auto retval = std::make_shared<std::vector<dsp::complex_t>>();
            retval->resize(v->size());
            auto rD = retval->data();
            auto wD = w->data();
            auto vD = v->data();
            for (int q = 0; q < v->size(); q++) {
                rD[q] = dsp::complex_t{ vD[q], 0 } * wD[q];
            }
            return retval;
        }

        inline FloatArray diveach(const Arg<std::vector<float>>& v, const Arg<std::vector<float>>& w) {
            auto retval = std::make_shared<std::vector<float>>();
            if (false) {
                retval->reserve(v->size());
                for (int q = 0; q < v->size(); q++) {
                    retval->emplace_back(v->at(q) / w->at(q));
                }
            }
            else {
                retval->resize(v->size());
                volk_32f_x2_divide_32f(retval->data(), v->data(), w->data(), v->size());
            }
            return retval;
        }

        inline bool npall(const Arg<std::vector<float>>& v) {
            int countZeros = 0;
            //            int firstZero = -1;
            for (auto d : *v) {
                if (d == 0) {
                    return false;
                }
                /*
                                if (countZeros == 0) {
                                    firstZero++;
                                }
                */
            }
            //            if (countZeros) {
            ////                std::cout << "npall: " << countZeros << "/" << v->size() << " first at " << firstZero << std::endl;
            //                return false;
            //            }
            return true;
        }

        inline FloatArray div(const Arg<std::vector<float>>& v, float e) {
            auto retval = std::make_shared<std::vector<float>>();
            if (false) {
                retval->reserve(v->size());
                for (auto d : *v) {
                    retval->emplace_back(d / e);
                }
            }
            else {
                retval->resize(v->size());
                volk_32f_s32f_multiply_32f(retval->data(), v->data(), 1.0 / e, v->size());
            }
            return retval;
        }

        inline FloatArray npminimum(const Arg<std::vector<float>>& v, float lim) {
            auto retval = std::make_shared<std::vector<float>>();
            retval->reserve(v->size());
            //            int ix = 0;
            for (auto d : *v) {
                if (d < lim) {
                    retval->emplace_back(d);
                }
                else {
                    retval->emplace_back(lim);
                }
                //                ix++;
                //                if (ix == 1000000) {
                //                    std::cout << "XX";
                //                }
            }
            return retval;
        }

        inline FloatArray npminimum_(const Arg<std::vector<float>>& v, float lim) {
            auto retval = std::make_shared<std::vector<float>>(v->data(), v->data() + v->size());
            auto rvD = retval->data();
            for (int q = 0; q < retval->size(); q++) {
                if (rvD[q] > lim) {
                    rvD[q] = lim;
                }
            }
            return retval;
        }


        inline ComplexArray div(const ComplexArray& v, float val) {
            auto retval = std::make_shared<std::vector<dsp::complex_t>>();
            if (false) {
                retval->reserve(v->size());
                for (auto d : *v) {
                    retval->emplace_back(d / val);
                }
            }
            else {
                retval->resize(v->size());
                volk_32fc_s32fc_multiply_32fc((lv_32fc_t*)retval->data(), (lv_32fc_t*)v->data(), lv_32fc_t(1.0f / val, 0.0f), v->size());
            }
            return retval;
        }

        inline FloatArray npmaximum(const Arg<std::vector<float>>& v, float lim) {
            auto retval = std::make_shared<std::vector<float>>(v->data(), v->data() + v->size());
            auto rvD = retval->data();
            for (int q = 0; q < retval->size(); q++) {
                if (rvD[q] < lim) {
                    rvD[q] = lim;
                }
            }
            return retval;
        }

        inline FloatArray npmaximum_(const Arg<std::vector<float>>& v, float lim) {
            auto rvD = v->data();
            for (int q = 0; q < v->size(); q++) {
                if (rvD[q] < lim) {
                    rvD[q] = lim;
                }
            }
            return v;
        }

        // array range
        inline FloatArray nparange(const Arg<std::vector<float>>& v, int begin, int end) {
            auto retval = std::make_shared<std::vector<float>>();
            retval->reserve(end - begin);
            for (int i = begin; i < end; i++) {
                retval->emplace_back(v->at(i));
            }
            return retval;
        }

        inline ComplexArray nparange(const Arg<std::vector<dsp::complex_t>>& v, int begin, int end) {
            auto retval = std::make_shared<std::vector<dsp::complex_t>>(v->begin() + begin, v->begin() + end);
            return retval;
        }

        // update array in-place
        inline void nparangeset(const FloatArray& v, int begin, const FloatArray& part) {
            for (int i = 0; i < part->size(); i++) {
                (*v)[begin + i] = part->at(i);
            }
        }

        inline void nparangeset(const ComplexArray& v, int begin, const ComplexArray& part) {
            memmove(v->data() + begin, part->data(), sizeof(part->at(0)) * part->size());
            //            for (int i = 0; i < part->size(); i++) {
            //                (*v)[begin + i] = part->at(i);
            //            }
        }

        inline FloatArray neg(const Arg<std::vector<float>>& v) {
            auto retval = std::make_shared<std::vector<float>>();
            retval->reserve(v->size());
            for (auto d : *v) {
                retval->emplace_back(-d);
            }
            return retval;
        }

        inline FloatArray npexp(const Arg<std::vector<float>>& v) {
            auto retval = std::make_shared<std::vector<float>>();
            if (false) {
                retval->reserve(v->size());
                for (auto d : *v) {
                    retval->emplace_back(exp(d));
                }
            }
            else {
                retval->resize(v->size());
                volk_32f_expfast_32f(retval->data(), v->data(), v->size());
            }
            return retval;
        }

        inline FloatArray nplog(const Arg<std::vector<float>>& v) {
            auto retval = std::make_shared<std::vector<float>>();
            for (auto d : *v) {
                retval->emplace_back(log(d));
            }
            return retval;
        }

        inline ComplexArray tocomplex(const FloatArray& v) {
            auto retval = std::make_shared<std::vector<dsp::complex_t>>();
            retval->reserve(v->size());
            for (auto d : *v) {
                retval->emplace_back(dsp::complex_t{ d, 0.0f });
            }
            return retval;
        }

        // only even window sizes
        inline FloatArray npmavg(const FloatArray& v, int windowSize) {
            auto retval = std::make_shared<std::vector<float>>();
            float sum = 0;
            float count = 0;
            auto ws2 = windowSize / 2;
            for (int ix = 0; ix < v->size() + ws2; ix++) {
                if (ix < v->size()) {
                    sum += v->at(ix);
                    count++;
                }
                if (ix > windowSize) {
                    count--;
                    sum -= v->at(ix - count);
                }
                if (ix >= ws2) {
                    retval->emplace_back(sum / count);
                }
            }
            if (retval->size() != v->size()) {
                abort();
            }
            return retval;
        }

        inline FloatArray npreal(const ComplexArray& v) {
            auto retval = std::make_shared<std::vector<float>>();
            for (auto d : *v) {
                retval->emplace_back(d.re);
            }
            return retval;
        }

        inline FloatArray npzeros(int size) {
            auto retval = std::make_shared<std::vector<float>>(size, 0.0);
            return retval;
        }

        inline ComplexArray npzeros_c(int size) {
            auto retval = std::make_shared<std::vector<dsp::complex_t>>(size, dsp::complex_t{ 0.0f, 0.0f });
            return retval;
        }

        inline ComplexArray resize(const ComplexArray& in, int nsize) {
            if (in->size() == nsize) {
                return in;
            }
            auto retval = std::make_shared<std::vector<dsp::complex_t>>();
            auto limit = in->size();
            if (nsize < in->size()) {
                limit = nsize;
            }
            retval->reserve(nsize);
            for (auto i = 0; i < limit; i++) {
                retval->emplace_back(in->at(i));
            }
            for (auto i = limit; i < nsize; i++) {
                retval->emplace_back(dsp::complex_t{ 0, 0 });
            }
            return retval;
        }

        inline FloatArray scipyspecialexpn(const FloatArray& in) {
            auto retval = std::make_shared<std::vector<float>>(in->size());
            auto rvD = retval->data();
            auto inD = in->data();
            for (auto q = 0; q < in->size(); q++) {
                rvD[q] = dsp::math::expn(inD[q]);
            }
            return retval;
        }

        inline FloatArray npabsolute(const ComplexArray& in) {
            auto retval = std::make_shared<std::vector<float>>();
            if (false) {
                retval->reserve(in->size());
                for (auto& v : *in) {
                    retval->emplace_back(v.amplitude());
                }
            }
            else {
                retval->resize(in->size());
                volk_32fc_magnitude_32f(retval->data(), (const lv_32fc_t*)in->data(), in->size());
            }
            return retval;
        }

        struct fftwPlan {
            int nbuckets;
            bool reverse;
            fftwf_plan_s* p;
            Arg<std::vector<dsp::complex_t>> input;
            Arg<std::vector<dsp::complex_t>> output;
            virtual ~fftwPlan() {
                fftwf_destroy_plan(p);
            }
        };

        inline Arg<fftwPlan> allocateFFTWPlan(bool backward, int buckets) {
            auto plan = std::make_shared<fftwPlan>();
            plan->nbuckets = buckets;
            plan->reverse = backward;
            plan->input = std::make_shared<std::vector<dsp::complex_t>>(buckets);
            plan->output = std::make_shared<std::vector<dsp::complex_t>>(buckets);
            auto p = fftwf_plan_dft_1d(buckets, (fftwf_complex*)plan->input->data(), (fftwf_complex*)plan->output->data(), backward ? FFTW_BACKWARD : FFTW_FORWARD, FFTW_ESTIMATE);
            plan->p = p;
            return plan;
        }

        inline ComplexArray npfftfft(const ComplexArray& in, const Arg<fftwPlan>& plan) {
            auto in0 = resize(in, plan->nbuckets);
            std::copy(in0->begin(), in0->end(), plan->input->begin());
            auto out0 = npzeros_c(plan->nbuckets);
            fftwf_execute(plan->p);
            std::copy(plan->output->begin(), plan->output->end(), out0->begin());
            if (plan->reverse) {
                return div(out0, plan->nbuckets);
            }
            else {
                return out0;
            }
        }

        inline std::string ftos(float x) {
            char buf[100];
            sprintf(buf, "%1.5f", x);
            return std::string(buf);
        }

        inline std::string sampleArr(const FloatArray& x) {
            return std::string("[") + ftos(x->at(0)) + "," + ftos(x->at(1)) + ",..," + ftos(x->at(40)) + ",..," + ftos(x->at(140)) + ",...]";
        }

        inline std::string sampleArr(const ComplexArray& x) {
            return std::string("[") + ftos(x->at(0).re) + "," + ftos(x->at(1).re) + ",..," + ftos(x->at(40).re) + ",...]";
        }


    }
}