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
            dmr_dibitBuffP += 49;
            int dibit = dibitBuf[dmr_dibitBuffP++];
            cc |= ((1 & (dibit >> 1))) << 0; //bit1
            cc |= (1 & dibit) << 1;     // bit 0

            dibit = dibitBuf[dmr_dibitBuffP++];
            cc |= (1 & (dibit >> 1)) << 2;      // bit 1
            cc |= (1 & dibit) << 3;     // bit 0

            dmr_status.dmr_status_cc = cc;

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

            for (int i = 0; i < 24; i++) {
                dibit = dibitBuf[dmr_dibitBuffP++] | 0b01;
                syncdata[i] = dibit;
                sync[i] = (dibit | 1) + 48;
            }
            sync[24] = 0;
            syncdata[24] = 0;

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
                    curr_state = STATE_SYNC;
                    break;
                }
            }
        }
        return usedDibits;
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
                    if ((strcmp (dmrv_sync, DMR_BS_DATA_SYNC) == 0) || (strcmp (dmrv_sync, DMR_MS_DATA_SYNC) == 0)) {
                        dmrv_muteslot = true;
                    } else if ((strcmp (dmrv_sync, DMR_BS_VOICE_SYNC) == 0) || (strcmp (dmrv_sync, DMR_MS_VOICE_SYNC) == 0)) {
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

        if ((strcmp (sync, DMR_BS_DATA_SYNC) == 0) || (strcmp (sync, DMR_MS_DATA_SYNC) == 0)) {
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

            if ((strcmp (sync, DMR_BS_DATA_SYNC) == 0) || (strcmp (sync, DMR_MS_DATA_SYNC) == 0)) {
                mutecurrentslot = 1;
                if (currentslot == 0) {
                    sprintf(slot0light, "[slot0]");
                } else {
                    sprintf(slot1light, "[slot1]");
                }
            } else if ((strcmp (sync, DMR_BS_VOICE_SYNC) == 0) || (strcmp (sync, DMR_MS_VOICE_SYNC) == 0)) {
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
