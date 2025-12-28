#pragma once

#include <dsp/processor.h>

// #include <osmocom/core/utils.h>
// #include <osmocom/core/talloc.h>

extern "C" {
    #include "tetra_common.h"
    #include "crypto/tetra_crypto.h"
    #include <phy/tetra_burst.h>
    #include <phy/tetra_burst_sync.h>
    #include "c-code/channel.h"
    #include "c-code/source.h"
}

namespace dsp {

    class osmotetradec : public Processor<uint8_t, float> {
        using base_type = Processor<uint8_t, float>;
    public:
        osmotetradec() {}
        
        ~osmotetradec() {
            free(tms->fragslots);
            free(trs);
            free(tms->t_display_st);
            free(tms->tcs);
            free(tms);
            
            free(conv_data);
            // talloc_free(trs);
            // talloc_free(tms->t_display_st);
            // talloc_free(tms->tcs);
            // talloc_free(tms);
        }

        osmotetradec(stream<uint8_t>* in) { init(in); }
        
        void init(stream<uint8_t>* in)  {
            /* Initialize tetra mac state and crypto state */
// #ifdef OTC_GLOBAL
//             tms = talloc_zero(OTC_GLOBAL, struct tetra_mac_state);
// #else
//             tms = talloc_zero(tetra_tall_ctx, struct tetra_mac_state);
// #endif
//             tetra_mac_state_init(tms);
//             tms->tcs = talloc_zero(NULL, struct tetra_crypto_state);
//             tms->t_display_st = talloc_zero(NULL, struct tetra_display_state);
//             tetra_crypto_state_init(tms->tcs);
// 
// #ifdef OTC_GLOBAL
//             trs = talloc_zero(OTC_GLOBAL, struct tetra_rx_state);
// #else
//             trs = talloc_zero(tetra_tall_ctx, struct tetra_rx_state);
// #endif
            tms = (struct tetra_mac_state*)malloc(sizeof(struct tetra_mac_state));
            memset(tms, 0, sizeof(struct tetra_mac_state));
            tetra_mac_state_init(tms);
            tms->tcs = (struct tetra_crypto_state*)malloc(sizeof(struct tetra_crypto_state));
            memset(tms->tcs, 0, sizeof(struct tetra_crypto_state));
            tms->t_display_st = (struct tetra_display_state*)malloc(sizeof(struct tetra_display_state));
            memset(tms->t_display_st, 0, sizeof(struct tetra_display_state));
            tetra_crypto_state_init(tms->tcs);
            trs = (struct tetra_rx_state*)malloc(sizeof(struct tetra_rx_state));
            memset(trs, 0, sizeof(struct tetra_rx_state));
            tms->fragslots = (struct fragslot*)malloc(sizeof(struct fragslot)*FRAGSLOT_NR_SLOTS);
            memset(tms->fragslots, 0, sizeof(struct fragslot)*FRAGSLOT_NR_SLOTS);

            conv_data = (float*)malloc(sizeof(float)*STREAM_BUFFER_SIZE);
            memset(conv_data, 0, sizeof(float)*STREAM_BUFFER_SIZE);


            trs->burst_cb_priv = tms;

            tms->put_voice_data = put_voice_data;
            tms->put_voice_data_ctx = this;
            tms->last_frame = 0;
            tms->curr_active_timeslot = 0;

            Init_Decod_Tetra();

            out_tmp_buff.init(32768);

            base_type::init(in);
        }

        //return current RX state. 0=unlocked, 1=know_next_start, 2=locked
        int getRxState() {
            switch(trs->state) {
                case RX_S_LOCKED:
                    return 2;
                case RX_S_KNOW_FSTART:
                    return 1;
                default:
                    return 0;
            }
        }

