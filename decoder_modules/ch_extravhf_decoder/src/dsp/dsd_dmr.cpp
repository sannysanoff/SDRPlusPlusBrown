#include "dsd.h"

namespace dsp { 

    int NewDSD::processDMRdata(int count, const uint8_t* in) {
        int usedDibits = 0;
        if(curr_state == STATE_PROC_FRAME_DMR_DATA) {
            uint8_t cc = 0;
            char bursttype[5];
            bursttype[4] = 0;
            char syncdata[25];
            char sync[25];
            int currentslot;
            dmr_dibitBuffP = fss_dibitBufP - 90;
            // CACH
            for (int i = 0; i < 12; i++) {
                int dibit = dibitBuf[dmr_dibitBuffP++];
                //TODO: use CACH
                if (i == 2) {
                    currentslot = (1 & (dibit >> 1));      // bit 1
                }
            }
            // Data 1 (first half of the BPTC(196,96)-coded payload, used by
            // the Voice LC Header / Terminator LC bursts to carry the
            // talkgroup/source info of the call)
            for (int i = 0; i < 49; i++) {
                int d = dibitBuf[dmr_dibitBuffP++];
                dmrlc_bits[(i * 2)]     = (1 & (d >> 1)); // bit 1
                dmrlc_bits[(i * 2) + 1] = (1 & d);        // bit 0
            }
            dmrBurstSlot = currentslot;
            // Start of the Slot Type field (CC(4) + Data Type(4) + first 2
            // parity bits of its Golay(20,8) codeword). Remembered so the
            // full 20-bit codeword can be re-read and FEC-corrected once its
            // mirrored tail (after the sync field) has streamed in.
            dmrSlotType1Pos = dmr_dibitBuffP;

            int dibit = dibitBuf[dmr_dibitBuffP++];
            cc |= (1 & (dibit >> 1)) << 3; // bit 1 (received first -> CC MSB)
            cc |= (1 & dibit) << 2;        // bit 0

            dibit = dibitBuf[dmr_dibitBuffP++];
            cc |= (1 & (dibit >> 1)) << 1; // bit 1
            cc |= (1 & dibit) << 0;        // bit 0 (received last -> CC LSB)

            ((currentslot == 0) ? dmr_status.dmr_status_s0_cc : dmr_status.dmr_status_s1_cc) = cc;

            dibit = dibitBuf[dmr_dibitBuffP++];
            bursttype[0] = (1 & (dibit >> 1)) + 48;       // bit 1
            bursttype[1] = (1 & dibit) + 48;      // bit 0

            dibit = dibitBuf[dmr_dibitBuffP++];
            bursttype[2] = (1 & (dibit >> 1)) + 48;       // bit 1
            bursttype[3] = (1 & dibit) + 48;      // bit 0

            // parity bit
            dibit = dibitBuf[dmr_dibitBuffP++];

            ((currentslot == 0) ? dmr_status.dmr_status_s0_lastburstt : dmr_status.dmr_status_s1_lastburstt) = strtol(bursttype, NULL, 2);

            if (strcmp (bursttype, "0000") == 0) {
                ((currentslot == 0) ? dmr_status.dmr_status_s0_lasttype : dmr_status.dmr_status_s1_lasttype) = "PI Header";
            } else if (strcmp (bursttype, "0001") == 0) {
                ((currentslot == 0) ? dmr_status.dmr_status_s0_lasttype : dmr_status.dmr_status_s1_lasttype) = "VOICE Header";
            } else if (strcmp (bursttype, "0010") == 0) {
                ((currentslot == 0) ? dmr_status.dmr_status_s0_lasttype : dmr_status.dmr_status_s1_lasttype) = "TLC";
            } else if (strcmp (bursttype, "0011") == 0) {
                ((currentslot == 0) ? dmr_status.dmr_status_s0_lasttype : dmr_status.dmr_status_s1_lasttype) = "CSBK";
            } else if (strcmp (bursttype, "0100") == 0) {
                ((currentslot == 0) ? dmr_status.dmr_status_s0_lasttype : dmr_status.dmr_status_s1_lasttype) = "MBC Header";
            } else if (strcmp (bursttype, "0101") == 0) {
                ((currentslot == 0) ? dmr_status.dmr_status_s0_lasttype : dmr_status.dmr_status_s1_lasttype) = "MBC";
            } else if (strcmp (bursttype, "0110") == 0) {
                ((currentslot == 0) ? dmr_status.dmr_status_s0_lasttype : dmr_status.dmr_status_s1_lasttype) = "DATA Header";
            } else if (strcmp (bursttype, "0111") == 0) {
                ((currentslot == 0) ? dmr_status.dmr_status_s0_lasttype : dmr_status.dmr_status_s1_lasttype) = "RATE 1/2 DATA";
            } else if (strcmp (bursttype, "1000") == 0) {
                ((currentslot == 0) ? dmr_status.dmr_status_s0_lasttype : dmr_status.dmr_status_s1_lasttype) = "RATE 3/4 DATA";
            } else if (strcmp (bursttype, "1001") == 0) {
                ((currentslot == 0) ? dmr_status.dmr_status_s0_lasttype : dmr_status.dmr_status_s1_lasttype) = "Idle";
            } else if (strcmp (bursttype, "1010") == 0) {
                ((currentslot == 0) ? dmr_status.dmr_status_s0_lasttype : dmr_status.dmr_status_s1_lasttype) = "RATE 1 DATA";
            } else {
                ((currentslot == 0) ? dmr_status.dmr_status_s0_lasttype : dmr_status.dmr_status_s1_lasttype) = "UNK";
            }

            // Voice LC Header and Terminator-with-LC carry the talkgroup /
            // source info (Full Link Control) in their Data1+Data2 payload.
            if (strcmp (bursttype, "0001") == 0) {
                dmrlc_pending = true;
                dmrlc_dtype = 1;
                dmrlc_slot = currentslot;
            } else if (strcmp (bursttype, "0010") == 0) {
                dmrlc_pending = true;
                dmrlc_dtype = 2;
                dmrlc_slot = currentslot;
            } else {
                dmrlc_pending = false;
            }

            for (int i = 0; i < 24; i++) {
                dibit = dibitBuf[dmr_dibitBuffP++] | 0b01;
                syncdata[i] = dibit;
                sync[i] = (dibit | 1) + 48;
            }
            sync[24] = 0;
            syncdata[24] = 0;

            // dmr_dibitBuffP now sits right after the sync field, i.e. right
            // before the second Slot Type half + Data2, which haven't
            // streamed in yet. Remember the position so we can pick them up
            // once STATE_PROC_FRAME_DMR_DATA_1 has buffered them.
            dmrlc_afterSyncPos = dmr_dibitBuffP;

            curr_state = STATE_PROC_FRAME_DMR_DATA_1;
            skipCtr = 0;
        } else if(curr_state == STATE_PROC_FRAME_DMR_DATA_1) {
            // current slot second half, cach, next slot 1st half
            for(int i = 0; i < count; i++) {
                if (fss_dibitBufP > 900000) {
                    fss_dibitBufP = 200;
                }
                dibitBuf[fss_dibitBufP++] = in[i];
                usedDibits++;
                skipCtr++;
                if(skipCtr == 120) {
                    // The mirrored tail of the Slot Type field (5 dibits / 10
                    // bits) has now streamed in at dmrlc_afterSyncPos; use it
                    // together with the head captured at dmrSlotType1Pos to
                    // FEC-correct the Colour Code / Data Type with Golay(20,8).
                    processDMRSlotType(dmrBurstSlot);

                    if (dmrlc_pending) {
                        // Skip the 5 remaining dibits (10 bits) of the mirrored
                        // Slot Type field, then read Data2 (49 dibits = 98 bits).
                        for (int k = 0; k < 49; k++) {
                            int d = dibitBuf[dmrlc_afterSyncPos + 5 + k];
                            dmrlc_bits[98 + (k * 2)]     = (1 & (d >> 1)); // bit 1
                            dmrlc_bits[98 + (k * 2) + 1] = (1 & d);       // bit 0
                        }
                        processDMRFullLC(dmrlc_dtype, dmrlc_slot);
                        dmrlc_pending = false;
                    }
                    curr_state = STATE_SYNC;
                    break;
                }
            }
        }
        return usedDibits;
    }

