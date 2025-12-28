/* CRC16 (most likely the ITU variant) working on plain bits as a trial
 * to find out if simple padding to the right is going to be enough
 */
/* (C) 2011 by Holger Hans Peter Freyther
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

#include <lower_mac/crc_simple.h>
#include <stdio.h>

/**
 * X.25 rec 2.2.7.4 Frame Check Sequence. This should be
 * CRC ITU-T from the kernel or such.
 */
#define GEN_POLY 0x1021

uint16_t get_nth_bit(const uint8_t *input, int _bit)
{
	uint16_t val;
	int byte = _bit / 8;
	int bit = 7 - _bit % 8;

	val = (input[byte] & (1 << bit)) >> bit;
	return val;
}

/**
 * This is mostly from http://en.wikipedia.org/wiki/Computation_of_CRC
 * Code fragment 2. Due some stupidity it took longer to implement than
 * it should have taken.
 */
uint16_t crc16_itut_bytes(uint16_t crc, const uint8_t *input, int number_bits)
{
	int i;

	for (i = 0; i < number_bits; ++i) {
		uint16_t bit = get_nth_bit(input, i);

		crc ^= bit << 15;
		if ((crc & 0x8000)) {
			crc <<= 1;
			crc ^= GEN_POLY;
		} else {
			crc <<= 1;
		}
	}

	return crc;
}

uint16_t crc16_itut_bits(uint16_t crc, const uint8_t *input, int number_bits)
{
	int i;

	for (i = 0; i < number_bits; ++i) {
		uint16_t bit = input[i] & 0x1;

		crc ^= bit << 15;
		if ((crc & 0x8000)) {
			crc <<= 1;
			crc ^= GEN_POLY;
		} else {
			crc <<= 1;
		}
	}

	return crc;
}

uint16_t crc16_itut_poly(uint16_t crc, uint32_t poly, const uint8_t *input, int number_bits)
{
	int i;

	for (i = 0; i < number_bits; ++i) {
		uint16_t bit = input[i] & 0x1;

		crc ^= bit << 15;
		if ((crc & 0x8000)) {
			crc <<= 1;
			crc ^= poly;
		} else {
			crc <<= 1;
		}
	}

	return crc;
}

uint16_t crc16_ccitt_bits(uint8_t *bits, unsigned int len)
{
	return crc16_itut_bits(0xffff, bits, len);
}
