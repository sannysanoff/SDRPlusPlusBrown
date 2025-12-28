/* Implementation of TETRA CMCE PDU parsing */

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

// #include <unistd.h>
// #include <osmocom/core/utils.h>

#include "tetra_cmce_pdu.h"

static const struct value_string cmce_pdut_d_names[] = {
	{ TCMCE_PDU_T_D_ALERT,			"D-ALERT" },
	{ TCMCE_PDU_T_D_CALL_PROCEEDING,	"D-CALL PROCEEDING" },
	{ TCMCE_PDU_T_D_CONNECT,		"D-CONNECT" },
	{ TCMCE_PDU_T_D_CONNECT_ACK,		"D-CONNECT ACK" },
	{ TCMCE_PDU_T_D_DISCONNECT,		"D-DISCONNECT" },
	{ TCMCE_PDU_T_D_INFO,			"D-INFO" },
	{ TCMCE_PDU_T_D_RELEASE,		"D-RELEASE" },
	{ TCMCE_PDU_T_D_SETUP,			"D-SETUP" },
	{ TCMCE_PDU_T_D_STATUS,			"D-STATUS" },
	{ TCMCE_PDU_T_D_TX_CEASED,		"D-TX CEASED" },
	{ TCMCE_PDU_T_D_TX_CONTINUE,		"D-TX CONTINUE" },
	{ TCMCE_PDU_T_D_TX_GRANTED,		"D-TX GRANTED" },
	{ TCMCE_PDU_T_D_TX_WAIT,		"D-TX WAIT" },
	{ TCMCE_PDU_T_D_TX_INTERRUPT,		"D-TX INTERRUPT" },
	{ TCMCE_PDU_T_D_CALL_RESTORE,		"D-TX CALL RESTORE" },
	{ TCMCE_PDU_T_D_SDS_DATA,		"D-SDS DATA" },
	{ TCMCE_PDU_T_D_FACILITY,		"D-FACILITY" },
	{ 0, NULL }
};

static const struct value_string cmce_pdut_u_names[] = {
	{ TCMCE_PDU_T_U_ALERT,		"U-ALERT" },
	{ TCMCE_PDU_T_U_CONNECT,	"U-CONNECT" },
	{ TCMCE_PDU_T_U_DISCONNECT,	"U-DISCONNECT" },
	{ TCMCE_PDU_T_U_INFO,		"U-INFO" },
	{ TCMCE_PDU_T_U_RELEASE,	"U-RELEASE" },
	{ TCMCE_PDU_T_U_SETUP,		"U-SETUP" },
	{ TCMCE_PDU_T_U_STATUS,		"U-STATUS" },
	{ TCMCE_PDU_T_U_TX_CEASED,	"U-TX CEASED" },
	{ TCMCE_PDU_T_U_TX_DEMAND,	"U-TX DEMAND" },
	{ TCMCE_PDU_T_U_CALL_RESTORE,	"U-TX CALL RESTORE" },
	{ TCMCE_PDU_T_U_SDS_DATA,	"U-SDS DATA" },
	{ TCMCE_PDU_T_U_FACILITY,	"U-FACILITY" },
	{ 0, NULL }
};

const char *tetra_get_cmce_pdut_name(uint16_t pdut, int uplink)
{
	if (uplink == 0)
		return get_value_string(cmce_pdut_d_names, pdut);
	else
		return get_value_string(cmce_pdut_u_names, pdut);
}