        int getCurrHyperframe() {
            return tms->t_display_st->curr_hyperframe;
        }
        int getCurrMultiframe() {
            return tms->t_display_st->curr_multiframe;
        }
        int getCurrFrame() {
            return tms->t_display_st->curr_frame;
        }
        int getTimeslotContent(int ts) { //0-other, 1-NORM1, 2-NORM2, 3-SYNC, 4-VOICE
            return tms->t_display_st->timeslot_content[ts];
        }
        int getDlUsage() {
            return tms->t_display_st->dl_usage;
        }
        int getUlUsage() {
            return tms->t_display_st->ul_usage;
        }
        char getAccess1Code() {
            return tms->t_display_st->access1_code;
        }
        char getAccess2Code() {
            return tms->t_display_st->access2_code;
        }
        int getAccess1() {
            return tms->t_display_st->access1;
        }
        int getAccess2() {
            return tms->t_display_st->access2;
        }
        int getDlFreq() {
            return tms->t_display_st->dl_freq;
        }
        int getUlFreq() {
            return tms->t_display_st->ul_freq;
        }
        int getMcc() {
            return tms->t_display_st->mcc;
        }
        int getMnc() {
            return tms->t_display_st->mnc;
        }
        int getCc() {
            return tms->t_display_st->cc;
        }
        bool getLastCrcFail() {
            return tms->t_display_st->last_crc_fail;
        }
        bool getAdvancedLink() {
            return tms->t_display_st->advanced_link;
        }
        bool getAirEncryption() {
            return tms->t_display_st->air_encryption;
        }
        bool getSndcpData() {
            return tms->t_display_st->sndcp_data;
        }
        bool getCircuitData() {
            return tms->t_display_st->circuit_data;
        }
        bool getVoiceService() {
            return tms->t_display_st->voice_service;
        }
        bool getNormalMode() {
            return tms->t_display_st->normal_mode;
        }
        bool getMigrationSupported() {
            return tms->t_display_st->migration_supported;
        }
        bool getNeverMinimumMode() {
            return tms->t_display_st->never_minimum_mode;
        }
        bool getPriorityCell() {
            return tms->t_display_st->priority_cell;
        }
        bool getDeregMandatory() {
            return tms->t_display_st->dereg_mandatory;
        }
        bool getRegMandatory() {
            return tms->t_display_st->reg_mandatory;
        }

        inline int process(int count, const uint8_t* in, float* out)  {
            int outcnt = 0;
            tetra_burst_sync_in(trs, (uint8_t*)in, count);
            if(out_tmp_buff.getReadable(false) > 0) {
                outcnt += out_tmp_buff.read(out, out_tmp_buff.getReadable(false));
            }
            outSymsCtr += outcnt;
            inSymsCtr += count;
            int requiredOut = inSymsCtr * 8 / 36;
            int remainingOut = requiredOut - outSymsCtr;
            bool decoding = (tms->t_display_st->timeslot_content[0] == 4) | (tms->t_display_st->timeslot_content[1] == 4) | (tms->t_display_st->timeslot_content[2] == 4) | (tms->t_display_st->timeslot_content[3] == 4);
            if(remainingOut > 0 && !decoding) {
                memset(&(out[outcnt]), 0, remainingOut*sizeof(float));
                outcnt += remainingOut;
            }
            outSymsCtr -= (std::min(outSymsCtr, requiredOut));
            inSymsCtr -= requiredOut * 36 / 8;
            return outcnt;
        }

        int run()  {
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

        static void put_voice_data(void* ctx, int count, int16_t* data) {
            osmotetradec* _this = (osmotetradec*) ctx;

            volk_16i_s32f_convert_32f(_this->conv_data, data, 32768.0f, count);
            if(_this->out_tmp_buff.getWritable(false) >= count) {
                _this->out_tmp_buff.write(_this->conv_data, count);
            }
        }

    private:
        int inSymsCtr = 0;
        int outSymsCtr = 0;
        void *tetra_tall_ctx = NULL;
        struct tetra_rx_state *trs = NULL;
        struct tetra_mac_state *tms = NULL;
        float *conv_data = NULL;
        buffer::RingBuffer<float> out_tmp_buff;
    };

}
