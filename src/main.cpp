#include <core.h>

#ifdef __APPLE__
extern "C" {
    void macosInit();
}
#endif

#include "../decoder_modules/cw_decoder/src/cwdecoder.h"

int main(int argc, char* argv[]) {
#ifdef __APPLE__
    macosInit();
#endif
    if (getenv("TEST_CW_COND") != nullptr) {
        cw_test();
        exit(0);
    }
    return sdrpp_main(argc, argv);
}