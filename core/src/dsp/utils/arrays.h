#pragma once
#include <memory>
#include <iostream>
#include <dsp/math.h>

namespace dsp {
    namespace arrays {

        template<class X> using Arg = std::shared_ptr<X>;
        typedef std::shared_ptr<std::vector<float>> FloatArray;
        typedef std::shared_ptr<std::vector<dsp::complex_t>> ComplexArray;

        inline std::string dumpArr(const FloatArray &x) {
            std::string s;
            auto minn = x->at(0);
            auto maxx = x->at(0);
            int lim = 10;
            for(int q=0; q<x->size(); q++) {
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
            return pre+s;
        }

        inline std::string dumpArr(const ComplexArray &x) {
            std::string s;
            auto minn = x->at(0).re;
            auto maxx = x->at(0).re;
            for(int q=0; q<x->size(); q++) {
                s.append(" ");
                auto v = x->at(q).re;
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
            return pre+s;
        }

        inline void dumpArr_(const FloatArray &x) {
            std::cout << dumpArr(x) << std::endl;
        }

        inline void dumpArr_(const ComplexArray &x) {
            std::cout << dumpArr(x) << std::endl;
        }

        inline FloatArray nphanning(int len) {
            auto retval = std::make_shared<std::vector<float>>();
            for (int i = 0; i < len; i++) {
                retval->emplace_back(0.5 - 0.5 * cos(2.0 * M_PI * i / (len - 1)));
            }
            return retval;
        }

        inline float npsum(const Arg<std::vector<float>> &v) {
            float f = 0;
            for (auto d: *v) {
                f += d;
            }
            return f;
        }

        inline FloatArray mul(const Arg<std::vector<float>> &v, float e) {
            auto retval = std::make_shared<std::vector<float>>();
            for (auto d: *v) {
                retval->emplace_back(d * e);
            }
            return retval;
        }

        inline FloatArray add(const Arg<std::vector<float>> &v, float e) {
            auto retval = std::make_shared<std::vector<float>>();
            for (auto d: *v) {
                retval->emplace_back(d + e);
            }
            return retval;
        }

        inline FloatArray addeach(const Arg<std::vector<float>> &v, const Arg<std::vector<float>> &w) {
            auto retval = std::make_shared<std::vector<float>>();
            for (int q = 0; q < v->size(); q++) {
                retval->emplace_back(v->at(q) + w->at(q));
            }
            return retval;
        }


        inline ComplexArray addeach(const ComplexArray &v, const ComplexArray &w) {
            auto retval = std::make_shared<std::vector<dsp::complex_t>>();
            for (int q = 0; q < v->size(); q++) {
                retval->emplace_back(v->at(q) + w->at(q));
            }
            return retval;
        }

        inline FloatArray muleach(const Arg<std::vector<float>> &v, const Arg<std::vector<float>> &w) {
            auto retval = std::make_shared<std::vector<float>>();
            for (int q = 0; q < v->size(); q++) {
                auto vv = v->at(q);
                auto ww = w->at(q);
                auto mul = vv * ww;
                if (mul == 0 && vv != 0 && ww != 0) {
                    mul = 1.175494351e-37;
                }
                retval->emplace_back(mul);
            }
            return retval;
        }

        inline ComplexArray muleach(const FloatArray &v, const ComplexArray &w) {
            auto retval = std::make_shared<std::vector<dsp::complex_t>>();
            for (int q = 0; q < v->size(); q++) {
                retval->emplace_back(dsp::complex_t{v->at(q), 0} * w->at(q));
            }
            return retval;
        }

        inline FloatArray diveach(const Arg<std::vector<float>> &v, const Arg<std::vector<float>> &w) {
            auto retval = std::make_shared<std::vector<float>>();
            for (int q = 0; q < v->size(); q++) {
                retval->emplace_back(v->at(q) / w->at(q));
            }
            return retval;
        }

        inline bool npall(const Arg<std::vector<float>> &v) {
            int countZeros = 0;
            int firstZero = -1;
            for (auto d: *v) {
                if (d == 0) {
                    countZeros++;
                }
                if (countZeros == 0) {
                    firstZero++;
                }
            }
            if (countZeros) {
//                std::cout << "npall: " << countZeros << "/" << v->size() << " first at " << firstZero << std::endl;
                return false;
            }
            return true;
        }

        inline FloatArray div(const Arg<std::vector<float>> &v, float e) {
            auto retval = std::make_shared<std::vector<float>>();
            for (auto d: *v) {
                retval->emplace_back(d / e);
            }
            return retval;
        }

        inline FloatArray npminimum(const Arg<std::vector<float>> &v, float lim) {
            auto retval = std::make_shared<std::vector<float>>();
            for (auto d: *v) {
                if (d < lim) {
                    retval->emplace_back(d);
                } else {
                    retval->emplace_back(lim);
                }
            }
            return retval;
        }

        inline ComplexArray div(const ComplexArray &v, float val) {
            auto retval = std::make_shared<std::vector<dsp::complex_t>>();
            for (auto d: *v) {
                retval->emplace_back(d / val);
            }
            return retval;
        }

        inline FloatArray npmaximum(const Arg<std::vector<float>> &v, float lim) {
            auto retval = std::make_shared<std::vector<float>>();
            for (auto d: *v) {
                if (d > lim) {
                    retval->emplace_back(d);
                } else {
                    retval->emplace_back(lim);
                }
            }
            return retval;
        }

// array range
        inline FloatArray nparange(const Arg<std::vector<float>> &v, int begin, int end) {
            auto retval = std::make_shared<std::vector<float>>();
            for (int i = begin; i < end; i++) {
                retval->emplace_back(v->at(i));
            }
            return retval;
        }

        inline ComplexArray nparange(const Arg<std::vector<dsp::complex_t>> &v, int begin, int end) {
            auto retval = std::make_shared<std::vector<dsp::complex_t>>();
            for (int i = begin; i < end; i++) {
                retval->emplace_back(v->at(i));
            }
            return retval;
        }

// update array in-place
        inline void nparangeset(const FloatArray &v, int begin, const FloatArray &part) {
            for (int i = 0; i < part->size(); i++) {
                (*v)[begin + i] = part->at(i);
            }
        }

        inline void nparangeset(const ComplexArray &v, int begin, const ComplexArray &part) {
            for (int i = 0; i < part->size(); i++) {
                (*v)[begin + i] = part->at(i);
            }
        }

        inline FloatArray neg(const Arg<std::vector<float>> &v) {
            auto retval = std::make_shared<std::vector<float>>();
            for (auto d: *v) {
                retval->emplace_back(-d);
            }
            return retval;
        }

        inline FloatArray npexp(const Arg<std::vector<float>> &v) {
            auto retval = std::make_shared<std::vector<float>>();
            for (auto d: *v) {
                retval->emplace_back(exp(d));
            }
            return retval;
        }

        inline FloatArray nplog(const Arg<std::vector<float>> &v) {
            auto retval = std::make_shared<std::vector<float>>();
            for (auto d: *v) {
                retval->emplace_back(log(d));
            }
            return retval;
        }

        inline ComplexArray tocomplex(const FloatArray &v) {
            auto retval = std::make_shared<std::vector<dsp::complex_t>>();
            for (auto d: *v) {
                retval->emplace_back(dsp::complex_t{d, 0.0f});
            }
            return retval;
        }

        inline FloatArray npreal(const ComplexArray &v) {
            auto retval = std::make_shared<std::vector<float>>();
            for (auto d: *v) {
                retval->emplace_back(d.re);
            }
            return retval;
        }

        inline FloatArray npzeros(int size) {
            auto retval = std::make_shared<std::vector<float>>();
            for (int i = 0; i < size; i++) {
                retval->emplace_back(0.0f);
            }
            return retval;
        }

        inline ComplexArray npzeros_c(int size) {
            auto retval = std::make_shared<std::vector<dsp::complex_t>>();
            for (int i = 0; i < size; i++) {
                retval->emplace_back(dsp::complex_t{0.0f, 0.0f});
            }
            return retval;
        }

        inline ComplexArray resize(const ComplexArray &in, int nsize) {
            if (in->size() == nsize) {
                return in;
            }
            auto retval = std::make_shared<std::vector<dsp::complex_t>>();
            auto limit = in->size();
            if (nsize < in->size()) {
                limit = nsize;
            }
            for (int i = 0; i < limit; i++) {
                retval->emplace_back(in->at(i));
            }
            for (int i = limit; i < nsize; i++) {
                retval->emplace_back(dsp::complex_t{0, 0});
            }
            return retval;
        }

        inline FloatArray scipyspecialexpn(const FloatArray &in) {
            auto retval = std::make_shared<std::vector<float>>();
            for (auto &v: *in) {
                retval->emplace_back(dsp::math::expn(v));
            }
            return retval;
        }

        inline FloatArray npabsolute(const ComplexArray &in) {
            auto retval = std::make_shared<std::vector<float>>();
            for (auto &v: *in) {
                retval->emplace_back(v.amplitude());
            }
            return retval;
        }

        inline ComplexArray npfftfft(const ComplexArray &in, int buckets, int axis) {
            auto in0 = resize(in, buckets);
            auto retval = std::make_shared<std::vector<dsp::complex_t>>();
            auto out0 = resize(in, buckets);
            auto p = fftwf_plan_dft_1d(buckets, (fftwf_complex *) in0->data(), (fftwf_complex *) out0->data(), FFTW_FORWARD, FFTW_ESTIMATE);
            fftwf_execute(p);
            fftwf_destroy_plan(p);
            return out0;
        }

        inline ComplexArray npfftifft(const ComplexArray &in, int buckets, int axis) {
            auto in0 = resize(in, buckets);
            auto retval = std::make_shared<std::vector<dsp::complex_t>>();
            auto out0 = resize(in, buckets);
            auto p = fftwf_plan_dft_1d(buckets, (fftwf_complex *) in0->data(), (fftwf_complex *) out0->data(), FFTW_BACKWARD, FFTW_ESTIMATE);
            fftwf_execute(p);
            fftwf_destroy_plan(p);
            return div(out0, buckets);
        }

        inline std::string ftos(float x) {
            char buf[100];
            sprintf(buf, "%1.5f", x);
            return std::string(buf);
        }

        inline std::string sampleArr(const FloatArray &x) {
            return std::string("[")+ftos(x->at(0))+","+ftos(x->at(1))+",..,"+ftos(x->at(40))+",..,"+ftos(x->at(140))+",...]";
        }

        inline std::string sampleArr(const ComplexArray &x) {
            return std::string("[")+ftos(x->at(0).re)+","+ftos(x->at(1).re)+",..,"+ftos(x->at(40).re)+",...]";
        }


    }
}