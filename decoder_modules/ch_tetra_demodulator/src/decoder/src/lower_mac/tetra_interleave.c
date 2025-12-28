/* Tetra block interleaver, according to ETSI EN 300 392-2 V3.2.1 (2007-09) */

/* This converts from type type-3 bits into type-4 bits (and vice versa) */

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
#include <string.h>
#include <stdio.h>
#include <errno.h>

// #include <osmocom/core/utils.h>

#include <tetra_common.h>

#include <lower_mac/tetra_interleave.h>

/* Section 8.2.4.1 Block interleaving for phase modulation */
static uint32_t block_interl_func(uint32_t K, uint32_t a, uint32_t i)
{
	return (1 + ( (a * i) % K));
}

void block_interleave(uint32_t K, uint32_t a, const uint8_t *in, uint8_t *out)
{
	uint32_t i;
	for (i = 1; i <= K; i++) {
		uint32_t k = block_interl_func(K, a, i);
		DEBUGP("interl: i=%u, k=%u\n", i, k);
		out[k-1] = in[i-1];
	}
}

void block_deinterleave(uint32_t K, uint32_t a, const uint8_t *in, uint8_t *out)
{
	uint32_t i;
	for (i = 1; i <= K; i++) {
		uint32_t k = block_interl_func(K, a, i);
		DEBUGP("deinterl: i=%u, k=%u\n", i, k);
		out[i-1] = in[k-1];
	}
}

/* EN 300 395-2 Section 5.5.3 Matrix interleaving (voice */
void matrix_interleave(uint32_t lines, uint32_t columns,
			const uint8_t *in, uint8_t *out)
{
	uint32_t i, j;

	for (i = 0; i < columns; i++) {
		for (j = 0; j < lines; j++)
			out[i*lines + columns] = in[j*columns + lines];
	}
}

void matrix_deinterleave(uint32_t lines, uint32_t columns,
			 const uint8_t *in, uint8_t *out)
{
	uint32_t i, j;

	for (i = 0; i < columns; i++) {
		for (j = 0; j < lines; j++)
			out[j*columns + lines] = in[i*lines + columns];
	}
}
