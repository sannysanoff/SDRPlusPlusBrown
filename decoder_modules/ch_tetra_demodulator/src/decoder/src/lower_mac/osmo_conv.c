#include "osmo_conv.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>


/* ------------------------------------------------------------------------ */
/* Decoding (viterbi)                                                       */
/* ------------------------------------------------------------------------ */

#define MAX_AE 0x00ffffff

/* Forward declaration for accerlated decoding with certain codes */
// int
// osmo_conv_decode_acc(const struct osmo_conv_code *code,
// 		     const sbit_t *input, ubit_t *output);

#define BIT2NRZ(REG,N)	(((REG >> N) & 0x01) * 2 - 1) * -1
#define NUM_STATES(K)	(K == 7 ? 64 : 16)

#define INIT_POINTERS(simd) \
{ \
	osmo_conv_metrics_k5_n2 = osmo_conv_##simd##_metrics_k5_n2; \
	osmo_conv_metrics_k5_n3 = osmo_conv_##simd##_metrics_k5_n3; \
	osmo_conv_metrics_k5_n4 = osmo_conv_##simd##_metrics_k5_n4; \
	osmo_conv_metrics_k7_n2 = osmo_conv_##simd##_metrics_k7_n2; \
	osmo_conv_metrics_k7_n3 = osmo_conv_##simd##_metrics_k7_n3; \
	osmo_conv_metrics_k7_n4 = osmo_conv_##simd##_metrics_k7_n4; \
	vdec_malloc = &osmo_conv_##simd##_vdec_malloc; \
	vdec_free = &osmo_conv_##simd##_vdec_free; \
}

static int init_complete = 0;

/**
 * These pointers are being initialized at runtime by the
 * osmo_conv_init() depending on supported SIMD extensions.
 */
static int16_t *(*vdec_malloc)(size_t n);
static void (*vdec_free)(int16_t *ptr);

void (*osmo_conv_metrics_k5_n2)(const int8_t *seq,
	const int16_t *out, int16_t *sums, int16_t *paths, int norm);
void (*osmo_conv_metrics_k5_n3)(const int8_t *seq,
	const int16_t *out, int16_t *sums, int16_t *paths, int norm);
void (*osmo_conv_metrics_k5_n4)(const int8_t *seq,
	const int16_t *out, int16_t *sums, int16_t *paths, int norm);
void (*osmo_conv_metrics_k7_n2)(const int8_t *seq,
	const int16_t *out, int16_t *sums, int16_t *paths, int norm);
void (*osmo_conv_metrics_k7_n3)(const int8_t *seq,
	const int16_t *out, int16_t *sums, int16_t *paths, int norm);
void (*osmo_conv_metrics_k7_n4)(const int8_t *seq,
	const int16_t *out, int16_t *sums, int16_t *paths, int norm);



/* Add-Compare-Select (ACS-Butterfly)
 * Compute 4 accumulated path metrics and 4 path selections. Note that path
 * selections are store as -1 and 0 rather than 0 and 1. This is to match
 * the output format of the SSE packed compare instruction 'pmaxuw'.
 */

static void acs_butterfly(int state, int num_states,
	int16_t metric, int16_t *sum,
	int16_t *new_sum, int16_t *path)
{
	int state0, state1;
	int sum0, sum1, sum2, sum3;

	state0 = *(sum + (2 * state + 0));
	state1 = *(sum + (2 * state + 1));

	sum0 = state0 + metric;
	sum1 = state1 - metric;
	sum2 = state0 - metric;
	sum3 = state1 + metric;

	if (sum0 >= sum1) {
		*new_sum = sum0;
		*path = -1;
	} else {
		*new_sum = sum1;
		*path = 0;
	}

	if (sum2 >= sum3) {
		*(new_sum + num_states / 2) = sum2;
		*(path + num_states / 2) = -1;
	} else {
		*(new_sum + num_states / 2) = sum3;
		*(path + num_states / 2) = 0;
	}
}

/* Branch metrics unit N=2 */
static void gen_branch_metrics_n2(int num_states, const int8_t *seq,
	const int16_t *out, int16_t *metrics)
{
	int i;

	for (i = 0; i < num_states / 2; i++) {
		metrics[i] = seq[0] * out[2 * i + 0] +
			seq[1] * out[2 * i + 1];
	}
}

/* Branch metrics unit N=3 */
static void gen_branch_metrics_n3(int num_states, const int8_t *seq,
	const int16_t *out, int16_t *metrics)
{
	int i;

	for (i = 0; i < num_states / 2; i++) {
		metrics[i] = seq[0] * out[4 * i + 0] +
			seq[1] * out[4 * i + 1] +
			seq[2] * out[4 * i + 2];
	}
}

