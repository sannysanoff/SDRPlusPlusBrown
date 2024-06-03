#include <config.h>
#include "dsd.h"

namespace dsp {
    void DSD::processNXDNData () {
        int i, dibit;

        if (errorbars == 1) {
//            printf ("DATA    ");
            status_last_nxdn_type = "DATA";
        }

        for (i = 0; i < 30; i++) {
            dibit = getDibit ();
        }

        for (i = 0; i < 144; i++) {
            dibit = getDibit ();
        }

        if (errorbars == 1) {
//            printf ("\n");
        }
    }

    void DSD::processNXDNVoice () {
        int i, j, dibit;
        char ambe_fr[4][24];
        const int *w, *x, *y, *z;
        const char *pr;

        if (errorbars == 1) {
            status_mbedecoding = true;
            status_errorbar = "";
//            printf ("VOICE e:");
            status_last_nxdn_type = "VOICE";
        }

        for (i = 0; i < 30; i++) {
            dibit = getDibit ();
        }

        pr = nxdnpr;
        for (j = 0; j < 4; j++) {
            w = nW;
            x = nX;
            y = nY;
            z = nZ;
            for (i = 0; i < 36; i++) {
                dibit = getDibit ();
                ambe_fr[*w][*x] = *pr ^ (1 & (dibit >> 1));   // bit 1
                pr++;
                ambe_fr[*y][*z] = (1 & dibit);        // bit 0
                w++;
                x++;
                y++;
                z++;
            }
            processMbeFrame (NULL, ambe_fr, NULL);
        }

        if (errorbars == 1) {
            status_mbedecoding = false;
//            printf ("\n");
        }
    }
}
