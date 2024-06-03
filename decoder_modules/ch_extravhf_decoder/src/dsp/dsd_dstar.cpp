#include <config.h>

#undef max
#undef min

#include "dsd.h"
#include <stdio.h>
#include <string.h>

namespace dsp {

    // Functions for processing the radio-header:
    // descramble
    // deinterleave
    // FECdecoder

    // (C) 2011 Jonathan Naylor G4KLX

    /*
    *	This program is free software; you can redistribute it and/or modify
    *	it under the terms of the GNU General Public License as published by
    *	the Free Software Foundation; version 2 of the License.
    *
    *	This program is distributed in the hope that it will be useful,
    *	but WITHOUT ANY WARRANTY; without even the implied warranty of
    *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    *	GNU General Public License for more details.
    */
    
    // This code was originally written by JOnathan Naylor, G4KLX, as part
    // of the "pcrepeatercontroller" project
    // More info:
    // http://groups.yahoo.com/group/pcrepeatercontroller



    // Changes:
    // Convert C++ to C

    // Version 20111106: initial release

    // function traceBack
    int traceBack (int * out, int * m_pathMemory0, int * m_pathMemory1, int * m_pathMemory2, int * m_pathMemory3) {
        enum FEC_STATE { S0, S1, S2, S3 } state;
        int loop;
        int length=0;

        state=S0;

        for (loop=329; loop >= 0; loop--, length++) {

            switch (state) {
                case S0: // if state S0
                    if (m_pathMemory0[loop]) {
                        state = S2; // lower path
                    } else {
                        state = S0; // upper path
                    }; // end else - if
                    out[loop]=0;
                    break;
                
                case S1: // if state S1
                    if (m_pathMemory1[loop]) {
                        state = S2; // lower path
                    } else {
                        state = S0; // upper path
                    }; // end else - if
                    out[loop]=1;
                    break;
                
                case S2: // if state S2
                    if (m_pathMemory2[loop]) {
                        state = S3; // lower path
                    } else {
                        state = S1; // upper path
                    }; // end else - if
                    out[loop]=0;
                    break;
                
                case S3: // if state S3
                    if (m_pathMemory3[loop]) {
                        state = S3; // lower path
                    } else {
                        state = S1; // upper path
                    }; // end else - if
                    out[loop]=1;
                    break;
                
            }; // end switch
        }; // end for

        return(length);
    }; // end function



    // function viterbiDecode
    void viterbiDecode (int n, int *data, int *m_pathMemory0, int *m_pathMemory1, int *m_pathMemory2, int *m_pathMemory3, int *m_pathMetric) {
        int tempMetric[4];
        int metric[8];
        int loop;

        int m1;
        int m2;

        metric[0]=(data[1]^0)+(data[0]^0);
        metric[1]=(data[1]^1)+(data[0]^1);
        metric[2]=(data[1]^1)+(data[0]^0);
        metric[3]=(data[1]^0)+(data[0]^1);
        metric[4]=(data[1]^1)+(data[0]^1);
        metric[5]=(data[1]^0)+(data[0]^0);
        metric[6]=(data[1]^0)+(data[0]^1);
        metric[7]=(data[1]^1)+(data[0]^0);

        // Pres. state = S0, Prev. state = S0 & S2
        m1=metric[0]+m_pathMetric[0];
        m2=metric[4]+m_pathMetric[2];
        if (m1<m2) {
            m_pathMemory0[n]=0;
            tempMetric[0]=m1;
        } else {
            m_pathMemory0[n]=1;
            tempMetric[0]=m2;
        }; // end else - if

        // Pres. state = S1, Prev. state = S0 & S2
        m1=metric[1]+m_pathMetric[0];
        m2=metric[5]+m_pathMetric[2];
        if (m1<m2) {
            m_pathMemory1[n]=0;
            tempMetric[1]=m1;
        } else {
            m_pathMemory1[n]=1;
            tempMetric[1]=m2;
        }; // end else - if

        // Pres. state = S2, Prev. state = S2 & S3
        m1=metric[2]+m_pathMetric[1];
        m2=metric[6]+m_pathMetric[3];
        if (m1<m2) {
            m_pathMemory2[n]=0;
            tempMetric[2]=m1;
        } else {
            m_pathMemory2[n]=1;
            tempMetric[2]=m2;
        }

        // Pres. state = S3, Prev. state = S1 & S3
        m1=metric[3]+m_pathMetric[1];
        m2=metric[7]+m_pathMetric[3];
        if (m1 < m2) {
            m_pathMemory3[n]=0;
            tempMetric[3]=m1;
        } else {
            m_pathMemory3[n]=1;
            tempMetric[3]=m2;
        }; // end else - if

        for (loop=0;loop<4;loop++) {
            m_pathMetric[loop]=tempMetric[loop];
        }; // end for

    }; // end function ViterbiDecode