    // ---- DMR Full Link Control decode (BPTC(196,96)) ----
    // Adapted from the publicly documented BPTC(196,96)/Hamming(15,11,3)/
    // Hamming(13,9,3) algorithm used by DMR (ETSI TS 102 361-1), following
    // the same structure as MMDVM's CBPTC19696/CHamming.

    bool NewDSD::dmrHamming15113Decode(bool* d) {
        bool c0 = d[0] ^ d[1] ^ d[2] ^ d[3] ^ d[5] ^ d[7] ^ d[8];
        bool c1 = d[1] ^ d[2] ^ d[3] ^ d[4] ^ d[6] ^ d[8] ^ d[9];
        bool c2 = d[2] ^ d[3] ^ d[4] ^ d[5] ^ d[7] ^ d[9] ^ d[10];
        bool c3 = d[0] ^ d[1] ^ d[2] ^ d[4] ^ d[6] ^ d[7] ^ d[10];

        uint8_t n = 0;
        n |= (c0 != d[11]) ? 0x01 : 0x00;
        n |= (c1 != d[12]) ? 0x02 : 0x00;
        n |= (c2 != d[13]) ? 0x04 : 0x00;
        n |= (c3 != d[14]) ? 0x08 : 0x00;

        switch (n) {
            case 0x01: d[11] = !d[11]; return true;
            case 0x02: d[12] = !d[12]; return true;
            case 0x04: d[13] = !d[13]; return true;
            case 0x08: d[14] = !d[14]; return true;
            case 0x09: d[0]  = !d[0];  return true;
            case 0x0B: d[1]  = !d[1];  return true;
            case 0x0F: d[2]  = !d[2];  return true;
            case 0x07: d[3]  = !d[3];  return true;
            case 0x0E: d[4]  = !d[4];  return true;
            case 0x05: d[5]  = !d[5];  return true;
            case 0x0A: d[6]  = !d[6];  return true;
            case 0x0D: d[7]  = !d[7];  return true;
            case 0x03: d[8]  = !d[8];  return true;
            case 0x06: d[9]  = !d[9];  return true;
            case 0x0C: d[10] = !d[10]; return true;
            default: return false;
        }
    }

