#pragma once

#include "utils/arrays.h"
#include "math.h"
#include "bgnoise.h"
#include <array>
#include <list>
#include <ctm.h>

class Datablock_Read;

namespace dsp {
    struct omlsa_mcra {

        std::shared_ptr<Datablock_Read> dtr;
        int sampleRate = 0;
        float compensation = 1.0f;   // share of the estimated noise that is subtracted
        omlsa_mcra() {
        }

        void reset();

        void setCompensation(float c);   // (in omlsa_mcra.cpp: Datablock_Read is incomplete here)

        void setSampleRate(int sampleRate) {
            this->sampleRate = sampleRate;
            reset();
        }

        int blockSize();

        bool process(short *in, int incount, short *out, int &outcount);


    };
}
