
#if defined(__MACH__) || defined(__ANDROID__)
#define register
#endif

// it is included here first, because on windows it would fail otherwise.
#include <itpp/itcomm.h>

#include <algorithm>

#include <config.h>
#include "dsd.h"

namespace dsp {

        int NewDSD::process(int count, const uint8_t* in, short* out) {
            int pos = 0;
            int outcnt = 0;
            while((count-pos) > 0) {
                switch(curr_state) {
                    case STATE_NOT_ENOUGH_DATA:
                    case STATE_NO_SYNC:
                    case STATE_SYNC:
                        pos += findFrameSync((count-pos), &in[pos]);
                        break;
                    case STATE_FSFND_DMR_DATA:
                    case STATE_FSFND_DMR_VOICE:
                    case STATE_FSFND_P25:
                        framesyncSymbolsRead = 0;
                        framesynctest_p = 25;
                        if(curr_state == STATE_FSFND_DMR_DATA) {
                            curr_state = STATE_PROC_FRAME_DMR_DATA;
                        } else if(curr_state == STATE_FSFND_DMR_VOICE) {
                            curr_state = STATE_PROC_FRAME_DMR_VOICE;
                        } else if(curr_state == STATE_FSFND_P25) {
                            p25_frame_ctr = 0;
                            p25_bch_code_idx = 0;
                            curr_state = STATE_PROC_FRAME_P25;
                        }
                        break;
                    case STATE_PROC_FRAME_DMR_DATA:
                    case STATE_PROC_FRAME_DMR_DATA_1:
                        pos += processDMRdata((count-pos), &in[pos]);
                        break;
                    case STATE_PROC_FRAME_DMR_VOICE:
                    case STATE_PROC_FRAME_DMR_VOICE_1:
                    case STATE_PROC_FRAME_DMR_VOICE_2:
                    case STATE_PROC_FRAME_DMR_VOICE_3:
                    case STATE_PROC_FRAME_DMR_VOICE_4:
                    case STATE_PROC_FRAME_DMR_VOICE_5:
                    case STATE_PROC_FRAME_DMR_VOICE_6:
                    case STATE_PROC_FRAME_DMR_VOICE_7:
                    case STATE_PROC_FRAME_DMR_VOICE_8:
                    case STATE_PROC_FRAME_DMR_VOICE_9:
                    case STATE_PROC_FRAME_DMR_VOICE_10:
                        pos += processDMRvoice((count-pos), &in[pos], &(out[outcnt]), &outcnt);
                        break;
                    case STATE_PROC_FRAME_P25:
                    case STATE_PROC_FRAME_P25_1:
                    case STATE_PROC_FRAME_P25_2:
                    case STATE_PROC_FRAME_P25_3:
                        pos += processP25frame((count-pos), &in[pos]);
                        break;
                    case STATE_PROC_FRAME_P25_HEXWORD:
                    case STATE_PROC_FRAME_P25_HEXWORD_G24_1:
                    case STATE_PROC_FRAME_P25_HEXWORD_HAMM_1:
                        pos += processP25HexWord((count-pos), &in[pos]);
                        break;
                    case STATE_PROC_FRAME_P25_DODECAWORD:
                    case STATE_PROC_FRAME_P25_DODECAWORD_1:
                        pos += processP25DodecaWord((count-pos), &in[pos]);
                        break;
                    case STATE_PROC_FRAME_P25_IMBE:
                        pos += processP25IMBEFrame((count-pos), &in[pos], &(out[outcnt]), &outcnt);
                        break;
                    case STATE_PROC_FRAME_P25_HDU:
                    case STATE_PROC_FRAME_P25_HDU_1:
                    case STATE_PROC_FRAME_P25_HDU_2:
                        pos += processP25HDU((count-pos), &in[pos]);
                        break;
                    case STATE_PROC_FRAME_P25_LDU1:
                    case STATE_PROC_FRAME_P25_LDU1_1:
                    case STATE_PROC_FRAME_P25_LDU1_2:
                    case STATE_PROC_FRAME_P25_LDU1_3:
                    case STATE_PROC_FRAME_P25_LDU1_4:
                    case STATE_PROC_FRAME_P25_LDU1_5:
                    case STATE_PROC_FRAME_P25_LDU1_6:
                    case STATE_PROC_FRAME_P25_LDU1_7:
                    case STATE_PROC_FRAME_P25_LDU1_8:
                        pos += processP25LDU1((count-pos), &in[pos]);
                        break;
                    case STATE_PROC_FRAME_P25_LDU2:
                    case STATE_PROC_FRAME_P25_LDU2_1:
                    case STATE_PROC_FRAME_P25_LDU2_2:
                    case STATE_PROC_FRAME_P25_LDU2_3:
                    case STATE_PROC_FRAME_P25_LDU2_4:
                    case STATE_PROC_FRAME_P25_LDU2_5:
                    case STATE_PROC_FRAME_P25_LDU2_6:
                    case STATE_PROC_FRAME_P25_LDU2_7:
                    case STATE_PROC_FRAME_P25_LDU2_8:
                        pos += processP25LDU2((count-pos), &in[pos]);
                        break;
                    case STATE_PROC_FRAME_P25_TDULC:
                    case STATE_PROC_FRAME_P25_TDULC_1:
                    case STATE_PROC_FRAME_P25_TDULC_2:
                        pos += processP25TDULC((count-pos), &in[pos]);
                        break;
                    case STATE_PROC_FRAME_P25_TDU:
                        pos += processP25TDU((count-pos), &in[pos]);
                        break;
                    case STATE_PROC_FRAME_P25_TSDU:
                        pos += processP25TSDU((count-pos), &in[pos]);
                        break;
                    case STATE_PROC_FRAME_P25_PDU:
                        pos += processP25PDU((count-pos), &in[pos]);
                        break;
                }
            }
            outSymsCtr += outcnt;
            inSymsCtr += count;
            int requiredOut = inSymsCtr * 5 / 3;
            int remainingOut = requiredOut - outSymsCtr;
            if(remainingOut > 0 && !mbe_status.mbe_status_decoding) {
                memset(&(out[outcnt]), 0, remainingOut*sizeof(short));
                outcnt += remainingOut;
            }
#ifdef _WIN32
            outSymsCtr -= (min(outSymsCtr, requiredOut));
#else
            outSymsCtr -= (std::min(outSymsCtr, requiredOut));
#endif
            inSymsCtr -= requiredOut * 3 / 5;
            return outcnt;
        }

