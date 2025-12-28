#ifndef TETRA_SCRAMB_H
#define TETRA_SCRAMB_H
/* TETRA scrambling according to Section 8.2.5 of EN 300 392-2 V3.2.1 */

/* This converts from type-4 to type-5 bits (and vice versa) */

#include <stdint.h>

/* Section 8.2.5.2:
	* For scrambling of BSCH, all bits e(1) to e(30) shall equal to zero
		* p(k) = e(1-k) for k = -29, ... 0
		* p(k) = 1	for k = -31, -30
 */
#define SCRAMB_INIT	3

uint32_t tetra_scramb_get_init(uint16_t mcc, uint16_t mnc, uint8_t colour);

int tetra_scramb_get_bits(uint32_t lfsr_init, uint8_t *out, int len);

/* XOR the bitstring at 'out/len' using the TETRA scrambling LFSR */
int tetra_scramb_bits(uint32_t lfsr_init, uint8_t *out, int len);

#endif /* TETRA_SCRAMB_H */