    // function FECdecoder 
    // returns outlen
    int FECdecoder (int * in, int * out) {
        int outLen;

        int m_pathMemory0[330];
        int m_pathMemory1[330];
        int m_pathMemory2[330];
        int m_pathMemory3[330];
        int m_pathMetric[4];

        int loop,loop2;

        int n=0;

        memset(m_pathMemory0,0,330*sizeof(int));
        memset(m_pathMemory1,0,330*sizeof(int));
        memset(m_pathMemory2,0,330*sizeof(int));
        memset(m_pathMemory3,0,330*sizeof(int));

        for (loop=0;loop<4;loop++) {
            m_pathMetric[loop]=0;
        }; // end for


        for (loop2=0;loop2<660;loop2+=2, n++) {
            int data[2];

            if (in[loop2]) {
                data[1]=1;
            } else {
                data[1]=0;
            }; // end else - if

            if (in[loop2+1]) {
                data[0]=1;
            } else {
                data[0]=0;
            }; // end else - if

            viterbiDecode(n, data, m_pathMemory0, m_pathMemory1, m_pathMemory2, m_pathMemory3, m_pathMetric);
        }; // end for

        outLen=traceBack(out, m_pathMemory0, m_pathMemory1, m_pathMemory2, m_pathMemory3);

        // Swap endian-ness
        // code removed (done converting bits into octets), done in main program

        //for (loop=0;loop<330;loop+=8) {
        //	int temp;
        //	temp=out[loop];out[loop]=out[loop+7];out[loop+7]=temp;
        //	temp=out[loop+1];out[loop+1]=out[loop+6];out[loop+6]=temp;
        //	temp=out[loop+2];out[loop+2]=out[loop+5];out[loop+5]=temp;
        //	temp=out[loop+3];out[loop+3]=out[loop+4];out[loop+4]=temp;
        //}

        return(outLen);

    }; // end function FECdecoder


    // function deinterleave
    void deinterleave (int * in, int * out) {
        int k=0;
        int loop=0;
        // function starts here

        // init vars
        k=0;

        for (loop=0;loop<660;loop++) {
            out[k]=in[loop];

            k += 24;

            if (k >= 672) {
                k -= 671;
            } else if (k >= 660) {
                k -= 647;
            }; // end elsif - if
        }; // end for

    }; // end function deinterleave



