/* TETRA upper MAC layer main routine, above TMV-SAP */

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// #include <unistd.h>

// #include <osmocom/core/utils.h>
// #include <osmocom/core/msgb.h>
// #include <osmocom/core/talloc.h>

#include "crypto/tetra_crypto.h"
#include "tetra_common.h"
#include "tetra_prim.h"
#include "tetra_upper_mac.h"
#include "tetra_mac_pdu.h"
// #include "tetra_llc_pdu.h"
// #include "tetra_llc.h"

/* FIXME move global fragslots to context variable */
// struct fragslot fragslots[FRAGSLOT_NR_SLOTS] = {0};

void init_fragslot(struct fragslot *fragslot)
{
	if (fragslot->msgb) {
		/* Should never be the case, but just to be sure */
		// talloc_free(fragslot->msgb);
		free(fragslot->msgb);
		memset(fragslot, 0, sizeof(struct fragslot));
	}
	fragslot->msgb = msgb_alloc(FRAGSLOT_MSGB_SIZE, "fragslot");
}

void cleanup_fragslot(struct fragslot *fragslot)
{
	if (fragslot->msgb) {
		// talloc_free(fragslot->msgb);
		free(fragslot->msgb);
	}
	memset(fragslot, 0, sizeof(struct fragslot));
}

void age_fragslots(struct tetra_mac_state *tms)
{
	int i;
	for (i = 0; i < FRAGSLOT_NR_SLOTS; i++) {
		if (tms->fragslots[i].active) {
			tms->fragslots[i].age++;
			if (tms->fragslots[i].age > N203) {
				// printf("\nFRAG: aged out old fragments for slot=%d fragments=%d length=%d timer=%d\n", i, tms->fragslots[i].num_frags, tms->fragslots[i].length, tms->fragslots[i].age);
				cleanup_fragslot(&tms->fragslots[i]);
			}
		}
	}
}

static int get_num_fill_bits(const unsigned char *l1h, int len_with_fillbits)
{
	for (int i = 1; i < len_with_fillbits; i++) {
		if (l1h[len_with_fillbits - i] == 1) {
			return i;
		}
	}
	// printf("WARNING get_fill_bits_len: no 1 bit within %d bytes from end\n", len_with_fillbits);
	return 0;
}

