# OSA103 source module

Receive-only source for the OSA103 / OSA103 Mini (FTDI FT232R as a 3 Mbaud UART,
Spartan-6 FPGA, AD9288 ADC, ADF4351 or MAX2871 LO). The protocol was reconstructed
from the vendor's `ExtIO_Osa.dll` v3.09 and is now **validated against live
hardware**: the builder reproduces the vendor's initial configuration frame
byte-for-byte (pinned by a regression test against a captured frame), and the
module streams real I/Q from the device at the declared 65,104 S/s with zero CRC
errors. Facts below marked "captured" were measured on the wire through an
FT_Write/FT_Read MITM proxy (see `tools/extio_capture/`).

The short version:

* Host frames: 25-byte sync (`AA 0F 55` ×8 then `00`, written as its own transfer),
  `u16 len = 1 + payload length`, `u8 type`, payload, CRC-16 (reflected `0xA001`,
  init `0xFFFF`, low byte first) covering len + type + payload.
* `A5 00 00` hello, unframed, sent first. The device answers with a 176-byte info
  packet.
* Type `0x03`: 73-byte register image (full field map below) — NCO and LO follow
  the dial, front-end flags, mode, calibration constants, fine NCO.
* Type `0x01`: eight ramp tables, 512 twelve-bit words each at `0xC000 + n·0x400`
  (structure below; generated algorithmically, byte-validated).
* No start/stop command. After the config frame plus tables the FPGA streams
  fixed-size packets: sync + `00` + 2048 bytes of little-endian `int16` I/Q pairs +
  CRC (2076 bytes, ~127 packets/s), plus `5A`-typed info packets.
* The FPGA image (336,758 bytes, header `AA 99 55 66`) is bit-banged in through
  D0/D2/D7 (DIN/CCLK/PROG_B). The vendor's loader works; ours currently
  half-configures the chip (see "Bit-bang loader status"), so after each power-up
  one vendor session is needed to configure the FPGA.

## Origin and status

The USB protocol implemented here was reconstructed from the vendor's ExtIO driver,
`ExtIO_Osa.dll` v3.09 (PE32, compiled 2022-05-16, md5
`bcab0dfd74186c2b99b8b2b68ccbc57c`), the Windows driver for the OSA103 / OSA103 Mini
published by the OSA103 project at http://www.osa103.ru, and then confirmed against
a live device (Windows 10 x64, OSA103, FT232R, serial `AH1STN9C`, FTDI driver
2.12.36.20). The vendor's own FTDI traffic was recorded with a logging `ftd2xx.dll`
proxy placed between HDSDR and the vendor DLL; every captured write was replayed
and diffed byte-for-byte against this module's builder until the diff was empty.

Validation level per layer:

* Config frame encoder: byte-exact against the captured vendor frame, 78/78 bytes
  including the CRC (`tests/vendor_config_9108500.h` + `tests/frame_test.cpp`).
* Ramp tables: same length/address/CRC layout, algorithmically generated ramps
  matching the captured structure (see "Ramp tables").
* Open sequence: the exact captured `FT_*` call order, pinned by `tests/d2xx_test.cpp`.
* Live stream: 765/766 data packets in a 6-second run, **0 CRC errors**, 391,680 I/Q
  pairs, measured 65.165–65.308 kS/s against the declared 65,104 S/s (0.1–0.3%).

Only protocol facts live in this tree — command type bytes, field offsets, constant
words, formulas. No vendor code, decompiled listing, binary or FPGA bitstream is
included or redistributed; if the FPGA image is needed it has to be extracted by the
user from their own copy of the driver. Nothing here is affiliated with or endorsed
by the vendor.

### How this was produced: static analysis plus live testing

Two methods were used together, and neither was trusted alone.

* **Static analysis.** `ExtIO_Osa.dll` v3.09 was read as an artifact, not as
  source: PE headers and sections, imports, strings, the FTDI call sequence, the
  embedded image header, the 32-bit calling convention of the exports. That gives
  hypotheses about what the driver sends and in which order.
