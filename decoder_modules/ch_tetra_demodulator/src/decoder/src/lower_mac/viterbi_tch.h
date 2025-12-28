#ifndef VITERBI_TCH_H
#define VITERBI_TCH_H

int conv_tch_encode(uint8_t *input, uint8_t *output, int n);
int conv_tch_decode(int8_t *input, uint8_t *output, int n);

#endif /* VITERBI_TCH_H */