static int rx_bcast(struct tetra_tmvsap_prim *tmvp, struct tetra_mac_state *tms)
{
	struct msgb *msg = tmvp->oph.msg;
	struct tetra_crypto_state *tcs = tms->tcs;
	struct tetra_si_decoded sid;
	uint32_t dl_freq, ul_freq;
	int i;

	memset(&sid, 0, sizeof(sid));
	macpdu_decode_sysinfo(&sid, msg->l1h);
	tmvp->u.unitdata.tdma_time.hn = sid.hyperframe_number;

	dl_freq = tetra_dl_carrier_hz(sid.freq_band,
				      sid.main_carrier,
				      sid.freq_offset);

	ul_freq = tetra_ul_carrier_hz(sid.freq_band,
				      sid.main_carrier,
				      sid.freq_offset,
				      sid.duplex_spacing,
				      sid.reverse_operation);

	// printf("BNCH SYSINFO (DL %u Hz, UL %u Hz), service_details 0x%04x ",
		// dl_freq, ul_freq, sid.mle_si.bs_service_details);
	tms->t_display_st->dl_freq = dl_freq;
	tms->t_display_st->ul_freq = ul_freq;
	if (sid.cck_valid_no_hf) {
		// printf("CCK ID %u", sid.cck_id);
	} else {
		// printf("Hyperframe %u", sid.hyperframe_number);
		tms->t_display_st->curr_hyperframe = sid.hyperframe_number;
	}
	// printf("\n");
	for (i = 0; i < 12; i++) {
		// printf("\t%s: %u\n", tetra_get_bs_serv_det_name(1 << i),
			// sid.mle_si.bs_service_details & (1 << i) ? 1 : 0);
			switch(i) {
				case 0:
					tms->t_display_st->advanced_link = (sid.mle_si.bs_service_details & (1 << i) ? 1 : 0);
					break;
				case 1:
					tms->t_display_st->air_encryption = (sid.mle_si.bs_service_details & (1 << i) ? 1 : 0);
					break;
				case 2:
					tms->t_display_st->sndcp_data = (sid.mle_si.bs_service_details & (1 << i) ? 1 : 0);
					break;
				case 4:
					tms->t_display_st->circuit_data = (sid.mle_si.bs_service_details & (1 << i) ? 1 : 0);
					break;
				case 5:
					tms->t_display_st->voice_service = (sid.mle_si.bs_service_details & (1 << i) ? 1 : 0);
					break;
				case 6:
					tms->t_display_st->normal_mode = (sid.mle_si.bs_service_details & (1 << i) ? 1 : 0);
					break;
				case 7:
					tms->t_display_st->migration_supported = (sid.mle_si.bs_service_details & (1 << i) ? 1 : 0);
					break;
				case 8:
					tms->t_display_st->never_minimum_mode = (sid.mle_si.bs_service_details & (1 << i) ? 1 : 0);
					break;
				case 9:
					tms->t_display_st->priority_cell = (sid.mle_si.bs_service_details & (1 << i) ? 1 : 0);
					break;
				case 10:
					tms->t_display_st->dereg_mandatory = (sid.mle_si.bs_service_details & (1 << i) ? 1 : 0);
					break;
				case 11:
					tms->t_display_st->reg_mandatory = (sid.mle_si.bs_service_details & (1 << i) ? 1 : 0);
					break;
			}
	}

	memcpy(&tms->last_sid, &sid, sizeof(sid));

	/* Update crypto state */
	tcs->la = sid.mle_si.la;
	tcs->cn = sid.main_carrier; /* FIXME this won't work when not tuned to the main carier */
	if (sid.cck_valid_no_hf) {
		if (sid.cck_id != tcs->cck_id) {
			tcs->cck_id = sid.cck_id;
			update_current_cck(tcs);
		}
	} else {
		tcs->hn = sid.hyperframe_number;
	}

	return -1; /* FIXME check this indeed fills slot */
}

const char *tetra_alloc_dump(const struct tetra_chan_alloc_decoded *cad, struct tetra_mac_state *tms)
{
	static char buf[64];
	char *cur = buf;
	unsigned int freq_band, freq_offset;

	if (cad->ext_carr_pres) {
		freq_band = cad->ext_carr.freq_band;
		freq_offset = cad->ext_carr.freq_offset;
	} else {
		freq_band = tms->last_sid.freq_band;
		freq_offset = tms->last_sid.freq_offset;
	}

	cur += sprintf(cur, "%s (TN%u/%s/%uHz)",
		tetra_get_alloc_t_name(cad->type), cad->timeslot,
		tetra_get_ul_dl_name(cad->ul_dl),
		tetra_dl_carrier_hz(freq_band, cad->carrier_nr, freq_offset));

	return buf;
}

