# Brown shared microphone regression

Run `python3 tests/brown_audio_sink/run.py` from the repository. Requires a C++17
compiler and VOLK (`pkg-config`). The harness uses the real DSP stream and
splitter, with simulated RtAudio hardware. It exercises both the RtAudio 5
exception path and RtAudio 6 return-code path, reference accounting, first-user
removal, missing devices, enable/disable/reconfigure, rollback, a stalled DSP
consumer, and coexistence with another provider's microphone.

For memory checking:

```
CXXFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' python3 tests/brown_audio_sink/run.py
```

After building **and installing**, start an isolated SDR++ configuration with
Brown Audio and two Radio module instances. Then run:

```
python3 tests/brown_audio_sink/live.py http://localhost:8080 Radio 'Radio 2'
```

This repeatedly creates and deletes the first and subsequent sink instances,
checks the UI remains responsive and validates invalid selections. It restores
the original sinks. It does not prove microphone samples are arriving: inspect
capture errors and test actual input separately. The shared microphone setting
is enabled by the first primary instance's legacy setting on initial migration;
thereafter its top-level `micInput` preference is shared. Secondary outputs can
keep capture alive, but cannot change microphone settings. Removing the first
primary transfers UI control to the next primary without restarting capture.

The runner also checks the preallocated SPSC output buffer for ordering across
wraparound, partial reads, silence on underrun, reset, and bounded shutdown when
hardware stops consuming. Config tests cover malformed JSON types, invalid
sample rates and unlocking after exceptions.

With Radio and Extra V/UHF loaded, run:

```
python3 tests/brown_audio_sink/module_controls.py http://localhost:8080
```

This tests queued enable/disable/query/command requests, missing instances and a
DMR wait that stays off the UI thread, including removal of its DSD demodulator
while waiting. Run in either classic or transceiver layout. Original module
state is restored. The optional `BackgroundDebugCommand` interface prepares
read-only jobs on the UI thread without changing the base module vtable.