/* Branch metrics unit N=4 */
static void gen_branch_metrics_n4(int num_states, const int8_t *seq,
	const int16_t *out, int16_t *metrics)
{
	int i;

	for (i = 0; i < num_states / 2; i++) {
		metrics[i] = seq[0] * out[4 * i + 0] +
			seq[1] * out[4 * i + 1] +
			seq[2] * out[4 * i + 2] +
			seq[3] * out[4 * i + 3];
	}
}

/* Path metric unit */
static void gen_path_metrics(int num_states, int16_t *sums,
	int16_t *metrics, int16_t *paths, int norm)
{
	int i;
	int16_t min;
	// int16_t new_sums[num_states];
	int16_t* new_sums = malloc(2*num_states);

	for (i = 0; i < num_states / 2; i++)
		acs_butterfly(i, num_states, metrics[i],
			sums, &new_sums[i], &paths[i]);

	if (norm) {
		min = new_sums[0];

		for (i = 1; i < num_states; i++)
			if (new_sums[i] < min)
				min = new_sums[i];

		for (i = 0; i < num_states; i++)
			new_sums[i] -= min;
	}

	memcpy(sums, new_sums, num_states * sizeof(int16_t));
	free(new_sums);
}

/* Not-aligned Memory Allocator */
int16_t *osmo_conv_gen_vdec_malloc(size_t n)
{
	return (int16_t *) malloc(sizeof(int16_t) * n);
}
void osmo_conv_gen_vdec_free(int16_t *ptr)
{
	free(ptr);
}

/* 16-state branch-path metrics units (K=5) */
void osmo_conv_gen_metrics_k5_n2(const int8_t *seq, const int16_t *out,
	int16_t *sums, int16_t *paths, int norm)
{
	int16_t metrics[8];

	gen_branch_metrics_n2(16, seq, out, metrics);
	gen_path_metrics(16, sums, metrics, paths, norm);
}
void osmo_conv_gen_metrics_k5_n3(const int8_t *seq, const int16_t *out,
	int16_t *sums, int16_t *paths, int norm)
{
	int16_t metrics[8];

	gen_branch_metrics_n3(16, seq, out, metrics);
	gen_path_metrics(16, sums, metrics, paths, norm);

}

void osmo_conv_gen_metrics_k5_n4(const int8_t *seq, const int16_t *out,
	int16_t *sums, int16_t *paths, int norm)
{
	int16_t metrics[8];

	gen_branch_metrics_n4(16, seq, out, metrics);
	gen_path_metrics(16, sums, metrics, paths, norm);

}

/* 64-state branch-path metrics units (K=7) */
void osmo_conv_gen_metrics_k7_n2(const int8_t *seq, const int16_t *out,
	int16_t *sums, int16_t *paths, int norm)
{
	int16_t metrics[32];

	gen_branch_metrics_n2(64, seq, out, metrics);
	gen_path_metrics(64, sums, metrics, paths, norm);

}

void osmo_conv_gen_metrics_k7_n3(const int8_t *seq, const int16_t *out,
	int16_t *sums, int16_t *paths, int norm)
{
	int16_t metrics[32];

	gen_branch_metrics_n3(64, seq, out, metrics);
	gen_path_metrics(64, sums, metrics, paths, norm);

}

void osmo_conv_gen_metrics_k7_n4(const int8_t *seq, const int16_t *out,
	int16_t *sums, int16_t *paths, int norm)
{
	int16_t metrics[32];

	gen_branch_metrics_n4(64, seq, out, metrics);
	gen_path_metrics(64, sums, metrics, paths, norm);
}

/* Trellis State
 * state - Internal lshift register value
 * prev  - Register values of previous 0 and 1 states
 */
struct vstate {
	unsigned state;
	unsigned prev[2];
};

/* Trellis Object
 * num_states - Number of states in the trellis
 * sums       - Accumulated path metrics
 * outputs    - Trellis output values
 * vals       - Input value that led to each state
 */
struct vtrellis {
	int num_states;
	int16_t *sums;
	int16_t *outputs;
	uint8_t *vals;
};

/* Viterbi Decoder
 * n         - Code order
 * k         - Constraint length
 * len       - Horizontal length of trellis
 * recursive - Set to '1' if the code is recursive
 * intrvl    - Normalization interval
 * trellis   - Trellis object
 * paths     - Trellis paths
 */
