#pragma once
#include <stdint.h>

typedef int8_t  sbit_t;
typedef uint8_t ubit_t;

/*! possibe termination types
 *
 *  The termination type will determine which state the encoder/decoder
 *  can start/end with. This is mostly taken care of in the high level API
 *  call. So if you use the low level API, you must take care of making the
 *  proper calls yourself.
 */
enum osmo_conv_term {
	CONV_TERM_FLUSH = 0,	/*!< Flush encoder state */
	CONV_TERM_TRUNCATION,	/*!< Direct truncation */
	CONV_TERM_TAIL_BITING,	/*!< Tail biting */
};

/*! structure describing a given convolutional code
 *
 *  The only required fields are N,K and the next_output/next_state arrays. The
 *  other can be left to default value of zero depending on what the code does.
 *  If 'len' is left at 0 then only the low level API can be used.
 */
struct osmo_conv_code {
	int N;				/*!< Inverse of code rate */
	int K;				/*!< Constraint length */
	int len;			/*!< # of data bits */

	enum osmo_conv_term term;	/*!< Termination type */

	const uint8_t (*next_output)[2];/*!< Next output array */
	const uint8_t (*next_state)[2];	/*!< Next state array  */

	const uint8_t *next_term_output;/*!< Flush termination output */
	const uint8_t *next_term_state;	/*!< Flush termination state  */

	const int *puncture;		/*!< Punctured bits indexes */
};

/*! convolutional decoder state */
struct osmo_conv_decoder {
	const struct osmo_conv_code *code; /*!< for which code? */

	int n_states;		/*!< number of states */

	int len;		/*!< Max o_idx (excl. termination) */

	int o_idx;		/*!< output index */
	int p_idx;		/*!< puncture index */

	unsigned int *ae;	/*!< accumulated error */
	unsigned int *ae_next;	/*!< next accumulated error (tmp in scan) */
	uint8_t *state_history;	/*!< state history [len][n_states] */
};


void osmo_conv_decode_init(struct osmo_conv_decoder *decoder,
                           const struct osmo_conv_code *code,
                           int len, int start_state);
void osmo_conv_decode_reset(struct osmo_conv_decoder *decoder, int start_state);
void osmo_conv_decode_rewind(struct osmo_conv_decoder *decoder);
void osmo_conv_decode_deinit(struct osmo_conv_decoder *decoder);

int osmo_conv_decode_scan(struct osmo_conv_decoder *decoder,
                          const sbit_t *input, int n);
int osmo_conv_decode_flush(struct osmo_conv_decoder *decoder,
                           const sbit_t *input);
int osmo_conv_decode_get_best_end_state(struct osmo_conv_decoder *decoder);
int osmo_conv_decode_get_output(struct osmo_conv_decoder *decoder,
                                ubit_t *output, int has_flush, int end_state);


	/* All-in-one */
int osmo_conv_decode(const struct osmo_conv_code *code,
                     const sbit_t *input, ubit_t *output);
