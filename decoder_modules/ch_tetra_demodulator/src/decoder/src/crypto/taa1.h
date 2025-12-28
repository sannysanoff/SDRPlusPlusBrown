#ifndef HAVE_TAA1_H
#define HAVE_TAA1_H

#include <inttypes.h>

#if __BYTE_ORDER == __LITTLE_ENDIAN
    #if _MSC_VER
        #define be32(x) _byteswap_ulong(x)
        #define be16(x) _byteswap_ushort(x)
    #else
        #define be32(x) __builtin_bswap32(x)
        #define be16(x) __builtin_bswap16(x)
    #endif
#else
#define be32(x) (x)
#define be16(x) (x)
#endif


/*
 * transformation functions used by TAxx primitives
 */
void transform_80_to_120(const uint8_t *lpBuffer, uint8_t *lpBufferOut);
void transform_80_to_128(const uint8_t *lpBuffer, uint8_t *lpBufferOut);
void transform_80_to_120_alt(const uint8_t *lpBuffer, uint8_t *lpBufferOut);
void transform_80_to_128_alt(const uint8_t *lpBuffer, uint8_t *lpBufferOut);
void transform_88_to_120(const uint8_t *lpBuffer, uint8_t *lpBufferOut);
void transform_120_to_88(const uint8_t *lpBuffer, uint8_t *lpBufferOut);
void transform_120_to_80_alt(const uint8_t *lpBuffer, uint8_t *lpBufferOut);
void transform_identity(const uint8_t *lpInput, uint8_t *lpOutput);
void transform_identity_inverse(const uint8_t *lpInput, uint8_t *lpOutput);

/*
 * TAxx primitives for authentication, key derivation and sealing functionality
 * (see ETSI EN 300 392-7)
 */
void ta11_ta41(uint8_t *lpKeyK, uint8_t *lpChallengeRs, uint8_t *lpKsOut);
void ta12_ta22(uint8_t *lpKeyKs, uint8_t *lpRand, uint8_t *lpResOut, uint8_t *lpDckOut);
void ta21(uint8_t *lpKeyK, uint8_t *lpChallengeRs, uint8_t *lpKspOut);
void ta31(uint8_t *lpUnsealedCck, uint8_t *lpCckId, uint8_t *lpDck, uint8_t *lpSealedCckOut);
void ta32(uint8_t *lpSealedCck, uint8_t *lpCckId, uint8_t *lpDck, uint8_t *lpUnsealedCckOut, uint8_t *lpMfOut);
void ta51(uint8_t *lpUnsealed, uint8_t *lpVn, uint8_t *lpKey, uint8_t *lpKeyN, uint8_t *lpSealedOut);
void ta52(uint8_t *lpSealed, uint8_t *lpKey, uint8_t *lpVn, uint8_t *lpUnsealedOut, uint8_t *lpMfOut, uint8_t *lpKeyNOut);
void ta71(uint8_t *lpGck, uint8_t *lpCck, uint8_t *lpMgckOut);
void ta81(uint8_t *lpUnsealedGck, uint8_t *lpGckVn, uint8_t *lpGckN, uint8_t *lpKey, uint8_t *lpSealedGckOut);
void ta82(uint8_t *lpSealedGck, uint8_t *lpGckVn, uint8_t *lpKey, uint8_t *lpUnsealedGckOut, uint8_t *lpMfOut, uint8_t *lpGckNOut);
void ta91(uint8_t *lpUnsealedGsko, uint8_t *lpGskoVn, uint8_t *lpKey, uint8_t *lpSealedGskoOut);
void ta92(uint8_t *lpSealedGsko, uint8_t *lpGskoVn, uint8_t *lpKey, uint8_t *lpUnsealedGskoOut, uint8_t *lpMfOut);

/*
 * TBxx non-cryptographic primitives also used for authentication and key derivation
 */
void tb4(uint8_t *lpDck1, uint8_t *lpDck2, uint8_t *lpDckOut);
void tb5(uint8_t *lpCn, uint8_t *lpLa, uint8_t *lpCc, uint8_t *lpCk, uint8_t *lpEckOut);
void tb6(uint8_t *lpSck, uint8_t *lpCn, uint8_t *lpSsi, uint8_t *lpEckOut);
void tb7(uint8_t *lpGsko, uint8_t *lpEgskoOut);

#endif /* HAVE_TAA1_H */