struct vdecoder {
	int n;
	int k;
	int len;
	int recursive;
	int intrvl;
	struct vtrellis trellis;
	int16_t **paths;

	void (*metric_func)(const int8_t *, const int16_t *,
		int16_t *, int16_t *, int);
};

/* Accessor calls */
static inline int conv_code_recursive(const struct osmo_conv_code *code)
{
	return code->next_term_output ? 1 : 0;
}

/* Left shift and mask for finding the previous state */
static unsigned vstate_lshift(unsigned reg, int k, int val)
{
	unsigned mask;

	if (k == 5)
		mask = 0x0e;
	else if (k == 7)
		mask = 0x3e;
	else
		mask = 0;

	return ((reg << 1) & mask) | val;
}

/* Bit endian manipulators */
static inline unsigned bitswap2(unsigned v)
{
	return ((v & 0x02) >> 1) | ((v & 0x01) << 1);
}

static inline unsigned bitswap3(unsigned v)
{
	return ((v & 0x04) >> 2) | ((v & 0x02) >> 0) |
		((v & 0x01) << 2);
}

static inline unsigned bitswap4(unsigned v)
{
	return ((v & 0x08) >> 3) | ((v & 0x04) >> 1) |
		((v & 0x02) << 1) | ((v & 0x01) << 3);
}

static inline unsigned bitswap5(unsigned v)
{
	return ((v & 0x10) >> 4) | ((v & 0x08) >> 2) | ((v & 0x04) >> 0) |
		((v & 0x02) << 2) | ((v & 0x01) << 4);
}

static inline unsigned bitswap6(unsigned v)
{
	return ((v & 0x20) >> 5) | ((v & 0x10) >> 3) | ((v & 0x08) >> 1) |
		((v & 0x04) << 1) | ((v & 0x02) << 3) | ((v & 0x01) << 5);
}

static unsigned bitswap(unsigned v, unsigned n)
{
	switch (n) {
	case 1:
		return v;
	case 2:
		return bitswap2(v);
	case 3:
		return bitswap3(v);
	case 4:
		return bitswap4(v);
	case 5:
		return bitswap5(v);
	case 6:
		return bitswap6(v);
	default:
		return 0;
	}
}

/* Generate non-recursive state output from generator state table
 * Note that the shift register moves right (i.e. the most recent bit is
 * shifted into the register at k-1 bit of the register), which is typical
 * textbook representation. The API transition table expects the most recent
 * bit in the low order bit, or left shift. A bitswap operation is required
 * to accommodate the difference.
 */
static unsigned gen_output(struct vstate *state, int val,
	const struct osmo_conv_code *code)
{
	unsigned out, prev;

	prev = bitswap(state->prev[0], code->K - 1);
	out = code->next_output[prev][val];
	out = bitswap(out, code->N);

	return out;
}

/* Populate non-recursive trellis state
 * For a given state defined by the k-1 length shift register, find the
 * value of the input bit that drove the trellis to that state. Also
 * generate the N outputs of the generator polynomial at that state.
 */
static int gen_state_info(uint8_t *val, unsigned reg,
	int16_t *output, const struct osmo_conv_code *code)
{
	int i;
	unsigned out;
	struct vstate state;

	/* Previous '0' state */
	state.state = reg;
	state.prev[0] = vstate_lshift(reg, code->K, 0);
	state.prev[1] = vstate_lshift(reg, code->K, 1);

	*val = (reg >> (code->K - 2)) & 0x01;

	/* Transition output */
	out = gen_output(&state, *val, code);

	/* Unpack to NRZ */
	for (i = 0; i < code->N; i++)
		output[i] = BIT2NRZ(out, i);

	return 0;
}

/* Generate recursive state output from generator state table */
static unsigned gen_recursive_output(struct vstate *state,
	uint8_t *val, unsigned reg,
	const struct osmo_conv_code *code, int pos)
{
	int val0, val1;
	unsigned out, prev;

	/* Previous '0' state */
	prev = vstate_lshift(reg, code->K, 0);
	prev = bitswap(prev, code->K - 1);

	/* Input value */
	val0 = (reg >> (code->K - 2)) & 0x01;
	val1 = (code->next_term_output[prev] >> pos) & 0x01;
	*val = val0 == val1 ? 0 : 1;

	/* Wrapper for osmocom state access */
	prev = bitswap(state->prev[0], code->K - 1);

	/* Compute the transition output */
	out = code->next_output[prev][*val];
	out = bitswap(out, code->N);

	return out;
}

