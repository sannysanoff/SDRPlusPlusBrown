#include <stdint.h>
#include <string.h>

#include <lower_mac/viterbi_cch.h>

void viterbi_dec_sb1_wrapper(const uint8_t *in, uint8_t *out, unsigned int sym_count)
{
	int8_t vit_inp[864*4] = {0};
	unsigned int i;

	for (i = 0; i < sym_count*4; i++) {
		switch (in[i]) {
		case 0:
			vit_inp[i] = 127;
			break;
		case 0xff:
			vit_inp[i] = 0;
			break;
		default:
			vit_inp[i] = -127;
			break;
		}
	}
	conv_cch_decode(vit_inp, out, sym_count);
}
