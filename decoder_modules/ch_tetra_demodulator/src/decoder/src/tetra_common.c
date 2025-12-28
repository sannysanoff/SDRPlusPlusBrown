/* Common routines for the TETRA PHY/MAC implementation */

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
#include <stdlib.h>

#include "tetra_common.h"

const char *get_value_string_or_null(const struct value_string *vs,
				     uint32_t val)
{
	int i;

	if (!vs)
		return NULL;

	for (i = 0;; i++) {
		if (vs[i].value == 0 && vs[i].str == NULL)
			break;
		if (vs[i].value == val)
			return vs[i].str;
	}

	return NULL;
}
const char *get_value_string(const struct value_string *vs, uint32_t val)
{
	const char *str = get_value_string_or_null(vs, val);
	if (str)
		return str;

	static char namebuf[255];
	snprintf(namebuf, sizeof(namebuf), "unknown 0x%"PRIx32, val);
	namebuf[sizeof(namebuf) - 1] = '\0';
	return namebuf;
}

/*! Allocate a new message buffer from given talloc context
 * \param[in] ctx talloc context from which to allocate
 * \param[in] size Length in octets, including headroom
 * \param[in] name Human-readable name to be associated with msgb
 * \returns dynamically-allocated \ref msgb
 *
 * This function allocates a 'struct msgb' as well as the underlying
 * memory buffer for the actual message data (size specified by \a size)
 * using the talloc memory context previously set by \ref msgb_set_talloc_ctx
 */
struct msgb *msgb_alloc_c(uint16_t size, const char *name)
{
	struct msgb *msg;

	// msg = talloc_named_const(ctx, sizeof(*msg) + size, name);
	msg = (struct msgb*)malloc(sizeof(*msg) + size);
	if (!msg) {
		return NULL;
	}

	/* Manually zero-initialize allocated memory */
	memset(msg, 0x00, sizeof(*msg) + size);

	msg->data_len = size;
	msg->len = 0;
	msg->data = msg->_data;
	msg->head = msg->_data;
	msg->tail = msg->_data;

	return msg;
}

/*! Allocate a new message buffer from tall_msgb_ctx
 * \param[in] size Length in octets, including headroom
 * \param[in] name Human-readable name to be associated with msgb
 * \returns dynamically-allocated \ref msgb
 *
 * This function allocates a 'struct msgb' as well as the underlying
 * memory buffer for the actual message data (size specified by \a size)
 * using the talloc memory context previously set by \ref msgb_set_talloc_ctx
 */
struct msgb *msgb_alloc(uint16_t size, const char *name)
{
	return msgb_alloc_c(size, name);
}





// #include <osmocom/core/utils.h>
// #include <osmocom/core/bits.h>

#include "tetra_common.h"
#include "tetra_prim.h"

uint32_t bits_to_uint(const uint8_t *bits, unsigned int len)
{
	uint32_t ret = 0;

	while (len--)
		ret = (ret << 1) | (*bits++ & 1);

	return ret;
}

static inline uint32_t tetra_band_base_hz(uint8_t band)
{
	return (band * 100000000);
}

static const int16_t tetra_carrier_offset[4] = {
	[0] =	     0,
	[1] =	  6250,
	[2] =	 -6250,
	[3] =	 12500,
};

uint32_t tetra_dl_carrier_hz(uint8_t band, uint16_t carrier, uint8_t offset)
{
	uint32_t freq = tetra_band_base_hz(band);
	freq += carrier * 25000;
	freq += tetra_carrier_offset[offset&3];
	return freq;
}

/* TS 100 392-15, Table 2 */
static const int16_t tetra_duplex_spacing[8][16] = { /* values are in kHz */
	[0] = { -1, 1600, 10000, 10000, 10000, 10000, 10000, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	[1] = { -1, 4500, -1, 36000, 7000, -1, -1, -1, 45000, 45000, -1, -1, -1, -1, -1, -1 },
	[2] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	[3] = { -1, -1, -1, 8000, 8000, -1, -1, -1, 18000, 18000, -1, -1, -1, -1, -1, -1 },
	[4] = { -1, -1, -1, 18000, 5000, -1, 30000, 30000, -1, 39000, -1, -1, -1, -1, -1, -1 },
	[5] = { -1, -1, -1, -1, 9500, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	[6] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	[7] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
};

uint32_t tetra_ul_carrier_hz(uint8_t band, uint16_t carrier, uint8_t offset,
			     uint8_t duplex, uint8_t reverse)
{
	uint32_t freq = tetra_dl_carrier_hz(band, carrier, offset);

	uint32_t duplex_spacing = tetra_duplex_spacing[duplex & 7][band & 15];

	if (duplex_spacing < 0) /* reserved for future standardization */
		return 0;

	duplex_spacing *= 1000; // make Hz

	if (reverse)
		freq += duplex_spacing;
	else
		freq -= duplex_spacing;

	return freq;
}

static const struct value_string tetra_sap_names[] = {
	{ TETRA_SAP_TP,		"TP-SAP" },
	{ TETRA_SAP_TMV,	"TMV-SAP" },
	{ TETRA_SAP_TMA,	"TMA-SAP" },
	{ TETRA_SAP_TMB,	"TMB-SAP" },
	{ TETRA_SAP_TMD,	"TMD-SAP" },
	{ 0, NULL }
};

static const struct value_string tetra_lchan_names[] = {
	{ TETRA_LC_UNKNOWN,	"UNKNOWN" },
	{ TETRA_LC_SCH_F,	"SCH/F" },
	{ TETRA_LC_SCH_HD,	"SCH/HD" },
	{ TETRA_LC_SCH_HU,	"SCH/HU" },
	{ TETRA_LC_STCH,	"STCH" },
	{ TETRA_LC_SCH_P8_F,	"SCH-P8/F" },
	{ TETRA_LC_SCH_P8_HD,	"SCH-P8/HD" },
	{ TETRA_LC_SCH_P8_HU,	"SCH-P8/HU" },
	{ TETRA_LC_AACH,	"AACH" },
	{ TETRA_LC_TCH,		"TCH" },
	{ TETRA_LC_BSCH,	"BSCH" },
	{ TETRA_LC_BNCH,	"BNCH" },
	{ 0, NULL }
};

const char *tetra_get_lchan_name(enum tetra_log_chan lchan)
{
	return get_value_string(tetra_lchan_names, lchan);
}

const char *tetra_get_sap_name(uint8_t sap)
{
	return get_value_string(tetra_sap_names, sap);
}

void tetra_mac_state_init(struct tetra_mac_state *tms)
{
	// INIT_LLIST_HEAD(&tms->voice_channels);
	tms->codec_first_pass = true;
}
