/* Tetra convolutional encoder, according to ETSI EN 300 392-2 V3.2.1 (2007-09) */

/* This converts from type type-2 bits into type-3 bits */

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
#include <stdlib.h>

// #include <osmocom/core/utils.h>
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

#include <tetra_common.h>
#include <lower_mac/tetra_conv_enc.h>

static char *dump_state(struct conv_enc_state *ces)
{
	static char pbuf[1024];
	snprintf(pbuf, sizeof(pbuf), "%u-%u-%u-%u", ces->delayed[0],
		ces->delayed[1], ces->delayed[2], ces->delayed[3]);
	return pbuf;
}

/* Mother code according to Section 8.2.3.1.1 */
static uint8_t conv_enc_in_bit(struct conv_enc_state *ces, uint8_t bit, uint8_t *out)
{
	uint8_t g1, g2, g3, g4;
	uint8_t *delayed = ces->delayed;

	DEBUGP("State in: %s, Input bit: %u: ", dump_state(ces), bit);

	/* G1 = 1 + D + D4 */
	g1 = (bit + delayed[0] + delayed[3]) % 2;
	/* G2 = 1 + D2 + D3 + D4 */
	g2 = (bit + delayed[1] + delayed[2] + delayed[3]) % 2;
	/* G3 = 1 + D + D2 + D4 */
	g3 = (bit + delayed[0] + delayed[1] + delayed[3]) % 2;
	/* G4 = 1 + D + D3 + D4 */
	g4 = (bit + delayed[0] + delayed[2] + delayed[3]) % 2;

	/* shift the state and input our new bit */
	ces->delayed[3] = ces->delayed[2];
	ces->delayed[2] = ces->delayed[1];
	ces->delayed[1] = ces->delayed[0];
	ces->delayed[0] = bit;

	DEBUGP("Output bits: %u-%u-%u-%u, State out: %s\n", g1, g2, g3, g4,
		dump_state(ces));

	*out++ = g1;
	*out++ = g2;
	*out++ = g3;
	*out++ = g4;

	return (g1 | (g2 << 1) | (g3 << 2) | (g4 << 3));
}

/* in: bit-per-byte (len), out: bit-per-byte (4*len) */
int conv_enc_input(struct conv_enc_state *ces, uint8_t *in, int len, uint8_t *out)
{
	int i;

	for (i = 0; i < len; i++) {
		conv_enc_in_bit(ces, in[i], out);
		out += 4;
	}
	return 0;
}

int conv_enc_init(struct conv_enc_state *ces)
{
	memset(ces->delayed, 0, sizeof(ces->delayed));
	return 0;
}

/* Puncturing */

const uint8_t P_rate2_3[] = { 0, 1, 2, 5 };
const uint8_t P_rate1_3[] = { 0, 1, 2, 3, 5, 6, 7 };

/* Voice */
const uint8_t P_rate8_12[] = { 0, 1, 2, 4 };
const uint8_t P_rate8_18[] = { 0, 1, 2, 3, 4, 5, 7, 8, 10, 11 };
const uint8_t P_rate8_17[] = { 0, 1, 2, 3, 4, 5, 7, 8, 10, 11, 13, 14, 16, 17, 19, 20, 22, 23 };

struct puncturer {
	enum tetra_rcpc_puncturer type;
	const uint8_t *P;
	uint8_t t;
	uint8_t period;
	uint32_t (*i_func)(uint32_t j);
};

static uint32_t i_func_equals(uint32_t j)
{
	return j;
}

static uint32_t i_func_292(uint32_t j)
{
	return (j + ((j-1)/65));
}

static uint32_t i_func_148(uint32_t j)
{
	return (j + ((j-1)/35));
}

/* Section 8.2.3.1.3 */
static const struct puncturer punct_2_3 = {
	.type = TETRA_RCPC_PUNCT_2_3,
	.P = P_rate2_3,
	.t = 3,
	.period = 8,
	.i_func = &i_func_equals,
};

