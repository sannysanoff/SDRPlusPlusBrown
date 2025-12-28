#ifndef TETRA_MAC_PDU
#define TETRA_MAC_PDU

#define MACPDU_LEN_2ND_STOLEN   -2
#define MACPDU_LEN_START_FRAG   -1

enum tetra_mac_pdu_types {
	TETRA_PDU_T_MAC_RESOURCE = 0,
	TETRA_PDU_T_MAC_FRAG_END = 1,
	TETRA_PDU_T_BROADCAST = 2,
	TETRA_PDU_T_MAC_SUPPL = 3,
};

enum tetra_mac_frage_pdu_types {
	TETRA_MAC_FRAGE_FRAG = 0,
	TETRA_MAC_FRAGE_END = 1,
};

enum tetra_mac_bcast_pdu_types {
	TETRA_MAC_BC_SYSINFO = 0,
	TETRA_MAC_BC_ACCESS_DEFINE = 1,
};

enum tetra_mac_supp_pdu_types {
	TETRA_MAC_SUPP_D_BLCK = 0,
};

enum tetra_bs_serv_details {
	BS_SERVDET_REG_RQD	= (1 << 11),
	BS_SERVDET_DEREG_RQD	= (1 << 10),
	BS_SERVDET_PRIO_CELL	= (1 << 9),
	BS_SERVDET_MIN_MODE	= (1 << 8),
	BS_SERVDET_MIGRATION	= (1 << 7),
	BS_SERVDET_SYS_W_SERV	= (1 << 6),
	BS_SERVDET_VOICE_SERV	= (1 << 5),
	BS_SERVDET_CSD_SERV	= (1 << 4),
	BS_SERVDET_SNDCP_SERV	= (1 << 2),
	BS_SERVDET_AIR_ENCR	= (1 << 1),
	BS_SERVDET_ADV_LINK	= (1 << 0),
};

enum tetra_mac_optional_field_flags {
	TETRA_MAC_OPT_FIELD_EVEN_MULTIFRAME = 0,
	TETRA_MAC_OPT_FIELD_ODD_MULTIFRAME  = 1,
	TETRA_MAC_OPT_FIELD_ACCESS_CODE     = 2,
	TETRA_MAC_OPT_FIELD_EXT_SERVICES    = 3
};

const char *tetra_get_bs_serv_det_name(uint32_t pdu_type);

struct tetra_mle_si_decoded {
	uint16_t la;
	uint16_t subscr_class;
	uint16_t bs_service_details;
};

struct tetra_si_decoded {
	uint16_t main_carrier;
	uint8_t freq_band;
	uint8_t freq_offset;
	uint8_t duplex_spacing;
	uint8_t reverse_operation;
	uint8_t num_of_csch;
	uint8_t ms_txpwr_max_cell;
	uint8_t rxlev_access_min;
	uint8_t access_parameter;
	uint8_t radio_dl_timeout;
	int cck_valid_no_hf;
	union {
		uint16_t cck_id;
		uint16_t hyperframe_number;
	};
	uint8_t option_field;
	union {
		uint32_t frame_bitmap;
		uint32_t access_code;
		uint32_t ext_service;
	};
	struct tetra_mle_si_decoded mle_si;
};

const char *tetra_get_macpdu_name(uint8_t pdu_type);

void macpdu_decode_sysinfo(struct tetra_si_decoded *sid, const uint8_t *si_bits);


/* Section 21.4.7.2 ACCESS-ASSIGN PDU */
enum tetra_acc_ass_hdr {
	TETRA_ACC_ASS_DLCC_ULCO,
	TETRA_ACC_ASS_DLF1_ULCA,
	TETRA_ACC_ASS_DLF1_ULAO,
	TETRA_ACC_ASS_DLF1_ULF1,
};

enum tetra_acc_ass_hdr_f18 {
	TETRA_ACC_ASS_ULCO,
	TETRA_ACC_ASS_ULCA,
	TETRA_ACC_ASS_ULAO,
	TETRA_ACC_ASS_ULCA2,
};

enum tetra_dl_usage {
	TETRA_DL_US_UNALLOC	= 0,
	TETRA_DL_US_ASS_CTRL	= 1,
	TETRA_DL_US_COM_CTRL	= 2,
	TETRA_DL_US_RESERVED	= 3,
	TETRA_DL_US_TRAFFIC,
};

enum tetra_ul_usage {
	TETRA_UL_US_UNALLOC	= 0,
	TETRA_UL_US_TRAFFIC,
};

