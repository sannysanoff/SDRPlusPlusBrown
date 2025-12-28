#ifndef TETRA_INTERLEAVE_H
#define TETRA_INTERLEAVE_H
/* Tetra block interleaver, according to ETSI EN 300 392-2 V3.2.1 (2007-09) */

/* This converts from type type-3 bits into type-4 bits (and vice versa) */

#include <stdint.h>

void block_interleave(uint32_t K, uint32_t a, const uint8_t *in, uint8_t *out);
void block_deinterleave(uint32_t K, uint32_t a, const uint8_t *in, uint8_t *out);

void matrix_interleave(uint32_t lines, uint32_t columns,
			const uint8_t *in, uint8_t *out);
void matrix_deinterleave(uint32_t lines, uint32_t columns,
			 const uint8_t *in, uint8_t *out);

#endif /* TETRA_INTERLEAVE_H */
