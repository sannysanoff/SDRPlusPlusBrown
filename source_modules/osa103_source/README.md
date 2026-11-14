# OSA103 source module

Receive-only source for the OSA103 / OSA103 Mini (FTDI FT232RL as a 3 Mbaud UART,
Spartan-6 FPGA, AD9288 ADC, ADF4351 or MAX2871 LO). The protocol was reconstructed by
static analysis of the vendor's `ExtIO_Osa.dll` v3.09 — no hardware was available
while writing it, so every assumption that could not be settled from the binary is a
runtime knob rather than a constant. The full protocol write-up lives outside this
tree; the short version:

* Host frames: 25-byte sync (`AA 0F 55` ×8 then `00`, written as its own transfer),
  `u16 len = 1 + payload length`, `u8 type`, payload, CRC-16 (reflected `0xA001`,
  init `0xFFFF`, low byte first).
* `A5 00 00` hello, unframed, sent first.
* Type `0x01`: 12-bit block write, 512 words per frame at `0xC000 + n·0x400`.
* Type `0x03`: 73-byte register image — NCO word, LO in Hz, mode, front-end flags,
  ADC trim, calibration bytes, clock divisor.
* No start/stop command. After the configuration frame the FPGA streams fixed-size
  packets that begin with the same sync and carry a flags byte plus 512 interleaved
  `int16` I/Q pairs.
* The FPGA image (336,758 bytes, header `AA 99 55 66`) is bit-banged in through
  D0/D2/D7 (DIN/CCLK/PROG_B) by the DLL on every open.

## Origin and status

The USB protocol implemented here was reconstructed by static analysis of the vendor's
ExtIO driver, `ExtIO_Osa.dll` v3.09 (PE32, compiled 2022-05-16, md5
`bcab0dfd74186c2b99b8b2b68ccbc57c`), the Windows driver for the OSA103 / OSA103 Mini
published by the OSA103 project at http://www.osa103.ru. The device's public schematic
and manual were used as supporting documentation.

Only protocol facts live in this tree — command type bytes, field offsets, constant
words, formulas. No vendor code, decompiled listing, binary or FPGA bitstream is
included or redistributed; if the FPGA image is needed it has to be extracted by the
user from their own copy of the driver. Nothing here is affiliated with or endorsed by
the vendor.

The code has never seen real hardware. Everything that could not be settled from the
binary is a runtime knob (see below), and the receive path is covered only by synthetic
packets through `tests/run_tests.sh`.

## Licence

Non-commercial / educational use only, with no warranty and no liability; commercial
use needs written permission. Full terms, provenance and the third-party notes
(FTDI D2XX, and why this plugin stays out of the SDR++ tree despite being GPL-linked)
are in `LICENSE.md` — read it before redistributing anything.

## Dependencies


Build time: CMake ≥ 3.13, a C++17 compiler, and an SDR++ tree (`sdrpp_core`). There
are **no FTDI headers and no link-time FTDI library** — D2XX is resolved with
`dlopen`/`GetProcAddress` at runtime, the same way the vendor DLL does it.

Runtime: the FTDI D2XX library only.

* Windows: `ftd2xx.dll` (FTDI CDM/D2XX driver package), placed next to `sdrpp.exe` or
  on `PATH`.
* macOS: `libftd2xx.dylib` from FTDI's D2XX dmg, in `/usr/local/lib`,
  `/opt/homebrew/lib`, or wherever the "D2XX library" field points.
* Linux: `libftd2xx.so` from FTDI's D2XX tarball, plus udev rules. The kernel's
  `ftdi_sio` driver must not claim the device — `rmmod ftdi_sio` (or a udev rule that
  unbinds it), otherwise D2XX reports `FT_DEVICE_NOT_FOUND`.

Optional: an FPGA bitstream file. The image is embedded in `ExtIO_Osa.dll`
(length at `0x494270`, data at file offset `0x92C74`); extract it if the device does
not come up configured, then point "Bitstream file" at it and enable
"Load FPGA bitstream".

## Build

```sh
cmake -S . -B build -G Ninja -DOPT_BUILD_OSA103_SOURCE=ON
ninja -C build osa103_source
```

The option is off by default on Android: D2XX does not exist there (FTDI ships a Java
USB driver instead), and the module is not in Android's hardcoded module list either.

## Offline tests

`tests/run_tests.sh` compiles the protocol layer and the D2XX binding standalone — no
SDR++ build, no device:

* `frame_test.cpp` checks the encoder against the byte layout and CRC the DLL
  produces (sync, both frame types, NCO/LO fields, image override) and drives the
  receive parser with a synthetic packet stream in awkward chunk sizes, in both
  inbound layouts, including corrupt-CRC rejection.
