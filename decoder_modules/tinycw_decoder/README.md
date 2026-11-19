# TinyCW CW decoder module

Whole-band CW decoder for SDR++ Brown, powered by TinyCW: a hybrid decoder
with a small trained model (13,932 weights, plain C).

Instead of decoding only the tuned VFO, the module takes the wide IQ stream,
scans the CW segment of the band currently in view, opens one narrow channel per
keyed signal and runs the model on each of them. Decoded text is printed to
stdout in 30 s batches; the waterfall overlay shows only the live transmission
count and the current search range.

## How it works

```
iqFrontEnd (full source bandwidth)
  └─ wide RxVFO  (CW segment in view, 96 kHz out)   ← one instance, retuned rarely
       ├─ scanner tap: 4096-point FFT, keying ratio p90/p10 per bin
       └─ Splitter ──► one RxVFO per active signal (3200 Hz out, 1.2 kHz wide)
                          └─ CW demod (tone → 700 Hz audio)
                               └─ 6 s sliding window every 1 s
                                    ├─ keying squelch (skip dead windows)
                                    ├─ main tone (screening rules, tinycw_mainband.h)
                                    ├─ conditioner forced to that tone (AGC → 90 Hz BPF → 600 Hz)
                                    ├─ spectrogram 65×T (torch-compatible geometry)
                                    └─ FastCW single band (key MLP at 600 Hz → tempo → CNN) → greedy CTC → text
```

Only full windows are decoded, in order: a fast feed never drops audio, it just
queues windows.

## Model

### History

