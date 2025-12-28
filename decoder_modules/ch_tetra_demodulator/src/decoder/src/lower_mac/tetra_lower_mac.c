/* TETRA lower MAC layer main routine, between TP-SAP and TMV-SAP */

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
#include <errno.h>
// #include <linux/limits.h>

// #include <osmocom/core/utils.h>
// #include <osmocom/core/msgb.h>
// #include <osmocom/core/talloc.h>

#include <tetra_common.h>
#include <tetra_tdma.h>
#include <phy/tetra_burst.h>
#include <phy/tetra_burst_sync.h>
#include <lower_mac/crc_simple.h>
#include <lower_mac/tetra_scramb.h>
#include <lower_mac/tetra_interleave.h>
#include <lower_mac/tetra_conv_enc.h>
#include <tetra_prim.h>
#include "tetra_upper_mac.h"
#include <lower_mac/viterbi.h>
#include <crypto/tetra_crypto.h>

#include "c-code/channel.h"
#include "c-code/source.h"

struct tetra_blk_param {
	const char *name;
	uint16_t type345_bits;
	uint16_t type2_bits;
	uint16_t type1_bits;
	uint16_t interleave_a;
	uint8_t have_crc16;
};

/* try to aggregate all of the magic numbers somewhere central */
static const struct tetra_blk_param tetra_blk_param[] = {
	[TPSAP_T_SB1] = {
		.name		= "SB1",
		.type345_bits	= 120,
		.type2_bits	= 80,
		.type1_bits	= 60,
		.interleave_a	= 11,
		.have_crc16	= 1,
	},
	[TPSAP_T_SB2] = {
		.name		= "SB2",
		.type345_bits	= 216,
		.type2_bits	= 144,
		.type1_bits	= 124,
		.interleave_a	= 101,
		.have_crc16	= 1,
	},
	[TPSAP_T_NDB] = {
		.name		= "NDB",
		.type345_bits	= 216,
		.type2_bits	= 144,
		.type1_bits	= 124,
		.interleave_a	= 101,
		.have_crc16	= 1,
	},
	[TPSAP_T_SCH_HU] = {
		.name		= "SCH/HU",
		.type345_bits	= 168,
		.type2_bits	= 112,
		.type1_bits	= 92,
		.interleave_a	= 13,
		.have_crc16	= 1,
	},
	[TPSAP_T_SCH_F] = {
		.name		= "SCH/F",
		.type345_bits	= 432,
		.type2_bits	= 288,
		.type1_bits	= 268,
		.interleave_a	= 103,
		.have_crc16	= 1,
	},
	[TPSAP_T_BBK] = {
		.name		= "BBK",
		.type345_bits	= 30,
		.type2_bits	= 30,
		.type1_bits	= 14,
	},
};

struct tetra_cell_data {
	uint16_t mcc;
	uint16_t mnc;
	uint8_t colour_code;
	struct tetra_tdma_time time;

	uint32_t scramb_init;
};

static struct tetra_cell_data _tcd, *tcd = &_tcd;

int is_bsch(struct tetra_tdma_time *tm)
{
	if (tm->fn == 18 && tm->tn == 4 - ((tm->mn+1)%4))
		return 1;
	return 0;
}

int is_bnch(struct tetra_tdma_time *tm)
{
	if (tm->fn == 18 && tm->tn == 4 - ((tm->mn+3)%4))
		return 1;
	return 0;
}

struct tetra_tmvsap_prim *tmvsap_prim_alloc(uint16_t prim, uint8_t op)
{
	struct tetra_tmvsap_prim *ttp;

	// ttp = talloc_zero(NULL, struct tetra_tmvsap_prim);
	ttp = malloc(sizeof(struct tetra_tmvsap_prim));
	memset(ttp, 0, sizeof(struct tetra_tmvsap_prim));
	ttp->oph.msg = msgb_alloc(412, "tmvsap_prim");
	ttp->oph.sap = TETRA_SAP_TMV;
	ttp->oph.primitive = prim;
	ttp->oph.operation = op;

	return ttp;
}