* `d2xx_test.cpp` drives the `dlopen` binding against `stub_d2xx.c`, verifying the
  device-info struct layout, symbol resolution, and that a configuration frame reaches
  `FT_Write` as 25 + 78 bytes.

## Configuration

Every protocol parameter has a default taken from the value observed in the DLL, and
every one of them is persisted in `<root>/osa103_source_config.json` (88 keys, seeded
with the defaults on first run) as well as editable in the module's UI, grouped into
Tuning, Front end, Samples, Protocol and Advanced. Integers are stored in the config
file as decimal, so `0x19` appears as 25 and `0x6258` as 25176; the UI shows hex for
the tag and word fields. An `imageOverrideHex` string, if it parses to exactly 73
bytes, replaces the generated register image byte for byte — the shortest path to
trying a captured or hand-edited image without touching code.

## Protocol unknowns (all exposed in the UI/config)

Front-end and register image, semantics not decoded:

* `0x18 00 08 FF` at image offset 0–3, `1F 00 40 FF` at 28–31, and
  `C3 00 06 00 80 FF` at 61–66: byte-identical for every configuration the DLL can
  produce. Probably fixed decimation/filter setup. Editable as tag bytes.
* Clock constant `0x6258` (Mini) vs `0x0028` (F) — which model selects which, and what
  the word means, is unconfirmed. Field "Clock const".
* ADC gain word (`0x8000`) and two offset words, plus five calibration bytes: values
  observed, meaning unknown. Separate fields.
* Rx/Tx tag byte `0x80 | 0x01 | 0x04`: only the `0x80` base was ever seen asserted for
  receive; the two low bits presumably flag Tx IF and the Tx path.
* Front-end flags byte `0x19`: bit0 preamp, bit1 50 Ω, bit2 dither, bit4 always set.
  Which physical pin each bit drives is inferred from the schematic net names only.
* Mode byte `0x18`: modulation/mode field, bit meanings unknown.

Tuning:

* NCO: `round(f · 2³¹ / 10⁸)` is confirmed (100 MHz → exactly `0x80000000`). Whether
  `f` is the dial frequency, dial + IF, or dial − IF is not: the DLL uses an IF of
  21.395 MHz in some paths. Knobs: "NCO frequency" mode, "IF (Hz)".
* LO field: dial Hz little endian in four bytes plus a fifth high byte; the actual
  synthesizer is programmed by the FPGA over SPI, so the achieved frequency and the
  injection side (high/low) are unknown. Knob: "LO scale".

Transport and framing:

* Whether the 25-byte sync is written before every frame or only once per session.
  Observed as its own `FT_Write` per frame.
* Whether the device replies to every frame; the DLL polls 100 × 5 ms for a reply,
  treats subtype `0x02` as "device ready" and `0x03` as "configuration accepted".
  Nothing is retried by this module — failures show up as CRC/noise counters.
* Inbound packets may or may not repeat the `u16` length field. The DLL's reader is
  written for fixed sizes, so the default is "no length field"; the knob "Rx length
  field" switches layout. Wrong choice is immediately visible as a runaway dropped-byte
  counter and zero packets.
* Whether the device enforces the CRC on host frames. Knob: "CRC enabled".
* Size of the `0x5A` info packet (149 bytes assumed). Knob: "0x5A packet bytes".

Sample stream:

* 2048 payload bytes per packet = 512 `int16` pairs is an inference from the buffer
  size the DLL passes to the ExtIO callback. Knob: "Rx I/Q bytes".
* Sample scale `2.5322` comes from an immediate in the DLL; it looks like a gain trim
  rather than a unit conversion, so the stream may be scaled wrong by a constant
  factor. Knob: "Sample scale".
* Endianness of `int16` samples, and whether I/Q need swapping. Knobs: endianness is
  compiled as little endian by default with a swap option, plus "Swap I/Q" and
  "Invert Q".
* Flag byte bit1 appears to mean "transmitter keyed" and mutes receive. Knob:
  "Mute on Tx flag".

Not implemented / still open:

* The type `0x01` block write uploads a 512-word table the DLL computes from the
  sample rate. The synthesis was not reconstructed, so the module can send a
  placeholder (`0x0555`) only if "Send type 1 table" is enabled. If the device needs
  real filter coefficients, this is the missing piece.
* Type `0x01` is sent once per configuration in the DLL; whether the device needs it at
  all for RX was never observed.
* Only one sample rate is advertised by the DLL, `65104.0` Hz, and `ExtIoSetSrate` has
  no effect on hardware. Whether other rates exist (by changing the clock divisor
  fields) is untested — the sample-rate box only affects SDR++'s DSP, not the device.