    bool NewDSD::dmrHamming1393Decode(bool* d) {
        bool c0 = d[0] ^ d[1] ^ d[3] ^ d[5] ^ d[6];
        bool c1 = d[0] ^ d[1] ^ d[2] ^ d[4] ^ d[6] ^ d[7];
        bool c2 = d[0] ^ d[1] ^ d[2] ^ d[3] ^ d[5] ^ d[7] ^ d[8];
        bool c3 = d[0] ^ d[2] ^ d[4] ^ d[5] ^ d[8];

        uint8_t n = 0;
        n |= (c0 != d[9])  ? 0x01 : 0x00;
        n |= (c1 != d[10]) ? 0x02 : 0x00;
        n |= (c2 != d[11]) ? 0x04 : 0x00;
        n |= (c3 != d[12]) ? 0x08 : 0x00;

        switch (n) {
            case 0x01: d[9]  = !d[9];  return true;
            case 0x02: d[10] = !d[10]; return true;
            case 0x04: d[11] = !d[11]; return true;
            case 0x08: d[12] = !d[12]; return true;
            case 0x0F: d[0] = !d[0]; return true;
            case 0x07: d[1] = !d[1]; return true;
            case 0x0E: d[2] = !d[2]; return true;
            case 0x05: d[3] = !d[3]; return true;
            case 0x0A: d[4] = !d[4]; return true;
            case 0x0D: d[5] = !d[5]; return true;
            case 0x03: d[6] = !d[6]; return true;
            case 0x06: d[7] = !d[7]; return true;
            case 0x0C: d[8] = !d[8]; return true;
            default: return false;
        }
    }

    // Deinterleave the 196 raw bits, run the row/column Hamming error
    // correction, and extract the resulting 96 bits (12 bytes) of payload.
    void NewDSD::dmrBptcDecode(const bool* rawBits, uint8_t* out) {
        bool deInter[196];
        for (int a = 0; a < 196; a++) {
            int interleaveSequence = (a * 181) % 196;
            deInter[a] = rawBits[interleaveSequence];
        }

        bool fixing;
        int count = 0;
        do {
            fixing = false;
            bool col[13];
            for (int c = 0; c < 15; c++) {
                int pos = c + 1;
                for (int a = 0; a < 13; a++) {
                    col[a] = deInter[pos];
                    pos += 15;
                }
                if (dmrHamming1393Decode(col)) {
                    pos = c + 1;
                    for (int a = 0; a < 13; a++) {
                        deInter[pos] = col[a];
                        pos += 15;
                    }
                    fixing = true;
                }
            }
            for (int r = 0; r < 9; r++) {
                int pos = (r * 15) + 1;
                if (dmrHamming15113Decode(deInter + pos)) {
                    fixing = true;
                }
            }
            count++;
        } while (fixing && count < 5);

        bool bData[96];
        int pos = 0;
        static const int ranges[9][2] = {
            {4, 11}, {16, 26}, {31, 41}, {46, 56}, {61, 71}, {76, 86}, {91, 101}, {106, 116}, {121, 131}
        };
        for (int r = 0; r < 9; r++) {
            for (int a = ranges[r][0]; a <= ranges[r][1]; a++, pos++) {
                bData[pos] = deInter[a];
            }
        }
        for (int i = 0; i < 12; i++) {
            uint8_t b = 0;
            for (int bi = 0; bi < 8; bi++) {
                b = (b << 1) | (bData[(i * 8) + bi] ? 1 : 0);
            }
            out[i] = b;
        }
    }

    // Decode a Voice LC Header (dtype==1) or Terminator-with-LC (dtype==2)
    // burst's payload into the talkgroup/private-call and source id fields.
    // NOTE: the RS(12,9) parity carried in lc[9..11] is not checked here, so
    // this is a best-effort decode of the LC content itself - though the
    // Slot Type (Data Type) that got us here *is* now Golay(20,8)-corrected,
    // see processDMRSlotType() below.
    void NewDSD::processDMRFullLC(int dtype, int slot) {
        uint8_t lc[12];
        dmrBptcDecode(dmrlc_bits, lc);

        uint8_t flco = lc[0] & 0x3F;
        bool group = (flco == 0x00); // FLCO Group Voice Channel User
        bool priv  = (flco == 0x03); // FLCO Unit to Unit (private/direct call)
        if (!group && !priv) {
            // Talker alias, GPS info or other LC subtype - not a call setup,
            // leave the previously known talkgroup/source alone.
            return;
        }

        uint32_t dst = ((uint32_t)lc[3] << 16) | ((uint32_t)lc[4] << 8) | lc[5];
        uint32_t src = ((uint32_t)lc[6] << 16) | ((uint32_t)lc[7] << 8) | lc[8];

        if (slot == 0) {
            dmr_status.dmr_status_s0_lc_valid = true;
            dmr_status.dmr_status_s0_group = group;
            dmr_status.dmr_status_s0_tgid = dst;
            dmr_status.dmr_status_s0_srcid = src;
        } else {
            dmr_status.dmr_status_s1_lc_valid = true;
            dmr_status.dmr_status_s1_group = group;
            dmr_status.dmr_status_s1_tgid = dst;
            dmr_status.dmr_status_s1_srcid = src;
        }

        // Log only when the talkgroup/destination actually changes, so this
        // doesn't spam once per Voice Header + once per Terminator of every
        // single call.
        if (dst != dmrLoggedTG[slot]) {
            dmrLoggedTG[slot] = dst;
            if (group) {
                flog::info("DMR slot{} TG {} (src {})", slot, dst, src);
            } else {
                flog::info("DMR slot{} DIRECT -> {} (src {})", slot, dst, src);
            }
        }
    }

