#pragma once
#include <dsp/hier_block.h>
#include <dsp/multirate/rational_resampler.h>
#include <dsp/correction/dc_blocker.h>
#include <dsp/routing/splitter.h>
#include <dsp/convert/stereo_to_mono.h>
#include <dsp/buffer/reshaper.h>
#include <dsp/convert/real_to_complex.h>
#include <dsp/convert/mono_to_stereo.h>
#include <dsp/convert/complex_to_real.h>
#include <dsp/taps/high_pass.h>
#include <dsp/block.h>
#include "common.h"

namespace dsp {

    //Input sample rate = 672 S/s
    //Based on http://yo3iiu.ro/blog/?p=779, author: Bogdan Diaconescu <yo3iiu@yo3iiu.ro>
    class DCSDecoder : public  Sink<float> {
        using base_type =  Sink<float>;
    public:
        DCSDecoder() {}

        DCSDecoder(stream<float>* in, int newSquelchCode) { init(in, newSquelchCode); }

        void init(stream<float>* in, int newSquelchCode) {
            _in = in;
            squelchCode = newSquelchCode;

            base_type::init(in);
        }

        void setSquelchCode(int newSquelchCode) {
            assert(base_type::_block_init);
            squelchCode = newSquelchCode;
        }

        inline int process(int count, const float* in) {
            if (count < 0) { return -1; }
            int8_t signs[count];
            volk_32f_binary_slicer_8i(signs, in, count);
            bool setAct = false;
            int ret;
            uint32_t bits, invbits;
            int bitsum;
            for(int i = 0; i < count - 23*sps; i++) {
                bits = 0;
                bitsum = 0;
                for(int k = 1; k <= 23*sps; k++) {
                    bitsum += signs[i+k-1];
                    if(k % sps == 0) {
                        bits >>= 1;
                        if(bitsum >= sps/2) {
                            bits |= 1 << 23;
                        }
                        bitsum = 0;
                    }
                }
                for(int k = 0; k < 22; k++) {
                    if ((ret = golay_match(bits)) > 0) {
                        golay_find(ret & 0777, &pcode, &ncode);
                        setAct = true;
                        break;
                    } else if ((ret = golay_match(~bits)) > 0 ) {
                        golay_find(ret & 0777, &ncode, &pcode);
                        setAct = true;
                        break;
                    }
                    bits = ((bits << 1)|(bits >> (23 - 1))) & ~(1<<24);
                }

            }
            if(setAct) {
                isActive = true;
                if(squelchCode == -ncode || squelchCode == pcode) {
                    codeMatch = true;
                } else {
                    codeMatch = false;
                }
            } else {
                isActive = false;
                codeMatch = false;
            }

            return 0;
        }

        int run() {
            int count = base_type::_in->read();
            if (count < 0) { return -1; }

            process(count, base_type::_in->readBuf);
            //no output
            base_type::_in->flush();
            return 0;
        }

        bool isActive = false;
        int pcode = 0;
        int ncode = 0;
        bool codeMatch = false;

    private:

