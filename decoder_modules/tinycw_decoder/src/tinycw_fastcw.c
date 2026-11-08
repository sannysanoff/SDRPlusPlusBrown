/* SPDX-License-Identifier: MIT
 * FastCW single-band inference for the SDR++ module.
 *
 * fastcw_core.inc is an unchanged copy of DeepFist fastcw/fastcw.c (key MLP,
 * tempo search, resampling, CNN). This file adds the single-band front end
 * (DeepFist fastcw/reference.py front_end_single) and a batch entry point
 * with the ONNX output layout. */
#define FASTCW_NO_MAIN
#include "fastcw_core.inc"
#include "tinycw_fastcw.h"

#define CENTER_BIN 16          /* 600 Hz: (600 - 400) / 12.5 */
#define NSINGLE (NW - NCAR)    /* weights without the carrier scorer */

static const float embedded[NSINGLE] = {
#include "fastcw_weights.inc"
};
static Model model;

int fastcw_frame_count(int T) { return (T + 1) / 2; }
int fastcw_max_frames(void) { return MAX_T; }

int fastcw_init(void) {
    memset(&model, 0, sizeof model);
    memcpy(model.w, embedded, sizeof embedded);
    return 0;
}

int fastcw_load_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    Model m;
    memset(&m, 0, sizeof m);
    size_t count = fread(m.w, sizeof(float), NSINGLE, fp);
    int extra = fgetc(fp) != EOF;
    fclose(fp);
    if (count != NSINGLE || extra) return -1;
    model = m;
    return 0;
}

/* Key logits at the fixed centre bin; dot length per sub-frame from the
 * global tempo, refined per transmission (pauses >= SPLIT_S split them). */
static void front_end_single(const Model *m, const float *spec, int T, float *key, double *dot) {
    static unsigned char on[MAX_S];
    static Span tx[MAX_S];
    const float *p = m->w + NMLP;
    int S = T * SUB;
    key_logits(m, spec, T, PAD + CENTER_BIN, NULL, key);
    double global = tempo_search(key, S, p);
    if (global <= 0) global = 12.0;
    for (int s = 0; s < S; s++) { dot[s] = global; on[s] = key[s] > 0; }
    int n = on_spans(on, S, (int)(SPLIT_S / SUB_S), tx);
    for (int i = 0; i < n; i++) {
        if (count_marks(key, tx[i].a, tx[i].b) < 4) continue;
        double d = tempo_search(key + tx[i].a, tx[i].b - tx[i].a, p);
        if (d > 0) for (int s = tx[i].a; s < tx[i].b; s++) dot[s] = d;
    }
}

/* infer() of fastcw.c with front_end_single(). */
void fastcw_infer(const float *input, float *output, int B, int T) {
    static float key[MAX_S], x[3 * MAX_STEP], lp[(size_t)MAX_STEP * C],
        padded[(size_t)FP * MAX_T], all[(size_t)F * MAX_T];
    static double dot[MAX_S], edge[MAX_STEP + 1];
    const Model *m = &model;
    int O = (T + 1) / 2, S = T * SUB;
    for (int b = 0; b < B; b++) {
        const float *spec = input + (size_t)b * F * T;
        memcpy(all, spec, (size_t)F * T * sizeof(float));
        float fill = quantile(all, F * T, .2);
        for (size_t i = 0; i < (size_t)PAD * T; i++) padded[i] = padded[(size_t)(PAD + F) * T + i] = fill;
        memcpy(padded + (size_t)PAD * T, spec, (size_t)F * T * sizeof(float));
        front_end_single(m, padded, T, key, dot);
        int n = resample(key, dot, S, x, edge);
        if (n > 0) cnn(m, x, n, lp);
        int k = 0;
        for (int j = 0; j < O; j++) {
            float *q = output + ((size_t)j * B + b) * C;
            double lo = 2.0 * SUB * j, hi = lo + 2.0 * SUB;
            int pick = -1;
            while (k < n && (edge[k] + edge[k + 1]) / 2 < lo) k++;
            for (int i = k; i < n && (edge[i] + edge[i + 1]) / 2 < hi; i++)
                if (pick < 0 || lp[(size_t)i * C] < lp[(size_t)pick * C]) pick = i;
            if (pick < 0)
                for (int i = k > 0 ? k - 1 : 0; i < n; i++)
                    if (edge[i] <= lo + SUB && edge[i + 1] > lo + SUB) { pick = i; break; }
            if (pick < 0) {
                const float normal = log1pf((C - 1) * expf(-16.f));
                for (int c = 0; c < C; c++) q[c] = -16.f - normal;
                q[0] = -normal;
                continue;
            }
            memcpy(q, lp + (size_t)pick * C, C * sizeof(float));
        }
    }
}