/* Populate recursive trellis state
 * The bit position of the systematic bit is not explicitly marked by the
 * API, so it must be extracted from the generator table. Otherwise,
 * populate the trellis similar to the non-recursive version.
 * Non-systematic recursive codes are not supported.
 */
static int gen_recursive_state_info(uint8_t *val,
	unsigned reg, int16_t *output, const struct osmo_conv_code *code)
{
	int i, j, pos = -1;
	int ns = NUM_STATES(code->K);
	unsigned out;
	struct vstate state;

	/* Previous '0' and '1' states */
	state.state = reg;
	state.prev[0] = vstate_lshift(reg, code->K, 0);
	state.prev[1] = vstate_lshift(reg, code->K, 1);

	/* Find recursive bit location */
	for (i = 0; i < code->N; i++) {
		for (j = 0; j < ns; j++) {
			if ((code->next_output[j][0] >> i) & 0x01)
				break;
		}

		if (j == ns) {
			pos = i;
			break;
		}
	}

	/* Non-systematic recursive code not supported */
	if (pos < 0)
		return -EPROTO;

	/* Transition output */
	out = gen_recursive_output(&state, val, reg, code, pos);

	/* Unpack to NRZ */
	for (i = 0; i < code->N; i++)
		output[i] = BIT2NRZ(out, i);

	return 0;
}

/* Release the trellis */
static void free_trellis(struct vtrellis *trellis)
{
	if (!trellis)
		return;

	vdec_free(trellis->outputs);
	vdec_free(trellis->sums);
	free(trellis->vals);
}

/* Initialize the trellis object
 * Initialization consists of generating the outputs and output value of a
 * given state. Due to trellis symmetry and anti-symmetry, only one of the
 * transition paths is utilized by the butterfly operation in the forward
 * recursion, so only one set of N outputs is required per state variable.
 */
static int generate_trellis(struct vdecoder *dec,
	const struct osmo_conv_code *code)
{
	struct vtrellis *trellis = &dec->trellis;
	int16_t *outputs;
	int i, rc;

	int ns = NUM_STATES(code->K);
	int olen = (code->N == 2) ? 2 : 4;

	trellis->num_states = ns;
	trellis->sums =	vdec_malloc(ns);
	trellis->outputs = vdec_malloc(ns * olen);
	trellis->vals = (uint8_t *) malloc(ns * sizeof(uint8_t));

	if (!trellis->sums || !trellis->outputs || !trellis->vals) {
		rc = -ENOMEM;
		goto fail;
	}

	/* Populate the trellis state objects */
	for (i = 0; i < ns; i++) {
		outputs = &trellis->outputs[olen * i];
		if (dec->recursive) {
			rc = gen_recursive_state_info(&trellis->vals[i],
				i, outputs, code);
		} else {
			rc = gen_state_info(&trellis->vals[i],
				i, outputs, code);
		}

		if (rc < 0)
			goto fail;

		/* Set accumulated path metrics to zero */
		trellis->sums[i] = 0;
	}

	/**
	 * For termination other than tail-biting, initialize the zero state
	 * as the encoder starting state. Initialize with the maximum
	 * accumulated sum at length equal to the constraint length.
	 */
	if (code->term != CONV_TERM_TAIL_BITING)
		trellis->sums[0] = INT8_MAX * code->N * code->K;

	return 0;

fail:
	free_trellis(trellis);
	return rc;
}

static void _traceback(struct vdecoder *dec,
	unsigned state, uint8_t *out, int len)
{
	int i;
	unsigned path;

	for (i = len - 1; i >= 0; i--) {
		path = dec->paths[i][state] + 1;
		out[i] = dec->trellis.vals[state];
		state = vstate_lshift(state, dec->k, path);
	}
}

static void _traceback_rec(struct vdecoder *dec,
	unsigned state, uint8_t *out, int len)
{
	int i;
	unsigned path;

	for (i = len - 1; i >= 0; i--) {
		path = dec->paths[i][state] + 1;
		out[i] = path ^ dec->trellis.vals[state];
		state = vstate_lshift(state, dec->k, path);
	}
}

/* Traceback and generate decoded output
 * Find the largest accumulated path metric at the final state except for
 * the zero terminated case, where we assume the final state is always zero.
 */