/* Section 8.2.3.1.4 */
static const struct puncturer punct_1_3 = {
	.type = TETRA_RCPC_PUNCT_1_3,
	.P = P_rate1_3,
	.t = 6,
	.period = 8,
	.i_func = &i_func_equals,
};

/* Section 8.2.3.1.5 */
static const struct puncturer punct_292_432 = {
	.type = TETRA_RCPC_PUNCT_292_432,
	.P = P_rate2_3,
	.t = 3,
	.period = 8,
	.i_func = &i_func_292,
};

/* Section 8.2.3.1.6 */
static const struct puncturer punct_148_432 = {
	.type = TETRA_RCPC_PUNCT_148_432,
	.P = P_rate1_3,
	.t = 6,
	.period = 8,
	.i_func = &i_func_148,
};

/* EN 300 395-2 Section 5.5.2.1 */
static const struct puncturer punct_112_168 = {
	.type = TETRA_RCPC_PUNCT_112_168,
	.P = P_rate8_12,
	.t = 3,
	.period = 6,
	.i_func = &i_func_equals,
};

/* EN 300 395-2 Section 5.5.2.2 */
static const struct puncturer punct_72_162 = {
	.type = TETRA_RCPC_PUNCT_72_162,
	.P = P_rate8_18,
	.t = 9,
	.period = 12,
	.i_func = &i_func_equals,
};

/* EN 300 395-2 Section 5.6.2.1 */
static const struct puncturer punct_38_80 = {
	.type = TETRA_RCPC_PUNCT_72_162,
	.P = P_rate8_17,
	.t = 17,
	.period = 24,
	.i_func = &i_func_equals,
};

static const struct puncturer *tetra_puncts[] = {
	[TETRA_RCPC_PUNCT_2_3]		= &punct_2_3,
	[TETRA_RCPC_PUNCT_1_3]		= &punct_1_3,
	[TETRA_RCPC_PUNCT_292_432]	= &punct_292_432,
	[TETRA_RCPC_PUNCT_148_432]	= &punct_148_432,
	[TETRA_RCPC_PUNCT_112_168]	= &punct_112_168,
	[TETRA_RCPC_PUNCT_72_162]	= &punct_72_162,
	[TETRA_RCPC_PUNCT_38_80]	= &punct_38_80,
};

/* Puncture the mother code (in) and write 'len' symbols to out */
int get_punctured_rate(enum tetra_rcpc_puncturer pu, uint8_t *in, uint32_t len, uint8_t *out)
{
	const struct puncturer *punct;
	uint32_t i, j, k;
	uint8_t t;
	const uint8_t *P;

	if (pu >= ARRAY_SIZE(tetra_puncts))
		return -EINVAL;

	punct = tetra_puncts[pu];
	t = punct->t;
	P = punct->P;

	/* Section 8.2.3.1.2 */
	for (j = 1; j <= len; j++) {
		i = punct->i_func(j);
		k = punct->period * ((i-1)/t) + P[i - t*((i-1)/t)];
		DEBUGP("j = %u, i = %u, k = %u\n", j, i, k);
		out[j-1] = in[k-1];
	}
	return 0;
}

/* De-Puncture the 'len' type-3 bits (in) and write mother code to out */
int tetra_rcpc_depunct(enum tetra_rcpc_puncturer pu, const uint8_t *in, uint32_t len, uint8_t *out)
{
	const struct puncturer *punct;
	uint32_t i, j, k;
	uint8_t t;
	const uint8_t *P;

	if (pu >= ARRAY_SIZE(tetra_puncts))
		return -EINVAL;

	punct = tetra_puncts[pu];
	t = punct->t;
	P = punct->P;

	/* Section 8.2.3.1.2 */
	for (j = 1; j <= len; j++) {
		i = punct->i_func(j);
		k = punct->period * ((i-1)/t) + P[i - t*((i-1)/t)];
		DEBUGP("j = %u, i = %u, k = %u\n", j, i, k);
		out[k-1] = in[j-1];
	}
	return 0;
}

