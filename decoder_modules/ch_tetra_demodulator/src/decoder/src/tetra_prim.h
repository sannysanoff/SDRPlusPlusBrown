#ifndef TETRA_PRIM_H
#define TETRA_PRIM_H

#include <stdint.h>

// #include <osmocom/core/prim.h>
















#include "tetra_common.h"

enum tetra_saps {
	TETRA_SAP_TP,	/* between PHY and lower MAC */
	TETRA_SAP_TMV,	/* between lower and upper MAC */
	TETRA_SAP_TMA,
	TETRA_SAP_TMB,
	TETRA_SAP_TMD,
};

/* Table 23.1 */
enum tmv_sap_prim {
	PRIM_TMV_UNITDATA,
	PRIM_TMV_CONFIGURE,
};

/* Table 23.2 */
struct tmv_unitdata_param {
	uint32_t mac_block_len;		/* length of mac block */
	enum tetra_log_chan lchan;	/* to which lchan do we belong? */
	int crc_ok;			/* was the CRC verified OK? */
	uint32_t scrambling_code;	/* which scrambling code was used */
	struct tetra_tdma_time tdma_time;/* TDMA timestamp  */
	int blk_num;				/* Indicates whether BLK1 or BLK2 in the downlink burst */
	//uint8_t mac_block[412];		/* maximum num of bits in a non-QAM chan */
};

/* Table 23.3 */
struct tmv_configure_param {
	/* FIXME */
	uint32_t scrambling_rx;
};

struct tetra_tmvsap_prim {
	struct osmo_prim_hdr oph;
	// char* msg;
	union {
		struct tmv_unitdata_param unitdata;
		struct tmv_configure_param configure;
	} u;
};

#endif
