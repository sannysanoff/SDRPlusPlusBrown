#ifndef TETRA_MLE_PDU_H
#define TETRA_MLE_PDU_H

#include <stdint.h>

/* 18.5.20 */
enum tetra_mle_pdu_type_d {
	TMLE_PDUT_D_NEW_CELL		= 0,
	TMLE_PDUT_D_PREPARE_FAIL	= 1,
	TMLE_PDUT_D_NWRK_BROADCAST	= 2,
	TMLE_PDUT_D_NWRK_BROADCAST_EXT	= 3,
	TMLE_PDUT_D_RESTORE_ACK		= 4,
	TMLE_PDUT_D_RESTORE_FAIL	= 5,
	TMLE_PDUT_D_CHANNEL_RESPONSE	= 6
};
enum tetra_mle_pdu_type_u {
	TMLE_PDUT_U_PREPARE		= 0,
	TMLE_PDUT_U_SECTOR_ADVICE	= 2,
	TMLE_PDUT_U_CHANNEL_ADVICE	= 3,
	TMLE_PDUT_U_RESTORE		= 4,
	TMLE_PDUT_U_CHANNEL_REQUEST	= 6,
};
const char *tetra_get_mle_pdut_name(unsigned int pdut, int uplink);

/* 18.5.21 */
enum tetra_mle_pdisc {
	TMLE_PDISC_MM		= 1,
	TMLE_PDISC_CMCE		= 2,
	TMLE_PDISC_SNDCP	= 4,
	TMLE_PDISC_MLE		= 5,
	TMLE_PDISC_MGMT		= 6,
	TMLE_PDISC_TEST		= 7,
};
const char *tetra_get_mle_pdisc_name(uint8_t pdisc);

#endif
