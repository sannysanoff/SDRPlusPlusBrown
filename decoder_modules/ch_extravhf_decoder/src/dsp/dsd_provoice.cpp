#include <config.h>

#include "dsd.h"

namespace dsp {
    void DSD::processProVoice () {
        int i, j, dibit;

        char imbe7100_fr1[7][24];
        char imbe7100_fr2[7][24];
        const int *w, *x;

        if (errorbars == 1) {
            status_mbedecoding = true;
            status_errorbar = "";
//            printf ("VOICE e:");
        }

        for (i = 0; i < 64; i++) {
            dibit = getDibit ();
        }

        // lid
        for (i = 0; i < 16; i++) {
            dibit = getDibit ();
        }

        for (i = 0; i < 64; i++) {
            dibit = getDibit ();
        }

        // imbe frames 1,2 first half
        w = pW;
        x = pX;

        for (i = 0; i < 11; i++) {
            for (j = 0; j < 6; j++) {
                dibit = getDibit ();
                imbe7100_fr1[*w][*x] = dibit;
                w++;
                x++;
            }
            w -= 6;
            x -= 6;
            for (j = 0; j < 6; j++) {
                dibit = getDibit ();
                imbe7100_fr2[*w][*x] = dibit;
                w++;
                x++;
            }
        }

        for (j = 0; j < 6; j++) {
            dibit = getDibit ();
            imbe7100_fr1[*w][*x] = dibit;
            w++;
            x++;
        }
        w -= 6;
        x -= 6;
        for (j = 0; j < 4; j++)  {
            dibit = getDibit ();
            imbe7100_fr2[*w][*x] = dibit;
            w++;
            x++;
        }

        // spacer bits
        dibit = getDibit ();
        dibit = getDibit ();

        // imbe frames 1,2 second half

        for (j = 0; j < 2; j++) {
            dibit = getDibit ();
            imbe7100_fr2[*w][*x] = dibit;
            w++;
            x++;
        }

        for (i = 0; i < 3; i++) {
            for (j = 0; j < 6; j++) {
                dibit = getDibit ();
                imbe7100_fr1[*w][*x] = dibit;
                w++;
                x++;
            }
            w -= 6;
            x -= 6;
            for (j = 0; j < 6; j++) {
                dibit = getDibit ();
                imbe7100_fr2[*w][*x] = dibit;
                w++;
                x++;
            }
        }

        for (j = 0; j < 5; j++) {
            dibit = getDibit ();
            imbe7100_fr1[*w][*x] = dibit;
            w++;
            x++;
        }
        w -= 5;
        x -= 5;
        for (j = 0; j < 5; j++) {
            dibit = getDibit ();
            imbe7100_fr2[*w][*x] = dibit;
            w++;
            x++;
        }

        for (i = 0; i < 7; i++) {
            for (j = 0; j < 6; j++) {
                dibit = getDibit ();
                imbe7100_fr1[*w][*x] = dibit;
                w++;
                x++;
            }
            w -= 6;
            x -= 6;
            for (j = 0; j < 6; j++) {
                dibit = getDibit ();
                imbe7100_fr2[*w][*x] = dibit;
                w++;
                x++;
            }
        }

        for (j = 0; j < 5; j++) {
            dibit = getDibit ();
            imbe7100_fr1[*w][*x] = dibit;
            w++;
            x++;
        }
        w -= 5;
        x -= 5;
        for (j = 0; j < 5; j++) {
            dibit = getDibit ();
            imbe7100_fr2[*w][*x] = dibit;
            w++;
            x++;
        }

        processMbeFrame (NULL, NULL, imbe7100_fr1);
        processMbeFrame (NULL, NULL, imbe7100_fr2);

        // spacer bits
        dibit = getDibit ();
        dibit = getDibit ();

        for (i = 0; i < 16; i++) {
            dibit = getDibit ();
        }

        // imbe frames 3,4 first half
        w = pW;
        x = pX;
        for (i = 0; i < 11; i++) {
            for (j = 0; j < 6; j++) {
                dibit = getDibit ();
                imbe7100_fr1[*w][*x] = dibit;
                w++;
                x++;
            }
            w -= 6;
            x -= 6;
            for (j = 0; j < 6; j++) {
                dibit = getDibit ();
                imbe7100_fr2[*w][*x] = dibit;
                w++;
                x++;
            }
        }
        for (j = 0; j < 6; j++) {
            dibit = getDibit ();
            imbe7100_fr1[*w][*x] = dibit;
            w++;
            x++;
        }
        w -= 6;
        x -= 6;
        for (j = 0; j < 4; j++) {
            dibit = getDibit ();
            imbe7100_fr2[*w][*x] = dibit;
            w++;
            x++;
        }

        // spacer bits
        dibit = getDibit ();
        dibit = getDibit ();

        // imbe frames 3,4 second half
        for (j = 0; j < 2; j++) {
            dibit = getDibit ();
            imbe7100_fr2[*w][*x] = dibit;
            w++;
            x++;
        }
        for (i = 0; i < 3; i++) {
            for (j = 0; j < 6; j++) {
                dibit = getDibit ();
                imbe7100_fr1[*w][*x] = dibit;
                w++;
                x++;
            }
            w -= 6;
            x -= 6;
            for (j = 0; j < 6; j++) {
                dibit = getDibit ();
                imbe7100_fr2[*w][*x] = dibit;
                w++;
                x++;
            }
        }

        for (j = 0; j < 5; j++) {
            dibit = getDibit ();
            imbe7100_fr1[*w][*x] = dibit;
            w++;
            x++;
        }
        w -= 5;
        x -= 5;
        for (j = 0; j < 5; j++) {
            dibit = getDibit ();
            imbe7100_fr2[*w][*x] = dibit;
            w++;
            x++;
        }

        for (i = 0; i < 7; i++) {
            for (j = 0; j < 6; j++) {
                dibit = getDibit ();
                imbe7100_fr1[*w][*x] = dibit;
                w++;
                x++;
            }
            w -= 6;
            x -= 6;
            for (j = 0; j < 6; j++) {
                dibit = getDibit ();
                imbe7100_fr2[*w][*x] = dibit;
                w++;
                x++;
            }
        }

        for (j = 0; j < 5; j++) {
            dibit = getDibit ();
            imbe7100_fr1[*w][*x] = dibit;
            w++;
            x++;
        }
        w -= 5;
        x -= 5;
        for (j = 0; j < 5; j++) {
            dibit = getDibit ();
            imbe7100_fr2[*w][*x] = dibit;
            w++;
            x++;
        }

        processMbeFrame (NULL, NULL, imbe7100_fr1);
        processMbeFrame (NULL, NULL, imbe7100_fr2);

        // spacer bits
        dibit = getDibit ();
        dibit = getDibit ();

        if (errorbars == 1) {
            status_mbedecoding = false;
//            printf ("\n");
        }
    }
}
