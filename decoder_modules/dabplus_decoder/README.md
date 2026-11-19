# dabplus_decoder — DAB/DAB+ as a mode of the radio module

A DAB/DAB+ receiver that is not a receiver of its own. The module injects a
`DAB` entry into every `RadioModuleInterface`'s mode list and hands the radio a
demodulator, so the programme runs on the radio's VFO, IF chain, AF chain and
audio sink exactly like NFM or DSD does. Selecting the mode widens the module
column: the radio's menu then carries the ensemble, the programme label, the
service list and the constellation, taking the space from the waterfall.

## Pieces

- `src/main.cpp` — module and injection. Registers a mode and a demodulator
  provider with each radio instance, and removes both when the module is
  disabled or a radio goes away.
- `src/dab_demod.h` — the mode itself: `demod::DAB`, an engine on the radio's IF
  stream, a resampler feeding the radio's AF chain, the station list and
  constellation, and the debug commands `dab_status` / `dab_select`.
- `src/dab_engine.{h,cpp}` — adapter around the pinned DAB-Radio engine:
  transmission mode, service selection, audio ring, published snapshot.

The demodulator also holds the source back. The engine decodes exactly as fast as
samples arrive: from a live radio that is the device rate, but a recording is
read many times faster, so the audio ring would fill and drop its backlog — heard
as gaps and a programme running ahead of real time. `inputHandler` therefore
waits until at most half a second of decoded audio is queued, which pushes back
through the VFO and IF chains to the file reader the way a slow sink does, and a
recording plays at its own speed. `audio_overflows` in the status JSON counts ring
overflows, and the e2e test asserts it stays at zero while the audio clock matches
the programme's.
- `src/dabplus_fft.{h}`, `src/dabplus_fft_core.cpp` — the engine's transforms,
  routed into the core's own FFT (`dsp::arrays`): FFTW3f on Linux and Windows,
  vDSP on macOS when the accelerated FFT is enabled. The plugin therefore links
  no FFT of its own; see THIRD_PARTY.md.
- `src/deps.cmake` — pins and hash-verified patches for the upstream engine.
- `src/cli.cpp` — headless decoder used by the e2e test and for offline checks.

## Radio interface additions

Two virtuals on `demod::Demodulator` carry the mode's extras, both defaulted so
no other demodulator changes:

- `handleDebugCommand(cmd, args)` — the radio offers unrecognised commands to the
  selected demodulator, which is how `dab_status` and `dab_select` work.
- `getExtraMenuWidth()` — the radio reports the width its mode needs, and
  `main_window.cpp` adds it to the module column, clamping it so the waterfall
  keeps at least half the window.

`ModuleManager::Instance` carries the same two virtuals, because that is the type
`main_window` iterates. Adding them changes the vtable of every instance class,
so all plugins must be rebuilt after this change — a stale plugin dies on load.

## Building

```
cmake -DOPT_BUILD_DABPLUS_DECODER=ON .
ninja dabplus_decoder dabplus_decoder_cli
ninja install          # plugins into root_dev/inst/lib/sdrpp/plugins
```

The first configure clones the five pinned dependencies; later configures reuse
them. On macOS, build products copied to the plugin directory need
`codesign --force --sign - <plugin>.dylib`, otherwise the OS kills the process
when it loads them.

## Testing

```
cd e2e
E2E_BUILD_DIR=../cmake-build-release E2E_ROOT_DEV=../root_dev \
    python3 test_dabplus_decoder.py
```

The test decodes the committed 0.9 s recording of the 11D "Bayern" ensemble
(222.064 MHz, Mode I) twice — on FFTW and, on macOS, on vDSP — then runs the same
recording through the radio module: mode present in the list, sync, ensemble,
service list, 48 kHz stereo audio, service switching, and a clean stop.

## Credits

`prog`'s DAB+ source ([@prog](https://x.com/prog) on X, Youssef Touil, the
Airspy author) is the parent of this port, running on William Yang's DAB-Radio
engine. The other dependencies are DAB-Radio and ViterbiDecoderCpp (William
Yang), fmt (Victor Zverovich and contributors), faad2, mpg123 (Michael Hipp and
contributors), and the core's FFT (FFTW, vDSP on macOS). THIRD_PARTY.md names
the authors in full and records the licence position, including the open
question about the licence of the reference engine patches.

## Limits

- Only Mode I is verified, against one recording. Modes II–IV are selectable and
  untested.
- MOT slideshow and TII transmitter identification are compiled but not surfaced
  in the UI.
- The Android build is not wired. Three things stand between this module and an
  `.so` that the phone can load: the option is OFF there, `core/src/core.cpp`
  hardcodes the module names Android may dlopen so `dabplus_decoder.so` would
  have to join that list, and faad2 and mpg123 have not been cross-compiled with
  the NDK (both are portable CMake C projects, mpg123 through `ports/cmake`, but
  that is untested). The FFT is not on that list of problems: the transforms go
  through the core's `dsp::arrays`, and Android's core already links
  `libfftw3f.so` from the SDR kit.
