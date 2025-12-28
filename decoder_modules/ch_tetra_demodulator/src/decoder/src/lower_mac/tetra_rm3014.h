#ifndef TETRA_RM3014_H
#define TETRA_RM3014_H

#include <stdint.h>

void tetra_rm3014_init(void);
uint32_t tetra_rm3014_compute(const uint16_t in);

/**
 * Decode @param inp to @param out and return if there was
 * an error in the input. In the future this should correct
 * the error or such.
 */
int tetra_rm3013_decode(const uint32_t inp, uint16_t *out);

#endif
