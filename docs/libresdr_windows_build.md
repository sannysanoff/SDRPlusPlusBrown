# LibreSDR Windows Build Notes

This document records the Windows build and packaging process that was used for the LibreSDR-oriented SDR++ Brown branch in this workspace.

It is written as a practical, repeatable setup for another machine.

## Goal

This branch targets LibreSDR boards that expose a PlutoSDR-compatible IIO topology and are used with the Pluto source module inside SDR++ Brown.

The key LibreSDR-specific goals are:

* detect LibreSDR even when Windows `iio_scan_context` does not return the board
* connect directly by `ip:` URI when scan-based discovery is unreliable
* support `CS8` and `CS16` IQ modes
* support dynamic sample-rate changes without restarting SDR++
* package a runnable Windows build with the required runtime DLLs

For 8-bit wideband firmware, use [`F5OEO/tezuka_fw`](https://github.com/F5OEO/tezuka_fw).

## Tested Environment

The build in this workspace was produced on Windows with:

* Visual Studio 2022 / MSBuild 17.x
* CMake with the `Visual Studio 17 2022` generator
* `vcpkg` toolchain at `C:/vcpkg/scripts/buildsystems/vcpkg.cmake`
* `vcpkg` triplet `x64-windows`
* PothosSDR installed under `C:/Program Files/PothosSDR`

## Required Software

Install these first:

1. Visual Studio 2022 with the Desktop C++ workload.
2. CMake.
3. Git.
4. `vcpkg`.
5. PothosSDR for `libiio`, `libad9361`, `libusb`, `pthread`, and several SDR hardware DLLs.

Optional hardware vendor packages:

* SDRplay API if you build the SDRplay source module
* codec2 runtime if you use modules that require `libcodec2.dll`
* RFNM and HydraSDR vendor runtimes only if you enable those modules

## vcpkg Packages

The configure cache in this workspace resolved these packages from `vcpkg`:

* `fftw3`
* `fftw3f`
* `glfw3`
* `itpp`
* `rtaudio`
* `portaudio`
* `zstd`

Example install command:

```powershell
vcpkg install fftw3 fftw3f glfw3 itpp rtaudio portaudio zstd --triplet x64-windows
```

This branch also packages these runtime DLLs from `vcpkg` when present:

* `itpp.dll`
* `rtaudio.dll`
* `portaudio.dll`
* `zstd.dll`

## DLL Sources Used On This Machine

The working Windows package on this machine collected runtime DLLs from these locations:

From `C:/vcpkg/installed/x64-windows/bin`:

* `itpp.dll`
* `rtaudio.dll`
* `portaudio.dll`
* `zstd.dll`

From `C:/Program Files/PothosSDR/bin`:

* `libiio.dll`
* `libad9361.dll`
* `libusb-1.0.dll`
* `pthreadVC2.dll`
* `volk.dll`
* `airspy.dll`
* `airspyhf.dll`
* `bladeRF.dll`
* `hackrf.dll`
* `LimeSuite.dll`
* `rtlsdr.dll`
* `sdrplay_api.dll`

Optional additional locations used by the packaging script if present:

* `C:/Program Files/codec2/lib/libcodec2.dll`
* `C:/Program Files/SDRplay/API/x64/sdrplay_api.dll`
* `C:/Program Files/RFNM/bin/*.dll`
* `C:/Program Files (x86)/hydrasdr_all/bin/hydrasdr.dll`

## Configure

The build directory used here is `build-win`.

Configure with:

```powershell
cmake -S . -B build-win `
  -G "Visual Studio 17 2022" `
  -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
```

The configure cache for this workspace used `x64-windows` and produced a `RelWithDebInfo` build.

## Enabled Modules In The Working Build

These options were enabled in the working `build-win/CMakeCache.txt`:

* `OPT_BUILD_AIRSPYHF_SOURCE=ON`
* `OPT_BUILD_AIRSPY_SOURCE=ON`
* `OPT_BUILD_ATV_DECODER=ON`
* `OPT_BUILD_AUDIO_SINK=ON`
* `OPT_BUILD_AUDIO_SOURCE=ON`
* `OPT_BUILD_CH_EXTRAVHF_DECODER=ON`
* `OPT_BUILD_CH_TETRA_DEMODULATOR=ON`
* `OPT_BUILD_DISCORD_PRESENCE=ON`
* `OPT_BUILD_FILE_SOURCE=ON`
* `OPT_BUILD_FREQUENCY_MANAGER=ON`
* `OPT_BUILD_FT8_DECODER=ON`
* `OPT_BUILD_HACKRF_SOURCE=ON`
* `OPT_BUILD_HERMES_SOURCE=ON`
* `OPT_BUILD_HL2_SOURCE=ON`
* `OPT_BUILD_IQ_EXPORTER=ON`
* `OPT_BUILD_KIWISDR_SOURCE=ON`
* `OPT_BUILD_METEOR_DEMODULATOR=ON`
* `OPT_BUILD_MPEG_ADTS_SINK=ON`
* `OPT_BUILD_NETWORK_SINK=ON`
* `OPT_BUILD_NETWORK_SOURCE=ON`
* `OPT_BUILD_NEW_PORTAUDIO_SINK=ON`
* `OPT_BUILD_NOISE_REDUCTION_LOGMMSE=ON`
* `OPT_BUILD_PAGER_DECODER=ON`
* `OPT_BUILD_PLUTOSDR_SOURCE=ON`
* `OPT_BUILD_RADIO=ON`
* `OPT_BUILD_RECORDER=ON`
* `OPT_BUILD_RFSPACE_SOURCE=ON`
* `OPT_BUILD_RIGCTL_CLIENT=ON`
* `OPT_BUILD_RIGCTL_SERVER=ON`
* `OPT_BUILD_RTL_SDR_SOURCE=ON`
* `OPT_BUILD_RTL_TCP_SOURCE=ON`
* `OPT_BUILD_SCANNER=ON`
* `OPT_BUILD_SDRPP_SERVER_SOURCE=ON`
* `OPT_BUILD_SPECTRAN_HTTP_SOURCE=ON`
* `OPT_BUILD_SPYSERVER_SOURCE=ON`
* `OPT_BUILD_TCI_SERVER=ON`

These remained disabled in the tested build:

* `OPT_BUILD_BLADERF_SOURCE=OFF`
* `OPT_BUILD_FOBOSSDR_SOURCE=OFF`
* `OPT_BUILD_HYDRASDR_SOURCE=OFF`
* `OPT_BUILD_LIMESDR_SOURCE=OFF`
* `OPT_BUILD_M17_DECODER=OFF`
* `OPT_BUILD_PERSEUS_SOURCE=OFF`
* `OPT_BUILD_RFNM_SOURCE=OFF`
* `OPT_BUILD_SDRPLAY_SOURCE=OFF`
* `OPT_BUILD_SOAPY_SOURCE=OFF`
* `OPT_BUILD_USRP_SOURCE=OFF`

Adjust these as needed on another machine.

## Build

Build with:

```powershell
cmake --build build-win --config RelWithDebInfo --parallel
```

If you only want to rebuild the LibreSDR-related path while iterating:

```powershell
cmake --build build-win --config RelWithDebInfo --target plutosdr_source sdrpp_core sdrpp --parallel
```

Expected main outputs:

* `build-win/RelWithDebInfo/sdrpp.exe`
* `build-win/core/RelWithDebInfo/sdrpp_core.dll`
* `build-win/source_modules/plutosdr_source/RelWithDebInfo/plutosdr_source.dll`

## Package

The Windows packaging script for this branch is:

* `make_windows_package.ps1`

Run it from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\make_windows_package.ps1 .\build-win .\root
```

Expected output:

* `sdrpp_windows_x64.zip`

The script:

* copies the runtime root from `root/`
* copies `sdrpp.exe` and DLLs from `build-win/RelWithDebInfo`
* copies `sdrpp_core.dll` from `build-win/core/RelWithDebInfo`
* walks the built module directories and copies the actual module DLLs that exist
* adds runtime DLLs from `vcpkg`, PothosSDR, and optional vendor SDK locations when present

## LibreSDR Notes

This branch includes LibreSDR-specific Pluto source changes:

* direct probing of `ip:192.168.2.1` and `ip:libresdr.local` when scan discovery fails
* capability-based acceptance of Pluto/LibreSDR-compatible IIO contexts
* normalized network URI selection using the board IP when available
* migration of per-device config when the same board appears under a different URI string
* `CS16` / `CS8` IQ selection in the Pluto source menu
* runtime sample-rate switching through stop/start/tune sequencing

For firmware-side 8-bit support, pair this branch with `tezuka_fw` on the board.

## Quick Verification

After launch, verify these points:

1. `PlutoSDR` source lists the LibreSDR board.
2. Pressing Play does not hang the application.
3. Spectrum and waterfall data start moving.
4. `IQ Mode` can be set to `CS8`.
5. Sample rate can be changed from the source menu without restarting SDR++.

## Known Practical Pitfall

On Windows, `iio_scan_context` may return no contexts even though direct `ip:` access works. This branch works around that by probing common LibreSDR URIs directly.