static int traceback(struct vdecoder *dec, uint8_t *out, int term, int len)
{
	int i, j, state_scan, sum, max = -1;
	unsigned path, state = 0;

	if (term == CONV_TERM_TAIL_BITING) {
		for (i = 0; i < dec->trellis.num_states; i++) {
			state_scan = i;
			for (j = len - 1; j >= 0; j--) {
				path = dec->paths[j][state_scan] + 1;
				state_scan = vstate_lshift(state_scan, dec->k, path);
			}
			if (state_scan != i)
				continue;
			sum = dec->trellis.sums[i];
			if (sum > max) {
				max = sum;
				state = i;
			}
		}
	}

	if ((max < 0) && (term != CONV_TERM_FLUSH)) {
		for (i = 0; i < dec->trellis.num_states; i++) {
			sum = dec->trellis.sums[i];
			if (sum > max) {
				max = sum;
				state = i;
			}
		}

		if (max < 0)
			return -EPROTO;
	}

	for (i = dec->len - 1; i >= len; i--) {
		path = dec->paths[i][state] + 1;
		state = vstate_lshift(state, dec->k, path);
	}

	if (dec->recursive)
		_traceback_rec(dec, state, out, len);
	else
		_traceback(dec, state, out, len);

	return 0;
}

/* Release decoder object */
static void vdec_deinit(struct vdecoder *dec)
{
	if (!dec)
		return;

	free_trellis(&dec->trellis);

	if (dec->paths != NULL) {
		vdec_free(dec->paths[0]);
		free(dec->paths);
	}
}

/* Initialize decoder object with code specific params
 * Subtract the constraint length K on the normalization interval to
 * accommodate the initialization path metric at state zero.
 */
static int vdec_init(struct vdecoder *dec, const struct osmo_conv_code *code)
{
	int i, ns, rc;

	ns = NUM_STATES(code->K);

	dec->n = code->N;
	dec->k = code->K;
	dec->recursive = conv_code_recursive(code);
	dec->intrvl = INT16_MAX / (dec->n * INT8_MAX) - dec->k;

	if (dec->k == 5) {
		switch (dec->n) {
		case 2:
/* rach len 14 is too short for neon */
			dec->metric_func = osmo_conv_metrics_k5_n2;
			break;
		case 3:
			dec->metric_func = osmo_conv_metrics_k5_n3;
			break;
		case 4:
			dec->metric_func = osmo_conv_metrics_k5_n4;
			break;
		default:
			return -EINVAL;
		}
	} else if (dec->k == 7) {
		switch (dec->n) {
		case 2:
			dec->metric_func = osmo_conv_metrics_k7_n2;
			break;
		case 3:
			dec->metric_func = osmo_conv_metrics_k7_n3;
			break;
		case 4:
			dec->metric_func = osmo_conv_metrics_k7_n4;
			break;
		default:
			return -EINVAL;
		}
	} else {
		return -EINVAL;
	}

	if (code->term == CONV_TERM_FLUSH)
		dec->len = code->len + code->K - 1;
	else
		dec->len = code->len;

	rc = generate_trellis(dec, code);
	if (rc)
		return rc;

	dec->paths = (int16_t **) malloc(sizeof(int16_t *) * dec->len);
	if (!dec->paths)
		goto enomem;

	dec->paths[0] = vdec_malloc(ns * dec->len);
	if (!dec->paths[0])
		goto enomem;

	for (i = 1; i < dec->len; i++)
		dec->paths[i] = &dec->paths[0][i * ns];

	return 0;

enomem:
	vdec_deinit(dec);
	return -ENOMEM;
}

/* Depuncture sequence with nagative value terminated puncturing matrix */
static int depuncture(const int8_t *in, const int *punc, int8_t *out, int len)
{
	int i, n = 0, m = 0;

	for (i = 0; i < len; i++) {
		if (i == punc[n]) {
			out[i] = 0;
			n++;
			continue;
		}

		out[i] = in[m++];
	}

	return 0;
}

/* Forward trellis recursion
 * Generate branch metrics and path metrics with a combined function. Only
 * accumulated path metric sums and path selections are stored. Normalize on
 * the interval specified by the decoder.
 */
static void forward_traverse(struct vdecoder *dec, const int8_t *seq)
{
	int i;

	for (i = 0; i < dec->len; i++) {
		dec->metric_func(&seq[dec->n * i],
			dec->trellis.outputs,
			dec->trellis.sums,
			dec->paths[i],
			!(i % dec->intrvl));
	}
}

/* Convolutional decode with a decoder object
 * Initial puncturing run if necessary followed by the forward recursion.
 * For tail-biting perform a second pass before running the backward
 * traceback operation.
 */
