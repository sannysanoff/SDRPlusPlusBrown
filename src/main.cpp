#include <core.h>

#ifdef __APPLE__
extern "C" {
    void macosInit();
}
#endif


int main(int argc, char* argv[]) {
#ifdef __APPLE__
    macosInit();
#endif

    return sdrpp_main(argc, argv);
}