* **Live testing.** Every hypothesis was written to a real OSA103 and the reply was
  captured through the FTDI MITM proxy. Predicted bytes and captured bytes were
  then diffed. A hypothesis the device rejected was corrected or dropped; nothing
  in this document rests on the binary alone.

The protocol layer, the module and the tests were written with the AI models
DeepSeek 4.1 Flash and GLM-5.3 working over those two evidence streams — static
artifacts on one side, captured frames and live run logs on the other. Where a live
run contradicted the code, the code lost. This is also where the honesty has to
start: machine-written protocol code reads plausible even when it is wrong, so each
claim here is pinned to a test or to a captured byte, and every place the method
failed is written down instead of hidden — the bit-bang loader still does not
configure the FPGA, and "Remaining unknowns" lists what is still guesswork.

## Licence

Non-commercial / educational use only, with no warranty and no liability; commercial
use needs written permission. Full terms, provenance and the third-party notes
(FTDI D2XX, and why this plugin stays out of the SDR++ tree despite being GPL-linked)
are in `LICENSE.md` — read it before redistributing anything.

## Copyright and support status

All rights in the OSA103 / OSA103 Mini hardware, its firmware, its FPGA bitstream,
the vendor `ExtIO_Osa.dll` and the vendor's documentation stay with the OSA103
project and its authors. None of their code, binary or bitstream is copied here.
This driver claims no copyright over the device, its firmware, its bitstream or the
vendor's software, and it grants you no rights in them. What is written here is an
independent implementation of observed behaviour; the source in this tree is
covered only by `LICENSE.md`.

This module is unofficial, unsupported and, in places, broken. It is not affiliated
with, endorsed by, or supported by the OSA103 project or its authors. Expect it to
fail: the FPGA bit-bang loader does not configure the chip, and "Remaining
unknowns" lists behaviour that is still guesswork. Use it at your own risk.
If you need a supported driver, use the vendor's ExtIO with the vendor's host
software.

## Dependencies

Build time: CMake ≥ 3.13, a C++17 compiler, and an SDR++ tree (`sdrpp_core`). There
are **no FTDI headers and no link-time FTDI library** — D2XX is resolved with
`LoadLibrary`/`GetProcAddress` (`dlopen` elsewhere) at runtime, exactly the way the
vendor DLL does it. A missing driver is a runtime condition: the module shows a red
error in the source panel and refuses to start, it never crashes the host.

Runtime: the FTDI D2XX library only.

* Windows: `ftd2xx.dll` from the FTDI CDM/D2XX driver package, on the default DLL
  search path (`System32` for 64-bit hosts, `SysWOW64` for 32-bit ones — both exist
  side by side on x64 Windows; this module binds whichever matches its process
  bitness). Note that 32-bit D2XX exports are **stdcall**, which the binding honours
  — calling them as cdecl corrupts the stack (that was a real crash, see
  `src/ftdi_d2xx.h`).
* macOS: `libftd2xx.dylib` from FTDI's D2XX dmg, in `/usr/local/lib`,
  `/opt/homebrew/lib`, or wherever the "D2XX library" field points.
* Linux: `libftd2xx.so` from FTDI's D2XX tarball, plus udev rules. The kernel's
  `ftdi_sio` driver must not claim the device — `rmmod ftdi_sio` (or a udev rule
  that unbinds it), otherwise D2XX reports `FT_DEVICE_NOT_FOUND`.

Optional: an FPGA bitstream file. The image is embedded in `ExtIO_Osa.dll`
(length at file offset `0x92C70`, u32 LE `0x52376` = 336,758; data at `0x92C84`,
first bytes `AA 99 55 66`). Extract it and point "Bitstream file" at it if the
device does not come up configured — but read "Bit-bang loader status" first,
because in practice the vendor's own loader is the reliable one.