        /*
        * Table with standard DCS codes.
        */
        #define DCSCodes_count 42
        #define DCSCodes_polarity 2
        #define DCSCodes_onecode_count 5
        static constexpr int DCSCodes [DCSCodes_count][DCSCodes_polarity][DCSCodes_onecode_count] = {
            {{023, 0340, 0766, 0, 0}, {047, 0375, 0707, 0, 0}},
            {{025, 0, 0, 0, 0}, {0244, 0417, 0176, 0, 0}},
            {{026, 0566, 0, 0, 0}, {0464, 0642, 0772, 0237, 0}},
            {{031, 0374, 0643, 0, 0}, {0627, 037, 0560, 0, 0}},
            {{032, 0, 0, 0, 0}, {051, 0520, 0771, 0, 0}},
            {{043, 0355, 0, 0, 0}, {0445, 0457, 0575, 0222}},
            {{054, 0405, 0675, 0, 0}, {0413, 0620, 0133, 0, 0}},
            {{065, 0301, 0, 0, 0}, {0271, 0427, 0510, 0762, 0}},
            {{071, 0603, 0717, 0746, 0}, {0306, 0761, 0147, 0303, 0}},
            {{072, 0470, 0701, 0, 0}, {0245, 0370, 0554, 0, 0}},
            {{073, 0640, 0, 0, 0}, {0506, 0574, 0224, 0313, 0}},
            {{074, 0360, 0721, 0, 0}, {0174, 0270, 0142, 0, 0}},
            {{0114, 0327, 0615, 0, 0}, {0712, 0136, 0502, 0, 0}},
            {{0115, 0534, 0674, 0, 0}, {0152, 0366, 0415, 0, 0}},
            {{0125, 0173, 0, 0, 0}, {0365, 0107, 0, 0, 0}},
            {{0131, 0572, 0702, 0, 0}, {0364, 0641, 0130, 0, 0}},
            {{0132, 0605, 0634, 0714, 0}, {0546, 0614, 0751, 0317, 0}},
            {{0134, 0273, 0, 0, 0}, {0223, 0350, 0475, 0750, 0}},
            {{0143, 0333, 0, 0, 0}, {0412, 0441, 0711, 0127, 0}},
            {{0155, 0233, 0660, 0, 0}, {0731, 0744, 0447, 0473, 0474}},
            {{0156, 0517, 0741, 0, 0}, {0265, 0426, 0171, 0, 0}},
            {{0162, 0416, 0553, 0, 0}, {0503, 0157, 0322, 0, 0}},
            {{0165, 0354, 0, 0, 0}, {0251, 0704, 0742, 0236, 0}},
            {{0172, 057, 0, 0, 0}, {036, 0137, 0, 0, 0}},
            {{0205, 0610, 0135, 0, 0}, {0263, 0736, 0213, 0, 0}},
            {{0226, 0557, 0104, 0, 0}, {0411, 0756, 0117, 0, 0}},
            {{0243, 0267, 0342, 0, 0}, {0351, 0353, 0435, 0, 0}},
            {{0261, 0567, 0227, 0, 0}, {0732, 0164, 0207, 0, 0}},
            {{0311, 0330, 0456, 0561, 0}, {0664, 0715, 0344, 0471, 0}},
            {{0315, 0321, 0673, 0, 0}, {0423, 0563, 0621, 0713, 0234}},
            {{0331, 0372, 0507, 0, 0}, {0465, 0656, 056, 0, 0}},
            {{0343, 0570, 0324, 0, 0}, {0532, 0161, 0345, 0, 0}},
            {{0346, 0616, 0635, 0724, 0}, {0612, 0706, 0254, 0314, 0}},
            {{0371, 0453, 0530, 0217, 0}, {0734, 066, 0, 0, 0}},
            {{0431, 0730, 0262, 0316, 0}, {0723, 0235, 0611, 0671, 0}},
            {{0432, 0276, 0326, 0, 0}, {0516, 0720, 067, 0, 0}},
            {{0466, 0666, 0144, 0, 0}, {0662, 0363, 0436, 0443, 0444}},
            {{0565, 0307, 0362, 0, 0}, {0703, 0150, 0256, 0, }},
            {{0606, 0630, 0153, 0, 0}, {0631, 0636, 0745, 0231, 0504}},
            {{0624, 075, 0501, 0, 0}, {0632, 0657, 0123, 0, 0}},
            {{0654, 0163, 0460, 0607, 0}, {0743, 0312, 0515, 0663, 0}},
            {{0754, 076, 0203, 0, 0}, {0116, 0734, 0, 0, 0}}
        };