struct punct_test_param {
	uint16_t type2_len;
	uint16_t type3_len;
	uint16_t mother_rate;	/* rate 1/3 for speech, 1/4 for data */
	enum tetra_rcpc_puncturer punct;
};

static const struct punct_test_param punct_test_params[] = {
	{ 80, 120, 4, TETRA_RCPC_PUNCT_2_3 },		/* BSCH */
	{ 292, 432, 4, TETRA_RCPC_PUNCT_292_432 },	/* TCH/4.8 */
	{ 148, 432, 4, TETRA_RCPC_PUNCT_148_432 },	/* TCH/2.4 */
	{ 144, 216, 4, TETRA_RCPC_PUNCT_2_3 },		/* SCH/HD, BNCH, STCH */
	{ 112, 168, 4, TETRA_RCPC_PUNCT_2_3 },		/* SCH/HU */
	{ 288, 432, 4, TETRA_RCPC_PUNCT_2_3 },		/* SCH/F */
	{ 112, 168, 3, TETRA_RCPC_PUNCT_112_168 },	/* Speech class 1 */
	{ 72, 162, 3, TETRA_RCPC_PUNCT_72_162 },	/* Speech class 2 */
	{ 38, 80, 3, TETRA_RCPC_PUNCT_38_80 },		/* Speech class 2 in STCH */
};

static int mother_memcmp(const uint8_t *mother, const uint8_t *depunct, uint32_t len)
{
	unsigned int i, equal = 0;

	for (i = 0; i < len; i++) {
		/* ignore any 0xff-initialized part */
		if (depunct[i] == 0xff)
			continue;
		if (depunct[i] != mother[i])
			return -1;
		equal++;
	}

	return equal;
}

static int test_one_punct(const struct punct_test_param *ptp)
{
	uint8_t *mother_buf;
	uint8_t *depunct_buf;
	uint8_t *type3_buf;
	int i, j, mother_len;

	printf("==> Testing Puncture/Depuncture mode %u (%u/%u)\n",
		ptp->punct, ptp->type2_len, ptp->type3_len);

	mother_len = ptp->type2_len * ptp->mother_rate;
	mother_buf = malloc(mother_len);
	depunct_buf = malloc(ptp->type2_len * ptp->mother_rate);
	type3_buf = malloc(ptp->type3_len);

	/* initialize mother buffer with sequence of bytes starting at 0 */
	for (i = 0, j = 0; i < mother_len; i++, j++) {
		if (j == 0xff)
			j = 0;
		mother_buf[i] = j;
	}

	/* puncture the mother_buf to type3_buf using rate 2/3 on 60 bits */
	get_punctured_rate(ptp->punct, mother_buf, ptp->type3_len, type3_buf);

	/* initialize the de-punctured buffer */
	memset(depunct_buf, 0xff, mother_len);

	/* de-puncture into the depunct_buf (i.e. what happens at the receiver) */
	tetra_rcpc_depunct(ptp->punct, type3_buf, ptp->type3_len, depunct_buf);

	DEBUGP("MOTH: %s\n", osmo_hexdump(mother_buf, mother_len));
	DEBUGP("PUNC: %s\n", osmo_hexdump(type3_buf, ptp->type3_len));
	DEBUGP("DEPU: %s\n", osmo_hexdump(depunct_buf, mother_len));

	i = mother_memcmp(mother_buf, depunct_buf, mother_len);
	if (i < 0) {
		fprintf(stderr, "Mother buf != Depunct buf\n");
		return i;
	} else if (i != ptp->type3_len) {
		fprintf(stderr, "Depunct buf only has %u equal symbols, we need %u\n",
			i, ptp->type3_len);
		return -EINVAL;
	}

	free(type3_buf);
	free(depunct_buf);
	free(mother_buf);

	return 0;
}

int tetra_punct_test(void)
{
	long unsigned int i;
	int rc;

	for (i = 0; i < ARRAY_SIZE(punct_test_params); i++) {
		rc = test_one_punct(&punct_test_params[i]);
		if (rc < 0)
			return rc;
	}

	return 0;
}
