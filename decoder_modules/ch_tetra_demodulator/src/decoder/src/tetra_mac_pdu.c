/* Implementation of some PDU parsing of the TETRA upper MAC */

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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

// #include <osmocom/core/utils.h>

#include "tetra_common.h"
#include "tetra_mac_pdu.h"

static void decode_d_mle_sysinfo(struct tetra_mle_si_decoded *msid, const uint8_t *bits)
{
	const uint8_t *cur = bits;

	msid->la = bits_to_uint(cur, 14); cur += 14;
	msid->subscr_class = bits_to_uint(cur, 16); cur += 16;
	msid->bs_service_details = bits_to_uint(cur, 12); cur += 12;
}

/* see 21.4.4.1 */
void macpdu_decode_sysinfo(struct tetra_si_decoded *sid, const uint8_t *si_bits)
{
	const uint8_t *cur     = si_bits;
	cur += 2; // skip Broadcast PDU header
	cur += 2; // skip Sysinfo PDU header

	sid->main_carrier      = bits_to_uint(cur, 12); cur += 12;
	sid->freq_band         = bits_to_uint(cur,  4); cur +=  4;
	sid->freq_offset       = bits_to_uint(cur,  2); cur +=  2;
	sid->duplex_spacing    = bits_to_uint(cur,  3); cur +=  3;
	sid->reverse_operation = *cur++;
	sid->num_of_csch       = bits_to_uint(cur,  2); cur +=  2;
	sid->ms_txpwr_max_cell = bits_to_uint(cur,  3); cur +=  3;
	sid->rxlev_access_min  = bits_to_uint(cur,  4); cur +=  4;
	sid->access_parameter  = bits_to_uint(cur,  4); cur +=  4;
	sid->radio_dl_timeout  = bits_to_uint(cur,  4); cur +=  4;
	sid->cck_valid_no_hf   = *cur++;
	if (sid->cck_valid_no_hf)
		sid->cck_id = bits_to_uint(cur, 16);
	else
		sid->hyperframe_number = bits_to_uint(cur, 16);
	sid->option_field      = bits_to_uint(cur,  2); cur +=  2;

	switch (sid->option_field) {
	case TETRA_MAC_OPT_FIELD_EVEN_MULTIFRAME:     // Even multiframe definition for TS mode
	case TETRA_MAC_OPT_FIELD_ODD_MULTIFRAME:      // Odd multiframe definition for TS mode
		sid->frame_bitmap = bits_to_uint(cur, 20); cur += 20;
		break;
	case TETRA_MAC_OPT_FIELD_ACCESS_CODE:         // Default definition for access code A
		sid->access_code = bits_to_uint(cur, 20); cur += 20;
		break;
	case TETRA_MAC_OPT_FIELD_EXT_SERVICES:        // Extended services broadcast
		sid->ext_service = bits_to_uint(cur, 20); cur += 20;
		break;
	}

	decode_d_mle_sysinfo(&sid->mle_si, si_bits + 124-42);  // could be also cur due to previous fixes
}

static const uint8_t addr_len_by_type[] = {
	[ADDR_TYPE_SSI]		= 24,
	[ADDR_TYPE_EVENT_LABEL]	= 10,
	[ADDR_TYPE_USSI]	= 24,
	[ADDR_TYPE_SMI]		= 24,
	[ADDR_TYPE_SSI_EVENT]	= 34,
	[ADDR_TYPE_SSI_USAGE]	= 30,
	[ADDR_TYPE_SMI_EVENT]	= 34,
};

/* 21.5.2 */
int macpdu_decode_chan_alloc(struct tetra_chan_alloc_decoded *cad, const uint8_t *bits)
{
	const uint8_t *cur = bits;

	cad->type =		bits_to_uint(cur, 2); cur += 2;
	cad->timeslot =		bits_to_uint(cur, 4); cur += 4;
	cad->ul_dl =		bits_to_uint(cur, 2); cur += 2;
	cad->clch_perm =	*cur++;
	cad->cell_chg_f =	*cur++;
	cad->carrier_nr =	bits_to_uint(cur, 12); cur += 12;

	cad->ext_carr_pres =	*cur++;
	if (cad->ext_carr_pres) {
		cad->ext_carr.freq_band =	bits_to_uint(cur, 4); cur += 4;
		cad->ext_carr.freq_offset =	bits_to_uint(cur, 2); cur += 2;
		cad->ext_carr.duplex_spc =	bits_to_uint(cur, 3); cur += 3;
		cad->ext_carr.reverse_oper =	bits_to_uint(cur, 1); cur += 1;
	}
	cad->monit_pattern =	bits_to_uint(cur, 2); cur += 2;
	if (cad->monit_pattern == 0) {
		cad->monit_patt_f18 =	bits_to_uint(cur, 2);
		cur += 2;
	}
	if (cad->ul_dl == 0) {
		cad->aug.ul_dl_ass =		bits_to_uint(cur, 2); cur += 2;
		cad->aug.bandwidth =		bits_to_uint(cur, 3); cur += 3;
		cad->aug.modulation =		bits_to_uint(cur, 3); cur += 3;
		cad->aug.max_ul_qam =		bits_to_uint(cur, 3); cur += 3;
		cur += 3; /* reserved */
		cad->aug.conf_chan_stat =	bits_to_uint(cur, 3); cur += 3;
		cad->aug.bs_imbalance =		bits_to_uint(cur, 4); cur += 4;
		cad->aug.bs_tx_rel =		bits_to_uint(cur, 5); cur += 5;
		cad->aug.napping_sts =		bits_to_uint(cur, 2); cur += 2;
		if (cad->aug.napping_sts == 1)
			cur += 11; /* napping info 21.5.2c */
		cur += 4; /* reserved */
		if (*cur++)
			cur += 16;
		if (*cur++)
			cur += 16;
		cur++;
	}
	return (int)(cur - bits);
}

