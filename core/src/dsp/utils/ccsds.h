#pragma once
#include <stdint.h>

namespace dsp {
    namespace ccsds {
        const uint8_t TO_DUAL_BASIS[256] = {
            0x00, 0x7b, 0xaf, 0xd4, 0x99, 0xe2, 0x36, 0x4d, 0xfa, 0x81, 0x55, 0x2e, 0x63, 0x18, 0xcc, 0xb7,
            0x86, 0xfd, 0x29, 0x52, 0x1f, 0x64, 0xb0, 0xcb, 0x7c, 0x07, 0xd3, 0xa8, 0xe5, 0x9e, 0x4a, 0x31,
            0xec, 0x97, 0x43, 0x38, 0x75, 0x0e, 0xda, 0xa1, 0x16, 0x6d, 0xb9, 0xc2, 0x8f, 0xf4, 0x20, 0x5b,
            0x6a, 0x11, 0xc5, 0xbe, 0xf3, 0x88, 0x5c, 0x27, 0x90, 0xeb, 0x3f, 0x44, 0x09, 0x72, 0xa6, 0xdd,
            0xef, 0x94, 0x40, 0x3b, 0x76, 0x0d, 0xd9, 0xa2, 0x15, 0x6e, 0xba, 0xc1, 0x8c, 0xf7, 0x23, 0x58,
            0x69, 0x12, 0xc6, 0xbd, 0xf0, 0x8b, 0x5f, 0x24, 0x93, 0xe8, 0x3c, 0x47, 0x0a, 0x71, 0xa5, 0xde,
            0x03, 0x78, 0xac, 0xd7, 0x9a, 0xe1, 0x35, 0x4e, 0xf9, 0x82, 0x56, 0x2d, 0x60, 0x1b, 0xcf, 0xb4,
            0x85, 0xfe, 0x2a, 0x51, 0x1c, 0x67, 0xb3, 0xc8, 0x7f, 0x04, 0xd0, 0xab, 0xe6, 0x9d, 0x49, 0x32,
            0x8d, 0xf6, 0x22, 0x59, 0x14, 0x6f, 0xbb, 0xc0, 0x77, 0x0c, 0xd8, 0xa3, 0xee, 0x95, 0x41, 0x3a,
            0x0b, 0x70, 0xa4, 0xdf, 0x92, 0xe9, 0x3d, 0x46, 0xf1, 0x8a, 0x5e, 0x25, 0x68, 0x13, 0xc7, 0xbc,
            0x61, 0x1a, 0xce, 0xb5, 0xf8, 0x83, 0x57, 0x2c, 0x9b, 0xe0, 0x34, 0x4f, 0x02, 0x79, 0xad, 0xd6,
            0xe7, 0x9c, 0x48, 0x33, 0x7e, 0x05, 0xd1, 0xaa, 0x1d, 0x66, 0xb2, 0xc9, 0x84, 0xff, 0x2b, 0x50,
            0x62, 0x19, 0xcd, 0xb6, 0xfb, 0x80, 0x54, 0x2f, 0x98, 0xe3, 0x37, 0x4c, 0x01, 0x7a, 0xae, 0xd5,
            0xe4, 0x9f, 0x4b, 0x30, 0x7d, 0x06, 0xd2, 0xa9, 0x1e, 0x65, 0xb1, 0xca, 0x87, 0xfc, 0x28, 0x53,
            0x8e, 0xf5, 0x21, 0x5a, 0x17, 0x6c, 0xb8, 0xc3, 0x74, 0x0f, 0xdb, 0xa0, 0xed, 0x96, 0x42, 0x39,
            0x08, 0x73, 0xa7, 0xdc, 0x91, 0xea, 0x3e, 0x45, 0xf2, 0x89, 0x5d, 0x26, 0x6b, 0x10, 0xc4, 0xbf
        };