```powershell
$b = [IO.File]::ReadAllBytes('C:\ExtIO_Osa.dll')
$len = [BitConverter]::ToUInt32($b, 0x92C70)
$img = New-Object byte[] $len
[Array]::Copy($b, 0x92C84, $img, 0, $len)
[IO.File]::WriteAllBytes('osa103_fpga.bin', $img)
```

## Build

```sh
cmake -S . -B build -G Ninja -DOPT_BUILD_OSA103_SOURCE=ON
ninja -C build osa103_source
```

The option is off by default on Android: D2XX does not exist there (FTDI ships a Java
USB driver instead), and the module is not in Android's hardcoded module list either.

## Offline tests

`tests/run_tests.sh` compiles the protocol layer and the D2XX binding standalone —
no SDR++ build, no device:

* `frame_test.cpp` pins the encoder to hardware truth: sync, both frame types,
  NCO/LO fields, all eight ramp tables, the image override — and a byte-for-byte
  regression against a real captured vendor configuration frame
  (`vendor_config_9108500.h`). It also drives the receive parser with a synthetic
  packet stream in awkward chunk sizes, in both inbound layouts, including
  corrupt-CRC rejection.
* `d2xx_test.cpp` drives the runtime binding against `stub_d2xx.c`, verifying the
  device-info struct layout, symbol resolution (including the captured
  `FT_SetFlowControl`/`FT_SetRts` open sequence), and that a configuration frame
  reaches `FT_Write` as 25 + 78 bytes.

On Windows (MinGW, no `sh`) the same two tests build like this; the stub needs an
import library because `d2xx_test` calls the fake entry points directly:

```powershell
g++ -std=c++17 -O1 -I../src frame_test.cpp ../src/osa103_proto.cpp -o frame_test.exe
gcc -shared -o ftd2xx_stub.dll stub_d2xx.c '-Wl,--out-implib,libftd2xx_stub.a'
g++ -std=c++17 -O1 -I../src d2xx_test.cpp ../src/osa103_proto.cpp ../src/ftdi_d2xx.cpp `
    libftd2xx_stub.a -o d2xx_test.exe
.\frame_test.exe
.\d2xx_test.exe .\ftd2xx_stub.dll
```

## Stream test tool

`tools/osa103_stream_test.cpp` is the module without SDR++: same D2XX binding, same
`Params`, same `Parser`, same start/stop/read-thread shape, but a console entry point
and no GUI. It brought the device up against real hardware before the module ran,
and it is the fastest way to check a transport change:

```
g++ -std=c++17 -O2 -Isrc tools/osa103_stream_test.cpp src/osa103_proto.cpp `
    src/ftdi_d2xx.cpp -o osa103_stream_test.exe
```

```
osa103_stream_test --list
osa103_stream_test --serial AH1STN9C --freq 9108500 --seconds 5 --dump iq.raw
osa103_stream_test --dump-config --freq 9108500    # print the frame, diff vs capture
osa103_stream_test --no-tables --dump-rx rx.raw --log
```

Options: `--list`, `--serial`, `--freq`, `--seconds` (0 = until Enter), `--rate`,
`--dump` (float32 I/Q), `--dump-rx` (raw byte stream), `--d2xx <path>`,
`--bitstream <file>`, `--sniff`, `--no-hello`, `--no-config`, `--no-tables`,
`--dump-config`, `--log`. It reports the same counters the module's stats panel shows
and exits non-zero if it started but no bytes came back — usable as a smoke test.

A working run against the live device looks like this (6 seconds, dial 9.1085 MHz):

```
data packets   : 765      CRC errors : 0
I/Q samples    : 391680   sample rate: 65.165 kS/s (declared 65104)
```

## Protocol reference

Everything in this section was confirmed on the wire unless stated otherwise.

### FTDI bring-up (captured vendor sequence, validated live)

