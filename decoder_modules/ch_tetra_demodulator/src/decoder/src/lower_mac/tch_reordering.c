/* Bit re-ordering for TETRA ACELP speech codec */

/* (C) 2011 by Harald Welte <laforge@gnumonks.org>
 *
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
#include <string.h>


/* EN 300 395-2 V1.3.1 Table 4 */

#define NUM_ACELP_CLASS0_BITS	51
/* 51 Unprotected Class0 bits */
static const uint8_t class0_positions[NUM_ACELP_CLASS0_BITS] = {
	35, 36, 37,
	38, 39, 40,
	41, 42, 33,
	47, 48,
	56,
	61, 62, 63,
	65, 66, 67,
	68, 69, 70,
	74, 75,
	83,
	88,
	89, 90, 91,
	92, 93, 94,
	95, 96, 97,
	101, 102,
	110,
	115,
	116, 117, 118,
	119, 120, 121,
	122, 123, 124,
	128, 129,
	137
};

#define NUM_ACELP_CLASS1_BITS	56
static const uint8_t class1_positions[NUM_ACELP_CLASS1_BITS] = {
	58, 85, 112,
	54, 81, 108, 135,
	50, 77,
	104, 131,
	45, 72, 99, 126,
	55, 82, 109, 136,
	5, 13, 34,
	8, 16, 17,
	22, 23, 24,
	25, 26,
	6, 14, 7, 15,
	60, 87 ,114,
	46,
	73, 100, 127,
	44, 71, 98, 125,
	33, 49,
	76, 103, 130,
	59, 86, 113,
	57, 84, 111
};

#define NUM_ACELP_CLASS2_BITS	30
static const uint8_t class2_positions[NUM_ACELP_CLASS2_BITS] = {
	18, 19, 20, 21,
	31, 32,
	53, 80, 107, 134,
	1, 2, 3, 4,
	9, 10, 11, 12,
	27, 28, 29, 30,
	52, 79, 106, 133,
	51, 78, 105, 132
};

#define NUM_ACELP_BITS	(NUM_ACELP_CLASS0_BITS+NUM_ACELP_CLASS1_BITS+NUM_ACELP_CLASS2_BITS)

/* reorder the 432-bit incoming (decoded) type2 bits of a full-rate speech
 * frame in order to generate two consecutive 216-bit ACELP codec frames */
void tetra_acelp_type2_to_codec(const uint8_t *in, uint8_t *out)
{
	int bit, frame;
	const uint8_t *in_cur = in;

	for (bit = 0; bit < NUM_ACELP_CLASS0_BITS; bit++) {
		for (frame = 0; frame < 2; frame++) 
			out[frame*NUM_ACELP_BITS + class0_positions[bit] - 1] = in_cur[2*bit + frame];
	}
	in_cur += 2*NUM_ACELP_CLASS0_BITS;

	for (bit = 0; bit < NUM_ACELP_CLASS1_BITS; bit++) {
		for (frame = 0; frame < 2; frame++) 
			out[frame*NUM_ACELP_BITS + class1_positions[bit] - 1] = in_cur[2*bit + frame];
	}
	in_cur += 2*NUM_ACELP_CLASS1_BITS;

	for (bit = 0; bit < NUM_ACELP_CLASS2_BITS; bit++) {
		for (frame = 0; frame < 2; frame++) 
			out[frame*NUM_ACELP_BITS + class2_positions[bit] - 1] = in_cur[2*bit + frame];
	}

	/* FIXME: same for STCH use */
}

void tetra_acelp_codec_to_acelp(const uint8_t *in, uint8_t *out)
{
	int bit, frame;
	uint8_t *out_cur = out;

	for (bit = 0; bit < NUM_ACELP_CLASS0_BITS; bit++) {
		for (frame = 0; frame < 2; frame++) 
			out_cur[2*bit + frame] = in[frame*NUM_ACELP_BITS + class0_positions[bit] - 1];
	}
	out_cur += 2*NUM_ACELP_CLASS0_BITS;

	for (bit = 0; bit < NUM_ACELP_CLASS1_BITS; bit++) {
		for (frame = 0; frame < 2; frame++) 
			out_cur[2*bit + frame] = in[frame*NUM_ACELP_BITS + class1_positions[bit] - 1];
	}
	out_cur += 2*NUM_ACELP_CLASS1_BITS;

	for (bit = 0; bit < NUM_ACELP_CLASS2_BITS; bit++) {
		for (frame = 0; frame < 2; frame++) 
			out_cur[2*bit + frame] = in[frame*NUM_ACELP_BITS + class2_positions[bit] - 1];
	}
}
