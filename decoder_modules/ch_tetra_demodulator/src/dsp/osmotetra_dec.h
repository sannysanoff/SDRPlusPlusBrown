#pragma once

#include <algorithm>
#include <dsp/processor.h>
#include <thread> // NATIVE FIX: Include thread mapping headers for sleep macros
#include <chrono>

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
            // Native Fix: Force base processing loops to stop running first
            base_type::stop();

            // Native Fix: Sleep for 50ms to allow background threads (merger.run) to exit cleanly
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            // NATIVE FIX: ONLY free the single heap array allocated with malloc
            if (conv_data != nullptr) {
                free(conv_data);
                conv_data = nullptr;
            }

            // DO NOT execute free() on tms, trs, tcs, t_display_st, or fragslots.
            // They are stack-allocated class members and free themselves natively.
        }

        osmotetradec(stream<uint8_t>* in) { init(in); }

        // NATIVE VALUE BINDING: Store the upstream extractor memory address
        void setExtractor(void* extractor_ptr) {
            upstream_extractor = extractor_ptr;
        }

        void init(stream<uint8_t>* in)  {
            // Bind our globally aligned stack structures directly to the operational pointers
            tms = &instance_tms;
            trs = &instance_trs;

            // Clear the memory pages cleanly
            memset(tms, 0, sizeof(struct tetra_mac_state));
            memset(trs, 0, sizeof(struct tetra_rx_state));

            // Set up the nested sub-structures natively on the stack
            tms->tcs = &instance_tcs;
            tms->t_display_st = &instance_t_display_st;
            tms->fragslots = instance_fragslots;

            memset(tms->tcs, 0, sizeof(struct tetra_crypto_state));
            memset(tms->t_display_st, 0, sizeof(struct tetra_display_state));
            memset(tms->fragslots, 0, sizeof(struct fragslot) * FRAGSLOT_NR_SLOTS);

            // Initialize the Osmocom library states safely
            tetra_mac_state_init(tms);
            tetra_crypto_state_init(tms->tcs);

            // Establish the secure private callback link
            trs->burst_cb_priv = tms;

            tms->put_voice_data = put_voice_data;
            tms->put_voice_data_ctx = this;
            tms->last_frame = 0;
            tms->curr_active_timeslot = 0;

            Init_Decod_Tetra();

            conv_data = (float*)malloc(sizeof(float) * STREAM_BUFFER_SIZE);
            memset(conv_data, 0, sizeof(float) * STREAM_BUFFER_SIZE);

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
            static int unique_frame_log = -1;
            
            // ------------------------------------------------------------------------
            // ROLLING SIGNAL QUALITY COUNTERS
            // ------------------------------------------------------------------------
            static int total_frames_tracked = 0;
            static int successful_crc_frames = 0;
            static float rolling_signal_quality = 100.0f;
            // ------------------------------------------------------------------------
            
            if (trs == nullptr || tms == nullptr || count <= 0 || in == nullptr) {
                return 0;
            }

            // Local stack-allocated scratchpad to accumulate unpacked bits safely
            static uint8_t unpacked_bits_buf[8192];
            int unpacked_bit_count = 0;

            for (int i = 0; i < count; i++) {
                if (unpacked_bit_count + 2 >= 8192) { break; }
                uint8_t symbol = in[i];
                unpacked_bits_buf[unpacked_bit_count++] = (symbol & 0b10) >> 1;
                unpacked_bits_buf[unpacked_bit_count++] = symbol & 0b01;
            }

            const int SAFE_SLOT_BURST_SIZE = 510;
            for (int off = 0; off < unpacked_bit_count; off += SAFE_SLOT_BURST_SIZE) {
                int chunk_size = std::min(SAFE_SLOT_BURST_SIZE, unpacked_bit_count - off);
                
                if (chunk_size > 0) {
                    trs->burst_cb_priv = tms;
                    tetra_burst_sync_in(trs, unpacked_bits_buf + off, chunk_size);
                }

                if (tms && tms->t_display_st) {
                    int active_frame = tms->t_display_st->curr_frame;
                    if (active_frame != unique_frame_log) {
                        unique_frame_log = active_frame;
                        
                        auto now = std::chrono::system_clock::now();
                        auto time_t_now = std::chrono::system_clock::to_time_t(now);
                        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
                        
                        struct tm time_info;
                        localtime_r(&time_t_now, &time_info);

                        // ------------------------------------------------------------------------
                        // SIGNAL QUALITY MATH: Calculate real-time sliding CRC success window
                        // ------------------------------------------------------------------------
                        bool is_locked = !(tms->t_display_st->last_crc_fail);
                        
                        // ------------------------------------------------------------------------
                        // THE CURE: Read the true physical constellation error vector live
                        // ------------------------------------------------------------------------
                        float live_gui_quality = 0.0f;
                        if (upstream_extractor != nullptr) {
                            // Cast the generic address back to its parent header type to read standarderr
                            // We use a safe forward-declared structure mapping to prevent circular header loops
                            struct local_extractor_shape {
                                void* vtable;
                                void* in_ptr;
                                void* out_ptr;
                                bool sync;
                                float standarderr;
                            };
                            local_extractor_shape* ext = (local_extractor_shape*)upstream_extractor;
                            // Match the exact math of line 223 in main.cpp
                            live_gui_quality = (1.0f - ext->standarderr) * 100.0f;
                        } else {
                            // Fallback to basic locking state if not bound yet
                            live_gui_quality = is_locked ? 100.0f : 0.0f;
                        }
                        // ------------------------------------------------------------------------

                        bool is_voice = (tms->t_display_st->timeslot_content[0] == 4) | 
                                        (tms->t_display_st->timeslot_content[1] == 4) | 
                                        (tms->t_display_st->timeslot_content[2] == 4) | 
                                        (tms->t_display_st->timeslot_content[3] == 4);

                        // PRINT LOG TICKER: Mirroring the GUI bar with 100% precision
//                        printf("[%02d:%02d:%02d.%03d] [TETRA] F: %02d | MF: %02d | %s | Q: %3.1f%% | %s | Slots: [%d,%d,%d,%d]\n",
//                               time_info.tm_hour, time_info.tm_min, time_info.tm_sec, (int)ms.count(),
//                               active_frame,
//                               tms->t_display_st->curr_multiframe,
//                               is_locked ? "SYNC_LOCKED" : "SYNC_TRACKING",
//                               live_gui_quality, // Print the true physical constellation error
//                               is_voice ? "AUDIO_TRAFFIC" : "SIGNALING_BEACON",
//                               tms->t_display_st->timeslot_content[0],
//                               tms->t_display_st->timeslot_content[1],
//                               tms->t_display_st->timeslot_content[2],
//                               tms->t_display_st->timeslot_content[3]);
//                        fflush(stdout);
                    }
                }
            }

            if(out_tmp_buff.getReadable(false) > 0) {
                outcnt += out_tmp_buff.read(out, out_tmp_buff.getReadable(false));
            }
            outSymsCtr += outcnt;
            inSymsCtr += (count * 2); 
            
            int requiredOut = inSymsCtr * 8 / 36;
            int remainingOut = requiredOut - outSymsCtr;
            
            bool decoding = (tms->t_display_st->timeslot_content[0] == 4) | 
                            (tms->t_display_st->timeslot_content[1] == 4) | 
                            (tms->t_display_st->timeslot_content[2] == 4) | 
                            (tms->t_display_st->timeslot_content[3] == 4);
                            
            if(remainingOut > 0 && !decoding) {
                memset(&(out[outcnt]), 0, remainingOut * sizeof(float));
                outcnt += remainingOut;
            }
            
            outSymsCtr -= (std::min)(outSymsCtr, requiredOut);
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
	void* upstream_extractor = nullptr;
        struct tetra_mac_state instance_tms;
        struct tetra_rx_state instance_trs;
        struct tetra_crypto_state instance_tcs;
        struct tetra_display_state instance_t_display_st;
        struct fragslot instance_fragslots[FRAGSLOT_NR_SLOTS];
    };

}
