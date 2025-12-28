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

#ifndef CRC_16
#define CRC_16

#include <stdint.h>

/**
 * Code to generate a CRC16-ITU-T as of the X.25 specification and
 * compatible with Linux's implementations. At least the polynom is
 * coming from the X.25 spec.
 */

/**
 * This is working of bits packed together. The high bits will be
 * accessed first. If you only pass one bit the array needs to contain
 * a 0xF0 instead of a 0x01. This interface is compatible with the
 * normal CRC interface (besides the length).
 */
uint16_t crc16_itut_bytes(uint16_t crc,
			  const uint8_t *input, const int number_bits);

/**
 * Each byte contains one bit. Calculate the CRC16-ITU-T for it.
 */
uint16_t crc16_itut_bits(uint16_t crc,
			 const uint8_t *input, const int number_bits);


uint16_t crc16_ccitt_bits(uint8_t *bits, unsigned int len);

#endif