    /// function scramble
    void scramble (int * in,int * out) {
        static const int SCRAMBLER_TABLE_BITS[] = {
            0,0,0,0,1,1,1,0,1,1,1,1,0,0,1,0,1,1,0,0,1,0,0,1,0,0,0,0,0,0,1,0,
            0,0,1,0,0,1,1,0,0,0,1,0,1,1,1,0,1,0,1,1,0,1,1,0,0,0,0,0,1,1,0,0,
            1,1,0,1,0,1,0,0,1,1,1,0,0,1,1,1,1,0,1,1,0,1,0,0,0,0,1,0,1,0,1,0,
            1,1,1,1,1,0,1,0,0,1,0,1,0,0,0,1,1,0,1,1,1,0,0,0,1,1,1,1,1,1,1,0,
            0,0,0,1,1,1,0,1,1,1,1,0,0,1,0,1,1,0,0,1,0,0,1,0,0,0,0,0,0,1,0,0,
            0,1,0,0,1,1,0,0,0,1,0,1,1,1,0,1,0,1,1,0,1,1,0,0,0,0,0,1,1,0,0,1,
            1,0,1,0,1,0,0,1,1,1,0,0,1,1,1,1,0,1,1,0,1,0,0,0,0,1,0,1,0,1,0,1,
            1,1,1,1,0,1,0,0,1,0,1,0,0,0,1,1,0,1,1,1,0,0,0,1,1,1,1,1,1,1,0,0,
            0,0,1,1,1,0,1,1,1,1,0,0,1,0,1,1,0,0,1,0,0,1,0,0,0,0,0,0,1,0,0,0,
            1,0,0,1,1,0,0,0,1,0,1,1,1,0,1,0,1,1,0,1,1,0,0,0,0,0,1,1,0,0,1,1,
            0,1,0,1,0,0,1,1,1,0,0,1,1,1,1,0,1,1,0,1,0,0,0,0,1,0,1,0,1,0,1,1,
            1,1,1,0,1,0,0,1,0,1,0,0,0,1,1,0,1,1,1,0,0,0,1,1,1,1,1,1,1,0,0,0,
            0,1,1,1,0,1,1,1,1,0,0,1,0,1,1,0,0,1,0,0,1,0,0,0,0,0,0,1,0,0,0,1,
            0,0,1,1,0,0,0,1,0,1,1,1,0,1,0,1,1,0,1,1,0,0,0,0,0,1,1,0,0,1,1,0,
            1,0,1,0,0,1,1,1,0,0,1,1,1,1,0,1,1,0,1,0,0,0,0,1,0,1,0,1,0,1,1,1,
            1,1,0,1,0,0,1,0,1,0,0,0,1,1,0,1,1,1,0,0,0,1,1,1,1,1,1,1,0,0,0,0,
            1,1,1,0,1,1,1,1,0,0,1,0,1,1,0,0,1,0,0,1,0,0,0,0,0,0,1,0,0,0,1,0,
            0,1,1,0,0,0,1,0,1,1,1,0,1,0,1,1,0,1,1,0,0,0,0,0,1,1,0,0,1,1,0,1,
            0,1,0,0,1,1,1,0,0,1,1,1,1,0,1,1,0,1,0,0,0,0,1,0,1,0,1,0,1,1,1,1,
            1,0,1,0,0,1,0,1,0,0,0,1,1,0,1,1,1,0,0,0,1,1,1,1,1,1,1,0,0,0,0,1,
            1,1,0,1,1,1,1,0,0,1,0,1,1,0,0,1,0,0,1,0,0,0,0,0,0,1,0,0,0,1,0,0,
            1,1,0,0,0,1,0,1,1,1,0,1,0,1,1,0,1,1,0,0,0,0,0,1,1,0,0,1,1,0,1,0,
            1,0,0,1,1,1,0,0,1,1,1,1,0,1,1,0};

        const int SCRAMBLER_TABLE_BITS_LENGTH=720;

        int loop=0;
        int m_count=0;


        for (loop=0; loop < 660; loop++) {
            out[loop] = in[loop] ^ SCRAMBLER_TABLE_BITS[m_count++];

            if (m_count >= SCRAMBLER_TABLE_BITS_LENGTH) {
                m_count = 0U;
            }; // end if
        }; // end for

    }; // end function scramble

    /*
    *
    * This code is taken largely from on1arf's GMSK code. Original copyright below:
    *
    *
    * Copyright (C) 2011 by Kristoff Bonne, ON1ARF
    *
    * This program is free software; you can redistribute it and/or modify
    * it under the terms of the GNU General Public License as published by
    * the Free Software Foundation; version 2 of the License.
    *
    * This program is distributed in the hope that it will be useful,
    * but WITHOUT ANY WARRANTY; without even the implied warranty of
    * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    * GNU General Public License for more details.
    *
    */
    void dstar_header_decode(int radioheaderbuffer[660]) {
        int radioheaderbuffer2[660];
        unsigned char radioheader[41];
        int octetcount, bitcount, loop;
        unsigned char bit2octet[] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};
        unsigned int FCSinheader;
        unsigned int FCScalculated;
        int len;