static int rx_resrc(struct tetra_tmvsap_prim *tmvp, struct tetra_mac_state *tms)
{
	struct msgb *msg = tmvp->oph.msg;
	struct tetra_crypto_state *tcs = tms->tcs;
	struct tetra_resrc_decoded rsd;
	struct msgb *fragmsgb;
	struct tetra_key *key = 0;
	int tmpdu_offset, slot;
	int pdu_bits; /* Full length of pdu, including fill bits */

	memset(&rsd, 0, sizeof(rsd));
	tmpdu_offset = macpdu_decode_resource(&rsd, msg->l1h, 0);

	if (rsd.macpdu_length == MACPDU_LEN_2ND_STOLEN) {
		pdu_bits = -1;				/* Fills slot */
		tms->cur_burst.blk2_stolen = true;	/* Next block is also stolen */
	} else if (rsd.macpdu_length == MACPDU_LEN_START_FRAG) {
		pdu_bits = -1;				/* Fills slot */
	} else {
		pdu_bits = rsd.macpdu_length * 8;	/* Length given */
		msg->tail = msg->head + pdu_bits;
		msg->len = (uint16_t)(msg->tail - msg->head);
	}

	/* Strip fill bits */
	if (rsd.fill_bits) {
		int num_fill_bits = get_num_fill_bits(msg->l1h, msgb_l1len(msg));
		msg->tail -= num_fill_bits;
		msg->len = (uint16_t)(msg->tail - msg->head);
	}

	/* Decrypt buffer if encrypted and key available */
	if (rsd.is_encrypted && tcdb->num_keys) {
		decrypt_identity(tcs, &rsd.addr);
		key = get_ksg_key(tcs, rsd.addr.ssi);

		if (key) {
			rsd.is_encrypted = !decrypt_mac_element(tcs, tmvp, key, msgb_l1len(msg), tmpdu_offset);
			if (rsd.chan_alloc_pres) {
				// Re-decode the channel allocation element to get accurate L2 start
				tmpdu_offset += macpdu_decode_chan_alloc(&rsd.cad, msg->l1h + tmpdu_offset);
			}
		}
	}

	/* We now have accurate length and start of TM-SDU, set LLC start in msg->l2h */
	msg->l2h = msg->l1h + tmpdu_offset;
	// printf("RESOURCE Encr=%u%s len_field=%d l1_len=%d l2_len=%d Addr=%s",
		// rsd.encryption_mode,
		// rsd.encryption_mode && !rsd.is_encrypted ? " DECRYPTED" : "",
		// rsd.macpdu_length, msgb_l1len(msg), msgb_l2len(msg),
		// tetra_addr_dump(&rsd.addr));

	if (rsd.chan_alloc_pres) {
		if (!rsd.is_encrypted) {
			// printf(" ChanAlloc=%s", tetra_alloc_dump(&rsd.cad, tms));
		} else {
			// printf(" ChanAlloc=ENCRYPTED");
		}
	}

	if (rsd.slot_granting.pres) {
		// printf(" SlotGrant=%u/%u", rsd.slot_granting.nr_slots,
			// rsd.slot_granting.delay);
	}

	if (rsd.addr.type == ADDR_TYPE_NULL) {
		pdu_bits = -1; /* No more PDUs in slot */
		goto out;
	}
	tms->ssi = rsd.addr.ssi;
	tms->usage_marker = rsd.addr.usage_marker;
	tms->addr_type = rsd.addr.type;

	if (msgb_l2len(msg) == 0)
		goto out; /* No l2 data */

	if (rsd.is_encrypted)
		goto out; /* Can't parse any further */

	// printf(": %s\n", osmo_ubit_dump(msg->l2h, msgb_l2len(msg)));
	if (rsd.macpdu_length != MACPDU_LEN_START_FRAG || !REASSEMBLE_FRAGMENTS) {
		/* Non-fragmented resource (or no reassembly desired) */
		// rx_tm_sdu(tms, msg, msgb_l2len(msg));
		//TODO: fix llc
	} else {
		/* Fragmented resource */
		slot = tmvp->u.unitdata.tdma_time.tn;
		if (tms->fragslots[slot].active) {
			// printf("\nWARNING: fragment slot still active\n");
			cleanup_fragslot(&tms->fragslots[slot]);
		}

		init_fragslot(&tms->fragslots[slot]);
		fragmsgb = tms->fragslots[slot].msgb;

		/* Copy l2 part to fragmsgb. l3h is constructed once all fragments are merged */
		fragmsgb = tms->fragslots[slot].msgb;
		fragmsgb->l1h = msgb_put(fragmsgb, msgb_l1len(msg));
		fragmsgb->l2h = fragmsgb->l1h + tmpdu_offset;
		fragmsgb->l3h = 0;
		memcpy(fragmsgb->l2h, msg->l2h, msgb_l2len(msg));

		// printf("\nFRAG-START slot=%d len=%d msgb=%s\n", slot, msgb_l2len(fragmsgb), osmo_ubit_dump(fragmsgb->l2h, msgb_l2len(msg)));
		tms->fragslots[slot].active = 1;
		tms->fragslots[slot].num_frags = 1;
		tms->fragslots[slot].length = msgb_l2len(msg);
		tms->fragslots[slot].encryption = rsd.encryption_mode > 0;
		tms->fragslots[slot].key = key;
	}

out:
	// printf("\n");
	return pdu_bits;
}

