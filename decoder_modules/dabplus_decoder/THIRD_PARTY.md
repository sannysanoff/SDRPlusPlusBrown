# Third-party code used by dabplus_decoder

## Credits

The DAB+ work in this module is a port, not an invention. The credit belongs to:

- **`prog`** ([@prog](https://x.com/prog) on X, Youssef Touil, the Airspy author
  and the author of SDR#) for the DAB+ source this module follows, and for the
  SDR# VDK (Vibe Development Kit) that reference plugin is built with. That is
  the reference engine recorded in its `SOURCE-VERSIONS.txt`: the coherent
  demapper with per-frame soft bits, the per-symbol soft-bit scaling, the
  null-power threshold and the X-PAD fix now carried by
  `patches/dab-radio-0a09ba34-vdk-engine.patch`. Youssef Touil is also named on
  the application's credits screen (`core/src/credits.cpp`).
- **William Yang** ([williamyang98](https://github.com/williamyang98)) for
  DAB-Radio, the engine under OFDM demodulation, FIC/FIG, MSC, PAD, MOT and the
  per-service audio channels, and for ViterbiDecoderCpp.
- **Victor Zverovich and the {fmt} contributors** for fmt.
- **The FAAD2 authors** listed in its `AUTHORS` file (M. Bakker,
  Alexander Kurpiers, Volker Fischer, Gian-Carlo Pascutto and others), and the
  maintainers of the upstream repository (`knik0/faad2`), for the AAC decoder.
- **Michael Hipp and the mpg123 contributors**, with Thomas Orgis and
  Taihei Momma as current maintainers, for the MPEG Layer II decoder.
- **Phil Karn, KA9Q**, for the Reed-Solomon and Viterbi algorithms
  ([ka9q.net/code/fec](http://www.ka9q.net/code/fec/), LGPL) that the engine's
  `reed_solomon_decoder` and `dab_viterbi_decoder` are built on.
- **Tristan Brindle** for the C++20 `span` implementation the engine includes as
  `utility/span.h` (Boost Software License 1.0).
- **The FFTW authors** (Matteo Frigo and Steven G. Johnson) and, on macOS,
  Apple's vDSP, for the transforms the engine runs on through the core.

## What is fetched

Nothing here is copied into this repository. `src/deps.cmake` fetches each
dependency from its upstream at the commit below, then refuses to build if the
checked-out commit differs from the pin. An already-populated build tree is not
fetched again, so reconfiguring and rebuilding offline works.

| Component | Upstream | Pin | Licence | Used for |
|---|---|---|---|---|
| DAB-Radio | https://github.com/williamyang98/DAB-Radio.git | `0a09ba34a40286cf77e0c48aa47cac64b86341ef` | MIT | `src/ofdm`, `src/dab`, `src/basic_radio` — OFDM demodulation, FIC/FIG, MSC, PAD, MOT, the service database and the per-service audio channels |
| ViterbiDecoderCpp | https://github.com/williamyang98/ViterbiDecoderCpp.git | `5749af98be5654771154d58aa7c1283bba792fb4` | LGPL-2.1 | soft-decision convolutional decoder for FIC and MSC (header-only) |
| fmt | https://github.com/fmtlib/fmt.git | `0147e08225db261f5689fc17a986ede7f1db56f0` | MIT | formatting in the engine's (disabled) log macros (header-only) |
| faad2 | https://github.com/knik0/faad2.git | `693690e37f417d394a5e65296c75c2d971ffec58` | GPL-2.0-or-later | DAB+ AAC decoding; built statically inside this build tree |
| mpg123 | https://github.com/madebr/mpg123.git | `31d1d478177ce923260cb6c69f037b9932c4dae5` | LGPL-2.1 only | DAB MPEG Layer II decoding; built statically inside this build tree |
| the FFT | not a dependency of this module: the transforms go through `src/dabplus_fft.h` into the core's `dsp::arrays` | core already links it (FFTW3f; vDSP on macOS when the accelerated FFT is enabled) | as the core | the engine's FFTs |

The pins match the engine revisions recorded by the SDR# reference
implementation this module was ported from (`SOURCE-VERSIONS.txt` in that
project).

A dependency directory that is not itself a checkout at its pin is discarded and
fetched again before the build is configured, including the FetchContent
download stamp: a stamp left behind by an earlier configure otherwise makes
FetchContent report success while leaving an empty source directory. The commit
is read only from a directory that has its own `.git`, because `git rev-parse`
in a plain directory walks up and answers with the enclosing SDR++ commit.

## Licence position

The application and its core are GPL-3.0 (`license` in the repository root), and
this module is part of that work. Every dependency above therefore has to be
GPL-3-compatible, and all of them are:

- DAB-Radio and fmt are MIT, which the GPL accepts as compatible.
- faad2 is GPL-2.0-**or-later**, not GPL-2.0-only. The "or later" clause is what
  allows it to be combined with a GPL-3 application; the sources carry it in
  every header. Do not record it as GPL-2.0 alone.
- ViterbiDecoderCpp and mpg123 are LGPL-2.1. LGPL-2.1 section 3 permits
  relicensing under GPL-2 or later, so static linking into a GPL-3 work is
  allowed. Both are linked statically here, which keeps their obligations: the
  LGPL text travels with the binary and the recipient must be able to relink.

`THIRD_PARTY.md` records names, pins and licences. It is not installed by
`make install` and the packaging scripts do not copy licence texts, so a
binary-only delivery of this module ships GPL-2.0-or-later (faad2) and
LGPL-2.1 (mpg123, ViterbiDecoderCpp) code without their licence texts. Ship
`Native/licenses`-style texts alongside the plugin before publishing binaries.

### The engine patches and the reference licence

Patch 1, `dab-radio-0a09ba34-vdk-engine.patch`, carries engine work taken from
the SDR# DAB+ reference tree, not written here: `src/ofdm/coherent_demapper.{h,cpp}`
and the `ofdm_demodulator` / `pad_processor` changes are that tree's files with
only the FFT call substituted. Patches 2 and 3 are authored in this repository.

The reference tree's own statements disagree with each other. Its top-level
licence is "Airspy ecosystem" only: use, copy, modify and distribute the software
"exclusively as part of the Airspy ecosystem", with redistribution outside it
"strictly prohibited without prior written consent from the copyright holders"
(Youssef Touil). Its third-party notices instead declare the native decoder
GPL-2.0-or-later, and its README declares only the newly written adapter code
GPL-2.0-or-later. No statement says which licence governs the engine files
above.

This module is distributed under GPL-3.0 outside the Airspy ecosystem. Credit
to the author appears in `## Credits` above, but credit is not a licence grant.
Treat the engine patches as uncleared until the copyright holder gives written
permission to redistribute them beyond the Airspy ecosystem, or confirms in
writing that those files are GPL-2.0-or-later.

## Patches

The DAB-Radio checkout is not used verbatim. Three patches are applied in order
after the pin is verified; each is sha256-checked against the value in
`src/deps.cmake` before `git apply` runs, and a mismatch aborts the configure
step rather than building an unverified engine. Patch 1 carries the reference
tree's work and its licence position is the open item in `## Licence position`;
patches 2 and 3 are authored in this repository and carry the application's
GPL-3.0.

| Patch | sha256 | What it changes |
|---|---|---|
| `patches/dab-radio-0a09ba34-vdk-engine.patch` | `af3c7050ebf59899eb25d8aedadfba411de36c54687aa229701a207eea561a1c` | the reference implementation's own engine work: coherent demapping with per-frame soft bits (`ofdm/coherent_demapper.{h,cpp}`), one soft-bit scale per symbol, a higher null-power threshold for shallow nulls, and an X-PAD field fix |
| `patches/dab-radio-0a09ba34-silence-guard.patch` | `97dc90892edc7a39674c394e5516f8d9f8c14a467e966063c22a04a1175eef4e` | guards the coarse-frequency peak interpolation against a zero peak sum. Silence, or a receiver with nothing tuned, yields an all-zero correlation spectrum; the resulting NaN used to enter the frequency correction and the soft bits and never cleared, so the demodulator stayed dead after the signal returned |
| `patches/dab-radio-0a09ba34-core-fft.patch` | `9c8418b9a95b5b720e6912f89e9cc9203e12c17df06a504445ae90d3e52eee43` | replaces the engine's direct `fftwf_*` calls with `dabplus::FFT`, which runs the transforms on the application's own FFT (`dsp::arrays`), so the plugin inherits the receiver's FFT engine — vDSP on macOS — instead of naming an FFT library itself |

Applying the pin plus the first patch reproduces the reference engine tree; the
second and third patches are the divergence from it, both verified by comparing
`src/` trees between a pristine clone and the patched checkout. The third patch
keeps the engine's arithmetic identical: the app's plan normalises the inverse
transform, and `dabplus::FFT` scales it back, so the demodulator sees the
unnormalised transforms its maths was written against.

The mpg123 checkout carries one patch of this repository's own,
`patches/mpg123-31d1d478-arm64-fpu.patch`
(`0a4e036d75d75d9b9b8a615dcc45c76d4524491f355c4476ed2c6d9f08040892`), applied
only when the target is ARM64 and not Apple, and verified the same way:

| Patch | sha256 | What it changes |
|---|---|---|
| `patches/mpg123-31d1d478-arm64-fpu.patch` | `0a4e036d75d75d9b9b8a615dcc45c76d4524491f355c4476ed2c6d9f08040892` | `ports/cmake/src/CMakeLists.txt`: an ARM64 target is treated as having an FPU. mpg123 asks `cmake_host_system_information(... HAS_FPU)`, which reads an x86 style flags line out of `/proc/cpuinfo`; Linux ARM64 has no such line, so the answer is no FPU, mpg123 picks fixed point math, and `libmpg123/optimize.h` then rejects the NEON64 decoder chosen in the same configure step |

Every `*.patch` file is marked `-text` in `.gitattributes`: the hashes above are
of the file bytes, and a checkout that rewrote line endings would change them
and abort the configure step.

## What is not used from those projects

DAB-Radio's CMake project, examples, GUI, easylogging, imgui, implot, stb,
argparse, librtlsdr and portaudio integrations are not built; the module
compiles only the library sources it needs (see the source list in
`CMakeLists.txt`). The engine's logging macros compile to no-ops, and the
`ettus`/`rtl_sdr` device examples are irrelevant to an SDR++ module.
