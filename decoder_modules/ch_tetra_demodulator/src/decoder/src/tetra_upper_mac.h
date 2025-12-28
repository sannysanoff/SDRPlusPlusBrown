#ifndef TETRA_UPPER_MAC_H
#define TETRA_UPPER_MAC_H

#include "tetra_prim.h"

#include "tetra_fragslot.h"

void upper_mac_init_fragslots();
int upper_mac_prim_recv(struct osmo_prim_hdr *op, void *priv);

#endif