void append_frag_bits(struct tetra_mac_state *tms, int slot, uint8_t *bits, int bitlen)
{
	struct msgb *fragmsgb;
	fragmsgb = tms->fragslots[slot].msgb;
	if (fragmsgb->len + bitlen > fragmsgb->data_len) {
		// printf(" WARNING: FRAG LENGTH ERROR!\n");
		return;
	}
	unsigned char *append_ptr = msgb_put(fragmsgb, bitlen);
	memcpy(append_ptr, bits, bitlen);

	tms->fragslots[slot].length = tms->fragslots[slot].length + bitlen;
	tms->fragslots[slot].num_frags++;
	tms->fragslots[slot].age = 0;
}

static int rx_macfrag(struct tetra_tmvsap_prim *tmvp, struct tetra_mac_state *tms)
{
	struct msgb *msg = tmvp->oph.msg;
	struct msgb *fragmsgb;
	int slot = tmvp->u.unitdata.tdma_time.tn;
	uint8_t *bits = msg->l1h;
	uint8_t fillbits_present;
	int n = 0;
	int m = 0;

	if (tms->fragslots[slot].active) {
		m = 2; n = n + m; /*  MAC-FRAG/END (01) */
		m = 1; n = n + m; /*  MAC-FRAG (0) */
		m = 1; fillbits_present = bits_to_uint(bits + n, m); n = n + m;
		msg->l2h = msg->l1h + n;

		/* MAC-FRAG will always fill remainder of the slot, but fill bits may be present */
		if (fillbits_present) {
			int num_fill_bits = get_num_fill_bits(msg->l1h, msgb_l1len(msg));
			msg->tail -= num_fill_bits;
			msg->len = (uint16_t)(msg->tail - msg->head);
		}

		/* Decrypt (if required) */
		fragmsgb = tms->fragslots[slot].msgb;
		if (tms->fragslots[slot].encryption && tms->fragslots[slot].key)
			decrypt_mac_element(tms->tcs, tmvp, tms->fragslots[slot].key, msgb_l1len(msg), n);

		/* Add frag to fragslot buffer */
		append_frag_bits(tms, slot, msg->l2h, msgb_l2len(msg));
		// printf("FRAG-CONT slot=%d added=%d msgb=%s\n", slot, msgb_l2len(msg), osmo_ubit_dump(fragmsgb->l2h, msgb_l2len(fragmsgb)));
	} else {
		// printf("WARNING got fragment without start packet for slot=%d\n", slot);
	}
	return -1; /* Always fills slot */
}

static int rx_macend(struct tetra_tmvsap_prim *tmvp, struct tetra_mac_state *tms)
{
	struct msgb *msg = tmvp->oph.msg;
	int slot = tmvp->u.unitdata.tdma_time.tn;
	struct tetra_resrc_decoded rsd;
	uint8_t *bits = msg->l1h;
	uint8_t fillbits_present, chanalloc_present, length_indicator, slot_granting;
	int num_fill_bits;
	struct msgb *fragmsgb;
	int n = 0;
	int m = 0;

	memset(&rsd, 0, sizeof(rsd));
	m = 2; n = n + m; /*  MAC-FRAG/END (01) */
	m = 1; n = n + m; /*  MAC-END (1) */
	m = 1; fillbits_present = bits_to_uint(bits + n, m); n = n + m;
	m = 1; n = n + m; /* position_of_grant */
	m = 6; length_indicator = bits_to_uint(bits + n, m); n = n + m;

	fragmsgb = tms->fragslots[slot].msgb;
	if (tms->fragslots[slot].active) {

		/* FIXME: handle napping bit in d8psk and qam */
		m = 1; slot_granting = bits_to_uint(bits + n, m); n = n + m;
		if (slot_granting) {
			/* FIXME: multiple slot granting in qam */
			m = 8; /* basic slot granting */ n = n + m;
		}
		m = 1; chanalloc_present = bits_to_uint(bits + n, m); n = n + m;

		/* Determine msg len, strip fill bits if any */
		msg->tail = msg->head + length_indicator * 8;
		msg->len = (uint16_t)(msg->tail - msg->head);
		if (fillbits_present) {
			num_fill_bits = get_num_fill_bits(msg->l1h, msgb_l1len(msg));
			msg->tail -= num_fill_bits;
			msg->len = (uint16_t)(msg->tail - msg->head);
		}

		/* Decrypt (if required) */
		if (tms->fragslots[slot].encryption && tms->fragslots[slot].key)
			decrypt_mac_element(tms->tcs, tmvp, tms->fragslots[slot].key, msgb_l1len(msg), n);

		/* Parse chanalloc element (if present) and update l2 offsets */
		if (chanalloc_present) {
			m = macpdu_decode_chan_alloc(&rsd.cad, bits + n); n = n + m;
		}

		msg->l2h = msg->l1h + n;
		append_frag_bits(tms, slot, msg->l2h, msgb_l2len(msg));
		// printf("FRAG-END slot=%d added=%d msgb=%s\n", slot, msgb_l2len(msg), osmo_ubit_dump(fragmsgb->l2h, msgb_l2len(fragmsgb)));

		/* Message is completed inside fragmsgb now */
		if (!tms->fragslots[slot].encryption || tms->fragslots[slot].key) {
			// rx_tm_sdu(tms, fragmsgb, tms->fragslots[slot].length);
			//TODO: FIX LLC
		}
	} else {
		// printf("FRAG: got end frag with len %d without start packet for slot=%d\n", length_indicator * 8, slot);
	}

	cleanup_fragslot(&tms->fragslots[slot]);
	return length_indicator * 8;
}