        scramble(radioheaderbuffer, radioheaderbuffer2);
        deinterleave(radioheaderbuffer2, radioheaderbuffer);
        len = FECdecoder(radioheaderbuffer, radioheaderbuffer2);
        memset(radioheader, 0, 41);
        // note we receive 330 bits, but we only use 328 of them (41 octets)
        // bits 329 and 330 are unused
        octetcount = 0;
        bitcount = 0;
        for (loop = 0; loop < 328; loop++) {
            if (radioheaderbuffer2[loop]) {
                radioheader[octetcount] |= bit2octet[bitcount];
            };
            bitcount++;
            // increase octetcounter and reset bitcounter every 8 bits
            if (bitcount >= 8) {
                octetcount++;
                bitcount = 0;
            }
        }
        // print header
//        printf("\nDSTAR HEADER: ");
//        printf("RPT 2: %c%c%c%c%c%c%c%c ", radioheader[3], radioheader[4],
//                radioheader[5], radioheader[6], radioheader[7], radioheader[8],
//                radioheader[9], radioheader[10]);
//        printf("RPT 1: %c%c%c%c%c%c%c%c ", radioheader[11], radioheader[12],
//                radioheader[13], radioheader[14], radioheader[15], radioheader[16],
//                radioheader[17], radioheader[18]);
//        printf("YOUR: %c%c%c%c%c%c%c%c ", radioheader[19], radioheader[20],
//                radioheader[21], radioheader[22], radioheader[23], radioheader[24],
//                radioheader[25], radioheader[26]);
//        printf("MY: %c%c%c%c%c%c%c%c/%c%c%c%c\n", radioheader[27],
//                radioheader[28], radioheader[29], radioheader[30], radioheader[31],
//                radioheader[32], radioheader[33], radioheader[34], radioheader[35],
//                radioheader[36], radioheader[37], radioheader[38]);
    }

    void DSD::processDSTAR() {
        // extracts AMBE frames from D-STAR voice frame
        int i, j, dibit;
        char ambe_fr[4][24];
        unsigned char data[9];
        unsigned int bits[4];
        int framecount;
        int sync_missed = 0;
        unsigned char slowdata[4];
        unsigned int bitbuffer = 0;
        const int *w, *x;

        if (errorbars == 1) {
//            printf("e:");
            status_mbedecoding = true;
            status_errorbar = "";
        }

        if (synctype == 18) {
            framecount = 0;
            synctype = 6;
        } else if (synctype == 19) {
            framecount = 0;
            synctype = 7;
        } else {
            framecount = 1; //just saw a sync frame; there should be 20 not 21 till the next
        }

        while (sync_missed < 3) {
            memset(ambe_fr, 0, 96);
            // voice frame
            w = dW;
            x = dX;

            for (i = 0; i < 72; i++) {
                dibit = getDibit();
                bitbuffer <<= 1;
                if (dibit == 1) {
                    bitbuffer |= 0x01;
                }
                if ((bitbuffer & 0x00FFFFFF) == 0x00AAB468) {
                    // we're slipping bits
//                    printf("sync in voice after i=%d, restarting\n", i);
                    //ugh just start over
                    i = 0;
                    w = dW;
                    x = dX;
                    framecount = 1;
                    continue;
                }
                ambe_fr[*w][*x] = (1 & dibit);
                w++;
                x++;
            }
            processMbeFrame(NULL, ambe_fr, NULL);

            //  data frame - 24 bits
            for (i = 73; i < 97; i++) {
                dibit = getDibit();
                bitbuffer <<= 1;
                if (dibit == 1) {
                    bitbuffer |= 0x01;
                }
                if ((bitbuffer & 0x00FFFFFF) == 0x00AAB468) {
                    // looking if we're slipping bits
                    if (i != 96) {
//                        printf("sync after i=%d\n", i);
                        i = 96;
                    }
                }
            }

            slowdata[0] = (bitbuffer >> 16) & 0x000000FF;
            slowdata[1] = (bitbuffer >> 8) & 0x000000FF;
            slowdata[2] = (bitbuffer) & 0x000000FF;
            slowdata[3] = 0;

            if ((bitbuffer & 0x00FFFFFF) == 0x00AAB468) {
                //We got sync!
                //printf("Sync on framecount = %d\n", framecount);
                sync_missed = 0;
            } else if ((bitbuffer & 0x00FFFFFF) == 0xAAAAAA) {
                //End of transmission
//                printf("End of transmission\n");
                goto end;
            } else if (framecount % 21 == 0) {
//                printf("Missed sync on framecount = %d, value = %x/%x/%x\n",
//                        framecount, slowdata[0], slowdata[1], slowdata[2]);
                sync_missed++;
            } else if (framecount != 0 && (bitbuffer & 0x00FFFFFF) != 0x000000) {
                slowdata[0] ^= 0x70;
                slowdata[1] ^= 0x4f;
                slowdata[2] ^= 0x93;
                //printf("unscrambled- %s",slowdata);
            } else if (framecount == 0) {
                //printf("never scrambled-%s\n",slowdata);
            }
            framecount++;
        }
        end: if (errorbars == 1) {
            status_mbedecoding = false;
//            printf("\n");
        }
    }

    void DSD::processDSTAR_HD() {
        int i, j;
        int radioheaderbuffer[660];
        for (j = 0; j < 660; j++) {
            radioheaderbuffer[j] = getDibit();
        }
        // Note: These routines contain GPLed code. Remove if you object to that.
        dstar_header_decode(radioheaderbuffer);
        //We officially have sync now, so just pass on to the above routine:
        processDSTAR();

    }
}