        int NewDSD::findFrameSync(int count, const uint8_t* in) {
            int usedDibits = 0;
            for(int i = 0; i < count; i++) {
                char dibit;
                if (fss_dibitBufP > 900000) {
                    fss_dibitBufP = 200;
                }
                if (in[i] == 0b01 || in[i] == 0b00) {
                    dibitBuf[fss_dibitBufP++] = in[i];
                    dibit = '1';
                } else {
                    dibitBuf[fss_dibitBufP++] = in[i];
                    dibit = '3';
                }

                framesynctest_buf[framesynctest_p] = dibit;
                usedDibits++;
                if(curr_state == STATE_NOT_ENOUGH_DATA) {
                    frame_status.sync = false;
                    framesyncSymbolsRead++;
                    if(framesyncSymbolsRead >= 18) {
                        curr_state = STATE_NO_SYNC;
                        frame_status.sync = false;
                    }
                    continue;
                }
                char framesynctest[25];
                framesynctest[24] = 0;
                strncpy (framesynctest, &(framesynctest_buf[framesynctest_p - 23]), 24);
                if ((strcmp (framesynctest, DMR_MS_DATA_SYNC) == 0) || (strcmp (framesynctest, DMR_BS_DATA_SYNC) == 0)) {
                    //DMR DATA FRAME SYNC FOUND!
                    framesynctest_offset = framesynctest_p;
                    curr_state = STATE_FSFND_DMR_DATA;
                    frame_status.sync = true;
                    frame_status.lasttype = Frame_status::LAST_DMR;
                    break;
                }
                if ((strcmp (framesynctest, DMR_MS_VOICE_SYNC) == 0) || (strcmp (framesynctest, DMR_BS_VOICE_SYNC) == 0)) {
                    //DMR VOICE FRAME SYNC FOUND!
                    framesynctest_offset = framesynctest_p;
                    curr_state = STATE_FSFND_DMR_VOICE;
                    dmrv_iter = 0;
                    dmrv_ctr = 0;
                    frame_status.sync = true;
                    frame_status.lasttype = Frame_status::LAST_DMR;
                    break;
                }
                if (strcmp (framesynctest, P25P1_SYNC) == 0) {
                    //P25p1+ FRAME SYNC FOUND!
                    framesynctest_offset = framesynctest_p;
                    curr_state = STATE_FSFND_P25;
                    frame_status.sync = true;
                    frame_status.lasttype = Frame_status::LAST_P25;
                    break;
                }
                if (strcmp (framesynctest, INV_P25P1_SYNC) == 0) {
                    //P25p1- FRAME SYNC FOUND!
                    framesynctest_offset = framesynctest_p;
                    curr_state = STATE_FSFND_P25;
                    frame_status.sync = true;
                    frame_status.lasttype = Frame_status::LAST_P25;
                    break;
                }
                if (framesynctest_p < 1023) {
                    framesynctest_p++;
                } else {
                    // buffer reset
                    framesynctest_p = 25;
                }

                if (curr_state == STATE_SYNC) {
                    if (framesynctest_p >= 1000){
                        frame_status.sync = false;
                        curr_state = STATE_NO_SYNC;
                    }
                }
            }
            return usedDibits;
        }

        void NewDSD::processMbeFrame(char imbe_fr[8][23], char ambe_fr[4][24], char imbe7100_fr[7][24], short* out, int* outcnt) {
            int i, n;
            char imbe_d[88];
            char ambe_d[49];
            float aout_abs, max, gainfactor, gaindelta, maxbuf;
            int errs = 0;
            int errs2 = 0;

            for (i = 0; i < 88; i++) {
                imbe_d[i] = 0;
            }

            //maybe will be used later
//            } else if ((synctype == 14) || (synctype == 15)) {
//                mbe_processImbe7100x4400Frame (base_type::out.writeBuf, &errs, &errs2, mbe_errStr, imbe7100_fr, imbe_d, &curMp, &prevMp, &prevMpEnhanced, MBE_uvquality);
//            } else if ((synctype == 6) || (synctype == 7)) {
//                mbe_processAmbe3600x2400Frame (base_type::out.writeBuf, &errs, &errs2, mbe_errStr, ambe_fr, ambe_d, &curMp, &prevMp, &prevMpEnhanced, MBE_uvquality);
            if(curr_state >= STATE_PROC_FRAME_DMR_VOICE && curr_state <= STATE_PROC_FRAME_DMR_VOICE_10) {
                //DMR
                mbe_processAmbe3600x2450Frame (out, &errs, &errs2, mbe_errStr, ambe_fr, ambe_d, &curMp, &prevMp, &prevMpEnhanced, MBE_uvquality);
            } else if(curr_state >= STATE_PROC_FRAME_P25 && curr_state <= STATE_PROC_FRAME_P25_PDU) {
                //P25p1
                mbe_processImbe7200x4400Frame (out, &errs, &errs2, mbe_errStr, imbe_fr, imbe_d, &curMp, &prevMp, &prevMpEnhanced, MBE_uvquality);
            }

            mbe_status.mbe_status_errorbar += std::string(mbe_errStr);

            *outcnt += 160;
        }




        // DMR filter
        #define DMR_FILT_ZEROS 60
        constexpr static const float dmrFiltGain = 7.423339364f;
        float dmrFiltV[DMR_FILT_ZEROS+1];
        constexpr static const float dmrFiltTaps[DMR_FILT_ZEROS+1] = { 
            -0.0083649323f, -0.0265444850f, -0.0428141462f, -0.0537571943f,
            -0.0564141052f, -0.0489161045f, -0.0310068662f, -0.0043393881f,
            +0.0275375106f, +0.0595423283f, +0.0857543325f, +0.1003565948f,
            +0.0986944931f, +0.0782804830f, +0.0395670487f, -0.0136691535f,
            -0.0744390415f, -0.1331834575f, -0.1788967208f, -0.2005995448f,
            -0.1889627181f, -0.1378439993f, -0.0454976231f, +0.0847488694f,
            +0.2444859269f, +0.4209222342f, +0.5982295474f, +0.7593684540f,
            +0.8881539892f, +0.9712773915f, +0.9999999166f, +0.9712773915f,
            +0.8881539892f, +0.7593684540f, +0.5982295474f, +0.4209222342f,
            +0.2444859269f, +0.0847488694f, -0.0454976231f, -0.1378439993f,
            -0.1889627181f, -0.2005995448f, -0.1788967208f, -0.1331834575f,
            -0.0744390415f, -0.0136691535f, +0.0395670487f, +0.0782804830f,
            +0.0986944931f, +0.1003565948f, +0.0857543325f, +0.0595423283f,
            +0.0275375106f, -0.0043393881f, -0.0310068662f, -0.0489161045f,
            -0.0564141052f, -0.0537571943f, -0.0428141462f, -0.0265444850f,
            -0.0083649323f,
        };

        // NXDN filter
        #define NXDN_FILT_ZEROS 134
        constexpr static const float nxdnFiltGain = 15.95930463f;
        float nxdnFiltV[NXDN_FILT_ZEROS+1];
        constexpr static const float nxdnFiltTaps[NXDN_FILT_ZEROS+1] = {
            +0.031462429f, +0.031747267f, +0.030401148f, +0.027362877f,
            +0.022653298f, +0.016379869f, +0.008737200f, +0.000003302f,
            -0.009468531f, -0.019262057f, -0.028914291f, -0.037935027f,
            -0.045828927f, -0.052119261f, -0.056372283f, -0.058221106f,
            -0.057387924f, -0.053703443f, -0.047122444f, -0.037734535f,
            -0.025769308f, -0.011595336f, +0.004287292f, +0.021260954f,
            +0.038610717f, +0.055550276f, +0.071252765f, +0.084885375f,
            +0.095646450f, +0.102803611f, +0.105731303f, +0.103946126f,
            +0.097138329f, +0.085197939f, +0.068234131f, +0.046586711f,
            +0.020828821f, -0.008239664f, -0.039608255f, -0.072081234f,
            -0.104311776f, -0.134843790f, -0.162160200f, -0.184736015f,
            -0.201094346f, -0.209863285f, -0.209831516f, -0.200000470f,
            -0.179630919f, -0.148282051f, -0.105841323f, -0.052543664f,
            +0.011020985f, +0.083912428f, +0.164857408f, +0.252278939f,
            +0.344336996f, +0.438979335f, +0.534000832f, +0.627109358f,
            +0.715995947f, +0.798406824f, +0.872214756f, +0.935487176f,
            +0.986548646f, +1.024035395f, +1.046939951f, +1.054644241f,
            +1.046939951f, +1.024035395f, +0.986548646f, +0.935487176f,
            +0.872214756f, +0.798406824f, +0.715995947f, +0.627109358f,
            +0.534000832f, +0.438979335f, +0.344336996f, +0.252278939f,
            +0.164857408f, +0.083912428f, +0.011020985f, -0.052543664f,
            -0.105841323f, -0.148282051f, -0.179630919f, -0.200000470f,
            -0.209831516f, -0.209863285f, -0.201094346f, -0.184736015f,
            -0.162160200f, -0.134843790f, -0.104311776f, -0.072081234f,
            -0.039608255f, -0.008239664f, +0.020828821f, +0.046586711f,
            +0.068234131f, +0.085197939f, +0.097138329f, +0.103946126f,
            +0.105731303f, +0.102803611f, +0.095646450f, +0.084885375f,
            +0.071252765f, +0.055550276f, +0.038610717f, +0.021260954f,
            +0.004287292f, -0.011595336f, -0.025769308f, -0.037734535f,
            -0.047122444f, -0.053703443f, -0.057387924f, -0.058221106f,
            -0.056372283f, -0.052119261f, -0.045828927f, -0.037935027f,
            -0.028914291f, -0.019262057f, -0.009468531f, +0.000003302f,
            +0.008737200f, +0.016379869f, +0.022653298f, +0.027362877f,
            +0.030401148f, +0.031747267f, +0.031462429f,
        };

