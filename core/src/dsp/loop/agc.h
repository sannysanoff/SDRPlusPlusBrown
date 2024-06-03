#pragma once
#include "../processor.h"

namespace dsp::loop {
    template <class T>
    class AGC : public Processor<T, T> {
        using base_type = Processor<T, T>;
    public:
        AGC() {}

        AGC(stream<T>* in, double setPoint, double attack, double decay, double maxGain, double maxOutputAmp, double initGain = 1.0) { init(in, setPoint, attack, decay, maxGain, maxOutputAmp, initGain); }

        void init(stream<T>* in, double setPoint, double attack, double decay, double maxGain, double maxOutputAmp, double initGain = 1.0) {
            _setPoint = setPoint;
            _attack = attack;
            _invAttack = 1.0f - _attack;
            _decay = decay;
            _invDecay = 1.0f - _decay;
            _maxGain = maxGain;
            _maxOutputAmp = maxOutputAmp;
            _initGain = initGain;
            _startEnvelope = 0;
            _frozen.store(false);
            amp = _setPoint / _initGain;
            base_type::init(in);
        }

        void setSetPoint(double setPoint) {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            _setPoint = setPoint;
        }

        void setAttack(double attack) {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            _attack = attack;
            _invAttack = 1.0f - _attack;
        }

        bool getAttack() {
            return _attack;
        }

        void setDecay(double decay) {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            _decay = decay;
            _invDecay = 1.0f - _decay;
        }

        float getDecay() {
            return _decay;
        }

        void setMaxGain(double maxGain) {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            _maxGain = maxGain;
        }

        void setMaxOutputAmp(double maxOutputAmp) {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            _maxOutputAmp = maxOutputAmp;
        }

        void setInitialGain(double initGain) {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            _initGain = initGain;
        }

        void reset() {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            amp = _setPoint / _initGain;
            _startEnvelope  =0;
        }

        void setFrozen(bool b) {
            _frozen.store(b);
        }

        inline int process(int count, T* in, T* out) {
            float envelope = 1.0f;
            if (_attack <= 0) {
                std::copy(in, in + count, out);
                return count;
            }
            for (int i = 0; i < count; i++) {
                // Get signal amplitude
                float inAmp, gain = 1.0;
                if constexpr (std::is_same_v<T, complex_t>) {
                    inAmp = in[i].amplitude();
                }
                if constexpr (std::is_same_v<T, float>) {
                    inAmp = fabsf(in[i]);
                }

                // Update average amplitude
                if (inAmp != 0.0f) {
                    if (!_frozen.load()) {
                        auto namp = (inAmp > amp) ? ((amp * _invAttack) + (inAmp * _attack)) : ((amp * _invDecay) + (inAmp * _decay));
                        if (!isnan(namp)) {
                            amp = namp;
                            gain = std::min<float>(_setPoint / amp, _maxGain);
                        }
                    }
                }
                else {
                    gain = 1.0f;
                }

                // If clipping is detected look ahead and correct
                if (false && inAmp*gain > _maxOutputAmp) {
                    float maxAmp = 0;
                    for (int j = i; j < count; j++) {
                        if constexpr (std::is_same_v<T, complex_t>) {
                            inAmp = in[j].amplitude();
                        }
                        if constexpr (std::is_same_v<T, float>) {
                            inAmp = fabsf(in[j]);
                        }
                        if (inAmp > maxAmp) { maxAmp = inAmp; }
                    }
                    amp = maxAmp;
                    gain = std::min<float>(_setPoint / amp, _maxGain);
                }

                if (_startEnvelope < _totalEnvelopeLength) {
                    envelope = _startEnvelope / (float)_totalEnvelopeLength;
                }
                _startEnvelope++;
                // Scale output by gain
                out[i] = in[i] * gain * envelope;
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

    protected:
        float _setPoint;
        float _attack;
        float _invAttack;
        float _decay;
        float _invDecay;
        float _maxGain;
        float _maxOutputAmp;
        float _initGain;
        int   _startEnvelope;
        int   _totalEnvelopeLength = 4800; // length of start envelope, circa 1/10 of second. This is needed to reduce clicks when e.g. switching SSB/AM
        std::atomic_bool _frozen;

        float amp = 1.0;

    };
}