static int rx_suppl(struct tetra_tmvsap_prim *tmvp, struct tetra_mac_state *tms)
{
	//struct tmv_unitdata_param *tup = &tmvp->u.unitdata;
	struct msgb *msg = tmvp->oph.msg;
	//struct tetra_suppl_decoded sud;
	int tmpdu_offset;

#if 0
	memset(&sud, 0, sizeof(sud));
	tmpdu_offset = macpdu_decode_suppl(&sud, msg->l1h, tup->lchan);
#else
	{
		uint8_t slot_granting = *(msg->l1h + 17);
		if (slot_granting)
			tmpdu_offset = 17 + 1 + 8;
		else
			tmpdu_offset = 17 + 1;
	}
#endif

	// printf("SUPPLEMENTARY MAC-D-BLOCK ");

	//if (sud.encryption_mode == 0)
	msg->l2h = msg->l1h + tmpdu_offset;
	// rx_tm_sdu(tms, msg, 100);
	//TODO: FIX LLC

	// printf("\n");
	return -1; /* TODO FIXME check length */
}

static void dump_access(struct tetra_access_field *acc, unsigned int num)
{
	// printf("ACCESS%u: %c/%u ", num, 'A'+acc->access_code, acc->base_frame_len);
}

static void rx_aach(struct tetra_tmvsap_prim *tmvp, struct tetra_mac_state *tms)
{
	struct tmv_unitdata_param *tup = &tmvp->u.unitdata;
	struct tetra_acc_ass_decoded aad;

	// printf("ACCESS-ASSIGN PDU: ");

	memset(&aad, 0, sizeof(aad));
	macpdu_decode_access_assign(&aad, tmvp->oph.msg->l1h,
				    tup->tdma_time.fn == 18 ? 1 : 0);

	if (aad.pres & TETRA_ACC_ASS_PRES_ACCESS1) {
		tms->t_display_st->access1_code = 'A'+(aad.access[0].access_code);
		tms->t_display_st->access1 = (aad.access[0].base_frame_len);
		dump_access(&aad.access[0], 1);
	}
	if (aad.pres & TETRA_ACC_ASS_PRES_ACCESS2) {
		dump_access(&aad.access[1], 2);
		tms->t_display_st->access2_code = 'A'+(aad.access[1].access_code);
		tms->t_display_st->access2 = (aad.access[1].base_frame_len);
	}
	if (aad.pres & TETRA_ACC_ASS_PRES_DL_USAGE) {
		// printf("DL_USAGE: %s ", tetra_get_dl_usage_name(aad.dl_usage));
		tms->t_display_st->dl_usage = aad.dl_usage;
	}
	if (aad.pres & TETRA_ACC_ASS_PRES_UL_USAGE) {
		// printf("UL_USAGE: %s ", tetra_get_ul_usage_name(aad.ul_usage));
		tms->t_display_st->ul_usage = aad.ul_usage;
	}

	/* save the state whether the current burst is traffic or not */
	if (aad.dl_usage > 3) {
		tms->cur_burst.is_traffic = aad.dl_usage;
	} else {
		tms->cur_burst.is_traffic = 0;
	}

	/* Reset slot stealing flags */
	tms->cur_burst.blk1_stolen = false;
	tms->cur_burst.blk2_stolen = false;

	// printf("\n");
}

