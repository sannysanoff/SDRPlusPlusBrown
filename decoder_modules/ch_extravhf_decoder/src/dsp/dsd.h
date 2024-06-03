#pragma once

#include "../../../../core/src/config.h"

#if defined(__MACH__) || defined(__ANDROID__)
#define register
#endif

#include <itpp/itcomm.h>

#define _USE_MATH_DEFINES
#include <math.h>

#ifdef WIN32
#undef max
#undef min
#endif


#include <dsp/processor.h>
#include <utils/flog.h>
extern "C" {
    #include <mbelib.h>
}
#include "common.h"
#include "dsd_p25_utils.h"
#include "Golay24.hpp"
#include "ReedSolomon.hpp"
#include "Hamming.hpp"



//Using https://github.com/szechyjs/dsd source code
/*
 * Copyright (C) 2010 DSD Author
 * GPG Key ID: 0x3F1D7FD0 (74EF 430D F7F2 0A48 FCE6  F630 FAA2 635D 3F1D 7FD0)
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

namespace dsp {

    class NewDSD : public Processor<uint8_t, short> {
        using base_type = Processor<uint8_t, short>;
    public:
        NewDSD() {}

        NewDSD(stream<uint8_t>* in) { init(in); }

        void init(stream<uint8_t>* in) {
            p25_frame_nac2[12] = 0;
            p25_ldu1_lsd1[8] = 0;
            p25_ldu1_lsd2[8] = 0;
            p25_ldu2_lsd1[8] = 0;
            p25_ldu2_lsd2[8] = 0;
            mbe_initMbeParms(&curMp, &prevMp, &prevMpEnhanced);
            base_type::init(in);
        }

        int process(int count, const uint8_t* in, short* out);

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

        struct Frame_status {
            bool sync = false;
            enum Lasttype {
                LAST_NOTHING,
                LAST_DMR,
                LAST_P25,
            } lasttype = LAST_NOTHING;
        };
        struct MBE_status {
            bool mbe_status_decoding = false;
            std::string mbe_status_errorbar = "";
        };
        struct DMR_status {
            uint8_t dmr_status_s0_lastburstt = 0;
            uint8_t dmr_status_s1_lastburstt = 0;
            std::string dmr_status_s0_lasttype = "";
            std::string dmr_status_s1_lasttype = "";
            uint8_t dmr_status_cc = 0;
        };
        struct P25_status {
            int p25_status_src = 0;
            bool p25_status_emr = false;
            int p25_status_tg = 0;
            int p25_status_othertg1 = 0;
            int p25_status_othertg2 = 0;
            int p25_status_othertg3 = 0;
            int p25_status_nac = 0;
            uint64_t p25_status_mi_0 = 0;
            uint16_t p25_status_mi_1 = 0;
            uint8_t p25_status_algid = 0;
            uint16_t p25_status_kid = 0;
            uint8_t p25_status_mfid = 0;
            uint16_t p25_status_tgid = 0;
            uint8_t p25_status_lcformat = 0;
            uint64_t p25_status_lcinfo = 0;
            uint8_t p25_status_lastduid[2];
            std::string p25_status_lasttype = "UNK";
            bool p25_status_irr_err = false;
        };

        Frame_status getFrameSyncStatus() {
            return frame_status;
        }
        MBE_status getMBEStatus() {
            return mbe_status;
        }
        DMR_status getDMRStatus() {
            return dmr_status;
        }
        P25_status getP25Status() {
            return p25_status;
        }

    private:

        int inSymsCtr = 0;
        int outSymsCtr = 0;

        enum state {
            STATE_NOT_ENOUGH_DATA,
            STATE_NO_SYNC,
            STATE_SYNC,
            STATE_FSFND_P25,
            STATE_FSFND_DMR_DATA,
            STATE_FSFND_DMR_VOICE,
            STATE_PROC_FRAME_DMR_DATA,
            STATE_PROC_FRAME_DMR_DATA_1,
            STATE_PROC_FRAME_DMR_VOICE,
            STATE_PROC_FRAME_DMR_VOICE_1,
            STATE_PROC_FRAME_DMR_VOICE_2,
            STATE_PROC_FRAME_DMR_VOICE_3,
            STATE_PROC_FRAME_DMR_VOICE_4,
            STATE_PROC_FRAME_DMR_VOICE_5,
            STATE_PROC_FRAME_DMR_VOICE_6,
            STATE_PROC_FRAME_DMR_VOICE_7,
            STATE_PROC_FRAME_DMR_VOICE_8,
            STATE_PROC_FRAME_DMR_VOICE_9,
            STATE_PROC_FRAME_DMR_VOICE_10,
            STATE_PROC_FRAME_P25,
            STATE_PROC_FRAME_P25_1,
            STATE_PROC_FRAME_P25_2,
            STATE_PROC_FRAME_P25_3,
            STATE_PROC_FRAME_P25_HEXWORD,
            STATE_PROC_FRAME_P25_HEXWORD_G24_1,
            STATE_PROC_FRAME_P25_HEXWORD_HAMM_1,
            STATE_PROC_FRAME_P25_DODECAWORD,
            STATE_PROC_FRAME_P25_DODECAWORD_1,
            STATE_PROC_FRAME_P25_IMBE,
            STATE_PROC_FRAME_P25_HDU,
            STATE_PROC_FRAME_P25_HDU_1,
            STATE_PROC_FRAME_P25_HDU_2,
            STATE_PROC_FRAME_P25_LDU1,
            STATE_PROC_FRAME_P25_LDU1_1,
            STATE_PROC_FRAME_P25_LDU1_2,
            STATE_PROC_FRAME_P25_LDU1_3,
            STATE_PROC_FRAME_P25_LDU1_4,
            STATE_PROC_FRAME_P25_LDU1_5,
            STATE_PROC_FRAME_P25_LDU1_6,
            STATE_PROC_FRAME_P25_LDU1_7,
            STATE_PROC_FRAME_P25_LDU1_8,
            STATE_PROC_FRAME_P25_LDU2,
            STATE_PROC_FRAME_P25_LDU2_1,
            STATE_PROC_FRAME_P25_LDU2_2,
            STATE_PROC_FRAME_P25_LDU2_3,
            STATE_PROC_FRAME_P25_LDU2_4,
            STATE_PROC_FRAME_P25_LDU2_5,
            STATE_PROC_FRAME_P25_LDU2_6,
            STATE_PROC_FRAME_P25_LDU2_7,
            STATE_PROC_FRAME_P25_LDU2_8,
            STATE_PROC_FRAME_P25_TDULC,
            STATE_PROC_FRAME_P25_TDULC_1,
            STATE_PROC_FRAME_P25_TDULC_2,
            STATE_PROC_FRAME_P25_TDU,
            STATE_PROC_FRAME_P25_TSDU,
            STATE_PROC_FRAME_P25_PDU,
        };
        state curr_state = STATE_NOT_ENOUGH_DATA;

        int skipCtr = 0;
        uint8_t dibitBuf[1000000];

        //Frame sync search

        Frame_status frame_status;

        int framesyncSymbolsRead = 0;
        int fss_dibitBufP = 200;
        char framesynctest_buf[1024];
        int framesynctest_p = 25;
        int framesynctest_offset = 0;
        #define INV_P25P1_SYNC "333331331133111131311111"
        #define P25P1_SYNC     "111113113311333313133333"
        #define DMR_BS_DATA_SYNC  "313333111331131131331131"
        #define DMR_BS_VOICE_SYNC "131111333113313313113313"
        #define DMR_MS_DATA_SYNC  "311131133313133331131113"
        #define DMR_MS_VOICE_SYNC "133313311131311113313331"
        int findFrameSync(int count, const uint8_t* in);

        //MBE
        MBE_status mbe_status;
        mbe_parms curMp;
        mbe_parms prevMp;
        mbe_parms prevMpEnhanced;
        char mbe_errStr[64];
        #define MBE_uvquality 3
        void processMbeFrame(char imbe_fr[8][23], char ambe_fr[4][24], char imbe7100_fr[7][24], short* out, int* outcnt);

        //DMR
        DMR_status dmr_status;

        int dmr_dibitBuffP = 0;
        int processDMRdata(int count, const uint8_t* in);
        int dmrv_iter = 0;
        int dmrv_ctr = 0;
        char dmrv_ambe_fr[4][24];
        char dmrv_ambe_fr2[4][24];
        char dmrv_ambe_fr3[4][24];
        const static int dmrv_const_rW[36];
        const static int dmrv_const_rX[36];
        const static int dmrv_const_rY[36];
        const static int dmrv_const_rZ[36];
        int dmrv_w_ctr = 0;
        int dmrv_x_ctr = 0;
        int dmrv_y_ctr = 0;
        int dmrv_z_ctr = 0;
        char dmrv_sync[25];
        char dmrv_syncdata[25];
        bool dmrv_muteslot = false;
        int dmr_lastslot = 0;
        int processDMRvoice(int count, const uint8_t* in, short* out, int* outcnt);

        //P25p1
        P25_status p25_status;
        int p25_frame_nac = 0;
        char p25_frame_nac2[13];
        char p25_frame_bch_code[63];
        int p25_bch_code_idx = 0;
        int p25_frame_ctr = 0;
        uint8_t p25_frame_duid[2];
        int p25_frame_prev_type = 0;
        unsigned char p25_frame_parity;
        int processP25frame(int count, const uint8_t* in);
        // Ideas taken from http://op25.osmocom.org/trac/wiki.png/browser/op25/gr-op25/lib/decoder_ff_impl.cc
        // See also p25_training_guide.pdf page 48.
        // See also tia-102-baaa-a-project_25-fdma-common_air_interface.pdf page 40.
        // BCH encoder/decoder implementation from IT++. GNU GPL 3 license.
        itpp::BCH p25_bch {itpp::BCH(63, 16, 11, "6 3 3 1 1 4 1 3 6 7 2 3 5 4 5 3", true)};
        /**
        * Convenience class to calculate the parity of the DUID values. Keeps a table with the expected outcomes
        * for fast lookup.
        */
        class P25_ParityTable {
            public:
                P25_ParityTable() {
                    for (unsigned int i=0; i < sizeof(table); i++) {
                        table[i] = 0;
                    }
                    table[get_index(1,1)] = 1;
                    table[get_index(2,2)] = 1;
                }

                unsigned char get_value(unsigned char x, unsigned char y) {
                    return table[get_index(x,y)];
                }
            private:
                unsigned char table[16];

                unsigned char get_index(unsigned char x, unsigned char y) {
                    return (x<<2) + y;
                }
        } p25_parity_table;
        int p25_check_nid();
        DSDGolay24 p25_golay24;
        DSDReedSolomon_36_20_17 p25_reed_solomon_36_20_17;
        DSDReedSolomon_24_12_13 p25_reed_solomon_24_12_13;
        DSDReedSolomon_24_16_9 p25_reed_solomon_24_16_9;
        Hamming_10_6_3_TableImpl p25_hamming;
        state p25_hexword_return;
        int p25_hexword_statuscnt = 0;
        int p25_hexword_ctr = 0;
        char p25_hexword[6];
        char p25_hexword_parity[12];
        bool p25_hexword_golay24 = true; //false=Hamming
        int processP25HexWord(int count, const uint8_t* in);
        state p25_dodecaword_return;
        int p25_dodecaword_statuscnt = 0;
        int p25_dodecaword_ctr = 0;
        char p25_dodecaword[12];
        char p25_dodecaword_parity[12];
        int processP25DodecaWord(int count, const uint8_t* in);
        /**
        * Uses the information from a corrected sequence of hex word.
        * The proper Golay 24 parity is calculated from the corrected hex word so we can also fix the Golay parity
        * that we read originally from the signal.
        * \param corrected_hex_data Pointer to a sequence of hex words that has been error corrected and therefore
        * we trust it's correct. Typically this are hex words that has been decoded successfully using a
        * Reed-Solomon variant.
        * \param hex_count The number of hex words in the sequence.
        */
        void P25correctGolayDibits6(char* corrected_hex_data, int hex_count);
        /**
        * Correct the information in analog_signal_array according with the content of data, which has been
        * error corrected and should be valid.
        * \param data A sequence of 12-bit words.
        * \param count Number of words in the sequence.
        */
        void P25correctGolayDibits12(char* data, int count);
        void P25correctHammingDibits(char* data, int count);
        /**
        * Reverse the order of bits in a 12-bit word. We need this to accommodate to the expected bit order in
        * some algorithms.
        * \param dodeca The 12-bit word to reverse.
        */
        void P25swapHexWordsBits(char* dodeca) {
          int j;
          for(j=0; j<6; j++) {
              int swap;
              swap = dodeca[j];
              dodeca[j] = dodeca[j+6];
              dodeca[j+6] = swap;
            }
        }
        /**
        * Reverse the order of bits in a sequence of six pairs of 12-bit words and their parities.
        * \param dodeca_data Pointer to the start of the 12-bit words sequence.
        * \param dodeca_parity Pointer to the parities sequence.
        */
        void P25swapHexWords(char* dodeca_data, char* dodeca_parity) {
          int i;
          for(i=0; i<6; i++) {
              P25swapHexWordsBits(dodeca_data + i*12);
              P25swapHexWordsBits(dodeca_parity + i*12);
          }
        }
        void P25updatelcwstatus();
        state p25_imbe_return;
        int p25_imbe_ctr = 0;
        int p25_imbe_statuscnt = 0;
        char p25_imbe_fr[8][23];
        /*
        * P25 Phase1 IMBE interleave schedule
        */
        const static int p25_const_iW[72];
        const static int p25_const_iX[72];
        const static int p25_const_iY[72];
        const static int p25_const_iZ[72];
        int p25_w_ctr = 0;
        int p25_x_ctr = 0;
        int p25_y_ctr = 0;
        int p25_z_ctr = 0;
        int processP25IMBEFrame(int count, const uint8_t* in, short* out, int* outcnt);
        char p25_hdu_hex_data[20][6];    // Data in hex-words (6 bit words). A total of 20 hex words.
        char p25_hdu_hex_parity[16][6]; // Parity of the data, again in hex-word format. A total of 16 parity hex words.
        int p25_hdu_ctr = 0;
        int processP25HDU(int count, const uint8_t* in);
        int p25_ldu1_ctr = 0;
        char p25_ldu1_hex_data[12][6];    // Data in hex-words (6 bit words). A total of 12 hex words.
        char p25_ldu1_hex_parity[12][6];  // Parity of the data, again in hex-word format. A total of 12 parity hex words.
        char p25_ldu1_lsd[8];
        char p25_ldu1_lsd_cyclic_parity[8];
        char p25_ldu1_lsd1[9];
        char p25_ldu1_lsd2[9];
        int processP25LDU1(int count, const uint8_t* in);
        int p25_ldu2_ctr = 0;
        char p25_ldu2_hex_data[16][6];    // Data in hex-words (6 bit words). A total of 16 hex words.
        char p25_ldu2_hex_parity[8][6];   // Parity of the data, again in hex-word format. A total of 12 parity hex words.
        char p25_ldu2_lsd[8];
        char p25_ldu2_lsd_cyclic_parity[8];
        char p25_ldu2_lsd1[9];
        char p25_ldu2_lsd2[9];
        int processP25LDU2(int count, const uint8_t* in);
        int p25_tdulc_ctr = 0;
        int p25_tdulc_statuscnt = 0;
        char p25_tdulc_dodeca_data[6][12];    // Data in 12-bit words. A total of 6 words.
        char p25_tdulc_dodeca_parity[6][12];  // Reed-Solomon parity of the data. A total of 6 parity 12-bit words.
        int processP25TDULC(int count, const uint8_t* in);
        int p25_tdu_ctr = 0;
        int p25_tdu_statuscnt = 0;
        int processP25TDU(int count, const uint8_t* in);
        int p25_tsdu_ctr = 0;
        int processP25TSDU(int count, const uint8_t* in);
        int processP25PDU(int count, const uint8_t* in);
    };





















    #define DSD_INPUT_BUFF_SIZE 240

    enum DSDDecoderType {
        DSDDecoderType_P25p1,
        DSDDecoderType_ProVoice,
        DSDDecoderType_X2TDMA,
        DSDDecoderType_DMR,
        DSDDecoderType_NXDN48,
        DSDDecoderType_NXDN96,
        DSDDecoderType_DSTAR,
        DSDDecoderType_Nothing,
        DSDDecoderType_Auto
    };

    class DSD : public Processor<short, short> {
        using base_type = Processor<short, short>;
    public:
        DSD() {}

        DSD(stream<short>* in) { init(in); }

        void init(stream<short>* in) {
            memset(dibitBuf, 0, sizeof(int)*200);
            memset(sbuf, 0, sizeof(int)*128);
            memset(maxbuf, 15000, sizeof(int)*1024);
            memset(minbuf, -15000, sizeof(int)*1024);
            mbe_initMbeParms(&curMp, &prevMp, &prevMpEnhanced);

            base_type::init(in);
        }

        int run() {
            finflag = 0;
            int sync = getFrameSync();
            if(sync != -2) {
                resetFrameSync();
                center = ((max) + (min)) / 2;
                umid = (((max) - center) * 5 / 8) + center;
                lmid = (((min) - center) * 5 / 8) + center;
                synctype = sync;
                if(synctype == -1) {
                    noCarrier();
                } else {
                    outSymsCtr = 0;
                    processFrame();
                }
            }
            int requiredOut = inSymsCtr * 8000 / 48000;
            int remainingOut = requiredOut - outSymsCtr;
//            printf("in %d out %d req %d\n", inSymsCtr, outSymsCtr, requiredOut);
            if(remainingOut > 0) {
                memset(base_type::out.writeBuf, 0, remainingOut*sizeof(short));
                base_type::out.swap(remainingOut);
            }
            inSymsCtr -= requiredOut * 48000 / 8000;
            return finflag;
        }

        bool status_sync = false;
        std::string status_last_proto = "";
        int status_last_nac = 0;
        int status_last_src = 0;
        int status_last_tg = 0;
        std::string status_last_p25_duid = "";
        std::string status_last_dmr_slot0_burst = "";
        std::string status_last_dmr_slot1_burst = "";
        std::string status_last_nxdn_type = "";
        std::string status_errorbar = "";
        bool status_mbedecoding = false;
        int status_lvl = 0;


    private:

        int inSymsCtr = 0;
        int outSymsCtr = 0;

        int nextSymbolPos = 0;
        int dibitBuf[1000000];
        int* dibitBufP = dibitBuf + 200;
        int repeat = 0;
        int center = 0;
        int jitter = -1;
        int synctype = -1;
        int min = -15000;
        int max = 15000;
        int lmid = 0;
        int umid = 0;
        int minref = -12000;
        int maxref = 12000;
        int lastsample = 0;
        int sbuf[128];
        int sidx = 0;
        int midx = 0;
        int maxbuf[1024];
        int minbuf[1024];
        char errStr[64];
        char fsubtype[16];
        char ftype[16];
        int symbolcnt = 0;
        int rfMod = 0;
        int numflips = 0;
        int lastsynctype = 0;
        int lastp25type = 0;
        int offset = 0;
        int carrier = 0;
        char tg[25][16]; //fill with 48
        int tgcount = 0;
        int lasttg = 0;
        int lastsrc = 0;
        int nac = 0;
        int errs = 0;
        int errs2 = 0;
        int optind = 0;
        int numtdulc = 0;
        int firstframe = 0;
        char slot0light[8] = " slot0 ";
        char slot1light[8] = " slot1 ";
        int samplesPerSymbol = 10;
        int symbolCenter = 4;
        char algid[9] = "________";
        char keyid[17] = "________________";
        int currentslot = 0;
        mbe_parms curMp;
        mbe_parms prevMp;
        mbe_parms prevMpEnhanced;
        P25_Heuristics p25Heuristics;
        P25_Heuristics invp25Heuristics;
        DSDGolay24 golay24;
        DSDReedSolomon_36_20_17 reed_solomon_36_20_17;
        DSDReedSolomon_24_12_13 reed_solomon_24_12_13;
        DSDReedSolomon_24_16_9 reed_solomon_24_16_9;
        Hamming_10_6_3_TableImpl hamming;
        // Ideas taken from http://op25.osmocom.org/trac/wiki.png/browser/op25/gr-op25/lib/decoder_ff_impl.cc
        // See also p25_training_guide.pdf page 48.
        // See also tia-102-baaa-a-project_25-fdma-common_air_interface.pdf page 40.
        // BCH encoder/decoder implementation from IT++. GNU GPL 3 license.
        itpp::BCH bch {itpp::BCH(63, 16, 11, "6 3 3 1 1 4 1 3 6 7 2 3 5 4 5 3", true)};
        int p25kid = 0;
        int lastDibit = 0;
        int invertedX2tdma = 1;
        int invertedDmr = 0;

        int onesymbol = 10;
        int errorbars = 1;
        int datascope = 0;
        int symboltiming = 0;
        int verbose = 2;
        int p25enc = 0;
        int p25lc = 1;
        int p25status = 1;
        int p25tg = 1;
        int scoperate = 3;
        int split = 0;
        int playoffset = 0;
        int frameDstar = 1;
        int frameX2tdma = 1;
        int frameP25p1 = 1;
        int frameNxdn48 = 1;
        int frameNxdn96 = 0;
        int frameDmr = 1;
        int frameProvoice = 1;
        int modC4fm = 1;
        int modQpsk = 1;
        int modGfsk = 1;
        int uvquality = 3;
        int modThreshold = 26;
        int ssize = 36;
        int msize = 15;
        int delay = 0;
        bool unmute_encrypted_p25 = true;
        bool useCosineFilter = true;
        int finflag = 0;

        short inputBuffer[DSD_INPUT_BUFF_SIZE];
        int inputBufReadPtr = 0;
        int inputBufWritePtr = 0;
        int inputBufDataRemains = 0;

        int framesyncSymbolsRead = 0;
        char framesynctest[25];
        char framesynctest18[19];
        char framesynctest32[33];
        char framesyncmodulation[8];
        int framesynctest_pos = 0;
        char framesynctest_buf[10240];
        char* framesynctest_p = framesynctest_buf + 10;
        int framesynclmin = 0;
        int framesynclmax = 0;
        int framesynclidx = 0;
        int framesynclbuf[24];
        int framesynclbuf2[24];
        int framesynclastt = 0;
        int framesyncsync = 0;

        /*
        * Frame sync patterns
        */
        #define INV_P25P1_SYNC "333331331133111131311111"
        #define P25P1_SYNC     "111113113311333313133333"

        #define X2TDMA_BS_VOICE_SYNC "113131333331313331113311"
        #define X2TDMA_BS_DATA_SYNC  "331313111113131113331133"
        #define X2TDMA_MS_DATA_SYNC  "313113333111111133333313"
        #define X2TDMA_MS_VOICE_SYNC "131331111333333311111131"

        #define DSTAR_HD       "131313131333133113131111"
        #define INV_DSTAR_HD   "313131313111311331313333"
        #define DSTAR_SYNC     "313131313133131113313111"
        #define INV_DSTAR_SYNC "131313131311313331131333"

        #define NXDN_MS_DATA_SYNC      "313133113131111333"
        #define INV_NXDN_MS_DATA_SYNC  "131311331313333111"
        #define NXDN_MS_VOICE_SYNC     "313133113131113133"
        #define INV_NXDN_MS_VOICE_SYNC "131311331313331311"
        #define INV_NXDN_BS_DATA_SYNC  "131311331313333131"
        #define NXDN_BS_DATA_SYNC      "313133113131111313"
        #define INV_NXDN_BS_VOICE_SYNC "131311331313331331"
        #define NXDN_BS_VOICE_SYNC     "313133113131113113"

        #define DMR_BS_DATA_SYNC  "313333111331131131331131"
        #define DMR_BS_VOICE_SYNC "131111333113313313113313"
        #define DMR_MS_DATA_SYNC  "311131133313133331131113"
        #define DMR_MS_VOICE_SYNC "133313311131311113313331"

        #define INV_PROVOICE_SYNC    "31313111333133133311331133113311"
        #define PROVOICE_SYNC        "13131333111311311133113311331133"
        #define INV_PROVOICE_EA_SYNC "13313133113113333311313133133311"
        #define PROVOICE_EA_SYNC     "31131311331331111133131311311133"
        /**
        * Convenience class to calculate the parity of the DUID values. Keeps a table with the expected outcomes
        * for fast lookup.
        */
        class ParityTable {
        private:
            unsigned char table[16];

            unsigned char get_index(unsigned char x, unsigned char y) {
                return (x<<2) + y;
            }

        public:
            ParityTable() {
                for (unsigned int i=0; i<sizeof(table); i++) {
                    table[i] = 0;
                }
                table[get_index(1,1)] = 1;
                table[get_index(2,2)] = 1;
            }

            unsigned char get_value(unsigned char x, unsigned char y) {
                return table[get_index(x,y)];
            }

        } parity_table;

        static int comp (const void *a, const void *b) {
            if (*((const int *) a) == *((const int *) b))
                return 0;
            else if (*((const int *) a) < *((const int *) b))
                return -1;
            else
                return 1;
        }

        static int invert_dibit(int dibit) {
            switch (dibit) {
                case 0:
                    return 2;
                case 1:
                    return 3;
                case 2:
                    return 0;
                case 3:
                    return 1;
                default:
                    // Error, shouldn't be here
                    assert(0);
                    return -1;
            }
        }

        //Demodulator
        short getSample();
        int getSymbol (int have_sync);
        void use_symbol (int symbol);
        int digitize (int symbol);
        int getDibitAndAnalogSignal (int* out_analog_signal);
        int getDibit ();
        void skipDibit (int count);
        void noCarrier ();
        void printFrameInfo ();
        void processFrame ();
        void printFrameSync (std::string frametype, int offset, char *modulation);
        void resetFrameSync();
        int getFrameSync();
        short dmrFilter(short sample);
        short nxdnFilter(short sample);
        //MBE
        void processMbeFrame (char imbe_fr[8][23], char ambe_fr[4][24], char imbe7100_fr[7][24]) {
            int i, n;
            char imbe_d[88];
            char ambe_d[49];
            float aout_abs, max, gainfactor, gaindelta, maxbuf;

            for (i = 0; i < 88; i++) {
                imbe_d[i] = 0;
            }

            if ((synctype == 0) || (synctype == 1)) {
                //  0 +P25p1
                //  1 -P25p1
                mbe_processImbe7200x4400Frame (base_type::out.writeBuf, &errs, &errs2, errStr, imbe_fr, imbe_d, &curMp, &prevMp, &prevMpEnhanced, uvquality);
            } else if ((synctype == 14) || (synctype == 15)) {
                mbe_processImbe7100x4400Frame (base_type::out.writeBuf, &errs, &errs2, errStr, imbe7100_fr, imbe_d, &curMp, &prevMp, &prevMpEnhanced, uvquality);
            } else if ((synctype == 6) || (synctype == 7)) {
                mbe_processAmbe3600x2400Frame (base_type::out.writeBuf, &errs, &errs2, errStr, ambe_fr, ambe_d, &curMp, &prevMp, &prevMpEnhanced, uvquality);
            } else {
                mbe_processAmbe3600x2450Frame (base_type::out.writeBuf, &errs, &errs2, errStr, ambe_fr, ambe_d, &curMp, &prevMp, &prevMpEnhanced, uvquality);
            }

            if (errorbars == 1) {
//                printf ("%s", errStr);
                status_errorbar += errStr;
            }

            base_type::out.swap(160);
            outSymsCtr+=160;
        }

        //DMR
        void processDMRdata ();
        void processDMRvoice ();
        const int rW[36] = {
            0, 1, 0, 1, 0, 1,
            0, 1, 0, 1, 0, 1,
            0, 1, 0, 1, 0, 1,
            0, 1, 0, 1, 0, 2,
            0, 2, 0, 2, 0, 2,
            0, 2, 0, 2, 0, 2
        };
        const int rX[36] = {
            23, 10, 22, 9, 21, 8,
            20, 7, 19, 6, 18, 5,
            17, 4, 16, 3, 15, 2,
            14, 1, 13, 0, 12, 10,
            11, 9, 10, 8, 9, 7,
            8, 6, 7, 5, 6, 4
        };
        const int rY[36] = {
            0, 2, 0, 2, 0, 2,
            0, 2, 0, 3, 0, 3,
            1, 3, 1, 3, 1, 3,
            1, 3, 1, 3, 1, 3,
            1, 3, 1, 3, 1, 3,
            1, 3, 1, 3, 1, 3
        };
        const int rZ[36] = {
            5, 3, 4, 2, 3, 1,
            2, 0, 1, 13, 0, 12,
            22, 11, 21, 10, 20, 9,
            19, 8, 18, 7, 17, 6,
            16, 5, 15, 4, 14, 3,
            13, 2, 12, 1, 11, 0
        };
        //DSTAR
        void processDSTAR ();
        void processDSTAR_HD ();
        const int dW[72] = {
                // 0-11
                0, 0,
                3, 2,
                1, 1,
                0, 0,
                1, 1,
                0, 0,

                // 12-23
                3, 2,
                1, 1,
                3, 2,
                1, 1,
                0, 0,
                3, 2,

                // 24-35
                0, 0,
                3, 2,
                1, 1,
                0, 0,
                1, 1,
                0, 0,

                // 36-47
                3, 2,
                1, 1,
                3, 2,
                1, 1,
                0, 0,
                3, 2,

                // 48-59
                0, 0,
                3, 2,
                1, 1,
                0, 0,
                1, 1,
                0, 0,

                // 60-71
                3, 2,
                1, 1,
                3, 3,
                2, 1,
                0, 0,
                3, 3,
        };
        const int dX[72] = {

                // 0-11
                10, 22,
                11, 9,
                10, 22,
                11, 23,
                8, 20,
                9, 21,

                // 12-23
                10, 8,
                9, 21,
                8, 6,
                7, 19,
                8, 20,
                9, 7,

                // 24-35
                6, 18,
                7, 5,
                6, 18,
                7, 19,
                4, 16,
                5, 17,

                // 36-47
                6, 4,
                5, 17,
                4, 2,
                3, 15,
                4, 16,
                5, 3,

                // 48-59
                2, 14,
                3, 1,
                2, 14,
                3, 15,
                0, 12,
                1, 13,

                // 60-71
                2, 0,
                1, 13,
                0, 12,
                10, 11,
                0, 12,
                1, 13,
        };
        //NXDN
        void processNXDNVoice ();
        void processNXDNData ();
        /*
        *  pseudorandom bit sequence
        */
        const char nxdnpr[145] = { 
            1, 0, 0, 1, 0, 0,
            0, 1, 0, 1, 0, 0,
            0, 0, 1, 0, 1, 0,
            1, 1, 0, 1, 0, 0,
            1, 1, 1, 1, 1, 1,
            0, 1, 1, 0, 0, 1,
            0, 0, 1, 0, 0, 1,
            0, 1, 1, 0, 1, 1,
            1, 1, 1, 1, 0, 0,
            1, 0, 0, 1, 1, 0,
            1, 0, 1, 0, 0, 1,
            1, 0, 0, 1, 1, 0,
            0, 0, 0, 0, 0, 0,
            1, 1, 0, 0, 0, 1,
            1, 0, 0, 1, 0, 1,
            0, 0, 0, 1, 1, 0,
            1, 0, 0, 1, 0, 1,
            1, 1, 1, 1, 1, 1,
            0, 1, 0, 0, 0, 1,
            0, 1, 1, 0, 0, 0,
            1, 1, 1, 0, 1, 0,
            1, 1, 0, 0, 1, 0,
            1, 1, 0, 0, 1, 1,
            1, 1, 0, 0, 0, 1 
        };
        /*
        * NXDN AMBE interleave schedule
        */
        const int nW[36] = {
            0, 1, 0, 1, 0, 1,
            0, 1, 0, 1, 0, 1,
            0, 1, 0, 1, 0, 1,
            0, 1, 0, 1, 0, 2,
            0, 2, 0, 2, 0, 2,
            0, 2, 0, 2, 0, 2
        };
        const int nX[36] = {
            23, 10, 22, 9, 21, 8,
            20, 7, 19, 6, 18, 5,
            17, 4, 16, 3, 15, 2,
            14, 1, 13, 0, 12, 10,
            11, 9, 10, 8, 9, 7,
            8, 6, 7, 5, 6, 4
        };
        const int nY[36] = {
            0, 2, 0, 2, 0, 2,
            0, 2, 0, 3, 0, 3,
            1, 3, 1, 3, 1, 3,
            1, 3, 1, 3, 1, 3,
            1, 3, 1, 3, 1, 3,
            1, 3, 1, 3, 1, 3
        };
        const int nZ[36] = {
            5, 3, 4, 2, 3, 1,
            2, 0, 1, 13, 0, 12,
            22, 11, 21, 10, 20, 9,
            19, 8, 18, 7, 17, 6,
            16, 5, 15, 4, 14, 3,
            13, 2, 12, 1, 11, 0
        };
        //P25
        void P25processlcw (char *lcformat, char *mfid, char *lcinfo);
        int P25checkNID(char* bch_code, int* new_nac, char* new_duid, unsigned char parity);
        int P25readDibit (char* output, int* status_count, int* analog_signal, int* did_read_status);
        void P25readDibitUpdateAnalogData (char* output, unsigned int count, int* status_count, P25_Heuristics::P25_AnalogSignal* analog_signal_array, int* analog_signal_index);
        void P25readWord (char* word, unsigned int length, int* status_count, P25_Heuristics::P25_AnalogSignal* analog_signal_array, int* analog_signal_index);
        void P25readGolay24Parity (char* parity, int* status_count, P25_Heuristics::P25_AnalogSignal* analog_signal_array, int* analog_signal_index);
        void P25readHammParity (char* parity, int* status_count, P25_Heuristics::P25_AnalogSignal* analog_signal_array, int* analog_signal_index);
        void P25CorrectHexWord (char* hex, char* parity);
        void P25swapHexWordsBits(char* dodeca);
        void P25swapHexWords(char* dodeca_data, char* dodeca_parity);
        void P25readAndCorrectHexWord (char* hex, int* status_count, P25_Heuristics::P25_AnalogSignal* analog_signal_array, int* analog_signal_index);
        void P25readAndCorrectHexWordHamming (char* hex, int* status_count, P25_Heuristics::P25_AnalogSignal* analog_signal_array, int* analog_signal_index);
        void P25readAndCorrectDodecaWord (char* dodeca, int* status_count, P25_Heuristics::P25_AnalogSignal* analog_signal_array, int* analog_signal_index);
        void P25correctHammingDibits(char* data, int count, P25_Heuristics::P25_AnalogSignal* analog_signal_array);
        void P25correctGolayDibits6(char* corrected_hex_data, int hex_count, P25_Heuristics::P25_AnalogSignal* analog_signal_array);
        void P25correctGolayDibits12(char* data, int count, P25_Heuristics::P25_AnalogSignal* analog_signal_array);
        void P25readZeros(P25_Heuristics::P25_AnalogSignal* analog_signal_array, unsigned int length, int* status_count, int new_sequence);
        void P25processHDU ();
        void P25processIMBE (int*);
        void P25processLDU1 ();
        void P25processLDU2 ();
        void P25processTDU ();
        void P25processTDULC ();
        /*
        * P25 Phase1 IMBE interleave schedule
        */
        const int iW[72] = {
            0, 2, 4, 1, 3, 5,
            0, 2, 4, 1, 3, 6,
            0, 2, 4, 1, 3, 6,
            0, 2, 4, 1, 3, 6,
            0, 2, 4, 1, 3, 6,
            0, 2, 4, 1, 3, 6,
            0, 2, 5, 1, 3, 6,
            0, 2, 5, 1, 3, 6,
            0, 2, 5, 1, 3, 7,
            0, 2, 5, 1, 3, 7,
            0, 2, 5, 1, 4, 7,
            0, 3, 5, 2, 4, 7
        };
        const int iX[72] = {
            22, 20, 10, 20, 18, 0,
            20, 18, 8, 18, 16, 13,
            18, 16, 6, 16, 14, 11,
            16, 14, 4, 14, 12, 9,
            14, 12, 2, 12, 10, 7,
            12, 10, 0, 10, 8, 5,
            10, 8, 13, 8, 6, 3,
            8, 6, 11, 6, 4, 1,
            6, 4, 9, 4, 2, 6,
            4, 2, 7, 2, 0, 4,
            2, 0, 5, 0, 13, 2,
            0, 21, 3, 21, 11, 0
        };
        const int iY[72] = {
            1, 3, 5, 0, 2, 4,
            1, 3, 6, 0, 2, 4,
            1, 3, 6, 0, 2, 4,
            1, 3, 6, 0, 2, 4,
            1, 3, 6, 0, 2, 4,
            1, 3, 6, 0, 2, 5,
            1, 3, 6, 0, 2, 5,
            1, 3, 6, 0, 2, 5,
            1, 3, 6, 0, 2, 5,
            1, 3, 7, 0, 2, 5,
            1, 4, 7, 0, 3, 5,
            2, 4, 7, 1, 3, 5
        };
        const int iZ[72] = {
            21, 19, 1, 21, 19, 9,
            19, 17, 14, 19, 17, 7,
            17, 15, 12, 17, 15, 5,
            15, 13, 10, 15, 13, 3,
            13, 11, 8, 13, 11, 1,
            11, 9, 6, 11, 9, 14,
            9, 7, 4, 9, 7, 12,
            7, 5, 2, 7, 5, 10,
            5, 3, 0, 5, 3, 8,
            3, 1, 5, 3, 1, 6,
            1, 14, 3, 1, 22, 4,
            22, 12, 1, 22, 20, 2
        };
        //ProVoice
        void processProVoice ();
        /*
        * ProVoice IMBE interleave schedule
        */
        const int pW[142] = {
            0, 1, 2, 3, 4, 6,
            0, 1, 2, 3, 4, 6,
            0, 1, 2, 3, 4, 6,
            0, 1, 2, 3, 5, 6,
            0, 1, 2, 3, 5, 6,
            0, 1, 2, 3, 5, 6,
            0, 1, 3, 4, 5, 6,
            1, 2, 3, 4, 5, 6,
            0, 1, 2, 3, 4, 6,
            0, 1, 2, 3, 4, 6,
            0, 1, 2, 3, 4, 6,
            0, 1, 2, 3, 5, 6,
            0, 1, 2, 3, 5, 6,
            0, 1, 2, 3, 5, 6,
            1, 2, 3, 4, 5, 6,
            1, 2, 3, 4, 5,
            0, 1, 2, 3, 4, 6,
            0, 1, 2, 3, 4, 6,
            0, 1, 2, 3, 5, 6,
            0, 1, 2, 3, 5, 6,
            0, 1, 2, 3, 5, 6,
            0, 1, 2, 4, 5, 6,
            1, 2, 3, 4, 5, 6,
            1, 2, 3, 4, 6
        };
        const int pX[142] = {
            18, 18, 17, 16, 7, 21,
            15, 15, 14, 13, 4, 18,
            12, 12, 11, 10, 1, 15,
            9, 9, 8, 7, 13, 12,
            6, 6, 5, 4, 10, 9,
            3, 3, 2, 1, 7, 6,
            0, 0, 22, 13, 4, 3,
            21, 20, 19, 10, 1, 0,
            17, 17, 16, 15, 6, 20,
            14, 14, 13, 12, 3, 17,
            11, 11, 10, 9, 0, 14,
            8, 8, 7, 6, 12, 11,
            5, 5, 4, 3, 9, 8,
            2, 2, 1, 0, 6, 5,
            23, 22, 21, 12, 3, 2,
            20, 19, 18, 9, 0,
            16, 16, 15, 14, 5, 19,
            13, 13, 12, 11, 2, 16,
            10, 10, 9, 8, 14, 13,
            7, 7, 6, 5, 11, 10,
            4, 4, 3, 2, 8, 7,
            1, 1, 0, 14, 5, 4,
            22, 21, 20, 11, 2, 1,
            19, 18, 17, 8, 22
        };
        //X2TDMA
        void processX2TDMAdata ();
        void processX2TDMAvoice ();
        const int aW[36] = {
            0, 1, 0, 1, 0, 1,
            0, 1, 0, 1, 0, 1,
            0, 1, 0, 1, 0, 1,
            0, 1, 0, 1, 0, 2,
            0, 2, 0, 2, 0, 2,
            0, 2, 0, 2, 0, 2
        };
        const int aX[36] = {
            23, 10, 22, 9, 21, 8,
            20, 7, 19, 6, 18, 5,
            17, 4, 16, 3, 15, 2,
            14, 1, 13, 0, 12, 10,
            11, 9, 10, 8, 9, 7,
            8, 6, 7, 5, 6, 4
        };
        const int aY[36] = {
            0, 2, 0, 2, 0, 2,
            0, 2, 0, 3, 0, 3,
            1, 3, 1, 3, 1, 3,
            1, 3, 1, 3, 1, 3,
            1, 3, 1, 3, 1, 3,
            1, 3, 1, 3, 1, 3
        };
        const int aZ[36] = {
            5, 3, 4, 2, 3, 1,
            2, 0, 1, 13, 0, 12,
            22, 11, 21, 10, 20, 9,
            19, 8, 18, 7, 17, 6,
            16, 5, 15, 4, 14, 3,
            13, 2, 12, 1, 11, 0
        };
    };

};
