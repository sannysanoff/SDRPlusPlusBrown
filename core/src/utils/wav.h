#pragma once
#include <string>
#include <fstream>
#include <stdint.h>
#include <mutex>
#include "riff.h"
#include "dsp/types.h"
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif


namespace wav {    
    #pragma pack(push, 1)
    struct FormatHeader {
        uint16_t codec;
        uint16_t channelCount;
        uint32_t sampleRate;
        uint32_t bytesPerSecond;
        uint16_t bytesPerSample;
        uint16_t bitDepth;
    };
    #pragma pack(pop)

    enum Format {
        FORMAT_WAV,
        FORMAT_RF64
    };

    enum SampleType {
        SAMP_TYPE_UINT8,
        SAMP_TYPE_INT16,
        SAMP_TYPE_INT32,
        SAMP_TYPE_FLOAT32
    };

    enum Codec {
        CODEC_PCM   = 1,
        CODEC_FLOAT = 3
    };

    class Writer {
    public:
        Writer(int channels = 2, uint64_t samplerate = 48000, Format format = FORMAT_WAV, SampleType type = SAMP_TYPE_INT16);
        ~Writer();

        bool open(std::string path);
        bool isOpen();
        void close();

        void setChannels(int channels);
        void setSamplerate(uint64_t samplerate);
        void setFormat(Format format);
        void setSampleType(SampleType type);

        size_t getSamplesWritten() { return samplesWritten; }

        void write(float* samples, int count);

    private:
        std::recursive_mutex mtx;
        FormatHeader hdr;
        riff::Writer rw;

        int _channels;
        uint64_t _samplerate;
        Format _format;
        SampleType _type;
        size_t bytesPerSamp;

        uint8_t* bufU8 = NULL;
        int16_t* bufI16 = NULL;
        int32_t* bufI32 = NULL;
        size_t samplesWritten = 0;
    };

    struct ComplexDumper {
        FILE *f;

        ComplexDumper(int sampleRate, const std::string &path) {
#ifdef NDEBUG
            f = nullptr;
#else
#ifdef _WIN32
            f = nullptr;
#else
            f = fopen(path.c_str(), "wb");
#endif
#endif
        }

        void dump(void* samples, int nSamples) {
            if (f) {
                fwrite(samples, sizeof(dsp::complex_t), nSamples, f);
            }
        }

        void clear() {
#ifndef NDEBUG
#ifndef _WIN32
            if (f) {
                ftruncate(fileno(f), 0); // windows version?
                fseek(f, 0, SEEK_SET);
            }
#endif
#endif
        }

        ~ComplexDumper() {
            if (f) {
                fclose(f);
            }
        }
    };

    class Reader {
    public:

        std::string error;

        Reader(std::string path) {
            error = "";
            file = std::ifstream(path.c_str(), std::ios::binary);
            if (!file.is_open()) {
                error = "cannot open file";
            }
            file.read((char*)&hdr, sizeof(WavHeader_t));
            valid = false;
            error = "signature mismatch";
            if (memcmp(hdr.signature, "RIFF", 4) != 0) { return; }
            if (memcmp(hdr.fileType, "WAVE", 4) != 0) { return; }
            error = "";
            valid = true;
        }

        uint16_t getBitDepth() {
            return hdr.bitDepth;
        }

        uint16_t getChannelCount() {
            return hdr.channelCount;
        }

        uint32_t getSampleRate() {
            return hdr.sampleRate;
        }

        bool isValid() {
            return valid;
        }

        void readSamples(void* data, size_t size) {
            char* _data = (char*)data;
            file.read(_data, size);
            int read = file.gcount();
            if (read < size) {
                file.clear();
                file.seekg(sizeof(WavHeader_t));
                file.read(&_data[read], size - read);
            }
            bytesRead += size;
        }

        size_t readSamples2(void* data, size_t size) {
            char* _data = (char*)data;
            file.read(_data, size);
            int read = file.gcount();
            return read;
        }

        void rewind() {
            file.seekg(sizeof(WavHeader_t));
        }

        void close() {
            file.close();
        }

    private:
        struct WavHeader_t {
            char signature[4];           // "RIFF"
            uint32_t fileSize;           // data bytes + sizeof(WavHeader_t) - 8
            char fileType[4];            // "WAVE"
            char formatMarker[4];        // "fmt "
            uint32_t formatHeaderLength; // Always 16
            uint16_t sampleType;         // PCM (1)
            uint16_t channelCount;
            uint32_t sampleRate;
            uint32_t bytesPerSecond;
            uint16_t bytesPerSample;
            uint16_t bitDepth;
            char dataMarker[4]; // "data"
            uint32_t dataSize;
        };

        bool valid = false;
        std::ifstream file;
        size_t bytesRead = 0;
        WavHeader_t hdr;
    };
}