/* According to table 21.90 */
static int decode_nr_slots(uint8_t in)
{
	const uint8_t dec_tbl[] = {
		[0x0]	= 0,	/* first sub-slot */
		[0x1]	= 1,
		[0x2]	= 2,
		[0x3]	= 3,
		[0x4]	= 4,
		[0x5]	= 5,
		[0x6]	= 6,
		[0x7]	= 8,
		[0x8]	= 10,
		[0x9]	= 13,
		[0xa]	= 17,
		[0xb]	= 24,
		[0xc]	= 34,
		[0xd]	= 51,
		[0xe]	= 68,
		[0xf]	= 0xff	/* second sub-slot */
	};
	return dec_tbl[in & 0xf];
}

static int decode_length(unsigned int length_ind)
{
	/* FIXME: Y2/Z2 for non-pi4 DQPSK */
	unsigned int y2 = 1, z2 = 1;

	if (length_ind == 0 || length_ind == 0x3b || length_ind == 0x3c)
		return -EINVAL;
	else if (length_ind <= 0x12)
		return y2 * length_ind;
	else if (length_ind <= 0x3a)
		return (18 * y2 + (length_ind - 18) * z2);
	else if (length_ind == 0x3e)
		return MACPDU_LEN_2ND_STOLEN;
	else if (length_ind == 0x3f)
		return MACPDU_LEN_START_FRAG;
	else
		return -EINVAL;
}


/* Section 21.4.3.1 MAC-RESOURCE */
int macpdu_decode_resource(struct tetra_resrc_decoded *rsd, const uint8_t *bits, uint8_t is_decrypted)
{
	const uint8_t *cur = bits + 2;
	rsd->fill_bits = bits_to_uint(cur, 1); cur += 1;
	rsd->grant_position = bits_to_uint(cur, 1); cur += 1;
	rsd->encryption_mode = bits_to_uint(cur, 2); cur += 2;
	rsd->is_encrypted = rsd->encryption_mode > 0 && !is_decrypted;
	rsd->rand_acc_flag = *cur++;
	rsd->macpdu_length = decode_length(bits_to_uint(cur, 6)); cur += 6;
	rsd->addr.type = bits_to_uint(cur, 3); cur += 3;
	switch (rsd->addr.type) {
	case ADDR_TYPE_NULL:
		return 0;
		break;
	case ADDR_TYPE_SSI:
	case ADDR_TYPE_USSI:
	case ADDR_TYPE_SMI:
		rsd->addr.ssi = bits_to_uint(cur, 24);
		break;
	case ADDR_TYPE_EVENT_LABEL:
		rsd->addr.event_label = bits_to_uint(cur, 10);
		break;
	case ADDR_TYPE_SSI_EVENT:
	case ADDR_TYPE_SMI_EVENT:
		rsd->addr.ssi = bits_to_uint(cur, 24);
		rsd->addr.event_label = bits_to_uint(cur+24, 10);
		break;
	case ADDR_TYPE_SSI_USAGE:
		rsd->addr.ssi = bits_to_uint(cur, 24);
		rsd->addr.usage_marker = bits_to_uint(cur+24, 6);
		break;
	default:
		return -EINVAL;
		break;
	}
	cur += addr_len_by_type[rsd->addr.type];
	/* no intermediate napping in pi/4 */
	rsd->power_control_pres = *cur++;
	if (rsd->power_control_pres)
		cur += 4;
	rsd->slot_granting.pres = *cur++;
	if (rsd->slot_granting.pres) {
#if 0
		/* check for multiple slot granting flag (can only exist in QAM) */
		if (*cur++) {
			cur += 0; //FIXME;
		} else {
#endif
			rsd->slot_granting.nr_slots =
				decode_nr_slots(bits_to_uint(cur, 4));
			cur += 4;
			rsd->slot_granting.delay = bits_to_uint(cur, 4);
			cur += 4;
#if 0
		}
#endif
	}
	rsd->chan_alloc_pres = *cur++;

	if (rsd->chan_alloc_pres && !rsd->is_encrypted)
		// We can only determine length if the frame is unencrypted
		cur += macpdu_decode_chan_alloc(&rsd->cad, cur);

	return (int)(cur - bits);
}

