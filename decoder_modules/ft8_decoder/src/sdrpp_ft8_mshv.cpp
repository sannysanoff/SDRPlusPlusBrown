#include <core.h>
#include <iostream>
#include <stdio.h>
#include <module.h>
#include <ctm.h>
#include <utils/wav.h>
#include <utils/riff.h>
#include "dsp/types.h"
#include "dsp/multirate/polyphase_resampler.h"
#include "dsp/multirate/rational_resampler.h"

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
    inline void decodeFT8(int threads, const char *mode, int sampleRate, dsp::stereo_t* samples, long long nsamples, std::function<void(const char*)> callback) {
        //
        //
        //
        mshv_init();

//        four2a_d2c_cnt = 0;

        std::vector<dsp::stereo_t> resampledV;

        if (sampleRate != 12000) {
            long long int outSize = 3 * (nsamples * 12000) / sampleRate;
            resampledV.resize(outSize);
            dsp::multirate::RationalResampler<dsp::stereo_t> res;
            res.init(nullptr, sampleRate, 12000);
            nsamples = res.process(nsamples, samples, resampledV.data());
            samples = resampledV.data();
            printf("Resampled, samples size=2 * %zu\n", resampledV.size()/2);
        }



        std::vector<short> converted;
        converted.reserve(nsamples);
        for (int q = 0; q < nsamples; q++) {
            converted.emplace_back(samples[q].l * 16383.52);
        }

        //    auto core = std::make_shared<MsCore>();
        //    core->ResampleAndFilter(converted.data(), converted.size());
        auto dms = std::make_shared<DecoderMs>();
        if (std::string("ft8") == mode) {
            dms->setMode(DMS_FT8);
        } else if (std::string("ft4") == mode) {
            dms->setMode(DMS_FT4);
        } else {
            fprintf(stderr, "ERROR: invalid mode is specified. Valid modes: ft8, ft4\n");
            exit(1);
        }
        {
            QStringList ql;
            ql << "CALL";
            ql << "CALL";
            dms->SetWords(ql, 0, 0);
        }
        {
            QStringList ql;
            ql << "CALL";
            ql << "";
            ql << "";
            ql << "";
            ql << "";
            dms->SetCalsHash(ql);
        }
        dms->SetResultsCallback(callback);
        dms->SetDecoderDeep(3);
        dms->SetThrLevel(threads);

        dms->SetDecode(converted.data(), converted.size(), "120000", 0, 4, false, true, false);
        while (dms->IsWorking()) {
            usleep(100000);
        }
        return;
    }

}


void doDecode(const char *mode, const char *path, int threads, std::function<void(int mode, std::vector<std::string> result)> callback) {
    mshv_init();
    FILE *f = fopen(path,"rb");
    if (!f) {
        fprintf(stderr,"ERROR Cannot open file %s\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc(size);
    if (!buf) {
        fprintf(stderr,"ERROR Cannot alloc %lld\n", size);
        exit(1);
    }
    (void)fread((void *)buf, size, 1, f);
    fclose(f);
    riff::ChunkHeader *riffHeader = (riff::ChunkHeader *)(buf);
    riff::ChunkHeader *fmtHeader = (riff::ChunkHeader *)(buf + 12);
    wav::FormatHeader *hdr = (wav::FormatHeader *)(buf+12+8); // skip RIFF + WAV
    riff::ChunkHeader *dta = (riff::ChunkHeader *)(buf+12+8 + sizeof (wav::FormatHeader));
    auto *data = (float *)((uint8_t *)dta + sizeof(riff::ChunkHeader));
    if (noisy_ft8) {
        printf("Channels: %d\n", hdr->channelCount);
        printf("SampleRate: %d\n", hdr->sampleRate);
        printf("BytesPerSample: %d\n", hdr->bytesPerSample);
        printf("BitDepth: %d\n", hdr->bitDepth);
        printf("Codec: %d\n", hdr->codec);
        fflush(stdout);
    }
    bool handled = hdr->codec == 3 && hdr->bitDepth == 32 && hdr->channelCount == 2;
    handled |= hdr->codec == 1 && hdr->bitDepth == 16 && hdr->channelCount == 2;
    if (!handled) {
        fprintf(stderr,"ERROR Want Codec/BitDepth/channels: 3/32/2 or 1/16/2\n");
    }
    int nSamples = ((char*)(buf + size)-(char *)data)/2/(hdr->bitDepth/8);
    if (noisy_ft8) {
        printf("NSamples: %d\n", nSamples);
    }

    std::vector<dsp::stereo_t> converted;
    if (hdr->codec == 1) {  // short samples
        auto ptr = (short *)dta;
        converted.resize(nSamples);
        float maxx = 0.0f;
        for(int q=0; q<nSamples; q++) {
            converted[q].l = ptr[2*q] / 32767.0;
            converted[q].r = ptr[2*q+1] / 32767.0;
            maxx = std::max<float>(maxx, converted[q].r);
            maxx = std::max<float>(maxx, converted[q].l);
        }
        data = (float*)converted.data();
        printf("d0: %f   %f   maxx: %f\n", data[100], data[101], maxx);
    }

    fflush(stdout);
    fflush(stderr);
    try {
        for(int q=0; q<1; q++) {
            auto ctm = currentTimeMillis();
            int outCount = 0;
//            spdlog::info("=================================");
            ft8::decodeFT8(threads, mode, hdr->sampleRate, (dsp::stereo_t*)data, nSamples, [&](const char *line) {
                std::vector<std::string> split;
                splitStringV(line,"\t\n", split);
                std::vector<std::string> formatted = {
                        split[1], // [0] == decode timestamp
                        split[6], // [1] == strength
                        "",        // [2] == ?? sclerosis
                        split[18], // [3] == frequency in band
                        split[12] // [4] message, along with participants (pipe-separated)
                };
                callback(!strcmp(mode, "ft8") ? ft8::DMS_FT8 : ft8::DMS_FT4, formatted);
            });
            if (noisy_ft8) {
                std::cout << "Time taken: " << currentTimeMillis() - ctm << " ms" << std::endl;
                std::cout << "DECODE_EOF" << std::endl;
                std::cout << "DECODE_EOF" << std::endl;
                fflush(stdout);
            }
        }
    } catch (std::runtime_error &e) {
        fprintf(stderr,"ERROR %s \n", e.what());
    }

}