        short DSD::getSample() {
            if(inputBufDataRemains > 0) {
                short ret = inputBuffer[++inputBufReadPtr];
                if(inputBufReadPtr >= DSD_INPUT_BUFF_SIZE) inputBufReadPtr = 0;
                inputBufDataRemains--;
                inSymsCtr++;
                return ret;
            } else {
                int count = base_type::_in->read();
                if(count < 1) {
                    finflag = -1;
                    return 0;
                }
                if(count < DSD_INPUT_BUFF_SIZE) {
                    for(int i = 0; i < count; i++) {
                        inputBuffer[++inputBufWritePtr] = base_type::_in->readBuf[i];
                        if(inputBufWritePtr >= DSD_INPUT_BUFF_SIZE) inputBufWritePtr = 0;
                        inputBufDataRemains++;
                    }
                    base_type::_in->flush();
                    return getSample();
                } else {
                    flog::error("ERROR! TOO MUCH DATA FOR DSD BUFFER!");
                    return 0;
                }
            }
        }

        int DSD::getSymbol (int have_sync) {
            int i, sum, symbol, count;
            short sample;
            int result;

            sum = 0;
            count = 0;
            for (i = 0; i < samplesPerSymbol; i++) {
                // timing control
                if ((i == 0) && (have_sync == 0)) {
                    if (samplesPerSymbol == 20) {
                        if ((jitter >= 7) && (jitter <= 10)) {
                            i--;
                        } else if ((jitter >= 11) && (jitter <= 14)) {
                            i++;
                        }
                    } else if (rfMod == 1) {
                        if ((jitter >= 0) && (jitter < symbolCenter)) {
                            i++;          // fall back
                        } else if ((jitter > symbolCenter) && (jitter < 10)) {
                            i--;          // catch up
                        }
                    } else if (rfMod == 2) {
                        if ((jitter >= symbolCenter - 1) && (jitter <= symbolCenter)) {
                            i--;
                        } else if ((jitter >= symbolCenter + 1) && (jitter <= symbolCenter + 2)) {
                            i++;
                        }
                    } else if (rfMod == 0) {
                        if ((jitter > 0) && (jitter <= symbolCenter)) {
                            i--;          // catch up
                        } else if ((jitter > symbolCenter) && (jitter < samplesPerSymbol)) {
                            i++;          // fall back
                        }
                    }
                    jitter = -1;
                }
                sample = getSample();

                if (useCosineFilter) {
                    if (lastsynctype >= 10 && lastsynctype <= 13)
                        sample = dmrFilter(sample);
                    else if (lastsynctype == 8 || lastsynctype == 9 || lastsynctype == 16 || lastsynctype == 17) {
                        if(samplesPerSymbol == 20) {
                            sample = nxdnFilter(sample);
                        } else { // the 12.5KHz NXDN filter is the same as the DMR filter
                            sample = dmrFilter(sample);
                        }
                    }
                }

                if ((sample > max) && (have_sync == 1) && (rfMod == 0)) {
                    sample = max;
                } else if ((sample < min) && (have_sync == 1) && (rfMod == 0)) {
                    sample = min;
                }

                if (sample > center) {
                    if (lastsample < center) {
                        numflips += 1;
                    }
                    if (sample > (maxref * 1.25)) {
                        if (lastsample < (maxref * 1.25)) {
                            numflips += 1;
                        }
                        if ((jitter < 0) && (rfMod == 1)) {
                            // first spike out of place
                            jitter = i;
                        }
                        if ((symboltiming == 1) && (have_sync == 0) && (lastsynctype != -1)) {
//                            printf ("O");
                        }
                    } else {
                        if ((symboltiming == 1) && (have_sync == 0) && (lastsynctype != -1)) {
//                            printf ("+");
                        }
                        if ((jitter < 0) && (lastsample < center) && (rfMod != 1)) {
                            // first transition edge
                            jitter = i;
                        }
                    }
                } else {
                    // sample < 0
                    if (lastsample > center) {
                        numflips += 1;
                    }
                    if (sample < (minref * 1.25)) {
                        if (lastsample > (minref * 1.25)) {
                            numflips += 1;
                            }
                        if ((jitter < 0) && (rfMod == 1)){
                            // first spike out of place
                            jitter = i;
                        }
                        if ((symboltiming == 1) && (have_sync == 0) && (lastsynctype != -1)) {
//                            printf ("X");
                        }
                    } else {
                        if ((symboltiming == 1) && (have_sync == 0) && (lastsynctype != -1)) {
//                            printf ("-");
                        }
                        if ((jitter < 0) && (lastsample > center) && (rfMod != 1)) {
                            // first transition edge
                            jitter = i;
                        }
                    }
                }
                if (samplesPerSymbol == 20) {
                    if ((i >= 9) && (i <= 11)) {
                        sum += sample;
                        count++;
                    }
                }
                if (samplesPerSymbol == 5) {
                    if (i == 2) {
                        sum += sample;
                        count++;
                    }
                } else {
                    if (rfMod == 0) {
                        // 0: C4FM modulation

                        if ((i >= symbolCenter - 1) && (i <= symbolCenter + 2)) {
                            sum += sample;
                            count++;
                        }
                    } else {
                        // 1: QPSK modulation
                        // 2: GFSK modulation
                        // Note: this has been changed to use an additional symbol to the left
                        // On the p25_raw_unencrypted.flac it is evident that the timing
                        // comes one sample too late.
                        // This change makes a significant improvement in the BER, at least for
                        // this file.
                        //if ((i == symbolCenter) || (i == symbolCenter + 1))
                        if ((i == symbolCenter - 1) || (i == symbolCenter + 1)) {
                            sum += sample;
                            count++;
                        }
                    }
                }
                lastsample = sample;
            }   // for

            symbol = (sum / count);

            if ((symboltiming == 1) && (have_sync == 0) && (lastsynctype != -1)) {
                if (jitter >= 0) {
//                    printf (" %i\n", jitter);
                } else {
//                    printf ("\n");
                }
            }

            symbolcnt++;
            return (symbol);
        }

        void DSD::use_symbol (int symbol) {
            int i;
            int sbuf2[128];
            int lmin, lmax, lsum;

            for (i = 0; i < ssize; i++) {
                sbuf2[i] = sbuf[i];
            }

            std::qsort (sbuf2, ssize, sizeof (int), comp);

            // continuous update of min/max in rfMod=1 (QPSK) mode
            // in c4fm min/max must only be updated during sync
            if (rfMod == 1) {
                lmin = (sbuf2[0] + sbuf2[1]) / 2;
                lmax = (sbuf2[(ssize - 1)] + sbuf2[(ssize - 2)]) / 2;
                minbuf[midx] = lmin;
                maxbuf[midx] = lmax;
                if (midx == (msize - 1)) {
                    midx = 0;
                } else {
                    midx++;
                }
                lsum = 0;
                for (i = 0; i < msize; i++) {
                    lsum += minbuf[i];
                }
                min = lsum / msize;
                lsum = 0;
                for (i = 0; i < msize; i++) {
                    lsum += maxbuf[i];
                }
                max = lsum / msize;
                center = ((max) + (min)) / 2;
                umid = (((max) - center) * 5 / 8) + center;
                lmid = (((min) - center) * 5 / 8) + center;
                maxref = (int)((max) * 0.80F);
                minref = (int)((min) * 0.80F);
            } else {
                maxref = max;
                minref = min;
            }

            // Increase sidx
            if (sidx == (ssize - 1)) {
                sidx = 0;
            } else {
                sidx++;
            }

            if (dibitBufP > dibitBuf + 900000) {
                dibitBufP = dibitBuf + 200;
            }
        }