static int conv_decode(struct vdecoder *dec, const int8_t *seq,
	const int *punc, uint8_t *out, int len, int term)
{
	int8_t* depunc = malloc(dec->len * dec->n);
	// int8_t depunc[dec->len * dec->n];

	if (punc) {
		depuncture(seq, punc, depunc, dec->len * dec->n);
		seq = depunc;
	}

	/* Propagate through the trellis with interval normalization */
	forward_traverse(dec, seq);

	if (term == CONV_TERM_TAIL_BITING)
		forward_traverse(dec, seq);

	free(depunc);
	return traceback(dec, out, term, len);
}

static void osmo_conv_init(void)
{
	init_complete = 1;
	INIT_POINTERS(gen);
}


/* All-in-one Viterbi decoding  */
int osmo_conv_decode_acc(const struct osmo_conv_code *code,
	const sbit_t *input, ubit_t *output)
{
	int rc;
	struct vdecoder dec;

	if (!init_complete)
		osmo_conv_init();

	if ((code->N < 2) || (code->N > 4) || (code->len < 1) ||
		((code->K != 5) && (code->K != 7)))
		return -EINVAL;

	rc = vdec_init(&dec, code);
	if (rc)
		return rc;

	rc = conv_decode(&dec, input, code->puncture,
		output, code->len, code->term);

	vdec_deinit(&dec);

	return rc;
}


void
osmo_conv_decode_init(struct osmo_conv_decoder *decoder,
		      const struct osmo_conv_code *code, int len,
		      int start_state)
{
	int n_states;

	/* Init */
	if (len <= 0)
		len = code->len;

	n_states = 1 << (code->K - 1);

	memset(decoder, 0x00, sizeof(struct osmo_conv_decoder));

	decoder->code = code;
	decoder->n_states = n_states;
	decoder->len = len;

	/* Allocate arrays */
	decoder->ae = malloc(sizeof(unsigned int) * n_states);
	decoder->ae_next = malloc(sizeof(unsigned int) * n_states);

	decoder->state_history = malloc(sizeof(uint8_t) * n_states * (len + decoder->code->K - 1));

	/* Classic reset */
	osmo_conv_decode_reset(decoder, start_state);
}

void
osmo_conv_decode_reset(struct osmo_conv_decoder *decoder, int start_state)
{
	int i;

	/* Reset indexes */
	decoder->o_idx = 0;
	decoder->p_idx = 0;

	/* Initial error */
	if (start_state < 0) {
		/* All states possible */
		memset(decoder->ae, 0x00, sizeof(unsigned int) * decoder->n_states);
	} else {
		/* Fixed start state */
		for (i = 0; i < decoder->n_states; i++) {
			decoder->ae[i] = (i == start_state) ? 0 : MAX_AE;
		}
	}
}

void
osmo_conv_decode_rewind(struct osmo_conv_decoder *decoder)
{
	int i;
	unsigned int min_ae = MAX_AE;

	/* Reset indexes */
	decoder->o_idx = 0;
	decoder->p_idx = 0;

	/* Initial error normalize (remove constant) */
	for (i = 0; i < decoder->n_states; i++) {
		if (decoder->ae[i] < min_ae)
			min_ae = decoder->ae[i];
	}

	for (i = 0; i < decoder->n_states; i++)
		decoder->ae[i] -= min_ae;
}

void
osmo_conv_decode_deinit(struct osmo_conv_decoder *decoder)
{
	free(decoder->ae);
	free(decoder->ae_next);
	free(decoder->state_history);

	memset(decoder, 0x00, sizeof(struct osmo_conv_decoder));
}

