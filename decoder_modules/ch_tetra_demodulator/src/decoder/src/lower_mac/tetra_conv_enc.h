#ifndef TETRA_CONV_ENC_H
#define TETRA_CONV_ENC_H

#include <stdint.h>

struct conv_enc_state {
	uint8_t delayed[4];
};


/* in: one-bit-per-byte out: 4bit-per-byte, both 'len' long */
int conv_enc_input(struct conv_enc_state *ces, uint8_t *in, int len, uint8_t *out);

int conv_enc_init(struct conv_enc_state *ces);

enum tetra_rcpc_puncturer {
	TETRA_RCPC_PUNCT_2_3,
	TETRA_RCPC_PUNCT_1_3,
	TETRA_RCPC_PUNCT_292_432,
	TETRA_RCPC_PUNCT_148_432,
	TETRA_RCPC_PUNCT_112_168,
	TETRA_RCPC_PUNCT_72_162,
	TETRA_RCPC_PUNCT_38_80,
};

/* Puncture the mother code (in) and write 'len' symbols to out */
int get_punctured_rate(enum tetra_rcpc_puncturer pu, uint8_t *in, uint32_t len, uint8_t *out);


/* De-Puncture the 'len' type-3 bits (in) and write mother code to out */
int tetra_rcpc_depunct(enum tetra_rcpc_puncturer pu, const uint8_t *in, uint32_t len, uint8_t *out);

/* Self-test the puncturing/de-puncturing */
int tetra_punct_test(void);

#endif /* TETRA_CONV_ENC_H */