        const uint8_t FROM_DUAL_BASIS[256] = {
            0x00, 0xcc, 0xac, 0x60, 0x79, 0xb5, 0xd5, 0x19, 0xf0, 0x3c, 0x5c, 0x90, 0x89, 0x45, 0x25, 0xe9,
            0xfd, 0x31, 0x51, 0x9d, 0x84, 0x48, 0x28, 0xe4, 0x0d, 0xc1, 0xa1, 0x6d, 0x74, 0xb8, 0xd8, 0x14,
            0x2e, 0xe2, 0x82, 0x4e, 0x57, 0x9b, 0xfb, 0x37, 0xde, 0x12, 0x72, 0xbe, 0xa7, 0x6b, 0x0b, 0xc7,
            0xd3, 0x1f, 0x7f, 0xb3, 0xaa, 0x66, 0x06, 0xca, 0x23, 0xef, 0x8f, 0x43, 0x5a, 0x96, 0xf6, 0x3a,
            0x42, 0x8e, 0xee, 0x22, 0x3b, 0xf7, 0x97, 0x5b, 0xb2, 0x7e, 0x1e, 0xd2, 0xcb, 0x07, 0x67, 0xab,
            0xbf, 0x73, 0x13, 0xdf, 0xc6, 0x0a, 0x6a, 0xa6, 0x4f, 0x83, 0xe3, 0x2f, 0x36, 0xfa, 0x9a, 0x56,
            0x6c, 0xa0, 0xc0, 0x0c, 0x15, 0xd9, 0xb9, 0x75, 0x9c, 0x50, 0x30, 0xfc, 0xe5, 0x29, 0x49, 0x85,
            0x91, 0x5d, 0x3d, 0xf1, 0xe8, 0x24, 0x44, 0x88, 0x61, 0xad, 0xcd, 0x01, 0x18, 0xd4, 0xb4, 0x78,
            0xc5, 0x09, 0x69, 0xa5, 0xbc, 0x70, 0x10, 0xdc, 0x35, 0xf9, 0x99, 0x55, 0x4c, 0x80, 0xe0, 0x2c,
            0x38, 0xf4, 0x94, 0x58, 0x41, 0x8d, 0xed, 0x21, 0xc8, 0x04, 0x64, 0xa8, 0xb1, 0x7d, 0x1d, 0xd1,
            0xeb, 0x27, 0x47, 0x8b, 0x92, 0x5e, 0x3e, 0xf2, 0x1b, 0xd7, 0xb7, 0x7b, 0x62, 0xae, 0xce, 0x02,
            0x16, 0xda, 0xba, 0x76, 0x6f, 0xa3, 0xc3, 0x0f, 0xe6, 0x2a, 0x4a, 0x86, 0x9f, 0x53, 0x33, 0xff,
            0x87, 0x4b, 0x2b, 0xe7, 0xfe, 0x32, 0x52, 0x9e, 0x77, 0xbb, 0xdb, 0x17, 0x0e, 0xc2, 0xa2, 0x6e,
            0x7a, 0xb6, 0xd6, 0x1a, 0x03, 0xcf, 0xaf, 0x63, 0x8a, 0x46, 0x26, 0xea, 0xf3, 0x3f, 0x5f, 0x93,
            0xa9, 0x65, 0x05, 0xc9, 0xd0, 0x1c, 0x7c, 0xb0, 0x59, 0x95, 0xf5, 0x39, 0x20, 0xec, 0x8c, 0x40,
            0x54, 0x98, 0xf8, 0x34, 0x2d, 0xe1, 0x81, 0x4d, 0xa4, 0x68, 0x08, 0xc4, 0xdd, 0x11, 0x71, 0xbd
        };