int
osmo_conv_decode_scan(struct osmo_conv_decoder *decoder,
		      const sbit_t *input, int n)
{
	const struct osmo_conv_code *code = decoder->code;

	int i, s, b, j;

	int n_states;
	unsigned int *ae;
	unsigned int *ae_next;
	uint8_t *state_history;
	sbit_t *in_sym;

	int i_idx, p_idx;

	/* Prepare */
	n_states = decoder->n_states;

	ae = decoder->ae;
	ae_next = decoder->ae_next;
	state_history = &decoder->state_history[n_states * decoder->o_idx];

	// in_sym = alloca(sizeof(sbit_t) * code->N);
	in_sym = malloc(sizeof(sbit_t) * code->N);

	i_idx = 0;
	p_idx = decoder->p_idx;

	/* Scan the treillis */
	for (i = 0; i < n; i++) {
		/* Reset next accumulated error */
		for (s = 0; s < n_states; s++)
			ae_next[s] = MAX_AE;

		/* Get input */
		if (code->puncture) {
			/* Hard way ... */
			for (j = 0; j < code->N; j++) {
				int idx = ((decoder->o_idx + i) * code->N) + j;
				if (idx == code->puncture[p_idx]) {
					in_sym[j] = 0;	/* Undefined */
					p_idx++;
				} else {
					in_sym[j] = input[i_idx];
					i_idx++;
				}
			}
		} else {
			/* Easy, just copy N bits */
			memcpy(in_sym, &input[i_idx], code->N);
			i_idx += code->N;
		}

		/* Scan all state */
		for (s = 0; s < n_states; s++) {
			/* Scan possible input bits */
			for (b = 0; b < 2; b++) {
				unsigned int nae;
				int ov, e;
				uint8_t m;

				/* Next output and state */
				uint8_t out = code->next_output[s][b];
				uint8_t state = code->next_state[s][b];

				/* New error for this path */
				nae = ae[s];			/* start from last error */
				m = 1 << (code->N - 1);		/* mask for 'out' bit selection */

				for (j = 0; j < code->N; j++) {
					int is = (int)in_sym[j];
					if (is) {
						ov = (out & m) ? -127 : 127; /* sbit_t value for it */
						e = is - ov;                 /* raw error for this bit */
						nae += (e * e) >> 9;         /* acc the squared/scaled value */
					}
					m >>= 1;                     /* next mask bit */
				}

				/* Is it survivor ? */
				if (ae_next[state] > nae) {
					ae_next[state] = nae;
					state_history[(n_states * i) + state] = s;
				}
			}
		}

		/* Copy accumulated error */
		memcpy(ae, ae_next, sizeof(unsigned int) * n_states);
	}

	/* Update decoder state */
	decoder->p_idx = p_idx;
	decoder->o_idx += n;

	free(in_sym);
	return i_idx;
}

int
osmo_conv_decode_flush(struct osmo_conv_decoder *decoder, const sbit_t *input)
{
	const struct osmo_conv_code *code = decoder->code;

	int i, s, j;

	int n_states;
	unsigned int *ae;
	unsigned int *ae_next;
	uint8_t *state_history;
	sbit_t *in_sym;

	int i_idx, p_idx;

	/* Prepare */
	n_states = decoder->n_states;

	ae = decoder->ae;
	ae_next = decoder->ae_next;
	state_history = &decoder->state_history[n_states * decoder->o_idx];

	// in_sym = alloca(sizeof(sbit_t) * code->N);
	in_sym = malloc(sizeof(sbit_t) * code->N);

	i_idx = 0;
	p_idx = decoder->p_idx;

	/* Scan the treillis */
	for (i = 0; i < code->K - 1; i++) {
		/* Reset next accumulated error */
		for (s = 0; s < n_states; s++)
			ae_next[s] = MAX_AE;

		/* Get input */
		if (code->puncture) {
			/* Hard way ... */
			for (j = 0; j < code->N; j++) {
				int idx = ((decoder->o_idx + i) * code->N) + j;
				if (idx == code->puncture[p_idx]) {
					in_sym[j] = 0;	/* Undefined */
					p_idx++;
				} else {
					in_sym[j] = input[i_idx];
					i_idx++;
				}
			}
		} else {
			/* Easy, just copy N bits */
			memcpy(in_sym, &input[i_idx], code->N);
			i_idx += code->N;
		}

		/* Scan all state */
		for (s = 0; s < n_states; s++) {
			unsigned int nae;
			int ov, e;
			uint8_t m;

			/* Next output and state */
			uint8_t out;
			uint8_t state;

			if (code->next_term_output) {
				out = code->next_term_output[s];
				state = code->next_term_state[s];
			} else {
				out = code->next_output[s][0];
				state = code->next_state[s][0];
			}

			/* New error for this path */
			nae = ae[s];			/* start from last error */
			m = 1 << (code->N - 1);		/* mask for 'out' bit selection */

			for (j = 0; j < code->N; j++) {
				int is = (int)in_sym[j];
				if (is) {
					ov = (out & m) ? -127 : 127; /* sbit_t value for it */
					e = is - ov;                 /* raw error for this bit */
					nae += (e * e) >> 9;         /* acc the squared/scaled value */
				}
				m >>= 1;                     /* next mask bit */
			}

			/* Is it survivor ? */
			if (ae_next[state] > nae) {
				ae_next[state] = nae;
				state_history[(n_states * i) + state] = s;
			}
		}

		/* Copy accumulated error */
		memcpy(ae, ae_next, sizeof(unsigned int) * n_states);
	}

	/* Update decoder state */
	decoder->p_idx = p_idx;
	decoder->o_idx += code->K - 1;

	free(in_sym);
	return i_idx;
}

