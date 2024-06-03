#include "dsd.h"

namespace dsp {
    void DSD::processX2TDMAdata () {
        int i, dibit;
        int *dibit_p;
        char sync[25];
        char syncdata[25];
        char cachdata[13];
        char cc[4];
        int aiei;
        char bursttype[5];

        cc[3] = 0;
        bursttype[4] = 0;

        dibit_p = dibitBufP - 90;

        // CACH
        for (i = 0; i < 12; i++) {
            dibit = *dibit_p;
            dibit_p++;
            if (invertedX2tdma == 1) {
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
        if (invertedX2tdma == 1) {
            dibit = (dibit ^ 2);
        }
        cc[0] = (1 & (dibit >> 1)) + 48;      // bit 1
        cc[1] = (1 & dibit) + 48;     // bit 0

        dibit = *dibit_p;
        dibit_p++;
        if (invertedX2tdma == 1) {
            dibit = (dibit ^ 2);
        }
        cc[2] = (1 & (dibit >> 1)) + 48;      // bit 1
        aiei = (1 & dibit);           // bit 0

        dibit = *dibit_p;
        dibit_p++;
        if (invertedX2tdma == 1) {
            dibit = (dibit ^ 2);
        }
        bursttype[0] = (1 & (dibit >> 1)) + 48;       // bit 1
        bursttype[1] = (1 & dibit) + 48;      // bit 0

        dibit = *dibit_p;
        dibit_p++;
        if (invertedX2tdma == 1) {
            dibit = (dibit ^ 2);
        }
        bursttype[2] = (1 & (dibit >> 1)) + 48;       // bit 1
        bursttype[3] = (1 & dibit) + 48;      // bit 0

        // parity bit
        dibit_p++;

        if (strcmp (bursttype, "0000") == 0) {
            sprintf (fsubtype, " PI Header    ");
        } else if (strcmp (bursttype, "0001") == 0) {
            sprintf (fsubtype, " VOICE Header ");
        } else if (strcmp (bursttype, "0010") == 0) {
            sprintf (fsubtype, " TLC          ");
        } else if (strcmp (bursttype, "0011") == 0) {
            sprintf (fsubtype, " CSBK         ");
        } else if (strcmp (bursttype, "0100") == 0) {
            sprintf (fsubtype, " MBC Header   ");
        } else if (strcmp (bursttype, "0101") == 0) {
            sprintf (fsubtype, " MBC          ");
        } else if (strcmp (bursttype, "0110") == 0) {
            sprintf (fsubtype, " DATA Header  ");
        } else if (strcmp (bursttype, "0111") == 0) {
            sprintf (fsubtype, " RATE 1/2 DATA");
        } else if (strcmp (bursttype, "1000") == 0) {
            sprintf (fsubtype, " RATE 3/4 DATA");
        } else if (strcmp (bursttype, "1001") == 0) {
            sprintf (fsubtype, " Slot idle    ");
        } else if (strcmp (bursttype, "1010") == 0) {
            sprintf (fsubtype, " Rate 1 DATA  ");
        } else {
            sprintf (fsubtype, "              ");
        }

        // signaling data or sync
        for (i = 0; i < 24; i++) {
            dibit = *dibit_p;
            dibit_p++;
            if (invertedX2tdma == 1) {
                dibit = (dibit ^ 2);
            }
            syncdata[i] = dibit;
            sync[i] = (dibit | 1) + 48;
        }
        sync[24] = 0;
        syncdata[24] = 0;

        if ((strcmp (sync, X2TDMA_BS_DATA_SYNC) == 0) || (strcmp (sync, X2TDMA_BS_DATA_SYNC) == 0)) {
            if (currentslot == 0) {
                sprintf (slot0light, "[slot0]");
            } else {
                sprintf (slot1light, "[slot1]");
            }
        }

        if (errorbars == 1) {
//            printf ("%s %s ", slot0light, slot1light);
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

    void DSD::processX2TDMAvoice () {
        // extracts AMBE frames from X2TDMA frame
        int i, j, dibit;
        int *dibit_p;
        char ambe_fr[4][24];
        char ambe_fr2[4][24];
        char ambe_fr3[4][24];
        const int *w, *x, *y, *z;
        char sync[25];
        char syncdata[25];
        char lcformat[9], mfid[9], lcinfo[57];
        char cachdata[13];
        char parity;
        int eeei, aiei;
        char mi[73];
        int burstd;
        int mutecurrentslot;
        int algidhex, kidhex;
        int msMode;

        lcformat[8] = 0;
        mfid[8] = 0;
        lcinfo[56] = 0;
        sprintf (mi, "________________________________________________________________________");
        eeei = 0;
        aiei = 0;
        burstd = 0;
        mutecurrentslot = 0;
        msMode = 0;

        dibit_p = dibitBufP - 144;
        for (j = 0; j < 6; j++)  {
            // 2nd half of previous slot
            for (i = 0; i < 54; i++) {
                if (j > 0) {
                    dibit = getDibit ();
                } else {
                    dibit = *dibit_p;
                    dibit_p++;
                    if (invertedX2tdma == 1) {
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
                    if (invertedX2tdma == 1) {
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
            w = aW;
            x = aX;
            y = aY;
            z = aZ;
            for (i = 0; i < 36; i++) {
                if (j > 0) {
                    dibit = getDibit ();
                } else {
                    dibit = *dibit_p;
                    dibit_p++;
                    if (invertedX2tdma == 1) {
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
            w = aW;
            x = aX;
            y = aY;
            z = aZ;
            for (i = 0; i < 18; i++) {
                if (j > 0) {
                    dibit = getDibit ();
                 } else {
                    dibit = *dibit_p;
                    dibit_p++;
                    if (invertedX2tdma == 1) {
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
                    if (invertedX2tdma == 1) {
                        dibit = (dibit ^ 2);
                    }
                }
                syncdata[i] = dibit;
                sync[i] = (dibit | 1) + 48;
            }
            sync[24] = 0;
            syncdata[24] = 0;

            if ((strcmp (sync, X2TDMA_BS_DATA_SYNC) == 0) || (strcmp (sync, X2TDMA_MS_DATA_SYNC) == 0)) {
                mutecurrentslot = 1;
                if (currentslot == 0) {
                    sprintf (slot0light, "[slot0]");
                } else {
                    sprintf (slot1light, "[slot1]");
                }
            } else if ((strcmp (sync, X2TDMA_BS_VOICE_SYNC) == 0) || (strcmp (sync, X2TDMA_MS_VOICE_SYNC) == 0)) {
                mutecurrentslot = 0;
                if (currentslot == 0) {
                    sprintf (slot0light, "[SLOT0]");
                } else {
                    sprintf (slot1light, "[SLOT1]");
                }
            }

            if ((strcmp (sync, X2TDMA_MS_VOICE_SYNC) == 0) || (strcmp (sync, X2TDMA_MS_DATA_SYNC) == 0)) {
                msMode = 1;
            }

            if ((j == 0) && (errorbars == 1)) {
//                printf ("%s %s  VOICE e:", slot0light, slot1light);
                status_mbedecoding = true;
                status_errorbar = "";
            }

            if (j == 1) {
                eeei = (1 & syncdata[1]);     // bit 0
                aiei = (1 & (syncdata[2] >> 1));      // bit 1

                if ((eeei == 0) && (aiei == 0)) {
                    lcformat[0] = (1 & (syncdata[4] >> 1)) + 48;      // bit 1
                    mfid[3] = (1 & syncdata[4]) + 48; // bit 0
                    lcinfo[6] = (1 & (syncdata[5] >> 1)) + 48;        // bit 1
                    lcinfo[16] = (1 & syncdata[5]) + 48;      // bit 0
                    lcinfo[26] = (1 & (syncdata[6] >> 1)) + 48;       // bit 1
                    lcinfo[36] = (1 & syncdata[6]) + 48;      // bit 0
                    lcinfo[46] = (1 & (syncdata[7] >> 1)) + 48;       // bit 1
                    parity = (1 & syncdata[7]) + 48;  // bit 0
                    lcformat[1] = (1 & (syncdata[8] >> 1)) + 48;      // bit 1
                    mfid[4] = (1 & syncdata[8]) + 48; // bit 0
                    lcinfo[7] = (1 & (syncdata[9] >> 1)) + 48;        // bit 1
                    lcinfo[17] = (1 & syncdata[9]) + 48;      // bit 0
                    lcinfo[27] = (1 & (syncdata[10] >> 1)) + 48;      // bit 1
                    lcinfo[37] = (1 & syncdata[10]) + 48;     // bit 0
                    lcinfo[47] = (1 & (syncdata[11] >> 1)) + 48;      // bit 1
                    parity = (1 & syncdata[11]) + 48; // bit 0
                    lcformat[2] = (1 & (syncdata[12] >> 1)) + 48;     // bit 1
                    mfid[5] = (1 & syncdata[12]) + 48;        // bit 0
                    lcinfo[8] = (1 & (syncdata[13] >> 1)) + 48;       // bit 1
                    lcinfo[18] = (1 & syncdata[13]) + 48;     // bit 0
                    lcinfo[28] = (1 & (syncdata[14] >> 1)) + 48;      // bit 1
                    lcinfo[38] = (1 & syncdata[14]) + 48;     // bit 0
                    lcinfo[48] = (1 & (syncdata[15] >> 1)) + 48;      // bit 1
                    parity = (1 & syncdata[15]) + 48; // bit 0
                    lcformat[3] = (1 & (syncdata[16] >> 1)) + 48;     // bit 1
                    mfid[6] = (1 & syncdata[16]) + 48;        // bit 0
                    lcinfo[9] = (1 & (syncdata[17] >> 1)) + 48;       // bit 1
                    lcinfo[19] = (1 & syncdata[17]) + 48;     // bit 0
                    lcinfo[29] = (1 & (syncdata[18] >> 1)) + 48;      // bit 1
                    lcinfo[39] = (1 & syncdata[18]) + 48;     // bit 0
                    lcinfo[49] = (1 & (syncdata[19] >> 1)) + 48;      // bit 1
                    parity = (1 & syncdata[19]) + 48; // bit 0
                } else {
                    mi[0] = (1 & (syncdata[4] >> 1)) + 48;    // bit 1
                    mi[11] = (1 & syncdata[4]) + 48;  // bit 0
                    mi[22] = (1 & (syncdata[5] >> 1)) + 48;   // bit 1
                    mi[32] = (1 & syncdata[5]) + 48;  // bit 0
                    mi[42] = (1 & (syncdata[6] >> 1)) + 48;   // bit 1
                    mi[52] = (1 & syncdata[6]) + 48;  // bit 0
                    mi[62] = (1 & (syncdata[7] >> 1)) + 48;   // bit 1
                    parity = (1 & syncdata[7]) + 48;  // bit 0
                    mi[1] = (1 & (syncdata[8] >> 1)) + 48;    // bit 1
                    mi[12] = (1 & syncdata[8]) + 48;  // bit 0
                    mi[23] = (1 & (syncdata[9] >> 1)) + 48;   // bit 1
                    mi[33] = (1 & syncdata[9]) + 48;  // bit 0
                    mi[43] = (1 & (syncdata[10] >> 1)) + 48;  // bit 1
                    mi[53] = (1 & syncdata[10]) + 48; // bit 0
                    mi[63] = (1 & (syncdata[11] >> 1)) + 48;  // bit 1
                    parity = (1 & syncdata[11]) + 48; // bit 0
                    mi[2] = (1 & (syncdata[12] >> 1)) + 48;   // bit 1
                    mi[13] = (1 & syncdata[12]) + 48; // bit 0
                    mi[24] = (1 & (syncdata[13] >> 1)) + 48;  // bit 1
                    mi[34] = (1 & syncdata[13]) + 48; // bit 0
                    mi[44] = (1 & (syncdata[14] >> 1)) + 48;  // bit 1
                    mi[54] = (1 & syncdata[14]) + 48; // bit 0
                    mi[64] = (1 & (syncdata[15] >> 1)) + 48;  // bit 1
                    parity = (1 & syncdata[15]) + 48; // bit 0
                    mi[3] = (1 & (syncdata[16] >> 1)) + 48;   // bit 1
                    mi[14] = (1 & syncdata[16]) + 48; // bit 0
                    mi[25] = (1 & (syncdata[17] >> 1)) + 48;  // bit 1
                    mi[35] = (1 & syncdata[17]) + 48; // bit 0
                    mi[45] = (1 & (syncdata[18] >> 1)) + 48;  // bit 1
                    mi[55] = (1 & syncdata[18]) + 48; // bit 0
                    mi[65] = (1 & (syncdata[19] >> 1)) + 48;  // bit 1
                    parity = (1 & syncdata[19]) + 48; // bit 0
                }
            } else if (j == 2) {
                if ((eeei == 0) && (aiei == 0)) {
                    lcformat[4] = (1 & (syncdata[4] >> 1)) + 48;      // bit 1
                    mfid[7] = (1 & syncdata[4]) + 48; // bit 0
                    lcinfo[10] = (1 & (syncdata[5] >> 1)) + 48;       // bit 1
                    lcinfo[20] = (1 & syncdata[5]) + 48;      // bit 0
                    lcinfo[30] = (1 & (syncdata[6] >> 1)) + 48;       // bit 1
                    lcinfo[40] = (1 & syncdata[6]) + 48;      // bit 0
                    lcinfo[50] = (1 & (syncdata[7] >> 1)) + 48;       // bit 1
                    parity = (1 & syncdata[7]) + 48;  // bit 0
                    lcformat[5] = (1 & (syncdata[8] >> 1)) + 48;      // bit 1
                    lcinfo[0] = (1 & syncdata[8]) + 48;       // bit 0
                    lcinfo[11] = (1 & (syncdata[9] >> 1)) + 48;       // bit 1
                    lcinfo[21] = (1 & syncdata[9]) + 48;      // bit 0
                    lcinfo[31] = (1 & (syncdata[10] >> 1)) + 48;      // bit 1
                    lcinfo[41] = (1 & syncdata[10]) + 48;     // bit 0
                    lcinfo[51] = (1 & (syncdata[11] >> 1)) + 48;      // bit 1
                    parity = (1 & syncdata[11]) + 48; // bit 0
                    lcformat[6] = (1 & (syncdata[12] >> 1)) + 48;     // bit 1
                    lcinfo[1] = (1 & syncdata[12]) + 48;      // bit 0
                    lcinfo[12] = (1 & (syncdata[13] >> 1)) + 48;      // bit 1
                    lcinfo[22] = (1 & syncdata[13]) + 48;     // bit 0
                    lcinfo[32] = (1 & (syncdata[14] >> 1)) + 48;      // bit 1
                    lcinfo[42] = (1 & syncdata[14]) + 48;     // bit 0
                    lcinfo[52] = (1 & (syncdata[15] >> 1)) + 48;      // bit 1
                    parity = (1 & syncdata[15]) + 48; // bit 0
                    lcformat[7] = (1 & (syncdata[16] >> 1)) + 48;     // bit 1
                    lcinfo[2] = (1 & syncdata[16]) + 48;      // bit 0
                    lcinfo[13] = (1 & (syncdata[17] >> 1)) + 48;      // bit 1
                    lcinfo[23] = (1 & syncdata[17]) + 48;     // bit 0
                    lcinfo[33] = (1 & (syncdata[18] >> 1)) + 48;      // bit 1
                    lcinfo[43] = (1 & syncdata[18]) + 48;     // bit 0
                    lcinfo[53] = (1 & (syncdata[19] >> 1)) + 48;      // bit 1
                    parity = (1 & syncdata[19]) + 48; // bit 0
                } else {
                    mi[4] = (1 & (syncdata[4] >> 1)) + 48;    // bit 1
                    mi[15] = (1 & syncdata[4]) + 48;  // bit 0
                    mi[26] = (1 & (syncdata[5] >> 1)) + 48;   // bit 1
                    mi[36] = (1 & syncdata[5]) + 48;  // bit 0
                    mi[46] = (1 & (syncdata[6] >> 1)) + 48;   // bit 1
                    mi[56] = (1 & syncdata[6]) + 48;  // bit 0
                    mi[66] = (1 & (syncdata[7] >> 1)) + 48;   // bit 1
                    parity = (1 & syncdata[7]) + 48;  // bit 0
                    mi[5] = (1 & (syncdata[8] >> 1)) + 48;    // bit 1
                    mi[16] = (1 & syncdata[8]) + 48;  // bit 0
                    mi[27] = (1 & (syncdata[9] >> 1)) + 48;   // bit 1
                    mi[37] = (1 & syncdata[9]) + 48;  // bit 0
                    mi[47] = (1 & (syncdata[10] >> 1)) + 48;  // bit 1
                    mi[57] = (1 & syncdata[10]) + 48; // bit 0
                    mi[67] = (1 & (syncdata[11] >> 1)) + 48;  // bit 1
                    parity = (1 & syncdata[11]) + 48; // bit 0
                    mi[6] = (1 & (syncdata[12] >> 1)) + 48;   // bit 1
                    mi[17] = (1 & syncdata[12]) + 48; // bit 0
                    mi[28] = (1 & (syncdata[13] >> 1)) + 48;  // bit 1
                    mi[38] = (1 & syncdata[13]) + 48; // bit 0
                    mi[48] = (1 & (syncdata[14] >> 1)) + 48;  // bit 1
                    mi[58] = (1 & syncdata[14]) + 48; // bit 0
                    mi[68] = (1 & (syncdata[15] >> 1)) + 48;  // bit 1
                    parity = (1 & syncdata[15]) + 48; // bit 0
                    mi[7] = (1 & (syncdata[16] >> 1)) + 48;   // bit 1
                    mi[18] = (1 & syncdata[16]) + 48; // bit 0
                    mi[29] = (1 & (syncdata[17] >> 1)) + 48;  // bit 1
                    mi[39] = (1 & syncdata[17]) + 48; // bit 0
                    mi[49] = (1 & (syncdata[18] >> 1)) + 48;  // bit 1
                    mi[59] = (1 & syncdata[18]) + 48; // bit 0
                    mi[69] = (1 & (syncdata[19] >> 1)) + 48;  // bit 1
                    parity = (1 & syncdata[19]) + 48; // bit 0
                }
            } else if (j == 3) {
                burstd = (1 & syncdata[1]);   // bit 0

                algid[0] = (1 & (syncdata[4] >> 1)) + 48;      // bit 1
                algid[1] = (1 & syncdata[4]) + 48;     // bit 0
                algid[2] = (1 & (syncdata[5] >> 1)) + 48;      // bit 1
                algid[3] = (1 & syncdata[5]) + 48;     // bit 0
                if (burstd == 0) {
                    algid[4] = (1 & (syncdata[8] >> 1)) + 48;  // bit 1
                    algid[5] = (1 & syncdata[8]) + 48; // bit 0
                    algid[6] = (1 & (syncdata[9] >> 1)) + 48;  // bit 1
                    algid[7] = (1 & syncdata[9]) + 48; // bit 0

                    keyid[0] = (1 & (syncdata[10] >> 1)) + 48; // bit 1
                    keyid[1] = (1 & syncdata[10]) + 48;        // bit 0
                    keyid[2] = (1 & (syncdata[11] >> 1)) + 48; // bit 1
                    keyid[3] = (1 & syncdata[11]) + 48;        // bit 0
                    keyid[4] = (1 & (syncdata[12] >> 1)) + 48; // bit 1
                    keyid[5] = (1 & syncdata[12]) + 48;        // bit 0
                    keyid[6] = (1 & (syncdata[13] >> 1)) + 48; // bit 1
                    keyid[7] = (1 & syncdata[13]) + 48;        // bit 0
                    keyid[8] = (1 & (syncdata[14] >> 1)) + 48; // bit 1
                    keyid[9] = (1 & syncdata[14]) + 48;        // bit 0
                    keyid[10] = (1 & (syncdata[15] >> 1)) + 48;        // bit 1
                    keyid[11] = (1 & syncdata[15]) + 48;       // bit 0
                    keyid[12] = (1 & (syncdata[16] >> 1)) + 48;        // bit 1
                    keyid[13] = (1 & syncdata[16]) + 48;       // bit 0
                    keyid[14] = (1 & (syncdata[17] >> 1)) + 48;        // bit 1
                    keyid[15] = (1 & syncdata[17]) + 48;       // bit 0
                } else {
                    sprintf (algid, "________");
                    sprintf (keyid, "________________");
                }
            } else if (j == 4) {
                if ((eeei == 0) && (aiei == 0)) {
                    mfid[0] = (1 & (syncdata[4] >> 1)) + 48;  // bit 1
                    lcinfo[3] = (1 & syncdata[4]) + 48;       // bit 0
                    lcinfo[14] = (1 & (syncdata[5] >> 1)) + 48;       // bit 1
                    lcinfo[24] = (1 & syncdata[5]) + 48;      // bit 0
                    lcinfo[34] = (1 & (syncdata[6] >> 1)) + 48;       // bit 1
                    lcinfo[44] = (1 & syncdata[6]) + 48;      // bit 0
                    lcinfo[54] = (1 & (syncdata[7] >> 1)) + 48;       // bit 1
                    parity = (1 & syncdata[7]) + 48;  // bit 0
                    mfid[1] = (1 & (syncdata[8] >> 1)) + 48;  // bit 1
                    lcinfo[4] = (1 & syncdata[8]) + 48;       // bit 0
                    lcinfo[15] = (1 & (syncdata[9] >> 1)) + 48;       // bit 1
                    lcinfo[25] = (1 & syncdata[9]) + 48;      // bit 0
                    lcinfo[35] = (1 & (syncdata[10] >> 1)) + 48;      // bit 1
                    lcinfo[45] = (1 & syncdata[10]) + 48;     // bit 0
                    lcinfo[55] = (1 & (syncdata[11] >> 1)) + 48;      // bit 1
                    parity = (1 & syncdata[11]) + 48; // bit 0
                    mfid[2] = (1 & (syncdata[12] >> 1)) + 48; // bit 1
                    lcinfo[5] = (1 & syncdata[12]) + 48;      // bit 0
                } else {
                    mi[8] = (1 & (syncdata[4] >> 1)) + 48;    // bit 1
                    mi[19] = (1 & syncdata[4]) + 48;  // bit 0
                    mi[30] = (1 & (syncdata[5] >> 1)) + 48;   // bit 1
                    mi[40] = (1 & syncdata[5]) + 48;  // bit 0
                    mi[50] = (1 & (syncdata[6] >> 1)) + 48;   // bit 1
                    mi[60] = (1 & syncdata[6]) + 48;  // bit 0
                    mi[70] = (1 & (syncdata[7] >> 1)) + 48;   // bit 1
                    parity = (1 & syncdata[7]) + 48;  // bit 0
                    mi[9] = (1 & (syncdata[8] >> 1)) + 48;    // bit 1
                    mi[20] = (1 & syncdata[8]) + 48;  // bit 0
                    mi[31] = (1 & (syncdata[9] >> 1)) + 48;   // bit 1
                    mi[41] = (1 & syncdata[9]) + 48;  // bit 0
                    mi[51] = (1 & (syncdata[10] >> 1)) + 48;  // bit 1
                    mi[61] = (1 & syncdata[10]) + 48; // bit 0
                    mi[71] = (1 & (syncdata[11] >> 1)) + 48;  // bit 1
                    parity = (1 & syncdata[11]) + 48; // bit 0
                    mi[10] = (1 & (syncdata[12] >> 1)) + 48;  // bit 1
                    mi[21] = (1 & syncdata[12]) + 48; // bit 0
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
            w = aW;
            x = aX;
            y = aY;
            z = aZ;
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

            if ((strcmp (sync, X2TDMA_BS_DATA_SYNC) == 0) || (msMode == 1)) {
                if (currentslot == 0) {
                    sprintf (slot1light, " slot1 ");
                } else {
                    sprintf (slot0light, " slot0 ");
                }
            } else if (strcmp (sync, X2TDMA_BS_VOICE_SYNC) == 0) {
                if (currentslot == 0) {
                    sprintf (slot1light, " SLOT1 ");
                } else {
                    sprintf (slot0light, " SLOT0 ");
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
//            printf ("\n");
            status_mbedecoding = false;
        }

        if (mutecurrentslot == 0) {
            if ((eeei == 0) && (aiei == 0)) {
                P25processlcw (lcformat, mfid, lcinfo);
            }
            if (p25enc == 1) {
                algidhex = strtol (algid, NULL, 2);
                kidhex = strtol (keyid, NULL, 2);
//                printf ("mi: %s algid: $%x kid: $%x\n", mi, algidhex, kidhex);
            }
        }
    }
}
