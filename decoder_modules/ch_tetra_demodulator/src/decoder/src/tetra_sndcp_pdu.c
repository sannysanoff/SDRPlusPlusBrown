/* Implementation of TETRA SNDCP PDU parsing */

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
#include "tetra_common.h"

#include "tetra_sndcp_pdu.h"

static const struct value_string sndcp_pdut_names[] = {
	{ SNDCP_PDU_T_ACT_PDP_ACCEPT,	"SN-ACTIVATE PDP ACCEPT" },
	{ SNDCP_PDU_T_DEACT_PDP_ACC,	"SN-DEACTIVATE PDP ACCEPT" },
	{ SNDCP_PDU_T_DEACT_PDP_DEMAND, "SN-DEACTIVATE PDP DEMAND" },
	{ SNDCP_PDU_T_ACT_PDP_REJECT,	"SN-ACTIVATE PDP REJECT" },
	{ SNDCP_PDU_T_UNITDATA,		"SN-UNITDATA" },
	{ SNDCP_PDU_T_DATA,		"SN-DATA" },
	{ SNDCP_PDU_T_DATA_TX_REQ,	"SN-DATA TX REQUEST" },
	{ SNDCP_PDU_T_DATA_TX_RESP,	"SN-DATA TX RESPONSE" },
	{ SNDCP_PDU_T_END_OF_DATA,	"SN-END OF DATA" },
	{ SNDCP_PDU_T_RECONNECT,	"SN-RECONNECT" },
	{ SNDCP_PDU_T_PAGE_REQUEST,	"SN-PAGE REQUEST" },
	{ SNDCP_PDU_T_NOT_SUPPORTED,	"SN-NOT SUPPORTED" },
	{ SNDCP_PDU_T_DATA_PRIORITY,	"SN-DATA PRIORITY" },
	{ SNDCP_PDU_T_MODIFY,		"SN-MODIFY" },
	{ 0, NULL }
};

const char *tetra_get_sndcp_pdut_name(uint8_t pdut, int uplink)
{
	/* FIXME: uplink */
	return get_value_string(sndcp_pdut_names, pdut);
}
