#ifndef TETRA_BURST_H
#define TETRA_BURST_H

#include <stdint.h>

#define BLK_1 1
#define BLK_2 2

enum tp_sap_data_type {
	TPSAP_T_SB1,
	TPSAP_T_SB2,
	TPSAP_T_NDB,
	TPSAP_T_BBK,
	TPSAP_T_SCH_HU,
	TPSAP_T_SCH_F,
};

extern void tp_sap_udata_ind(enum tp_sap_data_type type, int blk_num, const uint8_t *bits, unsigned int len, void *priv);

/* 9.4.4.2.6 Synchronization continuous downlink burst */
int build_sync_c_d_burst(uint8_t *buf, const uint8_t *sb, const uint8_t *bb, const uint8_t *bkn);

/* 9.4.4.2.5 Normal continuous downlink burst */
int build_norm_c_d_burst(uint8_t *buf, const uint8_t *bkn1, const uint8_t *bb, const uint8_t *bkn2, int two_log_chan);

enum tetra_train_seq {
	TETRA_TRAIN_NORM_1,
	TETRA_TRAIN_NORM_2,
	TETRA_TRAIN_NORM_3,
	TETRA_TRAIN_SYNC,
	TETRA_TRAIN_EXT,
};

/* find a TETRA training sequence in the burst buffer indicated */
int tetra_find_train_seq(const uint8_t *in, unsigned int end_of_in,
			 uint32_t mask_of_train_seq, unsigned int *offset);

#endif /* TETRA_BURST_H */