    std::string NewDSD::dmrDataTypeName(int dataType) {
        switch (dataType) {
            case 0:  return "PI Header";
            case 1:  return "VOICE Header";
            case 2:  return "TLC";
            case 3:  return "CSBK";
            case 4:  return "MBC Header";
            case 5:  return "MBC";
            case 6:  return "DATA Header";
            case 7:  return "RATE 1/2 DATA";
            case 8:  return "RATE 3/4 DATA";
            case 9:  return "Idle";
            case 10: return "RATE 1 DATA";
            default: return "UNK";
        }
    }

    // Reconstruct the 20-bit Slot Type Golay(20,8,7) codeword from its two
    // halves (10 bits captured right after Data1, 10 bits mirrored right
    // after the sync field) and FEC-correct the Colour Code / Data Type,
    // overwriting the provisional (uncorrected) values read earlier. This
    // is what prevents a single bit error from flipping the reported CC or
    // burst type into a bogus value.
    void NewDSD::processDMRSlotType(int slot) {
        bool stBits[20];
        for (int i = 0; i < 5; i++) {
            int d = dibitBuf[dmrSlotType1Pos + i];
            stBits[i * 2]     = (1 & (d >> 1));
            stBits[i * 2 + 1] = (1 & d);
        }
        for (int i = 0; i < 5; i++) {
            int d = dibitBuf[dmrlc_afterSyncPos + i];
            stBits[10 + (i * 2)]     = (1 & (d >> 1));
            stBits[10 + (i * 2) + 1] = (1 & d);
        }

        uint8_t data[3] = { 0, 0, 0 };
        for (int i = 0; i < 8; i++) {
            data[0] = (uint8_t)((data[0] << 1) | (stBits[i] ? 1 : 0));
        }
        for (int i = 0; i < 8; i++) {
            data[1] = (uint8_t)((data[1] << 1) | (stBits[8 + i] ? 1 : 0));
        }
        // Only the top 3 bits of data[2] participate in the Golay(20,8)
        // check (see Golay2087::decode) - the 20th received bit is unused.
        data[2] = (uint8_t)((stBits[16] << 7) | (stBits[17] << 6) | (stBits[18] << 5));

        uint8_t corrected = Golay2087::decode(data);
        uint8_t cc = corrected >> 4;
        uint8_t dataType = corrected & 0x0F;

        if (slot == 0) {
            dmr_status.dmr_status_s0_cc = cc;
            dmr_status.dmr_status_s0_lastburstt = dataType;
            dmr_status.dmr_status_s0_lasttype = dmrDataTypeName(dataType);
        } else {
            dmr_status.dmr_status_s1_cc = cc;
            dmr_status.dmr_status_s1_lastburstt = dataType;
            dmr_status.dmr_status_s1_lasttype = dmrDataTypeName(dataType);
        }

        // Log only on change, not once per burst (a burst comes in every
        // ~30ms per slot even when idle).
        if (cc != dmrLoggedCC[slot]) {
            dmrLoggedCC[slot] = cc;
            flog::info("DMR slot{} CC {}", slot, cc);
        }
    }

    const int NewDSD::dmrv_const_rW[] = {
        0, 1, 0, 1, 0, 1,
        0, 1, 0, 1, 0, 1,
        0, 1, 0, 1, 0, 1,
        0, 1, 0, 1, 0, 2,
        0, 2, 0, 2, 0, 2,
        0, 2, 0, 2, 0, 2
    };

    const int NewDSD::dmrv_const_rX[] = {
       23, 10, 22, 9, 21, 8,
       20, 7, 19, 6, 18, 5,
       17, 4, 16, 3, 15, 2,
       14, 1, 13, 0, 12, 10,
       11, 9, 10, 8, 9, 7,
       8, 6, 7, 5, 6, 4
    };

    const int NewDSD::dmrv_const_rY[] = {
       0, 2, 0, 2, 0, 2,
       0, 2, 0, 3, 0, 3,
       1, 3, 1, 3, 1, 3,
       1, 3, 1, 3, 1, 3,
       1, 3, 1, 3, 1, 3,
       1, 3, 1, 3, 1, 3
    };

    const int NewDSD::dmrv_const_rZ[] = {
        5, 3, 4, 2, 3, 1,
        2, 0, 1, 13, 0, 12,
        22, 11, 21, 10, 20, 9,
        19, 8, 18, 7, 17, 6,
        16, 5, 15, 4, 14, 3,
        13, 2, 12, 1, 11, 0
    };

