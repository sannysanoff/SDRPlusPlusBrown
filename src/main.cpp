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
    cw_test();
    return sdrpp_main(argc, argv);
}