static void decode_access_field(struct tetra_access_field *taf, uint8_t field)
{
	field &= 0x3f;
	taf->access_code = field >> 4;
	taf->base_frame_len = field & 0xf;
}

/* Section 21.4.7.2 ACCESS-ASSIGN PDU */
void macpdu_decode_access_assign(struct tetra_acc_ass_decoded *aad, const uint8_t *bits, int f18)
{
	uint8_t field1, field2;
	aad->hdr = bits_to_uint(bits, 2);
	field1 = bits_to_uint(bits+2, 6);
	field2 = bits_to_uint(bits+8, 6);

	if (f18 == 0) {
		switch (aad->hdr) {
		case TETRA_ACC_ASS_DLCC_ULCO:
			/* Field 1 and Field2 are Access fields */
			decode_access_field(&aad->access[0], field1);
			decode_access_field(&aad->access[1], field2);
			aad->pres |= TETRA_ACC_ASS_PRES_ACCESS1;
			aad->pres |= TETRA_ACC_ASS_PRES_ACCESS2;
			break;
		case TETRA_ACC_ASS_DLF1_ULCA:
			/* Field1: DL usage marker */
			aad->dl_usage = field1;
			aad->pres |= TETRA_ACC_ASS_PRES_DL_USAGE;
			/* Field2: Access field */
			decode_access_field(&aad->access[1], field2);
			aad->pres |= TETRA_ACC_ASS_PRES_ACCESS2;
			break;
		case TETRA_ACC_ASS_DLF1_ULAO:
			/* Field1: DL usage marker */
			aad->dl_usage = field1;
			aad->pres |= TETRA_ACC_ASS_PRES_DL_USAGE;
			/* Field2: Access field */
			decode_access_field(&aad->access[1], field2);
			aad->pres |= TETRA_ACC_ASS_PRES_ACCESS2;
			break;
		case TETRA_ACC_ASS_DLF1_ULF1:
			/* Field1: DL usage marker */
			aad->dl_usage = field1;
			aad->pres |= TETRA_ACC_ASS_PRES_DL_USAGE;
			/* Field2: UL usage marker */
			aad->ul_usage = field2;
			aad->pres |= TETRA_ACC_ASS_PRES_UL_USAGE;
			break;
		}
	} else {
		switch (aad->hdr) {
		case TETRA_ACC_ASS_ULCO:
			/* Field1 and Field2: Access field */
			decode_access_field(&aad->access[0], field1);
			decode_access_field(&aad->access[1], field2);
			aad->pres |= TETRA_ACC_ASS_PRES_ACCESS1;
			aad->pres |= TETRA_ACC_ASS_PRES_ACCESS2;
			break;
		case TETRA_ACC_ASS_ULCA:
			/* Field1 and Field2: Access field */
			decode_access_field(&aad->access[0], field1);
			decode_access_field(&aad->access[1], field2);
			aad->pres |= TETRA_ACC_ASS_PRES_ACCESS1;
			aad->pres |= TETRA_ACC_ASS_PRES_ACCESS2;
			break;
		case TETRA_ACC_ASS_ULAO:
			/* Field1 and Field2: Access field */
			decode_access_field(&aad->access[0], field1);
			decode_access_field(&aad->access[1], field2);
			aad->pres |= TETRA_ACC_ASS_PRES_ACCESS1;
			aad->pres |= TETRA_ACC_ASS_PRES_ACCESS2;
			break;
		case TETRA_ACC_ASS_ULCA2:
			/* Field1: Traffic usage marker (UMt) */
			/* FIXME */
			/* Field2: Access field */
			decode_access_field(&aad->access[1], field2);
			aad->pres |= TETRA_ACC_ASS_PRES_ACCESS2;
			break;
		}
	}
}

static const struct value_string tetra_macpdu_t_names[5] = {
	{ TETRA_PDU_T_MAC_RESOURCE,	"RESOURCE" },
	{ TETRA_PDU_T_MAC_FRAG_END,	"FRAG/END" },
	{ TETRA_PDU_T_BROADCAST,	"BROADCAST" },
	{ TETRA_PDU_T_MAC_SUPPL,	"SUPPLEMENTARY" },
	{ 0, NULL }
};

