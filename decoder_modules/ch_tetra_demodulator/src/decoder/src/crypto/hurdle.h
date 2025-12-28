#ifndef HAVE_HURDLE_H
#define HAVE_HURDLE_H

#include <inttypes.h>

struct hurdle_ctx {
	uint8_t abRoundKeys[256];
};

#define HURDLE_ENCRYPT 0
#define HURDLE_DECRYPT 1

void hurdle_set_key(uint8_t *k, struct hurdle_ctx *lpContextOut);
void HURDLE_encrypt(uint8_t abOutput[8], const uint8_t abInput[8], struct hurdle_ctx *lpKey, uint8_t eEncryptMode);
void HURDLE_enc_cbc(uint8_t abCiphertext[16], const uint8_t abPlaintext[16], uint8_t abKey[16]);
void HURDLE_dec_cts(uint8_t abPlaintext[15], const uint8_t abCiphertext[15], uint8_t abKey[16]);

extern const uint8_t g_abHurdleSbox[256];

#endif /* HAVE_HURDLE_H */