/* incoming TP-SAP UNITDATA.ind  from PHY into lower MAC */
void tp_sap_udata_ind(enum tp_sap_data_type type, int blk_num, const uint8_t *bits, unsigned int len, void *priv)
{
	/* various intermediary buffers */
	uint8_t type4[512];
	uint8_t type3dp[512*4];
	uint8_t type3[512];
	uint8_t type2[512];

	const struct tetra_blk_param *tbp = &tetra_blk_param[type];
	struct tetra_mac_state *tms = priv;
	struct tetra_crypto_state *tcs = tms->tcs;
	const char *time_str;

	/* TMV-SAP.UNITDATA.ind primitive which we will send to the upper MAC */
	struct tetra_tmvsap_prim *ttp;
	struct tmv_unitdata_param *tup;

	struct msgb *msg;

	ttp = tmvsap_prim_alloc(PRIM_TMV_UNITDATA, PRIM_OP_INDICATION);
	tup = &ttp->u.unitdata;
	msg = ttp->oph.msg;

	/* update the cell time */
	memcpy(&tcd->time, &t_phy_state.time, sizeof(tcd->time));
	time_str = tetra_tdma_time_dump(&tcd->time);

	if (type == TPSAP_T_SB2 && is_bnch(&tcd->time)) {
		tup->lchan = TETRA_LC_BNCH;
		// printf("BNCH FOLLOWS\n");
	}

	DEBUGP("%s %s type5: %s\n", tbp->name, tetra_tdma_time_dump(&tcd->time),
		osmo_ubit_dump(bits, tbp->type345_bits));

	/* De-scramble, pay special attention to SB1 pre-defined scrambling */
	memcpy(type4, bits, tbp->type345_bits);
	if (type == TPSAP_T_SB1) {
		tetra_scramb_bits(SCRAMB_INIT, type4, tbp->type345_bits);
		tup->scrambling_code = SCRAMB_INIT;
	} else {
		tetra_scramb_bits(tcd->scramb_init, type4, tbp->type345_bits);
		tup->scrambling_code = tcd->scramb_init;
	}

	DEBUGP("%s %s type4: %s\n", tbp->name, time_str,
		osmo_ubit_dump(type4, tbp->type345_bits));

	/* Handle block 1 slot stealing, see clause 19.4.4 */
	/* Block 1 is stolen if AACH says slot is traffic and burst used training sequence 1 */
	/* Block 2 is stolen if indicated in stolen block 1 resource len (-2) */
	if (tms->cur_burst.is_traffic && type == TPSAP_T_NDB && blk_num == BLK_1)
		tms->cur_burst.blk1_stolen = true;

	if (tbp->interleave_a) {
		/* Run block deinterleaving: type-3 bits */
		block_deinterleave(tbp->type345_bits, tbp->interleave_a, type4, type3);
		DEBUGP("%s %s type3: %s\n", tbp->name, time_str,
			osmo_ubit_dump(type3, tbp->type345_bits));
		/* De-puncture */
		memset(type3dp, 0xff, sizeof(type3dp));
		tetra_rcpc_depunct(TETRA_RCPC_PUNCT_2_3, type3, tbp->type345_bits, type3dp);
		DEBUGP("%s %s type3dp: %s\n", tbp->name, time_str,
			osmo_ubit_dump(type3dp, tbp->type2_bits*4));
		viterbi_dec_sb1_wrapper(type3dp, type2, tbp->type2_bits);
		DEBUGP("%s %s type2: %s\n", tbp->name, time_str,
			osmo_ubit_dump(type2, tbp->type2_bits));
	}

	if (tbp->have_crc16) {
		uint16_t crc = crc16_ccitt_bits(type2, tbp->type1_bits+16);
		// printf("CRC COMP: 0x%04x ", crc);
		if (crc == TETRA_CRC_OK) {
			// printf("OK\n");
			tup->crc_ok = 1;
			// printf("%s %s type1: %s\n", tbp->name, time_str,
				// osmo_ubit_dump(type2, tbp->type1_bits));
			tms->t_display_st->last_crc_fail = false;
		} else if(type != TPSAP_T_SCH_F) {
			// printf("WRONG\n");
			tms->t_display_st->last_crc_fail = true;
		}
	} else if (type == TPSAP_T_BBK) {
		/* FIXME: RM3014-decode */
		tup->crc_ok = 1;
		tms->t_display_st->last_crc_fail = false;
		memcpy(type2, type4, tbp->type2_bits);
		DEBUGP("%s %s type1: %s\n", tbp->name, time_str,
			osmo_ubit_dump(type2, tbp->type1_bits));
	}

	/* Set whether BLK1 or BLK2 in downlink burst (or 0 if not applicable) */
	tup->blk_num = blk_num;

	msg->l1h = msgb_put(msg, tbp->type1_bits);
	memcpy(msg->l1h, type2, tbp->type1_bits);

	switch (type) {
	case TPSAP_T_SB1:
		// printf("TMB-SAP SYNC CC %s(0x%02x) ", osmo_ubit_dump(type2+4, 6), bits_to_uint(type2+4, 6));
		// printf("TN %s(%u) ", osmo_ubit_dump(type2+10, 2), bits_to_uint(type2+10, 2) + 1);
		// printf("FN %s(%2u) ", osmo_ubit_dump(type2+12, 5), bits_to_uint(type2+12, 5));
		// printf("MN %s(%2u) ", osmo_ubit_dump(type2+17, 6), bits_to_uint(type2+17, 6));
		// printf("MCC %s(%u) ", osmo_ubit_dump(type2+31, 10), bits_to_uint(type2+31, 10));
		// printf("MNC %s(%u)\n", osmo_ubit_dump(type2+41, 14), bits_to_uint(type2+41, 14));
		tms->t_display_st->mcc = bits_to_uint(type2+31, 10);
		tms->t_display_st->mnc = bits_to_uint(type2+41, 14);
		tms->t_display_st->cc = bits_to_uint(type2+4, 6);
		/* obtain information from SYNC PDU */
		if (tup->crc_ok) {
			tcd->colour_code = bits_to_uint(type2+4, 6);
			tcd->time.tn = bits_to_uint(type2+10, 2) + 1;
			tcd->time.fn = bits_to_uint(type2+12, 5);
			tcd->time.mn = bits_to_uint(type2+17, 6);
			tcd->mcc = bits_to_uint(type2+31, 10);
			tcd->mnc = bits_to_uint(type2+41, 14);
			/* compute the scrambling code for the current cell */
			tcd->scramb_init = tetra_scramb_get_init(tcd->mcc, tcd->mnc, tcd->colour_code);
		}
		/* update the PHY layer time */
		memcpy(&t_phy_state.time, &tcd->time, sizeof(t_phy_state.time));
		tup->lchan = TETRA_LC_BSCH;

		/* Update colour code and network info for crypto IV generation */
		tcs->cc = tcd->colour_code;
		if (tcs->mcc != tcd->mcc || tcs->mnc != tcd->mnc)
			update_current_network(tcs, tcd->mcc, tcd->mnc);

		break;
	case TPSAP_T_SB2:
	case TPSAP_T_NDB:
		/* FIXME: do something */
		break;
	case TPSAP_T_BBK:
		tup->lchan = TETRA_LC_AACH;
		break;
	case TPSAP_T_SCH_F:
		tup->lchan = TETRA_LC_SCH_F;
		//Process voice frame
		if (tms->cur_burst.is_traffic) {
			int16_t block[690];
			/* Generate a block */
			memset(block, 0x00, sizeof(int16_t) * 690);
			for (int i = 0; i < 6; i++)
				block[115*i] = 0x6b21 + i;
   
			for (int i = 0; i < 114; i++)
				block[1+i] = type4[i] ? -127 : 127;
   
			for (int i = 0; i < 114; i++)
				block[116+i] = type4[114+i] ? -127 : 127;
   
			for (int i = 0; i < 114; i++)
				block[231+i] = type4[228+i] ? -127 : 127;
   
			for (int i = 0; i < 90; i++)
				block[346+i] = type4[342+i] ? -127 : 127;
			
			//i'm not sure this is legal, but i don't want to make temp files and execute external programs so
			
			//decoding the speech using the etsi codec
			
			int16_t interleaved_coded_array[432]; /*time-slot length at 7.2 kb/s*/
			int16_t  Coded_array[432];
			int16_t  Reordered_array[286];   /* 2 frames vocoder + 8 + 4 */
			//block1
			for(int i = 0; i < 114; i++) {
				interleaved_coded_array[0+i] = block[1+i];
			}
			//block2
			for(int i = 0; i < 114; i++) {
				interleaved_coded_array[114+i] = block[(161-45)+i];
			}
			//block3
			for(int i = 0; i < 114; i++) {
				interleaved_coded_array[(114*2)+i] = block[(321-45-45)+i];
			}
			//block4
			for(int i = 0; i < 90; i++) {
				interleaved_coded_array[(114*3)+i] = block[(481-45-45-45)+i];
			}
			
			for(int i = 0; i < 432; i++) {
				if((interleaved_coded_array[i] & 0x0080) == 0x0080) {
					interleaved_coded_array[i] = interleaved_coded_array[i] | 0xFF00;
				}
			}
			Desinterleaving_Speech(interleaved_coded_array, Coded_array);
			bool corrupted = Channel_Decoding(tms->codec_first_pass, 0, Coded_array, Reordered_array);
			tms->codec_first_pass = false;
			int16_t cdecoder_output[276];
			cdecoder_output[0] = corrupted;
			for(int i = 0; i < 137; i++) {
				cdecoder_output[1+i] = Reordered_array[i];
			}
			cdecoder_output[138] = corrupted;
			for(int i = 0; i < 137; i++) {
				cdecoder_output[139+i] = Reordered_array[137+i];
			}
			
			int16_t parm[24];
			int16_t synth[480];
			int16_t* synth_p2 = &(synth[240]);
			int16_t serial[138];
			for(int i = 0; i < 138; i++) {
				serial[i] = cdecoder_output[i];
			}
			Bits2prm_Tetra(serial, parm);	/* serial to parameters */
			Decod_Tetra(parm, synth);		/* decoder */
			Post_Process(synth, (int16_t)240);	/* Post processing of synthesis  */
			for(int i = 0; i < 138; i++) {
				serial[i] = cdecoder_output[i+138];
			}
			Bits2prm_Tetra(serial, parm);	/* serial to parameters */
			Decod_Tetra(parm, synth_p2);		/* decoder */
			Post_Process(synth_p2, (int16_t)240);	/* Post processing of synthesis  */
			//USE SYNTH
			if(tms->t_display_st->curr_frame != tms->last_frame) {
				tms->curr_active_timeslot = t_phy_state.time.tn;
				tms->last_frame = tms->t_display_st->curr_frame;
			}
			if(tms->curr_active_timeslot == t_phy_state.time.tn) {
				tms->put_voice_data(tms->put_voice_data_ctx, 480, synth);
			}
		}
		break;
	default:
		/* FIXME: do something */
		break;
	}

	int pdu_bits = 0;
	uint32_t offset = 0;
	uint8_t *orig_head = msg->head; /* The true start of the timeslot */
	uint8_t *orig_tail = msg->tail; /* The true end of the timeslot */
	while (offset < (uint32_t) (tbp->type1_bits - 16)) {
		/* send Rx time along with the TMV-UNITDATA.ind primitive */
		memcpy(&tup->tdma_time, &tcd->time, sizeof(tup->tdma_time));

		/* Parse MAC element in timeslot (just one, possibly more later in the loop) */
		pdu_bits = upper_mac_prim_recv(&ttp->oph, tms);

		/* Check if we are done (-1 returned) */
		if (pdu_bits < 0)
			break;

		/* Not done */
		/* Increment head and l1h ptrs */
		/* Reset tail to end of msg (may be altered by removing FCS) */
		offset += pdu_bits;
		msg->head = orig_head + offset;		/* New head is old head plus parsed len from prev msg */
		msg->tail = orig_tail;			/* Restore original tail */
		msg->len = (uint16_t)(msg->tail - msg->head);	/* Fixup len */
		msg->l1h = msg->head;
		msg->l2h = 0;
		msg->l3h = 0;
		msg->l4h = 0;
	}

// out:
	// talloc_free(msg);
	free(msg);
	// talloc_free(ttp);
	free(ttp);
}