const char *tetra_get_macpdu_name(uint8_t pdu_type)
{
	return get_value_string(tetra_macpdu_t_names, pdu_type);
}

static const struct value_string serv_det_names[] = {
	{ BS_SERVDET_REG_RQD,		"Registration mandatory" },
	{ BS_SERVDET_DEREG_RQD,		"De-registration mandatory" },
	{ BS_SERVDET_PRIO_CELL,		"Priority cell" },
	{ BS_SERVDET_MIN_MODE,		"Cell never uses minimum mode" },
	{ BS_SERVDET_MIGRATION,		"Migration supported" },
	{ BS_SERVDET_SYS_W_SERV,	"Normal mode" },
	{ BS_SERVDET_VOICE_SERV,	"Voice service" },
	{ BS_SERVDET_CSD_SERV,		"Circuit data" },
	{ BS_SERVDET_SNDCP_SERV,	"SNDCP data" },
	{ BS_SERVDET_AIR_ENCR,		"Air encryption" },
	{ BS_SERVDET_ADV_LINK,		"Advanced link" },
	{ 0, NULL },
};

const char *tetra_get_bs_serv_det_name(uint32_t pdu_type)
{
	return get_value_string(serv_det_names, pdu_type);
}

static const struct value_string dl_usage_names[] = {
	{ TETRA_DL_US_UNALLOC,	"Unallocated" },
	{ TETRA_DL_US_ASS_CTRL,	"Assigned control" },
	{ TETRA_DL_US_COM_CTRL,	"Common control" },
	{ TETRA_DL_US_RESERVED,	"Reserved" },
	{ 0, NULL },
};

const char *tetra_get_dl_usage_name(uint8_t num)
{
	if (num <= 3)
		return get_value_string(dl_usage_names, num);
	return "Traffic";
}
const char *tetra_get_ul_usage_name(uint8_t num)
{
	if (num == 0)
		return "Unallocated";
	return "Traffic";
}

static const struct value_string addr_type_names[] = {
	{ ADDR_TYPE_NULL,		"Null PDU" },
	{ ADDR_TYPE_SSI,		"SSI" },
	{ ADDR_TYPE_EVENT_LABEL,	"Event Label" },
	{ ADDR_TYPE_USSI,		"USSI (migrading MS un-exchanged)" },
	{ ADDR_TYPE_SMI,		"SMI (management)" },
	{ ADDR_TYPE_SSI_EVENT,		"SSI + Event Label" },
	{ ADDR_TYPE_SSI_USAGE,		"SSI + Usage Marker" },
	{ ADDR_TYPE_SMI_EVENT,		"SMI + Event Label" },
	{ 0, NULL }
};
const char *tetra_get_addr_t_name(uint8_t addrt)
{
	return get_value_string(addr_type_names, addrt);
}

static const struct value_string alloc_type_names[] = {
	{ TMAC_ALLOC_T_REPLACE,		"Replace" },
	{ TMAC_ALLOC_T_ADDITIONAL,	"Additional" },
	{ TMAC_ALLOC_T_QUIT_GO,		"Quit and go" },
	{ TMAC_ALLOC_T_REPL_SLOT1,	"Replace + Slot1" },
	{ 0, NULL }
};
const char *tetra_get_alloc_t_name(uint8_t alloct)
{
	return get_value_string(alloc_type_names, alloct);
}

const char *tetra_addr_dump(const struct tetra_addr *addr)
{
	static char buf[64];
	char *cur = buf;

	memset(buf, 0, sizeof(buf));
	cur += sprintf(cur, "%s(", tetra_get_addr_t_name(addr->type));
	switch (addr->type) {
	case ADDR_TYPE_NULL:
		break;
	case ADDR_TYPE_SSI:
	case ADDR_TYPE_USSI:
	case ADDR_TYPE_SMI:
		cur += sprintf(cur, "%u", addr->ssi);
		break;
	case ADDR_TYPE_EVENT_LABEL:
	case ADDR_TYPE_SSI_EVENT:
	case ADDR_TYPE_SMI_EVENT:
		cur += sprintf(cur, "%u/E%u", addr->ssi, addr->event_label);
		break;
	case ADDR_TYPE_SSI_USAGE:
		cur += sprintf(cur, "%u/U%u", addr->ssi, addr->usage_marker);
		break;
	}
	cur += sprintf(cur, ")");

	return buf;
}

static const struct value_string ul_dl_names[] = {
	{ 0, "Augmented" },
	{ 1, "Downlink only" },
	{ 2, "Uplink only" },
	{ 3, "Uplink + Downlink" },
	{ 0, NULL }
};
const char *tetra_get_ul_dl_name(uint8_t ul_dl)
{
	return get_value_string(ul_dl_names, ul_dl);
}