        int DSD::digitize (int symbol) {
            // determine dibit state
            if ((synctype == 6) || (synctype == 14)|| (synctype == 18)) {
                //  6 +D-STAR
                // 14 +ProVoice
                // 18 +D-STAR_HD

                if (symbol > center) {
                    *dibitBufP = 1;
                    dibitBufP++;
                    return (0);               // +1
                } else {
                    *dibitBufP = 3;
                    dibitBufP++;
                    return (1);               // +3
                }
            } else if ((synctype == 7) || (synctype == 15)|| (synctype == 19)) {
                //  7 -D-STAR
                // 15 -ProVoice
                // 19 -D-STAR_HD

                if (symbol > center) {
                    *dibitBufP = 1;
                    dibitBufP++;
                    return (1);               // +3
                } else {
                    *dibitBufP = 3;
                    dibitBufP++;
                    return (0);               // +1
                }
            } else if ((synctype == 1) || (synctype == 3) || (synctype == 5) || (synctype == 9) || (synctype == 11) || (synctype == 13) || (synctype == 17)) {
                //  1 -P25p1
                //  3 -X2-TDMA (inverted signal voice frame)
                //  5 -X2-TDMA (inverted signal data frame)
                //  9 -NXDN (inverted voice frame)
                // 11 -DMR (inverted signal voice frame)
                // 13 -DMR (inverted signal data frame)
                // 17 -NXDN (inverted data frame)

                int valid;
                int dibit;

                valid = 0;

                if (synctype == 1) {
                    // Use the P25 heuristics if available
                    valid = invp25Heuristics.estimate_symbol(rfMod, lastDibit, symbol, &dibit);
                }

                if (valid == 0) {
                    // Revert to the original approach: choose the symbol according to the regions delimited
                    // by center, umid and lmid
                    if (symbol > center) {
                        if (symbol > umid) {
                            dibit = 3;               // -3
                        } else {
                            dibit = 2;               // -1
                        }
                    } else {
                        if (symbol < lmid) {
                            dibit = 1;               // +3
                        } else {
                            dibit = 0;               // +2
                        }
                    }
                }

                lastDibit = dibit;

                // store non-inverted values in dibit_buf
                *dibitBufP = invert_dibit(dibit);
                dibitBufP++;
                return dibit;
            } else {
                //  0 +P25p1
                //  2 +X2-TDMA (non inverted signal data frame)
                //  4 +X2-TDMA (non inverted signal voice frame)
                //  8 +NXDN (non inverted voice frame)
                // 10 +DMR (non inverted signal data frame)
                // 12 +DMR (non inverted signal voice frame)
                // 16 +NXDN (non inverted data frame)

                int valid;
                int dibit;

                valid = 0;

                if (synctype == 0) {
                    // Use the P25 heuristics if available
                    valid = p25Heuristics.estimate_symbol(rfMod, lastDibit, symbol, &dibit);
                }

                if (valid == 0) {
                    // Revert to the original approach: choose the symbol according to the regions delimited
                    // by center, umid and lmid
                    if (symbol > center) {
                        if (symbol > umid) {
                            dibit = 1;               // +3
                        } else {
                            dibit = 0;               // +1
                        }
                    } else {
                        if (symbol < lmid) {
                            dibit = 3;               // -3
                        } else {
                            dibit = 2;               // -1
                        }
                    }
                }

                lastDibit = dibit;

                *dibitBufP = dibit;
                dibitBufP++;
                return dibit;
            }
        }

        int DSD::getDibitAndAnalogSignal (int* out_analog_signal) {
            int dibit;
            int symbol = getSymbol(1);
            numflips = 0;
            sbuf[sidx] = symbol;
            if (out_analog_signal != NULL) {
                *out_analog_signal = symbol;
            }
            use_symbol (symbol);
            dibit = digitize (symbol);

            return dibit;
        }

        /**
            * \brief This important method reads analog signal value and digitizes it.
            * Depending of the ongoing transmission it in converted into a bit (0/1) or a di-bit (00/01/10/11).
        */
        int DSD::getDibit () {
            return getDibitAndAnalogSignal(NULL);
        }
        
        void DSD::skipDibit (int count) {
            for(int i = 0; i < count; i++) {
                getDibit();
            }
        }

        void DSD::noCarrier () {
            dibitBufP = dibitBuf + 200;
            memset(dibitBuf, 0, sizeof (int) * 200);
            jitter = -1;
            lastsynctype = -1;
            carrier = 0;
            max = 15000;
            min = -15000;
            center = 0;
            sprintf(errStr, "");
            sprintf(fsubtype, "              ");
            sprintf(ftype, "             ");
            errs = 0;
            errs2 = 0;
            lasttg = 0;
            lastsrc = 0;
            lastp25type = 0;
            repeat = 0;
            nac = 0;
            numtdulc = 0;
            sprintf(slot0light, " slot0 ");
            sprintf(slot1light, " slot1 ");
            firstframe = 0;
            sprintf(algid, "________");
            sprintf(keyid, "________________");
            status_sync = false;
            status_mbedecoding = false;
        }

        void DSD::printFrameInfo () {
            int level;

            level = (int) max / 164;
            if (verbose > 0) {
//                printf ("inlvl: %2i%% ", level);
                status_lvl = level;
            }
            if (nac != 0) {
//                printf ("nac: %4X ", nac);
                status_last_nac = nac;
            }

            if (verbose > 1) {
//                printf ("src: %8i ", lastsrc);
                status_last_src = lastsrc;
            }
//            printf ("tg: %5i ", lasttg);
            status_last_tg = lasttg;
        }

