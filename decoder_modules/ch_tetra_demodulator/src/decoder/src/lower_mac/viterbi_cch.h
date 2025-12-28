#ifndef VITERBI_CCH_H
#define VITERBI_CCH_H

int conv_cch_encode(uint8_t *input, uint8_t *output, int n);
int conv_cch_decode(int8_t *input, uint8_t *output, int n);

#endif /* VITERBI_CCH_H */
