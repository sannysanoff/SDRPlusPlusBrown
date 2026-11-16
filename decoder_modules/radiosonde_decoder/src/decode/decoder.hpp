#pragma once

#include <cassert>
#include <cmath>
#include <sstream>
#include <dsp/block.h>
#include "common.hpp"
extern "C" {
#include "include/c50.h"
#include "include/dfm09.h"
#include "include/imet4.h"
#include "include/ims100.h"
#include "include/m10.h"
#include "include/mrzn1.h"
#include "include/rs41.h"
}

namespace radiosonde {
    inline float compute_dewpt(float temp, float rh) {
        if (rh <= 0.0f || temp <= -237.0f) { return 0.0f; }
        const float tmp = (logf(rh / 100.0f) + (17.27f * temp / (237.3f + temp))) / 17.27f;
        const float denom = 1.0f - tmp;
        if (fabsf(denom) < 1e-6f) { return 0.0f; }
        const float res = 237.3f * tmp / denom;
        return std::isfinite(res) ? res : 0.0f;
    }

    inline float altitude_to_pressure(float alt) {
        const float g0 = 9.80665f;
        const float M = 0.0289644f;
        const float R_star = 8.3144598f;

        const float hbs[] = {0.0f,      11000.0f, 20000.0f, 32000.0f, 47000.0f, 51000.0f, 77000.0f};
        const float Lbs[] = {-0.0065f,  0.0f,     0.001f,   0.0028f,  0.0f,     -0.0028f, -0.002f};
        const float Pbs[] = {101325.0f, 22632.1f, 5474.89f, 868.02f,  110.91f,  66.94f,   3.96f};
        const float Tbs[] = {288.15f,   216.65f,  216.65f,  228.65f,  270.65f,  270.65f,  214.65f};
        constexpr int layerCount = (int)(sizeof(Lbs) / sizeof(Lbs[0]));

        if (!std::isfinite(alt)) { return 0.0f; }

        int b = 0;
        for (; b < layerCount - 1; b++) {
            if (alt < hbs[b + 1]) { break; }
        }

        const float Lb = Lbs[b];
        const float Pb = Pbs[b];
        const float Tb = Tbs[b];
        const float hb = hbs[b];

        if (Lb != 0.0f) {
            const float ratio = (Tb + Lb * (alt - hb)) / Tb;
            if (ratio <= 0.0f) { return 0.0f; }
            const float p = 1e-2f * Pb * powf(ratio, -(g0 * M) / (R_star * Lb));
            return std::isfinite(p) ? p : 0.0f;
        }
        const float p = 1e-2f * Pb * expf(-g0 * M * (alt - hb) / (R_star * Tb));
        return std::isfinite(p) ? p : 0.0f;
    }

    template<typename T, T* (*decoder_init)(int), void (*decoder_deinit)(T*), ParserStatus (*decoder_get)(T*, SondeData*, const float*, size_t)>
    class Decoder : public dsp::block {
    public:
        Decoder() {}
        ~Decoder() {
            deinit();
        }

        void init(dsp::stream<float>* in, int samplerate, void (*callback)(SondeFullData* data, void* ctx), void* ctx) {
            m_in = in;
            m_ctx = ctx;
            m_callback = callback;
            m_decoder = decoder_init(samplerate);

            dsp::block::registerInput(m_in);
            dsp::block::_block_init = true;
        }

        void deinit() {
            if (!dsp::block::_block_init) { return; }
            dsp::block::stop();
            dsp::block::unregisterInput(m_in);
            dsp::block::_block_init = false;

            if (m_decoder) {
                decoder_deinit(m_decoder);
                m_decoder = nullptr;
            }
        }

        void reset() {
            m_data.init();
        }

        int run() override {
            SondeData fragment;
            int count;

            assert(dsp::block::_block_init);

            if ((count = m_in->read()) < 0) { return -1; }
            if (!m_decoder) {
                m_in->flush();
                return 0;
            }

            while (decoder_get(m_decoder, &fragment, m_in->readBuf, count) != PROCEED) {
                if (fragment.fields & DATA_SEQ) {
                    m_data.seq = fragment.seq;
                }

                if (fragment.fields & DATA_POS) {
                    m_data.lat = fragment.lat;
                    m_data.lon = fragment.lon;
                    m_data.alt = fragment.alt;
                }

                if (fragment.fields & DATA_SPEED) {
                    m_data.spd = fragment.speed;
                    m_data.hdg = fragment.heading;
                    m_data.climb = fragment.climb;
                }

                if (fragment.fields & DATA_TIME) {
                    m_data.time = fragment.time;
                }

                if (fragment.fields & DATA_PTU) {
                    m_data.calib_percent = fragment.calib_percent;
                    m_data.calibrated = m_data.calib_percent >= 100.0f;
                    m_data.temp = fragment.temp;
                    m_data.rh = fragment.rh;
                    m_data.pressure = fragment.pressure;
                    m_data.dewpt = compute_dewpt(m_data.temp, m_data.rh);
                }

                if (fragment.fields & DATA_SERIAL) {
                    fragment.serial[sizeof(fragment.serial) - 1] = '\0';
                    m_data.serial = fragment.serial;
                }

                if (fragment.fields & DATA_SHUTDOWN) {
                    m_data.burstkill = fragment.shutdown;
                }

                /* Auxiliary data */
                if (fragment.fields & DATA_OZONE) {
                    std::ostringstream auxStream;
                    auxStream.precision(2);
                    auxStream << "O3=" << std::fixed << fragment.o3_mpa << "mPa";
                    m_data.auxData = auxStream.str();
                }

                if (m_data.pressure <= 0.0f) {
                    m_data.pressure = altitude_to_pressure(m_data.alt);
                }

                if (fragment.fields && m_callback) {
                    m_callback(&m_data, m_ctx);
                }
            }

            m_in->flush();
            return 0;
        }

    private:
        dsp::stream<float>* m_in = nullptr;
        void (*m_callback)(SondeFullData* data, void* ctx) = nullptr;
        void* m_ctx = nullptr;
        T* m_decoder = nullptr;
        SondeFullData m_data;
    };
}