/* Section 21.5.1 */
enum tetra_access_field_bf_len {
	TETRA_ACC_BFL_RES_SUBS	= 0,
	TETRA_ACC_BFL_CLCH_SUBS	= 1,
	TETRA_ACC_BFL_ONGOING	= 2,
	TETRA_ACC_BFL_1		= 3,
	TETRA_ACC_BFL_2		= 4,
	TETRA_ACC_BFL_3		= 5,
	TETRA_ACC_BFL_4		= 7,
	TETRA_ACC_BFL_5		= 6,
	TETRA_ACC_BFL_6		= 8,
	TETRA_ACC_BFL_8		= 9,
	TETRA_ACC_BFL_10	= 0xa,
	TETRA_ACC_BFL_12	= 0xb,
	TETRA_ACC_BFL_16	= 0xc,
	TETRA_ACC_BFL_20	= 0xd,
	TETRA_ACC_BFL_24	= 0xe,
	TETRA_ACC_BFL_32	= 0xf,
};

struct tetra_access_field {
	uint8_t access_code;
	enum tetra_access_field_bf_len base_frame_len;
};

enum tetra_acc_ass_pres {
	TETRA_ACC_ASS_PRES_ACCESS1	= (1 << 0),
	TETRA_ACC_ASS_PRES_ACCESS2	= (1 << 1),
	TETRA_ACC_ASS_PRES_DL_USAGE	= (1 << 2),
	TETRA_ACC_ASS_PRES_UL_USAGE	= (1 << 3),
};

struct tetra_acc_ass_decoded {
	uint8_t hdr;
	uint32_t pres;	/* which of the fields below are present */

	enum tetra_ul_usage ul_usage;
	enum tetra_dl_usage dl_usage;
	struct tetra_access_field access[2];
};

void macpdu_decode_access_assign(struct tetra_acc_ass_decoded *aad, const uint8_t *bits, int f18);
const char *tetra_get_dl_usage_name(uint8_t num);
const char *tetra_get_ul_usage_name(uint8_t num);

enum tetra_mac_res_addr_type {
	ADDR_TYPE_NULL	= 0,
	ADDR_TYPE_SSI	= 1,
	ADDR_TYPE_EVENT_LABEL	= 2,
	ADDR_TYPE_USSI		= 3,
	ADDR_TYPE_SMI		= 4,
	ADDR_TYPE_SSI_EVENT	= 5,
	ADDR_TYPE_SSI_USAGE	= 6,
	ADDR_TYPE_SMI_EVENT	= 7,
};
const char *tetra_get_addr_t_name(uint8_t addrt);

enum tetra_mac_alloc_type {
	TMAC_ALLOC_T_REPLACE	= 0,
	TMAC_ALLOC_T_ADDITIONAL	= 1,
	TMAC_ALLOC_T_QUIT_GO	= 2,
	TMAC_ALLOC_T_REPL_SLOT1	= 3,
};
const char *tetra_get_alloc_t_name(uint8_t alloct);

struct tetra_chan_alloc_decoded {
	uint8_t type;
	uint8_t timeslot;
	uint8_t ul_dl;
	uint8_t clch_perm;
	uint8_t cell_chg_f;
	uint16_t carrier_nr;
	uint8_t ext_carr_pres;
	struct {
		uint8_t freq_band;
		uint8_t freq_offset;
		uint8_t duplex_spc;
		uint8_t reverse_oper;
	} ext_carr;
	uint8_t monit_pattern;
	uint8_t monit_patt_f18;
	struct {
		uint8_t ul_dl_ass;
		uint8_t bandwidth;
		uint8_t modulation;
		uint8_t max_ul_qam;
		uint8_t conf_chan_stat;
		uint8_t bs_imbalance;
		uint8_t bs_tx_rel;
		uint8_t napping_sts;
	} aug;
};

struct tetra_addr {
	uint8_t type;
	uint16_t mcc;
	uint16_t mnc;
	uint32_t ssi;

	uint16_t event_label;
	uint8_t usage_marker;
};

struct tetra_resrc_decoded {
	uint8_t fill_bits;
	uint8_t grant_position;
	uint8_t encryption_mode;
	uint8_t is_encrypted;    // Set to 0 if not encrypted or decrypted successfully
	uint8_t rand_acc_flag;
	int macpdu_length;
	struct tetra_addr addr;

	uint8_t power_control_pres;

	struct {
		uint8_t nr_slots;
		uint8_t delay;
		uint8_t pres;
	} slot_granting;

	uint8_t chan_alloc_pres;
	struct tetra_chan_alloc_decoded cad;
};
int macpdu_decode_resource(struct tetra_resrc_decoded *rsd, const uint8_t *bits, uint8_t is_decrypted);

int macpdu_decode_chan_alloc(struct tetra_chan_alloc_decoded *cad, const uint8_t *bits);

const char *tetra_addr_dump(const struct tetra_addr *addr);

const char *tetra_get_ul_dl_name(uint8_t ul_dl);

#endif