        void DSD::processFrame () {
            int i, j, dibit;
            char duid[3];
            char nac2[13];
            int level;
            char status_0;
            char bch_code[63];
            int index_bch_code;
            unsigned char parity;
            char v;
            int new_nac;
            char new_duid[3];
            int check_result;

            nac2[12] = 0;
            duid[2] = 0;
            j = 0;

            if (rfMod == 1) {
                maxref = (int)(max * 0.80F);
                minref = (int)(min * 0.80F);
            } else {
                maxref = max;
                minref = min;
            }

            if ((synctype == 8) || (synctype == 9)) {
                rfMod = 2;
                nac = 0;
                lastsrc = 0;
                lasttg = 0;
                if (errorbars == 1) {
                    if (verbose > 0) {
                        level = (int) max / 164;
//                        printf ("inlvl: %2i%% ", level);
                        status_lvl = level;
                    }
                }
                nac = 0;
                sprintf(fsubtype, " VOICE        ");
                processNXDNVoice ();
                return;
            } else if ((synctype == 16) || (synctype == 17)) {
                rfMod = 2;
                nac = 0;
                lastsrc = 0;
                lasttg = 0;
                if (errorbars == 1) {
                    if (verbose > 0) {
                        level = (int) max / 164;
//                        printf ("inlvl: %2i%% ", level);
                        status_lvl = level;
                    }
                }
                nac = 0;
                sprintf(fsubtype, " DATA         ");
                processNXDNData ();
                return;
            } else if ((synctype == 6) || (synctype == 7)) {
                nac = 0;
                lastsrc = 0;
                lasttg = 0;
                if (errorbars == 1) {
                    if (verbose > 0) {
                        level = (int) max / 164;
//                        printf ("inlvl: %2i%% ", level);
                        status_lvl = level;
                    }
                }
                nac = 0;
                sprintf(fsubtype, " VOICE        ");
                processDSTAR ();
                return;
            } else if ((synctype == 18) || (synctype == 19)) {
                nac = 0;
                lastsrc = 0;
                lasttg = 0;
                if (errorbars == 1) {
                    if (verbose > 0) {
                        level = (int) max / 164;
//                        printf ("inlvl: %2i%% ", level);
                        status_lvl = level;
                    }
                }
                nac = 0;
                sprintf(fsubtype, " DATA         ");
                processDSTAR_HD ();
                return;
            } else if ((synctype >= 10) && (synctype <= 13)) {
                nac = 0;
                lastsrc = 0;
                lasttg = 0;
                if (errorbars == 1) {
                    if (verbose > 0) {
                        level = (int) max / 164;
//                        printf ("inlvl: %2i%% ", level);
                        status_lvl = level;
                    }
                }
                if ((synctype == 11) || (synctype == 12)) {
                    sprintf(fsubtype, " VOICE        ");
                    processDMRvoice ();
                } else {
                    errStr[0] = 0;
                    processDMRdata ();
                }
                return;
            } else if ((synctype >= 2) && (synctype <= 5)) {
                nac = 0;
                if (errorbars == 1) {
                    printFrameInfo ();
                }
                if ((synctype == 3) || (synctype == 4)) {
                    sprintf(fsubtype, " VOICE        ");
                    processX2TDMAvoice ();
                } else {
                    errStr[0] = 0;
                    processX2TDMAdata ();
                }
                return;
            } else if ((synctype == 14) || (synctype == 15)) {
                nac = 0;
                lastsrc = 0;
                lasttg = 0;
                if (errorbars == 1) {
                    if (verbose > 0) {
                        level = (int) max / 164;
//                        printf ("inlvl: %2i%% ", level);
                        status_lvl = level;
                    }
                }
                sprintf(fsubtype, " VOICE        ");
                processProVoice ();
                return;
            } else {
                // Read the NAC, 12 bits
                j = 0;
                index_bch_code = 0;
                for (i = 0; i < 6; i++) {
                    dibit = getDibit ();

                    v = 1 & (dibit >> 1); // bit 1
                    nac2[j] = v + '0';
                    j++;
                    bch_code[index_bch_code] = v;
                    index_bch_code++;

                    v = 1 & dibit;        // bit 0
                    nac2[j] = v + '0';
                    j++;
                    bch_code[index_bch_code] = v;
                    index_bch_code++;
                }
                nac = strtol (nac2, NULL, 2);

                // Read the DUID, 4 bits
                for (i = 0; i < 2; i++) {
                    dibit = getDibit ();
                    duid[i] = dibit + '0';

                    bch_code[index_bch_code] = 1 & (dibit >> 1);  // bit 1
                    index_bch_code++;
                    bch_code[index_bch_code] = 1 & dibit;         // bit 0
                    index_bch_code++;
                }

                // Read the BCH data for error correction of NAC and DUID
                for (i = 0; i < 3; i++) {
                    dibit = getDibit ();

                    bch_code[index_bch_code] = 1 & (dibit >> 1);  // bit 1
                    index_bch_code++;
                    bch_code[index_bch_code] = 1 & dibit;         // bit 0
                    index_bch_code++;
                }
                // Intermission: read the status dibit
                status_0 = getDibit () + '0';
                // ... continue reading the BCH error correction data
                for (i = 0; i < 20; i++) {
                    dibit = getDibit ();

                    bch_code[index_bch_code] = 1 & (dibit >> 1);  // bit 1
                    index_bch_code++;
                    bch_code[index_bch_code] = 1 & dibit;         // bit 0
                    index_bch_code++;
                }

                // Read the parity bit
                dibit = getDibit ();
                bch_code[index_bch_code] = 1 & (dibit >> 1);      // bit 1
                parity = (1 & dibit);     // bit 0

                // Check if the NID is correct
                check_result = P25checkNID (bch_code, &new_nac, new_duid, parity);
                if (check_result) {
                    if (new_nac != nac) {
                        // NAC fixed by error correction
                        nac = new_nac;
                    }
                    if (strcmp(new_duid, duid) != 0) {
                        // DUID fixed by error correction
                        //printf("Fixing DUID %s -> %s\n", duid, new_duid);
                        duid[0] = new_duid[0];
                        duid[1] = new_duid[1];
                    }
                } else {
                    // Check of NID failed and unable to recover its value
                    //printf("NID error\n");
                    duid[0] = 'E';
                    duid[1] = 'E';
                }
            }

            if (strcmp (duid, "00") == 0) {
                // Header Data Unit
                if (errorbars == 1) {
                    printFrameInfo ();
//                    printf (" HDU\n");
                    status_last_p25_duid = "HDU";
                }
                lastp25type = 2;
                sprintf(fsubtype, " HDU          ");
                P25processHDU ();
            } else if (strcmp (duid, "11") == 0) {
                // Logical Link Data Unit 1
                if (errorbars == 1) {
                    printFrameInfo ();
//                    printf (" LDU1  ");
                    status_last_p25_duid = "LDU1";
                }
                lastp25type = 1;
                sprintf(fsubtype, " LDU1         ");
                numtdulc = 0;
                P25processLDU1 ();
            } else if (strcmp (duid, "22") == 0) {
                // Logical Link Data Unit 2
                if (lastp25type != 1) {
                    if (errorbars == 1) {
                        printFrameInfo ();
//                        printf (" Ignoring LDU2 not preceeded by LDU1\n");
                        status_last_p25_duid = "";
                    }
                    lastp25type = 0;
                    sprintf(fsubtype, "              ");
                } else {
                    if (errorbars == 1) {
                        printFrameInfo ();
//                        printf (" LDU2  ");
                        status_last_p25_duid = "LDU2";
                    }
                    lastp25type = 2;
                    sprintf(fsubtype, " LDU2         ");
                    numtdulc = 0;
                    P25processLDU2 ();
                }
            } else if (strcmp (duid, "33") == 0) {
                // Terminator with subsequent Link Control
                if (errorbars == 1) {
                    printFrameInfo ();
//                    printf (" TDULC\n");
                    status_last_p25_duid = "TDULC";
                }
                lasttg = 0;
                lastsrc = 0;
                lastp25type = 0;
                errStr[0] = 0;
                sprintf(fsubtype, " TDULC        ");
                numtdulc++;
                P25processTDULC ();
                errStr[0] = 0;
            } else if (strcmp (duid, "03") == 0) {
                // Terminator without subsequent Link Control
                if (errorbars == 1) {
                    printFrameInfo ();
//                    printf (" TDU\n");
                    status_last_p25_duid = "TDU";
                }
                lasttg = 0;
                lastsrc = 0;
                lastp25type = 0;
                errStr[0] = 0;
                sprintf(fsubtype, " TDU          ");

                P25processTDU ();
            } else if (strcmp (duid, "13") == 0) {
                if (errorbars == 1) {
                    printFrameInfo ();
//                    printf (" TSDU\n");
                    status_last_p25_duid = "TSDU";
                }
                lasttg = 0;
                lastsrc = 0;
                lastp25type = 3;
                sprintf(fsubtype, " TSDU         ");

                // Now processing NID

                skipDibit (328-25);
            } else if (strcmp (duid, "30") == 0) {
                if (errorbars == 1) {
                    printFrameInfo ();
//                    printf (" PDU\n");
                    status_last_p25_duid = "PDU";
                }
                lastp25type = 4;
                sprintf(fsubtype, " PDU          ");
            } else if (lastp25type == 1) {
                // try to guess based on previous frame if unknown type
                if (errorbars == 1) {
                    printFrameInfo ();
//                    printf ("(LDU2) ");
                    status_last_p25_duid = "(LDU2)";
                }
                // Guess that the state is LDU2
                lastp25type = 2;
                sprintf(fsubtype, "(LDU2)        ");
                numtdulc = 0;
                P25processLDU2 ();
            } else if (lastp25type == 2) {
                if (errorbars == 1) {
                    printFrameInfo ();
//                    printf ("(LDU1) ");
                    status_last_p25_duid = "(LDU1)";
                }
                // Guess that the state is LDU1
                lastp25type = 1;
                sprintf(fsubtype, "(LDU1)        ");
                numtdulc = 0;
                P25processLDU1 ();
            } else if (lastp25type == 3) {
                if (errorbars == 1) {
                    printFrameInfo ();
//                    printf (" (TSDU)\n");
                    status_last_p25_duid = "(TSDU)";
                }
                // Guess that the state is TSDU
                lastp25type = 3;
                sprintf(fsubtype, "(TSDU)        ");

                // Now processing NID

                skipDibit (328-25);
            } else if (lastp25type == 4) {
                if (errorbars == 1) {
                    printFrameInfo ();
//                    printf (" (PDU)\n");
                    status_last_p25_duid = "(PDU)";
                }
                lastp25type = 0;
            } else if(strcmp (duid, "EE") != 0) {
                lastp25type = 0;
                sprintf(fsubtype, "              ");
                if (errorbars == 1) {
                    printFrameInfo ();
//                    printf (" duid:%s *Unknown DUID*\n", duid);
                    status_last_p25_duid = "?(" + std::string(duid) + ")";
                }
            }
        }