    int NewDSD::processDMRvoice(int count, const uint8_t* in, short* out, int* outcnt) {
        int usedDibits = 0;
        int dibit;
        if(curr_state == STATE_PROC_FRAME_DMR_VOICE) {
            dmr_dibitBuffP = fss_dibitBufP - 144;
            dmrv_iter = 0;
            dmrv_ctr = 0;
            dmrv_muteslot = false;
            mbe_status.mbe_status_decoding = true;
            mbe_status.mbe_status_errorbar = "";
            curr_state = STATE_PROC_FRAME_DMR_VOICE_1;
        } else if(curr_state == STATE_PROC_FRAME_DMR_VOICE_1) {
            // 2nd half of previous slot
            for(int i = 0; i < count; i++) {
                if (dmrv_iter > 0) {
                    dibit = in[i];
                    usedDibits++;
                } else {
                    dibit = dibitBuf[dmr_dibitBuffP++];
                }
                dmrv_ctr++;
                if(dmrv_ctr == 54) {
                    dmrv_ctr = 0;
                    curr_state = STATE_PROC_FRAME_DMR_VOICE_2;
                    break;
                }
            }
        } else if(curr_state == STATE_PROC_FRAME_DMR_VOICE_2) {
            for(int i = 0; i < count; i++) {
                // CACH
                if (dmrv_iter > 0) {
                    dibit = in[i];
                    usedDibits++;
                } else {
                    dibit = dibitBuf[dmr_dibitBuffP++];
                }
                //TODO: use CACH
                if (dmrv_ctr == 2) {
                    int currentslot = (1 & (dibit >> 1));  // bit 1
                    dmr_lastslot = currentslot;
                }
                dmrv_ctr++;
                if(dmrv_ctr == 12) {
                    dmrv_ctr = 0;
                    curr_state = STATE_PROC_FRAME_DMR_VOICE_3;
                    dmrv_w_ctr = 0;
                    dmrv_x_ctr = 0;
                    dmrv_y_ctr = 0;
                    dmrv_z_ctr = 0;
                    break;
                }
            }
        } else if(curr_state == STATE_PROC_FRAME_DMR_VOICE_3) {
            // current slot frame 1
            for(int i = 0; i < count; i++) {
                if (dmrv_iter > 0) {
                    dibit = in[i];
                    usedDibits++;
                } else {
                    dibit = dibitBuf[dmr_dibitBuffP++];
                }
                dmrv_ambe_fr[dmrv_const_rW[dmrv_w_ctr]][dmrv_const_rX[dmrv_x_ctr]] = (1 & (dibit >> 1)); // bit 1
                dmrv_ambe_fr[dmrv_const_rY[dmrv_y_ctr]][dmrv_const_rZ[dmrv_z_ctr]] = (1 & dibit);        // bit 0
                dmrv_w_ctr++;
                dmrv_x_ctr++;
                dmrv_y_ctr++;
                dmrv_z_ctr++;
                dmrv_ctr++;
                if(dmrv_ctr == 36) {
                    dmrv_ctr = 0;
                    curr_state = STATE_PROC_FRAME_DMR_VOICE_4;
                    dmrv_w_ctr = 0;
                    dmrv_x_ctr = 0;
                    dmrv_y_ctr = 0;
                    dmrv_z_ctr = 0;
                    break;
                }
            }
        } else if(curr_state == STATE_PROC_FRAME_DMR_VOICE_4) {
            // current slot frame 2 first half
            for(int i = 0; i < count; i++) {
                if (dmrv_iter > 0) {
                    dibit = in[i];
                    usedDibits++;
                } else {
                    dibit = dibitBuf[dmr_dibitBuffP++];
                }
                dmrv_ambe_fr2[dmrv_const_rW[dmrv_w_ctr]][dmrv_const_rX[dmrv_x_ctr]] = (1 & (dibit >> 1)); // bit 1
                dmrv_ambe_fr2[dmrv_const_rY[dmrv_y_ctr]][dmrv_const_rZ[dmrv_z_ctr]] = (1 & dibit);        // bit 0
                dmrv_w_ctr++;
                dmrv_x_ctr++;
                dmrv_y_ctr++;
                dmrv_z_ctr++;
                dmrv_ctr++;
                if(dmrv_ctr == 18) {
                    dmrv_ctr = 0;
                    curr_state = STATE_PROC_FRAME_DMR_VOICE_5;
                    break;
                }
            }
        } else if(curr_state == STATE_PROC_FRAME_DMR_VOICE_5) {
            // signaling data or sync
            for(int i = 0; i < count; i++) {
                if (dmrv_iter > 0) {
                    dibit = in[i];
                    usedDibits++;
                } else {
                    dibit = dibitBuf[dmr_dibitBuffP++];
                }
                //TODO: use signaling data
                dmrv_sync[i] = (dibit | 1) + 48;
                dmrv_ctr++;
                if(dmrv_ctr == 24) {
                    dmrv_ctr = 0;
                    dmrv_sync[24] = 0;
                    dmrv_syncdata[24] = 0;
                    if ((strcmp (dmrv_sync, DMR_BS_DATA_SYNC) == 0) || (strcmp (dmrv_sync, DMR_MS_DATA_SYNC) == 0) || (strcmp (dmrv_sync, DMR_DIRECT_MODE_TS1_DATA_SYNC) == 0) || (strcmp (dmrv_sync, DMR_DIRECT_MODE_TS2_DATA_SYNC) == 0)) {
                        dmrv_muteslot = true;
                    } else if ((strcmp (dmrv_sync, DMR_BS_VOICE_SYNC) == 0) || (strcmp (dmrv_sync, DMR_MS_VOICE_SYNC) == 0) || (strcmp (dmrv_sync, DMR_DIRECT_MODE_TS1_VOICE_SYNC) == 0) || (strcmp (dmrv_sync, DMR_DIRECT_MODE_TS2_VOICE_SYNC) == 0)) {
                        dmrv_muteslot = false;
                    }
                    if ((strcmp (dmrv_sync, DMR_MS_VOICE_SYNC) == 0) || (strcmp (dmrv_sync, DMR_MS_DATA_SYNC) == 0)) {
                        //msMode is on, but i'm not sure it's required
                    }
                    if(dmrv_iter == 0) {
                        ((dmr_lastslot == 0) ? dmr_status.dmr_status_s0_lasttype : dmr_status.dmr_status_s1_lasttype) = "VOICE";
                    }
                    curr_state = STATE_PROC_FRAME_DMR_VOICE_6;
                    break;
                }
            }
        } else if(curr_state == STATE_PROC_FRAME_DMR_VOICE_6) {
            // current slot frame 2 second half
            for(int i = 0; i < count; i++) {
                dibit = in[i];
                usedDibits++;
                dmrv_ambe_fr2[dmrv_const_rW[dmrv_w_ctr]][dmrv_const_rX[dmrv_x_ctr]] = (1 & (dibit >> 1)); // bit 1
                dmrv_ambe_fr2[dmrv_const_rY[dmrv_y_ctr]][dmrv_const_rZ[dmrv_z_ctr]] = (1 & dibit);        // bit 0
                dmrv_w_ctr++;
                dmrv_x_ctr++;
                dmrv_y_ctr++;
                dmrv_z_ctr++;
                dmrv_ctr++;
                if(dmrv_ctr == 18) {
                    dmrv_ctr = 0;
                    if (!dmrv_muteslot) {
                        int processed = 0;
                        processMbeFrame (NULL, dmrv_ambe_fr, NULL, out, &processed);
                        processMbeFrame (NULL, dmrv_ambe_fr2, NULL, &(out[processed]), &processed);
                        *outcnt += processed;
                    }
                    dmrv_w_ctr = 0;
                    dmrv_x_ctr = 0;
                    dmrv_y_ctr = 0;
                    dmrv_z_ctr = 0;
                    curr_state = STATE_PROC_FRAME_DMR_VOICE_7;
                    break;
                }
            }
        } else if(curr_state == STATE_PROC_FRAME_DMR_VOICE_7) {
            // current slot frame 3
            for(int i = 0; i < count; i++) {
                dibit = in[i];
                usedDibits++;
                dmrv_ambe_fr3[dmrv_const_rW[dmrv_w_ctr]][dmrv_const_rX[dmrv_x_ctr]] = (1 & (dibit >> 1)); // bit 1
                dmrv_ambe_fr3[dmrv_const_rY[dmrv_y_ctr]][dmrv_const_rZ[dmrv_z_ctr]] = (1 & dibit);        // bit 0
                dmrv_w_ctr++;
                dmrv_x_ctr++;
                dmrv_y_ctr++;
                dmrv_z_ctr++;
                dmrv_ctr++;
                if(dmrv_ctr == 36) {
                    dmrv_ctr = 0;
                    if (!dmrv_muteslot) {
                        processMbeFrame (NULL, dmrv_ambe_fr3, NULL, out, outcnt);
                    }
                    curr_state = STATE_PROC_FRAME_DMR_VOICE_8;
                    break;
                }
            }
        } else if(curr_state == STATE_PROC_FRAME_DMR_VOICE_8) {
            // CACH + next slot + signaling data or sync
            for(int i = 0; i < count; i++) {
                dibit = in[i];
                usedDibits++;
                //TODO: use CACH
                dmrv_ctr++;
                if(dmrv_ctr == 12 + 54 + 24) {
                    dmrv_ctr = 0;
                    curr_state = STATE_PROC_FRAME_DMR_VOICE_9;
                    break;
                }
            }
        } else if(curr_state == STATE_PROC_FRAME_DMR_VOICE_9) {
            if (dmrv_iter == 5) {
                // 2nd half next slot + CACH + first half current slot
                for(int i = 0; i < count; i++) {
                    dibit = in[i];
                    usedDibits++;
                    dmrv_ctr++;
                    if(dmrv_ctr == 54 + 12 + 54) {
                        dmrv_ctr = 0;
                        curr_state = STATE_PROC_FRAME_DMR_VOICE_10;
                        break;
                    }
                }
            } else {
                curr_state = STATE_PROC_FRAME_DMR_VOICE_10;
            }
        } else if(curr_state == STATE_PROC_FRAME_DMR_VOICE_10) {
            dmrv_iter++;
            if(dmrv_iter >= 6) {
                mbe_status.mbe_status_decoding = false;
                curr_state = STATE_SYNC;
            } else {
                curr_state = STATE_PROC_FRAME_DMR_VOICE_1;
            }
        }
        return usedDibits;
    }









