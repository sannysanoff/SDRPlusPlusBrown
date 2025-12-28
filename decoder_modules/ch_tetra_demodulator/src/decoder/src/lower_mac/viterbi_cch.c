/* (C) 2011 by Sylvain Munaut <tnt@246tNt.com>
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

// #include <osmocom/core/conv.h>
#include "osmo_conv.h"

#include <lower_mac/viterbi_cch.h>


/*
 * G1 = 1 + D           + D4
 * G2 = 1     + D2 + D3 + D4
 * G3 = 1 + D + D2      + D4
 * G4 = 1 + D      + D3 + D4
 */

static const uint8_t conv_cch_next_output[][2] = {
	{  0, 15 }, { 11,  4 }, {  6,  9 }, { 13,  2 },
	{  5, 10 }, { 14,  1 }, {  3, 12 }, {  8,  7 },
	{ 15,  0 }, {  4, 11 }, {  9,  6 }, {  2, 13 },
	{ 10,  5 }, {  1, 14 }, { 12,  3 }, {  7,  8 },
};

static const uint8_t conv_cch_next_state[][2] = {
	{  0,  1 }, {  2,  3 }, {  4,  5 }, {  6,  7 },
	{  8,  9 }, { 10, 11 }, { 12, 13 }, { 14, 15 },
	{  0,  1 }, {  2,  3 }, {  4,  5 }, {  6,  7 },
	{  8,  9 }, { 10, 11 }, { 12, 13 }, { 14, 15 },
};


static const struct osmo_conv_code conv_cch = {
	.N = 4,
	.K = 5,
	.next_output = conv_cch_next_output,
	.next_state  = conv_cch_next_state,
};


int conv_cch_decode(int8_t *input, uint8_t *output, int n)
{
	struct osmo_conv_code code;

	memcpy(&code, &conv_cch, sizeof(struct osmo_conv_code));
	code.len = n;

	return osmo_conv_decode(&code, input, output);
}