static int rx_tmv_unitdata_ind(struct tetra_tmvsap_prim *tmvp, struct tetra_mac_state *tms)
{
	struct tmv_unitdata_param *tup = &tmvp->u.unitdata;
	struct msgb *msg = tmvp->oph.msg;
	uint8_t pdu_type = bits_to_uint(msg->l1h, 2);
	const char *pdu_name;
	int len_parsed;

	if (tup->lchan == TETRA_LC_BSCH)
		pdu_name = "SYNC";
	else if (tup->lchan == TETRA_LC_AACH)
		pdu_name = "ACCESS-ASSIGN";
	else {
		pdu_type = bits_to_uint(msg->l1h, 2);
		pdu_name = tetra_get_macpdu_name(pdu_type);
	}

	// printf("TMV-UNITDATA.ind %s %s CRC=%u %s\n",
		// tetra_tdma_time_dump(&tup->tdma_time),
		// tetra_get_lchan_name(tup->lchan),
		// tup->crc_ok, pdu_name);

	if (!tup->crc_ok)
		return -1;


	if (tup->tdma_time.fn == 18 && REASSEMBLE_FRAGMENTS)
		/* Age out old fragments */
		/* FIXME: also age out old event labels */
		age_fragslots(tms);

	len_parsed = -1; /* Default for cases where slot is filled or otherwise irrelevant */
	switch (tup->lchan) {
	case TETRA_LC_AACH:
		rx_aach(tmvp, tms);
		break;
	case TETRA_LC_BNCH:
	case TETRA_LC_UNKNOWN:
	case TETRA_LC_SCH_F:
		switch (pdu_type) {
		case TETRA_PDU_T_BROADCAST:
			len_parsed = rx_bcast(tmvp, tms);
			break;
		case TETRA_PDU_T_MAC_RESOURCE:
			len_parsed = rx_resrc(tmvp, tms);
			break;
		case TETRA_PDU_T_MAC_SUPPL:
			len_parsed = rx_suppl(tmvp, tms);
			break;
		case TETRA_PDU_T_MAC_FRAG_END:
			if (REASSEMBLE_FRAGMENTS) {
				if (msg->l1h[2] == TETRA_MAC_FRAGE_FRAG) {
					len_parsed = rx_macfrag(tmvp, tms);
				} else {
					len_parsed = rx_macend(tmvp, tms);
				}
			} else {
				len_parsed = -1; /* Can't determine len */
				if (msg->l1h[3] == TETRA_MAC_FRAGE_FRAG) {
					// printf("FRAG/END FRAG: ");
					msg->l2h = msg->l1h+4;
					// rx_tm_sdu(tms, msg, 100 /*FIXME*/);
					//TODO: FIX LLC
					// printf("\n");
				} else {
					// printf("FRAG/END END\n");
				}
			}
			break;
		default:
			len_parsed = -1; /* Can't determine len */
			// printf("STRANGE pdu=%u\n", pdu_type);
			break;
		}
		break;
	case TETRA_LC_BSCH:
		break;
	default:
		// printf("STRANGE lchan=%u\n", tup->lchan);
		break;
	}

	return len_parsed;
}

int upper_mac_prim_recv(struct osmo_prim_hdr *op, void *priv)
{
	struct tetra_tmvsap_prim *tmvp;
	struct tetra_mac_state *tms = priv;
	int pdu_bits = -1;

	switch (op->sap) {
	case TETRA_SAP_TMV:
		tmvp = (struct tetra_tmvsap_prim *) op;
		pdu_bits = rx_tmv_unitdata_ind(tmvp, tms);
		break;
	default:
		// printf("primitive on unknown sap\n");
		break;
	}

	return pdu_bits;
}