    void DSD::processDMRdata () {
        int i, dibit;
        int *dibit_p;
        char sync[25];
        char syncdata[25];
        char cachdata[13];
        char cc[5];
        char bursttype[5];

        cc[4] = 0;
        bursttype[4] = 0;

        dibit_p = dibitBufP - 90;

        // CACH
        for (i = 0; i < 12; i++) {
            dibit = *dibit_p;
            dibit_p++;
            if (invertedDmr == 1) {
                dibit = (dibit ^ 2);
            }
            cachdata[i] = dibit;
            if (i == 2) {
                currentslot = (1 & (dibit >> 1));      // bit 1
                if (currentslot == 0) {
                    slot0light[0] = '[';
                    slot0light[6] = ']';
                    slot1light[0] = ' ';
                    slot1light[6] = ' ';
                } else {
                    slot1light[0] = '[';
                    slot1light[6] = ']';
                    slot0light[0] = ' ';
                    slot0light[6] = ' ';
                }
            }
        }
        cachdata[12] = 0;

        // current slot
        dibit_p += 49;

        // slot type
        dibit = *dibit_p;
        dibit_p++;
        if (invertedDmr == 1) {
            dibit = (dibit ^ 2);
        }
        cc[0] = (1 & (dibit >> 1)) + 48;      // bit 1
        cc[1] = (1 & dibit) + 48;     // bit 0

        dibit = *dibit_p;
        dibit_p++;
        if (invertedDmr == 1) {
            dibit = (dibit ^ 2);
        }
        cc[2] = (1 & (dibit >> 1)) + 48;      // bit 1
        cc[3] = (1 & dibit) + 48;     // bit 0

        dibit = *dibit_p;
        dibit_p++;
        if (invertedDmr == 1) {
            dibit = (dibit ^ 2);
        }
        bursttype[0] = (1 & (dibit >> 1)) + 48;       // bit 1
        bursttype[1] = (1 & dibit) + 48;      // bit 0

        dibit = *dibit_p;
        dibit_p++;
        if (invertedDmr == 1) {
            dibit = (dibit ^ 2);
        }
        bursttype[2] = (1 & (dibit >> 1)) + 48;       // bit 1
        bursttype[3] = (1 & dibit) + 48;      // bit 0

        // parity bit
        dibit = *dibit_p;
        dibit_p++;

        if (strcmp (bursttype, "0000") == 0) {
            sprintf(fsubtype, " PI Header    ");
        } else if (strcmp (bursttype, "0001") == 0) {
            sprintf(fsubtype, " VOICE Header ");
        } else if (strcmp (bursttype, "0010") == 0) {
            sprintf(fsubtype, " TLC          ");
        } else if (strcmp (bursttype, "0011") == 0) {
            sprintf(fsubtype, " CSBK         ");
        } else if (strcmp (bursttype, "0100") == 0) {
            sprintf(fsubtype, " MBC Header   ");
        } else if (strcmp (bursttype, "0101") == 0) {
            sprintf(fsubtype, " MBC          ");
        } else if (strcmp (bursttype, "0110") == 0) {
            sprintf(fsubtype, " DATA Header  ");
        } else if (strcmp (bursttype, "0111") == 0) {
            sprintf(fsubtype, " RATE 1/2 DATA");
        } else if (strcmp (bursttype, "1000") == 0) {
            sprintf(fsubtype, " RATE 3/4 DATA");
        } else if (strcmp (bursttype, "1001") == 0) {
            sprintf(fsubtype, " Slot idle    ");
        } else if (strcmp (bursttype, "1010") == 0) {
            sprintf(fsubtype, " Rate 1 DATA  ");
        } else {
            sprintf(fsubtype, "              ");
        }

        // signaling data or sync
        for (i = 0; i < 24; i++) {
            dibit = *dibit_p;
            dibit_p++;
            if (invertedDmr == 1) {
                dibit = (dibit ^ 2);
            }
            syncdata[i] = dibit;
            sync[i] = (dibit | 1) + 48;
        }
        sync[24] = 0;
        syncdata[24] = 0;

        if ((strcmp (sync, DMR_BS_DATA_SYNC) == 0) || (strcmp (sync, DMR_MS_DATA_SYNC) == 0) || (strcmp (sync, DMR_DIRECT_MODE_TS1_DATA_SYNC) == 0) || (strcmp (sync, DMR_DIRECT_MODE_TS2_DATA_SYNC) == 0)) {
            if (currentslot == 0) {
                sprintf(slot0light, "[slot0]");
            } else {
                sprintf(slot1light, "[slot1]");
            }
        }

        if (errorbars == 1) {
//            printf ("%s %s ", slot0light, slot1light);
            if(currentslot == 0) {
                status_last_dmr_slot0_burst = fsubtype;
            } else {
                status_last_dmr_slot1_burst = fsubtype;
            }

        }

        // current slot second half, cach, next slot 1st half
        skipDibit (120);

        if (errorbars == 1) {
            if (strcmp (fsubtype, "              ") == 0) {
//                printf (" Unknown burst type: %s\n", bursttype);
            } else {
//                printf ("%s\n", fsubtype);
            }
        }
    }