        /*
        * Given an input value returns the accepted golay 
        * code (according to DCS table) and its treversed polarity counterpart.
        * If the golay code is not found returns (0).
        * If error returns (-1)
        */
        int golay_find(int v, int *dir, int *rev) {
            int m,n,p;

            if (dir == NULL || rev == NULL)
                return (-1);

            for (m = 0; m < DCSCodes_count; m++)
                for (n = 0; n < DCSCodes_polarity; n++)
                    for (p = 0; p < DCSCodes_onecode_count; p++)
                        if (DCSCodes[m][n][p] != 0 && DCSCodes[m][n][p] == v)
                        {
                            int d = (DCSCodes[m][n][0] & 0007) + (DCSCodes[m][n][0] & 0070)*1.25 + (DCSCodes[m][n][0] & 0700)*1.5625;
                            int r = (DCSCodes[m][(n+1)%2][0] & 0007) + (DCSCodes[m][(n+1)%2][0] & 0070)*1.25 + (DCSCodes[m][(n+1)%2][0] & 0700)*1.5625;
                            *dir = r;
                            *rev = d;
                            return (v);
                        }
            *dir = *rev = 0;
            return (0);
        }

        /*
        * receives a 23 bits bitstream and verfies if it matches
        * Returns 0 if not match
        * Else retrurn the matched value
        */
        static int golay_match(unsigned int v) {
            if ((v & (1 << 9)) == 0 && (v & (1 << 10)) == 0 && (v & (1 << 11)) == (1 << 11)) { //F1 F2 F3
                char c1 = (v >> 0) & 0x01; //C1
                char c2 = (v >> 1) & 0x01; //C2
                char c3 = (v >> 2) & 0x01; //C3
                char c4 = (v >> 3) & 0x01; //C4
                char c5 = (v >> 4) & 0x01; //C5
                char c6 = (v >> 5) & 0x01; //C6
                char c7 = (v >> 6) & 0x01; //C7
                char c8 = (v >> 7) & 0x01; //C8
                char c9 = (v >> 8) & 0x01; //C9

                char px, pcx;

                /* calculate and verify paryty bits */
                px = (v >> 12) & 0x01; //P1
                pcx = (c1 + c2 + c3 + c4 + c5 + c8) % 2;
                if (pcx != px)
                    return (0);

                px = (v >> 13) & 0x01; //P2
                pcx = (~((c2 + c3 +c4 +c5 +c6 + c9) % 2)) & 0x01;
                if (pcx != px)
                    return (0);

                px = (v >> 14) & 0x01; //P3
                pcx = (c1 + c2 + c6 + c7 +c8) % 2;
                if (pcx != px)
                    return (0);

                px = (v >> 15) & 0x01; //P4
                pcx = (~((c2 + c3 + c7 + c8 + c9) % 2)) & 0x01;
                if (pcx != px)
                    return (0);

                px = (v >> 16) & 0x01; //P5
                pcx = (~((c1 + c2 + c5 +c9) % 2)) & 0x01;
                if (px != pcx)
                    return (0);

                px = (v >> 17) & 0x01; //P6
                pcx = (~((c1 + c4 + c5 + c6 + c8) % 2)) & 0x01;
                if (px != pcx)
                    return (0);

                px = (v >> 18) & 0x01; //P7
                pcx = (c1 + c3 + c4 + c6 + c7 + c8 + c9) % 2;
                if (px != pcx)
                    return (0);

                px = (v >> 19) & 0x01; //P8
                pcx = (c2 + c4 + c5 +c7 + c8 + c9) % 2;
                if (px != pcx)
                    return (0);

                px = (v >> 20) & 0x01; //P9
                pcx = (c3 + c5 + c6 + c8 + c9) % 2;
                if (px != pcx)
                    return (0);

                px = (v >> 21) & 0x01; //P10
                pcx = (~((c4 + c6 + c7 + c9) % 2)) & 0x01;
                if (px != pcx)
                    return (0);

                px = (v >> 22) & 0x01; //P11
                pcx = (~((c1 + c2 + c3 + c4 + c7) % 2)) & 0x01;
                if (px != pcx)
                    return (0);

            } else
                return (0);

            return (v & 0x8fffff);
        }

        int squelchCode = +25;
        int sps = 5;
    };

