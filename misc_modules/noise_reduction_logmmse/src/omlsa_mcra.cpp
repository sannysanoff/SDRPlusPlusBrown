
#include "omlsa_mcra.h"
#include "omlsa_mcra/Datablock_Read.h"

namespace dsp {

    void omlsa_mcra::reset() {
        dtr = std::make_shared<Datablock_Read>(sampleRate, 1, 10000);
    }

    int omlsa_mcra::blockSize() {
        return 3*dtr->m_wlen15;
    }

    bool omlsa_mcra::process(short *in, int incount, short *out, int &outcount) {
        if (!dtr) {
            flog::info("ERROR omlsa_mcra: !dtr");
            memcpy(out, in, incount*sizeof(short));
            outcount = incount;
            return true;
        }
        short abnormal_flag = dtr->Data_procese(in, out, incount, outcount);
        if (abnormal_flag < 0) return false;
        return true;
    }

}