    void DSD::processDMRvoice () {
        // extracts AMBE frames from DMR frame
        int i, j, dibit;
        int *dibit_p;
        char ambe_fr[4][24];
        char ambe_fr2[4][24];
        char ambe_fr3[4][24];
        const int *w, *x, *y, *z;
        char sync[25];
        char syncdata[25];
        char cachdata[13];
        int mutecurrentslot;
        int msMode;

        mutecurrentslot = 0;
        msMode = 0;

        dibit_p = dibitBufP - 144;
        for (j = 0; j < 6; j++) {
            // 2nd half of previous slot
            for (i = 0; i < 54; i++) {
                if (j > 0) {
                    dibit = getDibit ();
                } else {
                    dibit = *dibit_p;
                    dibit_p++;
                    if (invertedDmr == 1) {
                        dibit = (dibit ^ 2);
                    }
                }
            }

            // CACH
            for (i = 0; i < 12; i++) {
                if (j > 0) {
                    dibit = getDibit ();
                } else {
                    dibit = *dibit_p;
                    dibit_p++;
                    if (invertedDmr == 1) {
                        dibit = (dibit ^ 2);
                    }
                }
                cachdata[i] = dibit;
                if (i == 2) {
                    currentslot = (1 & (dibit >> 1));  // bit 1
                    if (currentslot == 0) {
                        slot0light[0] = '[';
                        slot0light[6] = ']';
                        slot1light[0] = ' ';
                        slot1light[6] = ' ';
                    } else {
                        slot1light[0] = '[';
                        slot1light[6] = ']';
                        slot0light[0] = ' ';
                        slot0light[6] = ' ';
                    }
                }
            }
            cachdata[12] = 0;

            // current slot frame 1
            w = rW;
            x = rX;
            y = rY;
            z = rZ;
            for (i = 0; i < 36; i++) {
                if (j > 0) {
                    dibit = getDibit ();
                } else {
                    dibit = *dibit_p;
                    dibit_p++;
                    if (invertedDmr == 1) {
                        dibit = (dibit ^ 2);
                    }
                }
                ambe_fr[*w][*x] = (1 & (dibit >> 1)); // bit 1
                ambe_fr[*y][*z] = (1 & dibit);        // bit 0
                w++;
                x++;
                y++;
                z++;
            }

            // current slot frame 2 first half
            w = rW;
            x = rX;
            y = rY;
            z = rZ;
            for (i = 0; i < 18; i++) {
                if (j > 0) {
                    dibit = getDibit ();
                } else {
                    dibit = *dibit_p;
                    dibit_p++;
                    if (invertedDmr == 1) {
                        dibit = (dibit ^ 2);
                    }
                }
                ambe_fr2[*w][*x] = (1 & (dibit >> 1));        // bit 1
                ambe_fr2[*y][*z] = (1 & dibit);       // bit 0
                w++;
                x++;
                y++;
                z++;
            }

            // signaling data or sync
            for (i = 0; i < 24; i++) {
                if (j > 0) {
                    dibit = getDibit ();
                } else {
                    dibit = *dibit_p;
                    dibit_p++;
                    if (invertedDmr == 1) {
                        dibit = (dibit ^ 2);
                    }
                }
                syncdata[i] = dibit;
                sync[i] = (dibit | 1) + 48;
            }
            sync[24] = 0;
            syncdata[24] = 0;

            if ((strcmp (sync, DMR_BS_DATA_SYNC) == 0) || (strcmp (sync, DMR_MS_DATA_SYNC) == 0) || (strcmp (sync, DMR_DIRECT_MODE_TS1_DATA_SYNC) == 0) || (strcmp (sync, DMR_DIRECT_MODE_TS2_DATA_SYNC) == 0)) {
                mutecurrentslot = 1;
                if (currentslot == 0) {
                    sprintf(slot0light, "[slot0]");
                } else {
                    sprintf(slot1light, "[slot1]");
                }
            } else if ((strcmp (sync, DMR_BS_VOICE_SYNC) == 0) || (strcmp (sync, DMR_MS_VOICE_SYNC) == 0) || (strcmp (sync, DMR_DIRECT_MODE_TS1_VOICE_SYNC) == 0) || (strcmp (sync, DMR_DIRECT_MODE_TS2_VOICE_SYNC) == 0)) {
                mutecurrentslot = 0;
                if (currentslot == 0) {
                    sprintf(slot0light, "[SLOT0]");
                } else {
                    sprintf(slot1light, "[SLOT1]");
                }
            }
            if ((strcmp (sync, DMR_MS_VOICE_SYNC) == 0) || (strcmp (sync, DMR_MS_DATA_SYNC) == 0)) {
                msMode = 1;
            }

            if ((j == 0) && (errorbars == 1)) {
//                printf ("%s %s  VOICE e:", slot0light, slot1light);
                status_mbedecoding = true;
                status_errorbar = "";
                if(currentslot == 0) {
                    status_last_dmr_slot0_burst = "VOICE";
                } else {
                    status_last_dmr_slot1_burst = "VOICE";
                }
            }

            // current slot frame 2 second half
            for (i = 0; i < 18; i++) {
                dibit = getDibit ();
                ambe_fr2[*w][*x] = (1 & (dibit >> 1));        // bit 1
                ambe_fr2[*y][*z] = (1 & dibit);       // bit 0
                w++;
                x++;
                y++;
                z++;
            }

            if (mutecurrentslot == 0) {
                if (firstframe == 1) {
                    // we don't know if anything received before the first sync after no carrier is valid
                    firstframe = 0;
                } else {
                    processMbeFrame (NULL, ambe_fr, NULL);
                    processMbeFrame (NULL, ambe_fr2, NULL);
                }
            }

            // current slot frame 3
            w = rW;
            x = rX;
            y = rY;
            z = rZ;
            for (i = 0; i < 36; i++) {
                dibit = getDibit ();
                ambe_fr3[*w][*x] = (1 & (dibit >> 1));        // bit 1
                ambe_fr3[*y][*z] = (1 & dibit);       // bit 0
                w++;
                x++;
                y++;
                z++;
            }
            if (mutecurrentslot == 0) {
                processMbeFrame (NULL, ambe_fr3, NULL);
            }

            // CACH
            for (i = 0; i < 12; i++) {
                dibit = getDibit ();
                cachdata[i] = dibit;
            }
            cachdata[12] = 0;

            // next slot
            skipDibit (54);

            // signaling data or sync
            for (i = 0; i < 24; i++) {
                dibit = getDibit ();
                syncdata[i] = dibit;
                sync[i] = (dibit | 1) + 48;
            }
            sync[24] = 0;
            syncdata[24] = 0;

            if ((strcmp (sync, DMR_BS_DATA_SYNC) == 0) || (msMode == 1)) {
                if (currentslot == 0) {
                    sprintf(slot1light, " slot1 ");
                } else {
                    sprintf(slot0light, " slot0 ");
                }
            } else if (strcmp (sync, DMR_BS_VOICE_SYNC) == 0) {
                if (currentslot == 0) {
                    sprintf(slot1light, " SLOT1 ");
                } else {
                    sprintf(slot0light, " SLOT0 ");
                }
            }

            if (j == 5) {
                // 2nd half next slot
                skipDibit (54);

                // CACH
                skipDibit (12);

                // first half current slot
                skipDibit (54);
            }
        }

        if (errorbars == 1) {
            status_mbedecoding = false;
//            printf ("\n");
        }
    }
};