    class DCSSquelch : public Processor<stereo_t, stereo_t> {
        using base_type = Processor<stereo_t, stereo_t>;
    public:
        DCSSquelch() {}

        DCSSquelch(stream<stereo_t>* in, float inputSr) { init(in, inputSr); }

        ~DCSSquelch() {
            if (!base_type::_block_init) { return; }
            base_type::stop();
            // taps::free(outFilterTaps);
        }

        void init(stream<stereo_t>* in, float inputSr) {
            sTM.init(&measureStream);
            resamp.init(&sTM.out, inputSr, 672.0f);
            dcblock.init(&resamp.out, 0.01);
            reshape.init(&dcblock.out, 134, 0); //0.2sec
            dcsDecode.init(&reshape.out, squelchCode);

            // taps::free(outFilterTaps);
            // outFilterTaps = taps::highPass(300.0, 100.0, inputSr);
            // outFilter.init(NULL, outFilterTaps);

            //DIRTY HACK!!! I just don't know how to make it in better way
            sTM.start();
            resamp.start();
            dcblock.start();
            reshape.start();
            dcsDecode.start();

            base_type::init(in);
        }

        void start() {
            sTM.start();
            resamp.start();
            dcblock.start();
            reshape.start();
            dcsDecode.start();
            base_type::start();
        }

        void stop() {
            sTM.stop();
            resamp.stop();
            dcblock.stop();
            reshape.stop();
            dcsDecode.stop();
            base_type::stop();
        }

        void setInputSr(float inputSr) {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            base_type::tempStop();
            resamp.setInSamplerate(inputSr);
            // taps::free(outFilterTaps);
            // outFilterTaps = taps::highPass(300.0, 100.0, inputSr);
            // outFilter.setTaps(outFilterTaps);
            base_type::tempStart();
        }

        void setSquelchCode(int newSquelchCode) {
            assert(base_type::_block_init);
            squelchCode = newSquelchCode;
            dcsDecode.setSquelchCode(squelchCode);
        }

        void setSquelchEnabled(bool newSquelchEnabled) {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            base_type::tempStop();
            //Bypass filter when squelch is disabled
            base_type::tempStart();
            squelchEnabled = newSquelchEnabled;
        }

        int getCurrentDCSPCode() {
            assert(base_type::_block_init);
            return dcsDecode.pcode;
        }

        int getCurrentDCSNCode() {
            assert(base_type::_block_init);
            return dcsDecode.ncode;
        }

        bool getCurrentDCSActive() {
            assert(base_type::_block_init);
            return dcsDecode.isActive;
        }

        inline int process(int count, const stereo_t* in, stereo_t* out) {
            memcpy(measureStream.writeBuf, in, count*sizeof(stereo_t));
            measureStream.swap(count);
            if (!squelchEnabled) {
                memcpy(out, in, count * sizeof(stereo_t));
            } else if(dcsDecode.codeMatch) {
                // outFilter.process(count, in, out);
                memcpy(out, in, count * sizeof(stereo_t));
            } else {
                memset(out, 0, count * sizeof(stereo_t));
            }
            return count;
        }

        int run() {
            int count = base_type::_in->read();
            if (count < 0) { return -1; }

            int outCount = process(count, base_type::_in->readBuf, base_type::out.writeBuf);

            // Swap if some data was generated
            base_type::_in->flush();
            if (outCount) {
                if (!base_type::out.swap(outCount)) { return -1; }
            }
            return outCount;
        }

    private:
        //Measure chain
        stream<stereo_t> measureStream;
        convert::StereoToMono sTM;
        multirate::RationalResampler<float> resamp;
        correction::DCBlocker<float> dcblock;
        buffer::Reshaper<float> reshape;
        DCSDecoder dcsDecode;

        //Squelch chain
        // tap<float> outFilterTaps;
        // filter::FIR<stereo_t, float> outFilter;

        bool squelchEnabled = false;
        int squelchCode = +25;
    };

}