```
FT_OpenEx(serial, FT_OPEN_BY_SERIAL_NUMBER)
FT_SetBaudRate(3000000)
FT_SetDataCharacteristics(8, 1 stop bit, no parity)
FT_SetFlowControl(FT_FLOW_RTS_CTS, Xon=0, Xoff=0)   <- 0x0100
FT_SetRts()
FT_SetTimeouts(20, 20)
FT_SetLatencyTimer(2)
FT_Purge(FT_PURGE_RX | FT_PURGE_TX)
FT_StopInTask() ; FT_Purge(FT_PURGE_RX) ; FT_RestartInTask()
FT_Write(A5 00 00)                                  <- hello, unframed
FT_Write(sync 25) ; FT_Write(config 78)             <- type 0x03
FT_Write(sync 25) ; FT_Write(table 1033) × 8        <- type 0x01
... read loop: FT_GetQueueStatus-driven reads of up to 64 KiB
```

The flow-control + RTS pair and the Stop/Purge/Restart dance are both required:
without them the first packets still carry bit-bang echo garbage and the FPGA stays
silent. The vendor DLL exits bit-bang mode with `FT_SetBitMode(0x05, 1)` then
`(0x05, 0)` before this sequence when it has just loaded the FPGA image.

### Host frame format

25-byte sync, written as its own `FT_Write`, then one transfer with the rest:
`u16 len` (= 1 + payload length), `u8 type`, payload, CRC-16. The CRC is the
reflected `0xA001` polynomial (CRC-16/MODBUS parameters), init `0xFFFF`, low byte
first, computed over `len + type + payload`; a whole valid frame has CRC residue 0.

### Type 0x03 — configuration frame (78 bytes)

`len = 74`, then a 73-byte register image. Offsets below are image offsets
(`image[n]` = frame byte `3 + n`):

| offset | captured | meaning |
|---|---|---|
| 0–3 | `18 00 08 FF` | constant |
| 4–7 | `5C AC A8 0B` | **NCO-A** = round(dial_Hz · 2³¹ / 10⁸), LE. 100 MHz → exactly `0x80000000` |
| 8–12 | `14 FC 8A 00 00` | **LO** = dial_Hz, u32 LE, plus a 5th high byte (0 below 4 GHz) |
| 13 | `80` | Rx/Tx tag; `|0x01` Tx-IF, `|0x04` Tx path (only `0x80` seen for Rx) |
| 14–19 | `03 07 00 00 00 00` | divider group 1 |
| 20–25 | `03 07 00 00 00 00` | divider group 2 |
| 26–27 | `58 62` | clock constant `0x6258` (Mini); `0x0028` on the F variant |
| 28–31 | `1F 00 40 FF` | constant |
| 32 | `39` | **front-end flags**: bit0 preamp, bit1 50 Ω input, bit2 ADC dither, bit4 always set |
| 33–35 | zeros | |
| 36–37 | `00 00` | ADC gain word (vendor session sends zero) |
| 38–45 | zeros | |
| 46–51 | `54 76 95 97 23 DD` | calibration constants (device-specific; edit if your unit differs) |
| 52–55 | zeros | |
| 56 | `82` | constant |
| 57–58 | `01 AD` | constant |
| 59–60 | zeros | |
| 61 | `C3` | constant |
| 62–66 | `00 06 00 80 FF` | constant |
| 67 | `1D` | mode |
| 68–71 | `17 B7 D1 05` | **fine NCO** = round((dial_Hz − 16000) · 2³¹ / (2·10⁸)), LE |
| 72 | `13` | Tx level (unused for receive) |

Example: dial 9,108,500 Hz → NCO-A `0x0BA8AC5C` = 195,603,548, LO bytes
`14 FC 8A 00 00`, fine NCO `0x05D1B717` = 97,629,975 = (9,108,500 − 16,000) ·
2³¹/2·10⁸. The builder reproduces this frame byte-for-byte; the −16 kHz fine-NCO
offset is exact in the vendor's initial frame (retunes carry an extra vendor phase
trim that the device does not need from us).

### Type 0x01 — ramp tables (eight frames, 1033 bytes each)

`len = 1029`; payload = `00 04`, u16 address, then 512 twelve-bit words (LE, high
nibble masked). Addresses run `0xC000 + block·0x400`, blocks 0–7, sent right after
the config frame. The words are ramps, generated in `buildTableFrame`:

* blocks 0–3: `0x0555 + i`, `0x0754 + i`, `0x0953 + i`, `0x0B52 + i` for `i = 0..511`
* block 4: all zeros
* block 5: 169 zeros, then `i − 169` (0..342)
* blocks 6–7: `0x0157 + i`, `0x0356 + i` (closing at 0x0555)

The vendor computes these from the sample rate; the synthesis turned out to be this
fixed ramp set at the only supported rate. **The stream does not start unless the
tables follow the config frame** — live-confirmed: with `--no-tables` the device
answers the hello but never streams.

### Inbound packets (captured, live-validated)

Every inbound packet repeats the same 25-byte sync, then a type byte:

* Type `5A` — info packet, fixed 151 bytes after the sync: `5A` + 148 bytes + CRC
  (176 total). First body byte is the subtype: `02` = device ready (followed by
  firmware 3.05-style version bytes), `03` = configuration accepted.
* Type `00` — sample packet: 2048 bytes of little-endian `int16` I/Q pairs plus
  CRC (2076 total). At 65,104 S/s that is 512 pairs per ~32 ms packet,
  ~127 packets/s. **No flags byte, no length field** — the reader is fixed-size.

Parser defaults match this exactly (`dataBytes=2048`, `infoPacketBytes=151`,
`hasFlagsByte=false`, `rxHasLenField=false`); the knobs stay in the UI for
experimentation.

### Sample stream truth

* 2048 payload bytes = 512 interleaved `int16` pairs, little-endian, I first.
* Measured 65.165–65.308 kS/s against the declared 65,104 — the rate is real and
  fixed. `ExtIoSetSrate` has no effect on hardware; the sample-rate box only
  affects SDR++'s DSP.
* Scale: this module normalizes `int16 → float` by 1/32768 (SDR++ convention, full
  ADC scale = ±1.0). The vendor DLL instead multiplies by a fixed 2.5322 with no
  normalization; "Sample scale" reproduces its absolute levels if you need them.
* Endianness is little; "Swap I/Q" and "Invert Q" are available if a variant turns
  out to need them.

### What the hardware exposes (and does not)

There are **no RF gain or bandwidth controls**. The only things the protocol can
change are the dial frequency (NCO-A / LO / fine NCO above) and three front-end
toggles in the flags byte: preamp (bit0), 50 Ω input (bit1), ADC dither (bit2).
Bandwidth is fixed at the one supported sample rate (~65 kHz channel), and there is
no amplifier stage to control — the vendor's own panel offers exactly the same set.
The module therefore exposes no gain/bandwidth knobs; the "Front end" section only
carries the raw flags byte for experimentation.

## Live validation log

Recorded 2026-11 on Windows 10 x64, OSA103 (FT232R, serial `AH1STN9C`,
`FT_DEVICE_600`, PnP `USB\VID_0403&PID_6001`), FTDI driver 2.12.36.20, D2XX from
`C:\Windows\System32\ftd2xx.dll` (64-bit) and `C:\Windows\SysWOW64\ftd2xx.dll`
(32-bit).

What works, end to end with this module's code only:

* D2XX discovery, open, and the full captured `FT_*` bring-up sequence including
  RTS/CTS flow control (live through both the 64-bit and the 32-bit DLL; the 32-bit
  path required fixing the binding to stdcall — a cdecl/stdcall mismatch corrupts
  the stack on the third call).
* Hello answered: 176-byte `5A` info packet, subtype 02, firmware 3.05-style.
* Byte-exact type `0x03` config + eight ramp tables accepted (`5A` subtype 03
  replies observed).
* **Full sample stream**: 765/766 data packets in 6 s, 0 CRC errors, 391,680 I/Q
  pairs, measured 65.165–65.308 kS/s vs 65,104 declared, peak amplitudes consistent
  with a live antenna signal.
