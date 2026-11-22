# ExtIO capture harness

Purpose: record what the vendor's `ExtIO_Osa.dll` actually sends to an OSA103, so the
protocol knobs in this module can be settled (bit-bang sequence, register image, the
type `0x01` table) against ground truth instead of static analysis.

The vendor DLL has no FTDI import table — it loads `ftd2xx.dll` by name at runtime and
resolves `FT_*` with `GetProcAddress`. That makes it interceptable without touching
either the vendor DLL or FTDI's driver: put a logging `ftd2xx.dll` next to the host
process, let it forward to the real `C:\Windows\System32\ftd2xx.dll`, and record every
call and every `FT_Write` payload.

Two pieces live here:

* `ftd2xx_proxy.cpp` — the logging shim. Implements the D2XX surface the vendor DLL
  asks for (its strings list `FT_ListDevices`, `FT_GetModemStatus`, `FT_CyclePort`, …
  in addition to the usual suspects), forwards to the real library, and appends a
  trace to the file named by `OSA_PROXY_LOG`. Every `FT_Write` logs its length and the
  first 160 bytes, so the bit-bang stream, the hello and every frame are captured.
* `extio_host.cpp` — a headless ExtIO host: `InitHW` → `OpenHW` → `SetCallback` →
  `StartHW` → message pump → `StopHW` → `CloseHW`, printing whether the callback ever
  delivers samples.

## Bitness

Every `ExtIO_Osa.dll` on this machine (v3.09, md5 `bcab0dfd…bc57c`) is **32-bit PE**,
so both the shim and the host must be built i686. A 64-bit MinGW cannot load it
(`LoadLibrary` fails with `ERROR_BAD_EXE_FORMAT`, 193). An i686 toolchain is enough,
for example WinLibs MinGW-w64 i686 (unpack once, no installer):

Build with the i686 toolchain's `bin` **prepended to PATH** and plain `g++` /
`windres` invoked by name. Invoking the i686 `g++.exe` by full path while an x64
toolchain is also on PATH has produced broken links here: a PE whose header still
says i386 but which the 32-bit loader rejects with `ERROR_BAD_EXE_FORMAT` (193).

```
windres ftd2xx_version.rc -O coff -o ftd2xx_version.o
g++ -shared -O2 -o ftd2xx.dll ftd2xx_proxy.cpp ftd2xx_version.o -static-libgcc -static-libstdc++ -Wl,--kill-at
g++ -shared -O2 -o ftdpxx.dll ftd2xx_proxy.cpp ftd2xx_version.o -static-libgcc -static-libstdc++ -Wl,--kill-at
g++ -O2 -o extio_host.exe extio_host.cpp -static
```

Calling convention and export names are equally non-negotiable. The proxy's `FT_*`
entry points are `__attribute__((stdcall))` (already in `ftd2xx_proxy.cpp` — real
32-bit `ftd2xx.dll` exports are stdcall, and a cdecl shim called from the vendor
corrupts the stack), and the link **must** pass `-Wl,--kill-at`, otherwise MinGW
exports them as `FT_Write@16` and friends and the vendor's `GetProcAddress` returns
NULL for every function. That failure mode is total silence: the vendor treats the
D2XX library as broken, HDSDR keeps running on its previous input, and there is no
dialog and no crash — the capture just contains nothing, which looks exactly like
"the proxy was never loaded".

Deployment runtime dependency with the same silent-failure signature: the i686
proxy links `libwinpthread-1.dll` (i386), and that DLL must sit **next to the host
exe**. If the loader resolves an x64 `libwinpthread-1.dll` first (for example from
a MinGW installation on PATH), `LoadLibrary` of the proxy fails with
`ERROR_BAD_EXE_FORMAT` (193), the vendor falls back to the real `ftd2xx.dll`, and
the log never shows the `proxy attached` line.

`ftd2xx_version.rc` is **not optional**. `ExtIO_Osa.dll` statically imports
VERSION.DLL (`GetFileVersionInfoSizeA` / `GetFileVersionInfoA` / `VerQueryValueA`)
and validates the loaded D2XX library's file version immediately after
`FT_CreateDeviceInfoList`. A shim without a version resource fails that check and
the vendor aborts with a "driver files missing" style dialog and unloads — with
the shim gone, the host falls back to its previously configured input, which looks
like the ExtIO "ran" but silently captured nothing. The resource mirrors the real
driver on this machine (FileVersion 3.2.21.1, CDM package 2.12.36.20) so the check
passes; both `040904E4` and `040904B0` string blocks are provided for either
hardcoded `VerQueryValue` lookup.

Run from a directory holding a copy of `ExtIO_Osa.dll`:

```
set OSA_PROXY_LOG=proxy.log
extio_host.exe ExtIO_Osa.dll 5 100e6
```

## What the capture showed on 2026-11 (Windows 10, real OSA103 attached)

* The vendor DLL does resolve `ftd2xx.dll` from the host process' directory, so the
  shim is loaded (verified: `GetModuleFileName` returns the shim, the log gets the
  attach line, `FT_CreateDeviceInfoList` is called and sees the device, count 1).