        void DSD::printFrameSync (std::string frametype, int offset, char *modulation) {

//            if (verbose > 0) {
//                printf ("Sync: %s ", frametype.c_str());
//            }
//            if (verbose > 2) {
//                printf ("o: %4i ", offset);
//            }
//            if (verbose > 1) {
//                printf ("mod: %s ", modulation);
//            }
            status_last_proto = frametype;
            status_sync = true;
        }

        void DSD::resetFrameSync() {
            for (int i = 18; i < 24; i++) {
                framesynclbuf[i] = 0;
                framesynclbuf2[i] = 0;
            }
            framesyncSymbolsRead = 0;
            framesynctest[24] = 0;
            framesynctest18[18] = 0;
            framesynctest32[32] = 0;
            framesynctest_pos = 0;
            framesynctest_p = framesynctest_buf + 10;
            framesyncsync = 0;
            framesynclmin = 0;
            framesynclmax = 0;
            framesynclidx = 0;
            framesynclastt = 0;
            numflips = 0;
            if ((symboltiming == 1) && (carrier == 1)) {
//                printf ("\nSymbol Timing:\n");
            }
        }

        int DSD::getFrameSync() {
            int symbol, i, lsum, dibit;
            /* detects frame sync and returns frame type
            *  0 = +P25p1
            *  1 = -P25p1
            *  2 = +X2-TDMA (non inverted signal data frame)
            *  3 = -X2-TDMA (inverted signal voice frame)
            *  4 = +X2-TDMA (non inverted signal voice frame)
            *  5 = -X2-TDMA (inverted signal data frame)
            *  6 = +D-STAR
            *  7 = -D-STAR
            *  8 = +NXDN (non inverted voice frame)
            *  9 = -NXDN (inverted voice frame)
            * 10 = +DMR (non inverted signal data frame)
            * 11 = -DMR (inverted signal voice frame)
            * 12 = +DMR (non inverted signal voice frame)
            * 13 = -DMR (inverted signal data frame)
            * 14 = +ProVoice
            * 15 = -ProVoice
            * 16 = +NXDN (non inverted data frame)
            * 17 = -NXDN (inverted data frame)
            * 18 = +D-STAR_HD
            * 19 = -D-STAR_HD
            * -1 = no sync
            * -2 = not enough symbols
            */
            framesyncSymbolsRead++;
            symbol = getSymbol(0);
            sbuf[sidx] = symbol;
            if (sidx == (ssize - 1)) {
                sidx = 0;
            }

            framesynclbuf[framesynclidx] = symbol;
            if (framesynclidx == 23) {
                framesynclidx = 0;
            } else {
                framesynclidx++;
            }

            if (framesynclastt == 23) {
                framesynclastt = 0;
                if (numflips > modThreshold) {
                    if (modQpsk == 1) {
                        rfMod = 1;
                    }
                } else if (numflips > 18) {
                    if (modGfsk == 1) {
                        rfMod = 2;
                    }
                } else {
                    if (modC4fm == 1) {
                        rfMod = 0;
                    }
                }
                numflips = 0;
            } else {
                framesynclastt++;
            }

            if (dibitBufP > dibitBuf + 900000) {
                dibitBufP = dibitBuf + 200;
            }

            //determine dibit state
            if (symbol > 0) {
                *dibitBufP = 1;
                dibitBufP++;
                dibit = 49;               // '1'
            } else {
                *dibitBufP = 3;
                dibitBufP++;
                dibit = 51;               // '3'
            }

            *framesynctest_p = dibit;
            if (framesyncSymbolsRead >= 18) {
                for (i = 0; i < 24; i++) {
                    framesynclbuf2[i] = framesynclbuf[i];
                }
                qsort (framesynclbuf2, 24, sizeof (int), comp);
                framesynclmin = (framesynclbuf2[2] + framesynclbuf2[3] + framesynclbuf2[4]) / 3;
                framesynclmax = (framesynclbuf2[21] + framesynclbuf2[20] + framesynclbuf2[19]) / 3;

                if (rfMod == 1) {
                    minbuf[midx] = framesynclmin;
                    maxbuf[midx] = framesynclmax;
                    if (midx == (msize - 1)) {
                        midx = 0;
                    } else {
                        midx++;
                    }
                    lsum = 0;
                    for (i = 0; i < msize; i++) {
                        lsum += minbuf[i];
                    }
                    min = lsum / msize;
                    lsum = 0;
                    for (i = 0; i < msize; i++) {
                        lsum += maxbuf[i];
                    }
                    max = lsum / msize;
                    center = ((max) + (min)) / 2;
                }

                if (rfMod == 0) {
                    sprintf (framesyncmodulation, "C4FM");
                } else if (rfMod == 1) {
                    sprintf (framesyncmodulation, "QPSK");
                } else if (rfMod == 2) {
                    sprintf (framesyncmodulation, "GFSK");
                }

                strncpy (framesynctest, (framesynctest_p - 23), 24);
                if (frameP25p1 == 1) {
                    if (strcmp (framesynctest, P25P1_SYNC) == 0) {
                        carrier = 1;
                        offset = framesynctest_pos;
                        max = ((max) + framesynclmax) / 2;
                        min = ((min) + framesynclmin) / 2;
                        sprintf(ftype, " P25 Phase 1 ");
                        if (errorbars == 1) {
                            printFrameSync ( "+P25p1", framesynctest_pos + 1, framesyncmodulation);
                        }
                        lastsynctype = 0;
                        return (0);
                    }
                    if (strcmp (framesynctest, INV_P25P1_SYNC) == 0) {
                        carrier = 1;
                        offset = framesynctest_pos;
                        max = ((max) + framesynclmax) / 2;
                        min = ((min) + framesynclmin) / 2;
                        sprintf(ftype, " P25 Phase 1 ");
                        if (errorbars == 1) {
                            printFrameSync ( "-P25p1", framesynctest_pos + 1, framesyncmodulation);
                        }
                        lastsynctype = 1;
                        return (1);
                    }
                }
                if (frameX2tdma == 1) {
                    if ((strcmp (framesynctest, X2TDMA_BS_DATA_SYNC) == 0) || (strcmp (framesynctest, X2TDMA_MS_DATA_SYNC) == 0)) {
                        carrier = 1;
                        offset = framesynctest_pos;
                        max = ((max) + (framesynclmax)) / 2;
                        min = ((min) + (framesynclmin)) / 2;
                        if (invertedX2tdma == 0) {
                            // data frame
                            sprintf(ftype, " X2-TDMA     ");
                            if (errorbars == 1) {
                                printFrameSync ( "+X2-TDMA", framesynctest_pos + 1, framesyncmodulation);
                            }
                            lastsynctype = 2;
                            return (2);
                        } else {
                            // inverted voice frame
                            sprintf(ftype, " X2-TDMA     ");
                            if (errorbars == 1) {
                                printFrameSync ( "-X2-TDMA", framesynctest_pos + 1, framesyncmodulation);
                            }
                            if (lastsynctype != 3) {
                                firstframe = 1;
                            }
                            lastsynctype = 3;
                            return (3);
                        }
                    }
                    if ((strcmp (framesynctest, X2TDMA_BS_VOICE_SYNC) == 0) || (strcmp (framesynctest, X2TDMA_MS_VOICE_SYNC) == 0)) {
                        carrier = 1;
                        offset = framesynctest_pos;
                        max = ((max) + framesynclmax) / 2;
                        min = ((min) + framesynclmin) / 2;
                        if (invertedX2tdma == 0) {
                            // voice frame
                            sprintf(ftype, " X2-TDMA     ");
                            if (errorbars == 1) {
                                printFrameSync ( "+X2-TDMA", framesynctest_pos + 1, framesyncmodulation);
                            }
                            if (lastsynctype != 4) {
                                firstframe = 1;
                            }
                            lastsynctype = 4;
                            return (4);
                        } else {
                            // inverted data frame
                            sprintf(ftype, " X2-TDMA     ");
                            if (errorbars == 1) {
                                printFrameSync ( "-X2-TDMA", framesynctest_pos + 1, framesyncmodulation);
                            }
                            lastsynctype = 5;
                            return (5);
                        }
                    }
                }
                if (frameDmr == 1) {
                    if ((strcmp (framesynctest, DMR_MS_DATA_SYNC) == 0) || (strcmp (framesynctest, DMR_BS_DATA_SYNC) == 0)) {
                        carrier = 1;
                        offset = framesynctest_pos;
                        max = ((max) + (framesynclmax)) / 2;
                        min = ((min) + (framesynclmin)) / 2;
                        if (invertedDmr == 0) {
                            // data frame
                            sprintf(ftype, " DMR         ");
                            if (errorbars == 1){
                                printFrameSync ( "+DMR", framesynctest_pos + 1, framesyncmodulation);
                            }
                            lastsynctype = 10;
                            return (10);
                        } else {
                            // inverted voice frame
                            sprintf(ftype, " DMR         ");
                            if (errorbars == 1) {
                                printFrameSync ( "-DMR", framesynctest_pos + 1, framesyncmodulation);
                            }
                            if (lastsynctype != 11) {
                                firstframe = 1;
                            }
                            lastsynctype = 11;
                            return (11);
                        }
                    }
                    if ((strcmp (framesynctest, DMR_MS_VOICE_SYNC) == 0) || (strcmp (framesynctest, DMR_BS_VOICE_SYNC) == 0)) {
                        carrier = 1;
                        offset = framesynctest_pos;
                        max = ((max) + framesynclmax) / 2;
                        min = ((min) + framesynclmin) / 2;
                        if (invertedDmr == 0) {
                            // voice frame
                            sprintf(ftype, " DMR         ");
                            if (errorbars == 1) {
                                printFrameSync ( "+DMR", framesynctest_pos + 1, framesyncmodulation);
                            }
                            if (lastsynctype != 12) {
                                firstframe = 1;
                            }
                            lastsynctype = 12;
                            return (12);
                        } else {
                            // inverted data frame
                            sprintf(ftype, " DMR         ");
                            if (errorbars == 1) {
                                printFrameSync ( "-DMR", framesynctest_pos + 1, framesyncmodulation);
                            }
                            lastsynctype = 13;
                            return (13);
                        }
                    }
                }
                if (frameProvoice == 1) {
                    strncpy (framesynctest32, (framesynctest_p - 31), 32);
                    if ((strcmp (framesynctest32, PROVOICE_SYNC) == 0) || (strcmp (framesynctest32, PROVOICE_EA_SYNC) == 0)) {
                        carrier = 1;
                        offset = framesynctest_pos;
                        max = ((max) + framesynclmax) / 2;
                        min = ((min) + framesynclmin) / 2;
                        sprintf(ftype, " ProVoice    ");
                        if (errorbars == 1) {
                            printFrameSync ( "-ProVoice", framesynctest_pos + 1, framesyncmodulation);
                        }
                        lastsynctype = 14;
                        return (14);
                    } else if ((strcmp (framesynctest32, INV_PROVOICE_SYNC) == 0) || (strcmp (framesynctest32, INV_PROVOICE_EA_SYNC) == 0)) {
                        carrier = 1;
                        offset = framesynctest_pos;
                        max = ((max) + framesynclmax) / 2;
                        min = ((min) + framesynclmin) / 2;
                        sprintf(ftype, " ProVoice    ");
                        if (errorbars == 1) {
                            printFrameSync ( "-ProVoice", framesynctest_pos + 1, framesyncmodulation);
                        }
                        lastsynctype = 15;
                        return (15);
                    }
                }
                if ((frameNxdn96 == 1) || (frameNxdn48 == 1)) {
                    strncpy (framesynctest18, (framesynctest_p - 17), 18);
                    if ((strcmp (framesynctest18, NXDN_BS_VOICE_SYNC) == 0) || (strcmp (framesynctest18, NXDN_MS_VOICE_SYNC) == 0)) {
                        if ((lastsynctype == 8) || (lastsynctype == 16)) {
                            carrier = 1;
                            offset = framesynctest_pos;
                            max = ((max) + framesynclmax) / 2;
                            min = ((min) + framesynclmin) / 2;
                            if (samplesPerSymbol == 20) {
                                sprintf(ftype, " NXDN48      ");
                                if (errorbars == 1) {
                                    printFrameSync ( "+NXDN48", framesynctest_pos + 1, framesyncmodulation);
                                }
                            } else {
                                sprintf(ftype, " NXDN96      ");
                                if (errorbars == 1) {
                                    printFrameSync ( "+NXDN96", framesynctest_pos + 1, framesyncmodulation);
                                }
                            }
                            lastsynctype = 8;
                            return (8);
                        } else {
                            lastsynctype = 8;
                        }
                    } else if ((strcmp (framesynctest18, INV_NXDN_BS_VOICE_SYNC) == 0) || (strcmp (framesynctest18, INV_NXDN_MS_VOICE_SYNC) == 0)) {
                        if ((lastsynctype == 9) || (lastsynctype == 17)) {
                            carrier = 1;
                            offset = framesynctest_pos;
                            max = ((max) + framesynclmax) / 2;
                            min = ((min) + framesynclmin) / 2;
                            if (samplesPerSymbol == 20) {
                                sprintf(ftype, " NXDN48      ");
                                if (errorbars == 1) {
                                    printFrameSync ( "-NXDN48", framesynctest_pos + 1, framesyncmodulation);
                                }
                            } else {
                                sprintf(ftype, " NXDN96      ");
                                if (errorbars == 1) {
                                    printFrameSync ( "-NXDN96", framesynctest_pos + 1, framesyncmodulation);
                                }
                            }
                            lastsynctype = 9;
                            return (9);
                        } else {
                            lastsynctype = 9;
                        }
                    } else if ((strcmp (framesynctest18, NXDN_BS_DATA_SYNC) == 0) || (strcmp (framesynctest18, NXDN_MS_DATA_SYNC) == 0)) {
                        if ((lastsynctype == 8) || (lastsynctype == 16)) {
                            carrier = 1;
                            offset = framesynctest_pos;
                            max = ((max) + framesynclmax) / 2;
                            min = ((min) + framesynclmin) / 2;
                            if (samplesPerSymbol == 20) {
                                sprintf(ftype, " NXDN48      ");
                                if (errorbars == 1) {
                                    printFrameSync ( "+NXDN48", framesynctest_pos + 1, framesyncmodulation);
                                }
                            } else {
                                sprintf(ftype, " NXDN96      ");
                                if (errorbars == 1) {
                                    printFrameSync ( "+NXDN96", framesynctest_pos + 1, framesyncmodulation);
                                }
                            }
                            lastsynctype = 16;
                            return (16);
                        } else {
                            lastsynctype = 16;
                        }
                    } else if ((strcmp (framesynctest18, INV_NXDN_BS_DATA_SYNC) == 0) || (strcmp (framesynctest18, INV_NXDN_MS_DATA_SYNC) == 0)) {
                        if ((lastsynctype == 9) || (lastsynctype == 17)) {
                            carrier = 1;
                            offset = framesynctest_pos;
                            max = ((max) + framesynclmax) / 2;
                            min = ((min) + framesynclmin) / 2;
                            if (samplesPerSymbol == 20) {
                                sprintf(ftype, " NXDN48      ");
                                if (errorbars == 1) {
                                    printFrameSync ( "-NXDN48", framesynctest_pos + 1, framesyncmodulation);
                                }
                            } else {
                                sprintf(ftype, " NXDN96      ");
                                if (errorbars == 1) {
                                    printFrameSync ( "-NXDN96", framesynctest_pos + 1, framesyncmodulation);
                                }
                            }
                            lastsynctype = 17;
                            return (17);
                        } else {
                            lastsynctype = 17;
                        }
                    }
                }
                if (frameDstar == 1) {
                    if (strcmp (framesynctest, DSTAR_SYNC) == 0) {
                        carrier = 1;
                        offset = framesynctest_pos;
                        max = ((max) + framesynclmax) / 2;
                        min = ((min) + framesynclmin) / 2;
                        sprintf(ftype, " D-STAR      ");
                        if (errorbars == 1) {
                            printFrameSync ( "+D-STAR", framesynctest_pos + 1, framesyncmodulation);
                        }
                        lastsynctype = 6;
                        return (6);
                    }
                    if (strcmp (framesynctest, INV_DSTAR_SYNC) == 0) {
                        carrier = 1;
                        offset = framesynctest_pos;
                        max = ((max) + framesynclmax) / 2;
                        min = ((min) + framesynclmin) / 2;
                        sprintf(ftype, " D-STAR      ");
                        if (errorbars == 1) {
                            printFrameSync ( "-D-STAR", framesynctest_pos + 1, framesyncmodulation);
                        }
                        lastsynctype = 7;
                        return (7);
                    }
                    if (strcmp (framesynctest, DSTAR_HD) == 0) {
                        carrier = 1;
                        offset = framesynctest_pos;
                        max = ((max) + framesynclmax) / 2;
                        min = ((min) + framesynclmin) / 2;
                        sprintf(ftype, " D-STAR_HD   ");
                        if (errorbars == 1)
                            {
                            printFrameSync ( "+D-STAR_HD", framesynctest_pos + 1, framesyncmodulation);
                            }
                        lastsynctype = 18;
                        return (18);
                    }
                    if (strcmp (framesynctest, INV_DSTAR_HD) == 0) {
                        carrier = 1;
                        offset = framesynctest_pos;
                        max = ((max) + framesynclmax) / 2;
                        min = ((min) + framesynclmin) / 2;
                        sprintf(ftype, " D-STAR_HD   ");
                        if (errorbars == 1) {
                            printFrameSync ( "-D-STAR_HD", framesynctest_pos + 1, framesyncmodulation);
                        }
                        lastsynctype = 19;
                        return (19);
                    }
                }

                if ((framesyncSymbolsRead == 24) && (lastsynctype != -1)) {
                    if ((lastsynctype == 0) && ((lastp25type == 1) || (lastp25type == 2))) {
                        carrier = 1;
                        offset = framesynctest_pos;
                        max = ((max) + (framesynclmax)) / 2;
                        min = ((min) + (framesynclmin)) / 2;
                        sprintf(ftype, "(P25 Phase 1)");
                        if (errorbars == 1) {
                            printFrameSync ( "(+P25p1)", framesynctest_pos + 1, framesyncmodulation);
                        }
                        lastsynctype = -1;
                        return (0);
                    } else if ((lastsynctype == 1) && ((lastp25type == 1) || (lastp25type == 2))) {
                        carrier = 1;
                        offset = framesynctest_pos;
                        max = ((max) + framesynclmax) / 2;
                        min = ((min) + framesynclmin) / 2;
                        sprintf(ftype, "(P25 Phase 1)");
                        if (errorbars == 1) {
                            printFrameSync ( "(-P25p1)", framesynctest_pos + 1, framesyncmodulation);
                        }
                        lastsynctype = -1;
                        return (1);
                    } else if ((lastsynctype == 3) && ((strcmp (framesynctest, X2TDMA_BS_VOICE_SYNC) != 0) || (strcmp (framesynctest, X2TDMA_MS_VOICE_SYNC) != 0))) {
                        carrier = 1;
                        offset = framesynctest_pos;
                        max = ((max) + framesynclmax) / 2;
                        min = ((min) + framesynclmin) / 2;
                        sprintf(ftype, "(X2-TDMA)    ");
                        if (errorbars == 1) {
                            printFrameSync ( "(-X2-TDMA)", framesynctest_pos + 1, framesyncmodulation);
                        }
                        lastsynctype = -1;
                        return (3);
                    } else if ((lastsynctype == 4) && ((strcmp (framesynctest, X2TDMA_BS_DATA_SYNC) != 0) || (strcmp (framesynctest, X2TDMA_MS_DATA_SYNC) != 0))) {
                        carrier = 1;
                        offset = framesynctest_pos;
                        max = ((max) + framesynclmax) / 2;
                        min = ((min) + framesynclmin) / 2;
                        sprintf(ftype, "(X2-TDMA)    ");
                        if (errorbars == 1) {
                            printFrameSync ( "(+X2-TDMA)", framesynctest_pos + 1, framesyncmodulation);
                        }
                        lastsynctype = -1;
                        return (4);
                    } else if ((lastsynctype == 11) && ((strcmp (framesynctest, DMR_BS_VOICE_SYNC) != 0) || (strcmp (framesynctest, DMR_MS_VOICE_SYNC) != 0))) {
                        carrier = 1;
                        offset = framesynctest_pos;
                        max = ((max) + framesynclmax) / 2;
                        min = ((min) + framesynclmin) / 2;
                        sprintf(ftype, "(DMR)        ");
                        if (errorbars == 1) {
                            printFrameSync ( "(-DMR)", framesynctest_pos + 1, framesyncmodulation);
                        }
                        lastsynctype = -1;
                        return (11);
                    } else if ((lastsynctype == 12) && ((strcmp (framesynctest, DMR_BS_DATA_SYNC) != 0) || (strcmp (framesynctest, DMR_MS_DATA_SYNC) != 0))) {
                        carrier = 1;
                        offset = framesynctest_pos;
                        max = ((max) + framesynclmax) / 2;
                        min = ((min) + framesynclmin) / 2;
                        sprintf(ftype, "(DMR)        ");
                        if (errorbars == 1) {
                            printFrameSync ( "(+DMR)", framesynctest_pos + 1, framesyncmodulation);
                        }
                        lastsynctype = -1;
                        return (12);
                    }
                }
            }

            if (framesynctest_pos < 10200) {
                framesynctest_pos++;
                framesynctest_p++;
            } else {
                // buffer reset
                framesynctest_pos = 0;
                framesynctest_p = framesynctest_buf;
                noCarrier();
            }

            if (lastsynctype != 1){
                if (framesynctest_pos >= 1800){
                    if ((errorbars == 1) && (verbose > 1) && (carrier == 1)) {
//                        printf ("Sync: no sync\n");
                    }
                    noCarrier();
                    return (-1);
                }
            }
            return -2;
        }

        short DSD::dmrFilter(short sample) {
            float sum; int i;
            for (i = 0; i < DMR_FILT_ZEROS; i++)
                dmrFiltV[i] = dmrFiltV[i+1];

            dmrFiltV[DMR_FILT_ZEROS] = sample; // unfiltered sample in
            sum = 0.0f;

            for (i = 0; i <= DMR_FILT_ZEROS; i++)
                sum += (dmrFiltTaps[i] * dmrFiltV[i]);

            return (short)(sum / dmrFiltGain); // filtered sample out
        }

        short DSD::nxdnFilter(short sample) {
            float sum; int i;
            for (i = 0; i < NXDN_FILT_ZEROS; i++)
                nxdnFiltV[i] = nxdnFiltV[i+1];

            nxdnFiltV[NXDN_FILT_ZEROS] = sample; // unfiltered sample in
            sum = 0.0f;

            for (i = 0; i <= NXDN_FILT_ZEROS; i++)
                sum += (nxdnFiltTaps[i] * nxdnFiltV[i]);

            return (short)(sum / nxdnFiltGain); // filtered sample out
        }

};
