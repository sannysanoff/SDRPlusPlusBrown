#include <stdint.h>
#include <stdio.h>
// #include <unistd.h>
#include <string.h>

// #include <osmocom/core/msgb.h>
// #include <osmocom/core/talloc.h>
// #include <osmocom/core/bits.h>

#include "tetra_mle_pdu.h"
#include "tetra_mle.h"
#include "tetra_mm_pdu.h"
#include "tetra_cmce_pdu.h"
#include "tetra_sndcp_pdu.h"
#include "tetra_mle_pdu.h"

//TODO: stole D-* parser from sq5bpf

/* Receive TL-SDU (LLC SDU == MLE PDU) */
int rx_tl_sdu(struct tetra_mac_state *tms, struct msgb *msg, unsigned int len)
{
	uint8_t *bits = msg->l3h;
	uint8_t mle_pdisc = bits_to_uint(bits, 3);

	// printf("TL-SDU(%s): %s ", tetra_get_mle_pdisc_name(mle_pdisc),
		// osmo_ubit_dump(bits, len));
	switch (mle_pdisc) {
	case TMLE_PDISC_MM:
		// printf("%s\n", tetra_get_mm_pdut_name(bits_to_uint(bits+3, 4), 0));
		break;
	case TMLE_PDISC_CMCE:
		// printf("%s\n", tetra_get_cmce_pdut_name(bits_to_uint(bits+3, 5), 0));
		break;
	case TMLE_PDISC_SNDCP:
		// printf("%s ", tetra_get_sndcp_pdut_name(bits_to_uint(bits+3, 4), 0));
		// printf(" NSAPI=%u PCOMP=%u, DCOMP=%u",
			// bits_to_uint(bits+3+4, 4),
			// bits_to_uint(bits+3+4+4, 4),
			// bits_to_uint(bits+3+4+4+4, 4));
		// printf(" V%u, IHL=%u",
			// bits_to_uint(bits+3+4+4+4+4, 4),
			// 4*bits_to_uint(bits+3+4+4+4+4+4, 4));
		// printf(" Proto=%u\n",
			// bits_to_uint(bits+3+4+4+4+4+4+4+64, 8));
		break;
	case TMLE_PDISC_MLE:
		// printf("%s\n", tetra_get_mle_pdut_name(bits_to_uint(bits+3, 3), 0));
		break;
	default:
		break;
	}
	return len;
}