The prototype was the [DeepFist](https://github.com/n9bc/DeepFist) neural
network (ONNX, CNN + CTC). It was too slow for a wideband decode, with one model
pass per channel for dozens of channels. It was discarded: the DeepFist model
is not used in this module. Some DeepFist code is used, ported to C++ (GPL-3.0-or-later,
Brent Crier, N9BC):

* `src/tinycw_conditioner.h`: `deepfist/features/conditioner.py`,
* `src/tinycw_spectrogram.h`: `deepfist/features/spectrogram.py`,
* `src/tinycw_ctc.h`: `deepfist/morse/alphabet.py` and `deepfist/model/decode.py`,
* `src/tinycw_squelch.h`: `tools/squelch.py`,
* `src/tinycw_calls.h`: the callsign pattern of `tools/rescore.py`.

A hybrid decoder was trained in its place:

1. **Hybrid structure.** A small key MLP reads the keying at the fixed 600 Hz
   bin. A tempo step resamples it to 3 steps per dot. A small CNN and a greedy
   CTC decode turn it into text.
2. **Corpus.** About 900 off-air clips (908 wav files, 12 s each) were cut
   from IQ recordings. A screening step kept the clips with one station and
   no QRM.
3. **Reference decoding.** Several open-source CW decoders decoded every clip:
   Morse-CW-Decoder, CW Studio, signal-decoder, ggmorse and the ag1le LSTM
   decoder.
4. **Ground truth.** An LLM cleaned their outputs into one text per clip.
   Unclear passages are marked `?` and match any text in training and
   scoring.
5. **Training.** The model was trained on 80 % of the clips (split by a name
   hash), with augmented copies and synthetic clips. The other 20 % is the test
   split.

### Run time

FastCW single band, in C: `src/tinycw_fastcw.c` wraps `src/fastcw_core.inc`.
The weights (13,932 floats) are embedded from `src/fastcw_weights.inc`. Nothing is downloaded at configure or run time
and no inference runtime is linked: the model is a plain C forward pass.
`<root>/tinycw/fastcw.weights` overrides the embedded weights.

**One signal per decoder.** Each channel window (panorama and focus) gets the
main tone by the rules the training clips were screened with
(`src/tinycw_mainband.h`):

1. 160-point frames at 3200 Hz (50 ms, 20 Hz bands), 25 ms hop.
2. On frame: ≥ 10 dB over the noise floor, a spectral peak, within 30 dB of the
   loudest band within 200 Hz.
3. Of the 5 most energetic spectral peaks in 100–1200 Hz, the band with the
   most present seconds wins (presence over energy).
4. Tone = the FFT peak of the whole window within ±20 Hz of that band.

The conditioner is then forced to that tone, and the model reads the fixed
600 Hz bin (no carrier scorer, no partner search).

**Accuracy** on the test split of the corpus (130 clips, against the
LLM-cleaned ground truth, best of `llm_ground` / `llm_ground_v1`): 12.5 % CER with this C++ chain,
12.1 % in the Python reference. The difference is
the mapping of CNN steps to the 30 ms output grid.

**Cost** per 12 s window (M-series, release): main tone 4.5 ms,
conditioner + spectrogram 1.6 ms, model 1.5 ms. `inferBatch` runs the batch
items one after the other: batching saves nothing. The model is serialized by
a mutex (static scratch).

## Waterfall markers

Two tiny dots sit stacked at one X — the carrier frequency — in the band that the
module reserves between the frequency scale and the waterfall
(`WaterFall::onReserveOverlayHeight`, 8 px). One channel is one X:

* **yellow** (upper) — keyed CW detected, so a channel was opened,
* **cyan** (lower) — that channel actually produced text.

Yellow without cyan means "detected but not decoded".

Both hold for 5 s after the last event (`EngineConfig::markerHoldSec`), and they
are clamped to the visible frequency range. Nothing is drawn over the spectrum
trace: the band is inserted above the waterfall, not inside the scope.

## GUI

* **Model** — status line: the loaded weights (embedded, or the `.weights`
  override) or the load error.
* **Pause** — stops decoding without tearing the DSP graph down.
* **decode CPU** — decode-thread load over the last second, summed over workers:
  with 3 workers fully busy it reads `300% of 300%`.
* **threads** — decode workers, 1..hardware concurrency (default: `hw-1`, capped
  at 4). Channels are striped over the workers by a stable id, each worker owns
  its own conditioner/spectrogram scratch, and the model pass is serialized by
  a mutex (static scratch, no nested parallelism). The adaptive width
  policy uses the load relative to the thread budget, so raising the thread
  count widens the decoded band instead of oversubscribing one core.
* **Max decoded width (global)** — one slider, CPU-adaptive by default: the
  policy starts at 2 kHz and widens while the decode thread has headroom, up to
  70 kHz (the widest HF CW segment), and narrows when it saturates (75 % / 45 %
  thresholds). Dragging the slider switches to manual.
* **Band range (from / to)** — per-band sliders, stored in
  `tinycw_decoder_config.json`.
* **Row lifetime / channel idle** — `rowHoldSec` (default 24) removes a decoded
  row that has produced no text for that long, and `channelIdleSec` (default 24)
  tears down a channel that shows no keying for that long even while the
  scanner still reports a spot at its frequency. Both live in
  `tinycw_decoder_config.json`. The effective range is the intersection of the
  setting, the CW segment and what the source can actually see; partial coverage
  is reported in the panel.

## Band plan

Hard-coded IARU Region 1 CW segments (`src/tinycw_bands.h`):

| Band | CW segment | | Band | CW segment |
|---|---|---|---|---|
| 160m | 1810–1838 | | 17m | 18068–18095 |
| 80m | 3500–3570 | | 15m | 21000–21070 |
| 60m | 5354–5366 | | 12m | 24890–24915 |
| 40m | 7000–7060 (contest) | | 10m | 28000–28070 |
| 30m | 10100–10130 | | 6m | 50000–50100 |
| 20m | 14000–14070 | | 2m | 144000–144150 |

When the span covers several bands — a wide source, or a zoomed-out view — the
band the tuned centre sits in is used, and if the centre sits between bands, the
one whose edge is nearest. (Picking the lowest band in view meant a 10 MHz span
decoded 60m while the operator was listening on 40m, and 30m after tuning to
20m.)

## Build

No external dependencies:

```bash
cmake -S . -B build -DOPT_BUILD_TINYCW_DECODER=ON
cmake --build build --target tinycw_decoder tinycw_cli -j8
```

## Headless CLI (no UI)

```bash
./build/decoder_modules/tinycw_decoder/tinycw_cli \
    --wav ~/recordings/baseband_14034485Hz_19-09-12_13-08-2022---cw-contest-good-quality-.wav \
    --max-width 70 --speed 1
```

The CLI feeds a baseband WAV through the *same* Engine the module uses and prints
30 s batches. Its switches control only the paths the GUI uses: `--focus HZ`
(the Follow VFO decoder at HZ), `--band 30m`, `--range 7000-7060`, `--center HZ`,
`--max-width KHZ`, `--no-adapt`, `--channels N`, `--threads N`, `--model PATH`
(the `.weights` override), `--batch S`, `--speed X`, `--duration S`,
`--threads-cycle S` (live thread resize), `--restart-at S` (play/stop reset),
`--list-bands`, `--verbose`.

Exit at the end of a run is immediate (`_Exit`) because SDR++ core globals have
GL static destructors that require a live context.

## Tests

Unit tests (band plan, range resolution, adaptive width, spectrogram,
conditioner, squelch, CTC) and an end-to-end test that plays a real CW contest
recording and checks that channels open and text arrives:

```bash
cmake -S . -B build-tests -DBUILD_TESTS=ON -DOPT_BUILD_TINYCW_DECODER=ON
cmake --build build-tests -j8
./build-tests/sdrpp -r root_dev -t tinycw_units
./build-tests/sdrpp -r root_dev -t tinycw_wav_e2e   # needs weights
```

The recordings are the repository's own, in `e2e/recordings/`, which the test
build compiles in as the default. `--test_root DIR` names another directory
instead. The E2E test reads `TINYCW_E2E_SECONDS` (default 60). It creates the
module instance itself, so the root config needs no module entry.

Note: on macOS this harness aborts when launched without a debugger (it happens
for the pre-existing tests too); run it as
`lldb -b -o run -- ./build-tests/sdrpp ...`.

## Measured behaviour (MacBook, arm64, release)

* 20m contest recording, 70 kHz segment: 14–20 live channels.
* 1 worker: ~68 % of one core at 3× feed; saturates at ~4.4× realtime.
* 4 workers: same feed at ~22 % per core; reaches ~7× realtime, 354 % of 400 % CPU.
* Adaptive width settles around 20–40 kHz at 1× realtime on this machine.
* Accuracy: 12.5 % CER on the 130 test clips (see Model). This module adds
  channelization and squelch on top of the same front end.

## Limitations

* Signals closer than ~250 Hz are merged by the scanner; only one station per
  channel is decoded (the conditioner locks onto the dominant tone).
* Narrow channels (< 800 Hz) hurt accuracy: CW keying sidebands need room.
* No tuning/click-to-tune integration yet; the decoded text is stdout-only.