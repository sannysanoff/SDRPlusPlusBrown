#include <iostream>
#include <stdio.h>
#include <module.h>
#include <ctm.h>
#include "dsp/types.h"

#include "ft8_etc/mshv_support.h"
#include "ft8_etc/mscore.h"
#include "ft8_etc/decoderms.h"

#ifdef __linux__
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif

#ifdef __APPLE__
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#endif

#include <utils/usleep.h>
#include <utils/strings.h>

namespace ft8 {
    enum {
        DMS_FT8 = 11,
        DMS_FT4 = 13
    } DecoderMSMode;


    // input stereo samples, nsamples (number of pairs of float)
    void decodeFT8(int threads, const char *mode, int sampleRate, dsp::stereo_t *samples, int nsamples,
                   std::function<void(int mode, QStringList result)> callback) {
        //
        // .
        //
        char b[10];
        // debugPrintf("# hello here 2!, inputBuffer=%x, stack var =%p ", &samples[0], b);
        mshv_init();

        //        four2a_d2c_cnt = 0;

        if (sampleRate != 12000) {
            // decodeResultOutput("# bad samplerate");
            return;
        }
        // decodeResultOutput("# proceed1");

        std::vector<short> converted;
        converted.resize(nsamples);
        for (int q = 0; q < nsamples; q++) {
            converted[q] = samples[q].l * 16383.52;
        }
        decodeResultOutput("# decodeft8 begin");

        //    auto core = std::make_shared<MsCore>();
        //    core->ResampleAndFilter(converted.data(), converted.size());
        auto dms = std::make_shared<DecoderMs>();
        // debugPrintf("#\ndms\n=\n%x\n]]]", dms.get());
        if (std::string("ft8") == mode) {
            // debugPrintf("# set mode ft8");
            dms->setMode(DMS_FT8);
            // debugPrintf("# set mode ft8 ok ");
        } else if (std::string("ft4") == mode) {
            dms->setMode(DMS_FT4);
        } else {
            fprintf(stderr, "ERROR: invalid mode is specified. Valid modes: ft8, ft4\n");
            exit(1);
        }
        // debugPrintf("# call adds, dms=%x", dms.get());
        {
            QStringList ql;
            ql << "CALL";
            ql << "CALL";
            dms->SetWords(ql, 0, 0);
        } {
            QStringList ql;
            ql << "CALL";
            ql << "";
            ql << "";
            ql << "";
            ql << "";
            dms->SetCalsHash(ql);
        }
        decodeResultOutput("# p2");
        dms->SetResultsCallback(callback);
        decodeResultOutput("# p3");
        dms->SetDecoderDeep(3);
        decodeResultOutput("# p4");
        dms->SetThrLevel(threads);

        // debugPrintf("# calling decode: conv size=%u data=%x", converted.size(), converted.data());

        dms->SetDecode(converted.data(), converted.size(), "120000", 0, 4, false, true, false);
        while (dms->IsWorking()) {
            usleep(100000);
        }
        return;
    }


#define INPUT_BUFFER_SIZE 2000000
    static dsp::stereo_t inputBuffer[INPUT_BUFFER_SIZE] = {0};

    WASM_EXPORT("getFT8InputBuffer")

    void *getFT8InputBuffer() {
        return inputBuffer;
    }

    WASM_EXPORT("getFT8InputBufferSize")

    int getFT8InputBufferSize() {
        return INPUT_BUFFER_SIZE;
    }


    WASM_EXPORT("decodeFT8MainAt12000")

    void decodeFT8MainAt12000(int nsamples) {
        char b[20000000];
        // debugPrintf("# hello here!, inputBuffer=%x %x, stack var=%p", &inputBuffer[0], inputBuffer, b);
        decodeFT8(1, "ft8", 12000, &inputBuffer[0], nsamples, [](int, QStringList) {
        });
    }

    WASM_EXPORT("decodeFT4MainAt12000")
    void decodeFT4MainAt12000(int nsamples) {
        decodeFT8(1, "ft4", 12000, inputBuffer, nsamples, [](int, QStringList) {
        });
    }
}

#ifdef __wasm__

WASM_EXPORT("wasmMalloc") void *wasmMalloc(int size) { return malloc(size); }
WASM_EXPORT("wasmFree") void wasmFree(void *ptr) { free(ptr); }

#endif