* `InitHW` then crashes with an access violation inside the DLL's own
  `ExtIO_Osa!@@Main_thread@Finalize` frame — it is a Delphi/VCL binary and its form
  initialization does not survive a non-Delphi console host. The same crash happens
  with a message pump running and with `ShowGUI(true)`.
* Consequence: this headless host cannot complete the capture. The remaining route is
  to run the shim from a real Delphi host — HDSDR (which is on this machine) or any
  other ExtIO-capable Delphi SDR — by placing `ftd2xx.dll` (the shim) into the host's
  executable directory, selecting `ExtIO_Osa.dll`, starting the receiver, and reading
  `proxy.log` afterwards. The shim is passive and forwards everything, so the host
  behaves exactly as with the real driver.

The log line format is `CALL <name>` at entry, one result line per call, and
`FT_Write(len=N) -> 0, sent N: <hex>` for every write, capped at 160 bytes per line —
the bit-bang stream will produce many lines; the interesting ones are the short writes
after it (hello, type `0x03` frame, type `0x01` blocks) and the first `FT_Read` payloads.

## MITM via a patched DLL name

Placing the shim next to a host exe only works when the host runs from a writable
directory. To capture from *any* host, patch the vendor binary itself: in
`ExtIO_Osa.dll` v3.09 (md5 `bcab0dfd…`) at file offset `0xE529F` sits the
null-terminated Windows load name `"ftd2xx.dll"` (directly after its Linux sibling
`"lin_ftd2xx.dll"` at `0xE528D`). Rewriting it same-length to `"ftdpxx.dll"` is a
one-byte patch (`'2'` 0x32 → `'p'` 0x70); the patched DLL then LoadLibrary()es the
shim by its own name. Deployment that worked against the real device:

* copy the vendor DLL, apply the byte patch, name it distinctly (e.g.
  `ExtIO_Osa_MITM.dll`) so it cannot be confused with the original in a file picker;
* build the shim with the version resource above and deploy it as **both**
  `ftd2xx.dll` and `ftdpxx.dll` in the same directory as the host exe and the
  patched ExtIO — the host's exe dir is the first LoadLibrary search step, so the
  interception no longer depends on the process working directory;
* launch the host with that directory as working directory anyway, so the vendor's
  name-based `GetFileVersionInfoA("ftdpxx.dll")` also finds the shim as a data file.

The interception itself is verified end-to-end on this machine: the shim attaches,
the vendor resolves `FT_CreateDeviceInfoList`, sees the device (count 1), passes its
VERSION.DLL driver check, and every call is forwarded to the real
`C:\Windows\SysWOW64\ftd2xx.dll`, so the host behaves exactly as with the unmodified
driver.

Three Windows notes for whoever reruns this:

* HDSDR stores its configuration (including `ExtIOdllname`, the full path of the
  selected ExtIO) in `HKCU\Software\HDSDR`, so a copied HDSDR inherits the original's
  configuration and auto-loads the last-selected ExtIO. That auto-load path only
  probes the DLL — **no FTDI traffic happens**. The vendor's bring-up (bit-bang,
  hello, config frame, tables) runs when the ExtIO is selected through
  **Options → Select EXTIO Unit…** — not on auto-load, not on stop/start. Wait for
  the waterfall to come alive (~5 s FPGA load), then close HDSDR.
* A console host cannot complete the capture regardless of shims — the vendor DLL is
  a 32-bit Delphi/VCL binary and dies in `InitHW` outside a Delphi host, so run the
  capture from HDSDR itself.
* Working deployment layout, all in the writable HDSDR copy's directory:
  `ExtIO_Osa_MITM.dll` (the byte-patched vendor DLL), `ftdpxx.dll` (the proxy),
  `ftd2xx.dll` (the same proxy under its second name), `libwinpthread-1.dll` (i386),
  and `HDSDR.exe` itself.

## What the successful capture produced (2026-11)

Two full vendor sessions against the live device (the second is preserved as
`capture_run2.log`) yielded every fact this module was validated against:

* the complete `FT_*` bring-up order, including RTS/CTS flow control and the
  `StopInTask → Purge(RX) → RestartInTask` handshake;
* the initial 78-byte type `0x03` configuration frame — now the byte-exact
  regression reference `tests/vendor_config_9108500.h`; diffing it against this
  module's builder until 0/78 differences is how the flags byte `0x39`, mode
  `0x1D`, the calibration sextet, the `82 01 AD` constants and the −16 kHz
  fine-NCO offset were all pinned;
* the full bit-bang FPGA load stream (mask and level bytes) — reproduced in the
  module's loader, with the USB pacing caveat documented in the main README;
* the eight type `0x01` ramp blocks, from which the generated structure in
  `buildTableFrame` was derived;
* enough `FT_Read` traffic to decode both inbound packet formats (176-byte `5A`
  info packets, 2076-byte sample packets), confirmed afterwards by live runs of
  the module's own stream tool with zero CRC errors.
