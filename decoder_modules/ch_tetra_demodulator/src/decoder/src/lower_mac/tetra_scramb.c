/* TETRA scrambling according to Section 8.2.5 of EN 300 392-2 V3.2.1 */

/* This converts from type-4 to type-5 bits (and vice versa) */

/* (C) 2011 by Harald Welte <laforge@gnumonks.org>
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdint.h>
#include <lower_mac/tetra_scramb.h>

/* Tap macro for the standard XOR / Fibonacci form */
#define ST(x, y)	((x) >> (32-y))

/* Tap macro and constant for the Galois form */
#define GL(x)		(1<<(x-1))
#define GALOIS_LFSR	(GL(32)|GL(26)|GL(23)|GL(22)|GL(16)|GL(12)|GL(11)|GL(10)|GL(8)|GL(7)|GL(5)|GL(4)|GL(2)|GL(1))

#if 1
static uint8_t next_lfsr_bit(uint32_t *lf)
{
	uint32_t lfsr = *lf;
	uint32_t bit;

	/* taps: 32 26 23 22 16 12 11 10 8 7 5 4 2 1 */
	bit = (ST(lfsr, 32) ^ ST(lfsr, 26) ^ ST(lfsr, 23) ^ ST(lfsr, 22) ^
	       ST(lfsr, 16) ^ ST(lfsr, 12) ^ ST(lfsr, 11) ^ ST(lfsr, 10) ^
	       ST(lfsr, 8) ^ ST(lfsr,  7) ^ ST(lfsr,  5) ^ ST(lfsr,  4) ^
	       ST(lfsr, 2) ^ ST(lfsr,  1)) & 1;
	lfsr = (lfsr >> 1) | (bit << 31);

	/* update the caller's LFSR state */
	*lf = lfsr;

	return bit & 0xff;
}
#else
/* WARNING: this version somehow does not produce the desired result! */
static uint8_t next_lfsr_bit(uint32_t *lf)
{
	uint32_t lfsr = *lf;
	uint32_t bit = lfsr & 1;

	lfsr = (lfsr >> 1) ^ (uint32_t)(0 - ((lfsr & 1u) & GALOIS_LFSR));

	*lf = lfsr;

	return bit;
}
#endif

int tetra_scramb_get_bits(uint32_t lfsr_init, uint8_t *out, int len)
{
	int i;

	for (i = 0; i < len; i++)
		out[i] = next_lfsr_bit(&lfsr_init);

	return 0;
}

/* XOR the bitstring at 'out/len' using the TETRA scrambling LFSR */
int tetra_scramb_bits(uint32_t lfsr_init, uint8_t *out, int len)
{
	int i;

	for (i = 0; i < len; i++)
		out[i] ^= next_lfsr_bit(&lfsr_init);

	return 0;
}

uint32_t tetra_scramb_get_init(uint16_t mcc, uint16_t mnc, uint8_t colour)
{
	uint32_t scramb_init;

	mcc &= 0x3ff;
	mnc &= 0x3fff;
	colour &= 0x3f;

	scramb_init = colour | (mnc << 6) | (mcc << 20);
	scramb_init = (scramb_init << 2) | SCRAMB_INIT;

	return scramb_init;
}
