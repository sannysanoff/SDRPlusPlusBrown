// SPDX-License-Identifier: MIT
// FastCW single-band model (DeepFist fastcw/, C). Replaces the ONNX CNN+CTC.
//
// Input : spectrograms [batch][65][T] (tinycw_spectrogram.h), audio already
//         conditioned to the channel's main tone (re-centred at 600 Hz).
// Output: log probs [O][batch][48], O = (T + 1) / 2 -- the ONNX output layout.
//
// Path per window: key MLP at the fixed 600 Hz bin -> tempo search -> 3 steps
// per dot -> CNN -> back to the 30 ms output grid. No carrier scorer.
// Not thread-safe (static scratch): callers serialize (tinycw::Model does).
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int fastcw_frame_count(int T);                 // O for T input frames
int fastcw_max_frames(void);                   // largest T accepted
// Loads the embedded weights; returns 0 on success.
int fastcw_init(void);
// Loads weights from a file (13,932 floats, single band); 0 on success.
int fastcw_load_file(const char* path);
void fastcw_infer(const float* spec, float* out, int batch, int T);

#ifdef __cplusplus
}
#endif