        const uint8_t SCRAMBLING_SEQUENCE[255] = {
            0xFF, 0x48, 0x0E, 0xC0, 0x9A, 0x0D, 0x70, 0xBC, 0x8E, 0x2C, 0x93, 0xAD, 0xA7, 0xB7, 0x46, 0xCE,
            0x5A, 0x97, 0x7D, 0xCC, 0x32, 0xA2, 0xBF, 0x3E, 0x0A, 0x10, 0xF1, 0x88, 0x94, 0xCD, 0xEA, 0xB1,
            0xFE, 0x90, 0x1D, 0x81, 0x34, 0x1A, 0xE1, 0x79, 0x1C, 0x59, 0x27, 0x5B, 0x4F, 0x6E, 0x8D, 0x9C,
            0xB5, 0x2E, 0xFB, 0x98, 0x65, 0x45, 0x7E, 0x7C, 0x14, 0x21, 0xE3, 0x11, 0x29, 0x9B, 0xD5, 0x63,
            0xFD, 0x20, 0x3B, 0x02, 0x68, 0x35, 0xC2, 0xF2, 0x38, 0xB2, 0x4E, 0xB6, 0x9E, 0xDD, 0x1B, 0x39,
            0x6A, 0x5D, 0xF7, 0x30, 0xCA, 0x8A, 0xFC, 0xF8, 0x28, 0x43, 0xC6, 0x22, 0x53, 0x37, 0xAA, 0xC7,
            0xFA, 0x40, 0x76, 0x04, 0xD0, 0x6B, 0x85, 0xE4, 0x71, 0x64, 0x9D, 0x6D, 0x3D, 0xBA, 0x36, 0x72,
            0xD4, 0xBB, 0xEE, 0x61, 0x95, 0x15, 0xF9, 0xF0, 0x50, 0x87, 0x8C, 0x44, 0xA6, 0x6F, 0x55, 0x8F,
            0xF4, 0x80, 0xEC, 0x09, 0xA0, 0xD7, 0x0B, 0xC8, 0xE2, 0xC9, 0x3A, 0xDA, 0x7B, 0x74, 0x6C, 0xE5,
            0xA9, 0x77, 0xDC, 0xC3, 0x2A, 0x2B, 0xF3, 0xE0, 0xA1, 0x0F, 0x18, 0x89, 0x4C, 0xDE, 0xAB, 0x1F,
            0xE9, 0x01, 0xD8, 0x13, 0x41, 0xAE, 0x17, 0x91, 0xC5, 0x92, 0x75, 0xB4, 0xF6, 0xE8, 0xD9, 0xCB,
            0x52, 0xEF, 0xB9, 0x86, 0x54, 0x57, 0xE7, 0xC1, 0x42, 0x1E, 0x31, 0x12, 0x99, 0xBD, 0x56, 0x3F,
            0xD2, 0x03, 0xB0, 0x26, 0x83, 0x5C, 0x2F, 0x23, 0x8B, 0x24, 0xEB, 0x69, 0xED, 0xD1, 0xB3, 0x96,
            0xA5, 0xDF, 0x73, 0x0C, 0xA8, 0xAF, 0xCF, 0x82, 0x84, 0x3C, 0x62, 0x25, 0x33, 0x7A, 0xAC, 0x7F,
            0xA4, 0x07, 0x60, 0x4D, 0x06, 0xB8, 0x5E, 0x47, 0x16, 0x49, 0xD6, 0xD3, 0xDB, 0xA3, 0x67, 0x2D,
            0x4B, 0xBE, 0xE6, 0x19, 0x51, 0x5F, 0x9F, 0x05, 0x08, 0x78, 0xC4, 0x4A, 0x66, 0xF5, 0x58
        };

        const uint32_t ASM_VALUE = 0x1ACFFC1D;
        const uint8_t ASM_BYTES[4] = { 0x1A, 0xCF, 0xFC, 0x1D };
        const uint8_t ASM_SYMS[16] = { 0b00, 0b01, 0b10, 0b10, 0b11, 0b00, 0b11, 0b11, 0b11, 0b11, 0b11, 0b00, 0b00, 0b01, 0b11, 0b01 };
        const uint8_t ASM_BITS[32] = { 0, 0, 0, 1, 1, 0, 1, 0, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 0, 1 };

        class FrameDataDecoder {
        public:
            FrameDataDecoder(int interleaving, bool dualBasis, int rsBlockSize, int rsParitySize) {
                _interleaving = interleaving;
                _dualBasis = dualBasis;
                _rsBlockSize = rsBlockSize;
                _rsParitySize = rsParitySize;
            }

            void decode(uint8_t* in, uint8_t* out, int count) {
                // Deinterleave
                if (_dualBasis) {
                    for (int i = 0; i < count; i++) {
                        workBuffer[i % _interleaving][i / _interleaving] = FROM_DUAL_BASIS[in[i]];
                    }
                }
                else {
                    for (int i = 0; i < count; i++) {
                        workBuffer[i % _interleaving][i / _interleaving] = in[i];
                    }
                }

                // Reed solomon

                // Reinterleave and descramble if needed
                if (_dualBasis) {
                    for (int i = 0; i < count; i++) {
                        out[i] = TO_DUAL_BASIS[workOutputBuffer[i % _interleaving][i / _interleaving]];
                    }
                }
                else {
                    for (int i = 0; i < count; i++) {
                        out[i] = workOutputBuffer[i % _interleaving][i / _interleaving];
                    }
                }
            }

        private:
            uint8_t workBuffer[5][255];
            uint8_t workOutputBuffer[5][255];
            int _interleaving;
            bool _dualBasis;
            int _rsBlockSize;
            int _rsParitySize;
        };

        inline void descramble(uint8_t* in, uint8_t* out, int count) {
            for (int i = 0; i < count; i++) {
                out[i] = in[i] ^ SCRAMBLING_SEQUENCE[i % 255];
            }
        }
    }
}