int
osmo_conv_decode_get_best_end_state(struct osmo_conv_decoder *decoder)
{
	const struct osmo_conv_code *code = decoder->code;

	unsigned int min_ae;
	int min_state;
	int s;

	/* If flushed, we _know_ the end state */
	if (code->term == CONV_TERM_FLUSH)
		return 0;

	/* Search init */
	min_state = -1;
	min_ae = MAX_AE;

	/* If tail biting, we search for the minimum path metric that
	 * gives a circular traceback (i.e. start_state == end_state */
	if (code->term == CONV_TERM_TAIL_BITING) {
		int t, n, i;
		uint8_t *sh_ptr;

		for (s = 0; s < decoder->n_states; s++) {
			/* Check if that state traces back to itself */
			n = decoder->o_idx;
			sh_ptr = &decoder->state_history[decoder->n_states * (n-1)];
			t = s;

			for (i = n - 1; i >= 0; i--) {
				t = sh_ptr[t];
				sh_ptr -= decoder->n_states;
			}

			if (s != t)
				continue;

			/* If it does, consider it */
			if (decoder->ae[s] < min_ae) {
				min_ae = decoder->ae[s];
				min_state = s;
			}
		}

		if (min_ae < MAX_AE)
			return min_state;
	}

	/* Finally, just the lowest path metric */
	for (s = 0; s < decoder->n_states; s++) {
		/* Is it smaller ? */
		if (decoder->ae[s] < min_ae) {
			min_ae = decoder->ae[s];
			min_state = s;
		}
	}

	return min_state;
}

int
osmo_conv_decode_get_output(struct osmo_conv_decoder *decoder,
			    ubit_t *output, int has_flush, int end_state)
{
	const struct osmo_conv_code *code = decoder->code;

	int min_ae;
	uint8_t min_state, cur_state;
	int i, n;

	uint8_t *sh_ptr;

	/* End state ? */
	if (end_state < 0)
		end_state = osmo_conv_decode_get_best_end_state(decoder);

	if (end_state < 0)
		return -1;

	min_state = (uint8_t) end_state;
	min_ae = decoder->ae[end_state];

	/* Traceback */
	cur_state = min_state;

	n = decoder->o_idx;

	sh_ptr = &decoder->state_history[decoder->n_states * (n-1)];

		/* No output for the K-1 termination input bits */
	if (has_flush) {
		for (i = 0; i < code->K - 1; i++) {
			cur_state = sh_ptr[cur_state];
			sh_ptr -= decoder->n_states;
		}
		n -= code->K - 1;
	}

	/* Generate output backward */
	for (i = n - 1; i >= 0; i--) {
		min_state = cur_state;
		cur_state = sh_ptr[cur_state];

		sh_ptr -= decoder->n_states;

		if (code->next_state[cur_state][0] == min_state)
			output[i] = 0;
		else
			output[i] = 1;
	}

	return min_ae;
}

/*! All-in-one convolutional decoding function
 *  \param[in] code description of convolutional code to be used
 *  \param[in] input array of soft bits (coded)
 *  \param[out] output array of unpacked bits (decoded)
 *
 * This is an all-in-one function, taking care of
 * \ref osmo_conv_decode_init, \ref osmo_conv_decode_scan,
 * \ref osmo_conv_decode_flush, \ref osmo_conv_decode_get_best_end_state,
 * \ref osmo_conv_decode_get_output and \ref osmo_conv_decode_deinit.
 */
int
osmo_conv_decode(const struct osmo_conv_code *code,
		 const sbit_t *input, ubit_t *output)
{
	struct osmo_conv_decoder decoder;
	int rv, l;

	/* Use accelerated implementation for supported codes */
	if ((code->N <= 4) && ((code->K == 5) || (code->K == 7)))
		return osmo_conv_decode_acc(code, input, output);

	osmo_conv_decode_init(&decoder, code, 0, 0);

	if (code->term == CONV_TERM_TAIL_BITING) {
		osmo_conv_decode_scan(&decoder, input, code->len);
		osmo_conv_decode_rewind(&decoder);
	}

	l = osmo_conv_decode_scan(&decoder, input, code->len);

	if (code->term == CONV_TERM_FLUSH)
		osmo_conv_decode_flush(&decoder, &input[l]);

	rv = osmo_conv_decode_get_output(&decoder, output,
		code->term == CONV_TERM_FLUSH,		/* has_flush */
		-1					/* end_state */
	);

	osmo_conv_decode_deinit(&decoder);

	return rv;
}
