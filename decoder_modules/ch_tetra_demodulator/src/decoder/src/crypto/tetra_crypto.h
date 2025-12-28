/* Cryptography related helper, key management and wrapper functions */
/*
 * Copyright (C) 2023 Midnight Blue B.V.
 *
 * Author: Wouter Bokslag <w.bokslag [ ] midnightblue [ ] nl>
 *
 * SPDX-License-Identifier: AGPL-3.0+
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * See the COPYING file in the main directory for details.
 */


#ifndef TETRA_CRYPTO_H
#define TETRA_CRYPTO_H

#include <inttypes.h>
#include <stdbool.h>

#include "../tetra_prim.h"
#include "../tetra_tdma.h"
#include "../tetra_mac_pdu.h"

#define TCDB_ALLOC_BLOCK_SIZE 16

enum tetra_key_type {
	KEYTYPE_UNDEFINED		= 0,

	/* Keytypes usable as ECK after applying TB5 */
	KEYTYPE_CCK_SCK			= 1, /* SCK in class 2, CCK in class 3 networks */
	KEYTYPE_DCK			= 2, /* FIXME: add support for DCK */
	KEYTYPE_MGCK			= 4, /* FIXME: add support for MGCK */
	KEYTYPE_GCK			= 8, /* FIXME: add support. Used with SCK/CCK to derive MGCK */
};

enum tetra_ksg_type {
	UNKNOWN				= 0,
	KSG_TEA1			= 1, /* KSG number value 0 */
	KSG_TEA2			= 2, /* KSG number value 1 */
	KSG_TEA3			= 3, /* KSG number value 2 */
	KSG_TEA4			= 4, /* KSG number value 3 */
	KSG_TEA5			= 5, /* KSG number value 4 */
	KSG_TEA6			= 6, /* KSG number value 5 */
	KSG_TEA7			= 7, /* KSG number value 6 */
	KSG_PROPRIETARY			= 8, /* KSG number values 9-15 */
};

enum tetra_security_class {
	NETWORK_CLASS_UNDEFINED		= 0,
	NETWORK_CLASS_1			= 1,
	NETWORK_CLASS_2			= 2,
	NETWORK_CLASS_3			= 3,
};

struct tetra_netinfo {
	uint32_t mcc;
	uint32_t mnc;
	enum tetra_ksg_type ksg_type;			/* KSG used in this network */
	enum tetra_security_class security_class;	/* Security class this network employs */
};

struct tetra_key {
	uint32_t index;				/* position in key list */
	uint32_t mnc;
	uint32_t mcc;
	enum tetra_key_type key_type;
	uint32_t key_num;			/* key_vn or whatever key numbering system is relevant for this key type */
	uint32_t addr;				/* ISSI, GSSI, or whatever address is relevant for this key type */
	uint8_t  key[16];			/* Currently keys are 80 bits, but we may want to support 128-bit keys as well */
	struct tetra_netinfo *network_info;	/* Network with which the key is associated */
};

struct tetra_crypto_database {
	uint32_t num_keys;
	struct tetra_key *keys;
	uint32_t num_nets;
	struct tetra_netinfo *nets;
	int nets_cnt;
};
extern struct tetra_crypto_database *tcdb;

struct tetra_crypto_state {
	uint32_t mnc;			/* Network info for selecting keys */
	uint32_t mcc;			/* Network info for selecting keys */
	uint32_t cck_id;			/* CCK or SCK id used on network (from SYSINFO) */
	int hn;				/* Hyperframe number for IV (from SYSINFO) */
	int la;				/* location area for TB5 */
	int cn;				/* carrier number for TB5. WARNING: only set correctly if tuned to main control channel */
	int cc;				/* colour code for TB5 */
	struct tetra_netinfo *network;	/* pointer to network info struct loaded from file */
	struct tetra_key *cck;		/* pointer to CCK or SCK for this network and version (from SYSINFO) */
};

const char *tetra_get_key_type_name(enum tetra_key_type);
const char *tetra_get_ksg_type_name(enum tetra_ksg_type);
const char *tetra_get_security_class_name(uint8_t pdut);

/* Key loading / unloading */
void tetra_crypto_state_init(struct tetra_crypto_state *tcs);
int load_keystore(char *filename);

/* Keystream generation and decryption functions */
uint32_t tea_build_iv(struct tetra_tdma_time *tm, uint16_t hn, uint8_t dir);
bool decrypt_identity(struct tetra_crypto_state *tcs, struct tetra_addr *addr);
bool decrypt_mac_element(struct tetra_crypto_state *tcs, struct tetra_tmvsap_prim *tmvp, struct tetra_key *key, int l1_len, int tmpdu_offset);
bool decrypt_voice_timeslot(struct tetra_crypto_state *tcs, struct tetra_tdma_time *tdma_time, int16_t *type1_bits);

/* Key selection and crypto state management */
struct tetra_netinfo *get_network_info(uint32_t mcc, uint32_t mnc);
struct tetra_key *get_ksg_key(struct tetra_crypto_state *tcs, int addr);
void update_current_network(struct tetra_crypto_state *tcs, int mcc, int mnc);
void update_current_cck(struct tetra_crypto_state *tcs);

char *dump_network_info(struct tetra_netinfo *network);
char *dump_key(struct tetra_key *k);

#endif /* TETRA_CRYPTO_H */