* Clean stop: read thread joins, `FT_Close` succeeds, no leaked handle.
* Both offline suites pass on Windows/MinGW and pin the byte layout to the capture.

How it was obtained (the interesting part):

* A logging `ftd2xx` proxy (`tools/extio_capture/`) was written and placed between
  HDSDR and the vendor's 32-bit Delphi ExtIO. Getting the vendor DLL to load the
  proxy at all required: exporting both `ftd2xx.dll` and the vendor-patched name
  `ftdpxx.dll` (one byte changed in the vendor DLL at file offset `0xE529F`,
  `"ftd2xx.dll"` → `"ftdpxx.dll"`), stdcall exports linked with `-Wl,--kill-at`, an
  FTDI-matching VERSIONINFO resource, and an i386 `libwinpthread-1.dll` beside the
  proxy (a PATH-visible x64 copy silently fails `LoadLibrary` — the vendor falls
  back to the real driver and you capture nothing).
* The vendor ExtIO must be picked through HDSDR's **Options → Select EXTIO Unit…**
  menu; the startup auto-load path only probes and never calls `InitHW`, so no FTDI
  traffic happens until the menu selection.
* The captured config frame was diffed against this module's builder until the diff
  reached 0/78 bytes (see `tests/`); the inbound formats were decoded from a raw
  `--dump-rx` capture and pinned in the parser + tests.

## Bit-bang loader status

The vendor DLL configures the FPGA on every open by bit-banging the image through
async mode (`FT_SetBitMode(0x85, 1)`; DIN on D0, CCLK on D2, PROG_B on D7, one byte
per level change). The module implements the same waveform and writes the same
bytes, but **the USB write pacing differs enough that the chip comes up
half-configured**: the host then reads a ~4 KiB `F6`/`F7` echo artifact and the FPGA
stays silent. Until the pacing is matched (likely the vendor writes the bit-bang
stream in smaller chunks with sleeps), the practical bring-up after each power-up
is: one HDSDR session with the vendor ExtIO (it configures the FPGA and the chip
stays configured while USB-powered), close HDSDR, then use this module — which then
streams perfectly. The "Load FPGA bitstream" option is off by default for exactly
this reason.

## Configuration

Every protocol parameter has a hardware-validated default, and every one of them is
persisted in `<root>/osa103_source_config.json` (seeded with the defaults on first
run) as well as editable in the module's UI, grouped into Tuning, Front end,
Samples, Protocol and Advanced. Integers are stored as decimal, so flags `0x39`
appear as 57 and the clock const `0x6258` as 25176; the UI shows hex for the tag and
word fields. An `imageOverrideHex` string, if it parses to exactly 73 bytes, replaces
the generated register image byte for byte — the shortest path to trying a captured
or hand-edited image without touching code.

Note for upgraders: configs written by pre-validation versions carry stale defaults
(flags `0x19`, mode `0x18`, "Rx flags byte" on). Delete `osa103_source_config.json`
once and let the module reseed it.

## Remaining unknowns

Settled by hardware (no longer knobs you need to touch): inbound framing, the
ramp-table requirement, the open sequence, flags/mode values, both NCO formulas, LO
units, sample endianness, packet sizes, the real sample rate.

Still open, all still editable in the UI/config:

* The constant tag groups (`18 00 08 FF`, `1F 00 40 FF`, `C3 00 06 00 80 FF`,
  `82`, `01 AD`) and the calibration sextet `54 76 95 97 23 DD` are byte-fixed in
  every capture; their semantics are unknown, and the calibration bytes may be
  device-specific.
* The clock constant `0x6258` (Mini) vs `0x0028` (F) — which model selects which
  is unconfirmed.
* The divider groups' meaning, and whether any rate other than 65,104 S/s can be
  produced by changing them (the vendor only ever advertises one rate).
* The `0x5A` info packet's 148-byte body beyond the subtype and version bytes.
* Injection side (high/low) of the LO — the synthesizer is programmed by the FPGA
  over SPI from the LO word; the achieved frequency tracks the dial exactly at the
  tested frequencies.
