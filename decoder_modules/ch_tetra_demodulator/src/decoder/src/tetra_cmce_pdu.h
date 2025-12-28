#ifndef TETRA_CMCE_PDU_H
#define TETRA_CMCE_PDU_H

#include "tetra_common.h"
#include <stdint.h>

/* 14.8.28 */
enum tetra_cmce_pdu_type_d {
	TCMCE_PDU_T_D_ALERT		= 0x00,
	TCMCE_PDU_T_D_CALL_PROCEEDING	= 0x01,
	TCMCE_PDU_T_D_CONNECT		= 0x02,
	TCMCE_PDU_T_D_CONNECT_ACK	= 0x03,
	TCMCE_PDU_T_D_DISCONNECT	= 0x04,
	TCMCE_PDU_T_D_INFO		= 0x05,
	TCMCE_PDU_T_D_RELEASE		= 0x06,
	TCMCE_PDU_T_D_SETUP		= 0x07,
	TCMCE_PDU_T_D_STATUS		= 0x08,
	TCMCE_PDU_T_D_TX_CEASED		= 0x09,
	TCMCE_PDU_T_D_TX_CONTINUE	= 0x0a,
	TCMCE_PDU_T_D_TX_GRANTED	= 0x0b,
	TCMCE_PDU_T_D_TX_WAIT		= 0x0c,
	TCMCE_PDU_T_D_TX_INTERRUPT	= 0x0d,
	TCMCE_PDU_T_D_CALL_RESTORE	= 0x0e,
	TCMCE_PDU_T_D_SDS_DATA		= 0x0f,
	TCMCE_PDU_T_D_FACILITY		= 0x10,
};

enum tetra_cmce_pdu_type_u {
	TCMCE_PDU_T_U_ALERT		= 0x00,
	/* reserved */
	TCMCE_PDU_T_U_CONNECT		= 0x02,
	/* reserved */
	TCMCE_PDU_T_U_DISCONNECT	= 0x04,
	TCMCE_PDU_T_U_INFO		= 0x05,
	TCMCE_PDU_T_U_RELEASE		= 0x06,
	TCMCE_PDU_T_U_SETUP		= 0x07,
	TCMCE_PDU_T_U_STATUS		= 0x08,
	TCMCE_PDU_T_U_TX_CEASED		= 0x09,
	TCMCE_PDU_T_U_TX_DEMAND		= 0x0a,
	/*reserved*/
	TCMCE_PDU_T_U_CALL_RESTORE	= 0x0e,
	TCMCE_PDU_T_U_SDS_DATA		= 0x0f,
	TCMCE_PDU_T_U_FACILITY		= 0x10,
	/*reserved*/
};

const char *tetra_get_cmce_pdut_name(uint16_t pdut, int uplink);

#endif /* TETRA_CMCE_PDU_H */
