# Agent Instructions for SDR++ Brown Fork

This document provides guidance for AI agents working with the SDR++ Brown Fork codebase.

## Additional Docs
- [AGENTS-windows.md](./AGENTS-windows.md) — Windows build via windows-mcp (winget/vcpkg toolchain, VS2022 build, runtime assembly, procdump/cdb crash diagnosis, E2E tests)
- [AGENTS-debugging.md](./AGENTS-debugging.md) — HTTP debug server, module/procfs commands, GUI automation, lldb crash/hang debugging, bugfix workflow

## Building Locally (Linux - Debian/Ubuntu based)

The following steps outline how to build the SDR++ project locally on a Debian/Ubuntu-based system. These steps are derived from the CI build process and local build attempts.

**1. Install System Dependencies:**

Use `apt` to install necessary packages. `libvolk2-dev` might not be available on newer distributions (e.g., Ubuntu 24.04 Noble); replace it with `libvolk-dev` and `libcpu-features-dev`.

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    git \
    libfftw3-dev \
    libglfw3-dev \
    libvolk-dev \
    libcpu-features-dev \
    libzstd-dev \
    libairspyhf-dev \
    libairspy-dev \
    libiio-dev \
    libad9361-dev \
    librtaudio-dev \
    libhackrf-dev \
    librtlsdr-dev \
    libusb-1.0-0-dev \
    libbladerf-dev \
    liblimesuite-dev \
    p7zip-full \
    wget \
    portaudio19-dev \
    libcodec2-dev \
    autoconf \
    libtool \
    xxd \
    libspdlog-dev
```

**2. Install SDRPlay API:**

Download and install the SDRPlay API.

```bash
wget https://www.sdrplay.com/software/SDRplay_RSP_API-Linux-3.15.2.run
7z x ./SDRplay_RSP_API-Linux-3.15.2.run
7z x ./SDRplay_RSP_API-Linux-3.15.2
SDRPLAY_ARCH=$(dpkg --print-architecture)
sudo cp $SDRPLAY_ARCH/libsdrplay_api.so.3.15 /usr/lib/libsdrplay_api.so
sudo cp inc/* /usr/include/
rm -f SDRplay_RSP_API-Linux-3.15.2.run SDRplay_RSP_API-Linux-3.15.2 # Clean up
rm -rf $SDRPLAY_ARCH inc # Clean up
```

**3. Install Additional Libraries (libperseus-sdr, librfnm, libfobos):**

The CI script involves cloning these libraries from GitHub, building, and installing them. However, due to persistent tooling issues with the `run_in_bash_session` tool when handling directories created by `git clone` (even if cloned into `/tmp`), these libraries were **disabled** in the local build attempt via CMake options.

If these libraries are essential for your task and the tooling issues are resolved, the original steps from `docker_builds/debian_bookworm/do_build.sh` are:

*   **libperseus:**
    ```bash
    # git clone https://github.com/Microtelecom/libperseus-sdr
    # cd libperseus-sdr
    # autoreconf -i
    # ./configure
    # make
    # sudo make install
    # sudo ldconfig
    # cd ..
    # rm -rf libperseus-sdr
    ```
*   **librfnm:**
    ```bash
    # git clone https://github.com/AlexandreRouma/librfnm
    # cd librfnm
    # mkdir build
    # cd build
    # cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
    # make -j$(nproc)
    # sudo make install
    # cd ../..
    # rm -rf librfnm
    ```
*   **libfobos:**
    ```bash
    # git clone https://github.com/AlexandreRouma/libfobos
    # cd libfobos
    # mkdir build
    # cd build
    # cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
    # make -j$(nproc)
    # sudo make install
    # cd ../..
    # rm -rf libfobos
    ```

**Pitfall:** The `run_in_bash_session` tool may error out with `ValueError: Unexpected error: return_code: 1 stderr_contents: "cat: /app/libname: Is a directory"` after `git clone`. This seems to be an internal tooling issue when it tries to synchronize workspace state.

**4. Build SDR++:**

Create a build directory and run CMake with the appropriate options (disabling problematic libraries as needed) and then Make.

```bash
# Ensure no problematic cloned directories are in /app if previous attempts failed
sudo rm -rf /app/libperseus-sdr
sudo rm -rf /app/librfnm
sudo rm -rf /app/libfobos

sudo rm -rf build
sudo mkdir build
sudo chown $(whoami):$(whoami) build
cd build

cmake /app \
    -DOPT_BUILD_BLADERF_SOURCE=ON \
    -DOPT_BUILD_LIMESDR_SOURCE=ON \
    -DOPT_BUILD_SDRPLAY_SOURCE=ON \
    -DOPT_BUILD_NEW_PORTAUDIO_SINK=ON \
    -DOPT_BUILD_M17_DECODER=ON \
    -DOPT_BUILD_CH_EXTRAVHF_DECODER=ON \
    -DOPT_BUILD_PERSEUS_SOURCE=OFF \
    -DOPT_BUILD_RFNM_SOURCE=OFF \
    -DOPT_BUILD_FOBOSSDR_SOURCE=OFF

make VERBOSE=1 -j$(nproc)
```

**Key Pitfalls & Solutions during Build SDR++ step:**
*   **Initial `libvolk2-dev` failure:** Replaced with `libvolk-dev` and `libcpu-features-dev` during dependency installation.
*   **CMake source directory issues:**
    *   Initially, `cmake ..` from within the `build` directory failed to find `CMakeLists.txt` or picked the wrong directory.
    *   **Solution:** Explicitly provide the path to the source directory: `cmake /app [options...]`.
*   **Permission issues with `build` directory:**
    *   `mkdir build` failed with permission denied.
    *   **Solution:** Use `sudo mkdir build` and then `sudo chown $(whoami):$(whoami) build` to ensure the current user owns the build directory before `cd`ing into it and running `cmake` and `make` without `sudo`.
*   **Tooling errors with `git clone`:** As mentioned in step 3, direct cloning and building of some external libraries caused persistent errors with the `run_in_bash_session` tool. The workaround was to disable these modules via CMake flags (`-DOPT_BUILD_PERSEUS_SOURCE=OFF`, etc.).

**5. Output:**

The compiled application `sdrpp` and various module `.so` files will be located in the `build/` directory (specifically, `build/sdrpp` and `build/core/` for `libsdrpp_core.so`, with other modules in their respective subdirectories under `build/`).

## Agent Behavior Guidelines

### ⚠️ CRITICAL: Git Operations

**NEVER push commits to the remote repository unless explicitly requested by the user.**

- ❌ DO NOT run `git push` automatically after commits
- ❌ DO NOT run `git push` after merges
- ❌ DO NOT run `git push` as part of any automated workflow
- ✅ ONLY push when the user explicitly says "push" or requests it
- ✅ Always confirm before pushing if unsure

This applies to all branches including `master` and feature branches.

### Committing
- You MAY commit changes locally with appropriate messages
- You MAY create commits to save work
- You MAY prepare commits for review
- Wait for explicit user approval before pushing

## Building Locally (macOS / ARM64)

**CMake Configuration:**

```bash
cd /Users/san/Fun/SDRPlusPlus/cmake-build-debug
cmake .. \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -G "Unix Makefiles" \
    -DOPT_BUILD_PLUTOSDR_SOURCE=OFF \
    -DCMAKE_INSTALL_PREFIX=/Users/san/Fun/SDRPlusPlus/root_dev/inst \
    -DOPT_BUILD_CH_EXTRAVHF_DECODER:BOOL=ON
```

**Build & Install:**

```bash
make -j$(nproc)
make install
```

> **⚠️ Must run `make install` after any source change to redeploy `.dylib`/`.so` modules into `root_dev/inst/`. Without `make install`, `sdrpp` loads stale modules from the build directory or an old install location, making it impossible to test runtime fixes (e.g., stream teardown hangs).

## Debugging

See [AGENTS-debugging.md](./AGENTS-debugging.md) for detailed debugging and remote control capabilities.

## Windows Build Caveats

### MSVC `min`/`max` macro clash

`<Windows.h>` defines `min` and `max` as preprocessor macros that break `std::min(...)` and `std::max(...)` with `error C2589: '(': illegal token on right side of '::'`.

Fix by parenthesizing the call:
```cpp
// Before (breaks on MSVC):
int x = std::min(a, b);
// After:
int x = (std::min)(a, b);
```

Search for unguarded uses with `grep 'std::\(min\|max\)('`.

### VS 2026 drops Windows 7/8 runtime support

Since June 2026, GitHub Actions `windows-latest` and `windows-2025` images ship **VS 2026** (MSVC 14.51), whose redistributable no longer supports Windows 7/8/8.1. The minimum target is Windows 10.

To build binaries compatible with Windows 7, pin the runner to `windows-2022` in CI:
```yaml
runs-on: windows-2022
```

## Adding a New Module: Integration Checklist

A new module needs entries in several places. Building the shared library is not enough. Missing one of these paths means the module is absent on some platform, or present but never instantiated.

Rule of thumb: build the module on every OS the module's code supports. Guard a module per platform only when that platform cannot support it (hardware API or library missing). Do not require a manual switch for a pure CPU / no-dependency module.

### 1. Build-time inclusion (all desktop platforms)

`CMakeLists.txt`:

```cmake
option(OPT_BUILD_<NAME> "Build the <Name> module" ON)      # ~line 60-90
...
if (OPT_BUILD_<NAME>)
    add_subdirectory("decoder_modules/<name>")
endif (OPT_BUILD_<NAME>)
```

- Default `ON` when the module has no external dependency and runs on every OS.
- Default `OFF` when it needs an optional library (`libbladerf`, `libfobos`, ...).
- Add a platform guard only when needed. Existing examples: `if (NOT ANDROID)` around `sink_modules/portaudio_sink` / the CLI, `if (ANDROID) ... else ()` for `OPT_BUILD_OSA103_SOURCE` (no FTDI D2XX on Android), `elseif(ANDROID)` skip for `misc_modules/tci_server`.
- The module's own `CMakeLists.txt` must `include(${SDRPP_MODULE_CMAKE})`. That file creates the shared library, sets `PREFIX ""` (so the file is `<name>.so` / `<name>.dll` / `<name>.dylib`) and adds `install(TARGETS <name> DESTINATION lib/sdrpp/plugins)`.

### 1a. Load inclusion and default enabled flag (desktop)

Desktop needs no per-module load list. `core/src/gui/main_window.cpp:127-141` scans `modulesDirectory` and loads every file with extension `SDRPP_MOD_EXTENTSION`. The library only has to reach the modules directory (see section 3).

Every module instance still needs a default entry in `core/src/core.cpp` `defConfig`:

```cpp
defConfig["moduleInstances"]["<Instance Name>"]["module"] = "<module_name>";
defConfig["moduleInstances"]["<Instance Name>"]["enabled"] = true;  // or false
```

- `enabled = true`: the instance is created and activated at startup.
- `enabled = false`: the module is loaded and the instance appears in the module list, but it stays switched off. Use this for high-CPU instances (TinyCW decoder, TETRA, Radiosonde) so the user opts in. `main_window.cpp:152-158` creates the instance, then calls `core::moduleManager.disableInstance(name)` when `enabled` is false.
- Also add a repair block next to the existing ones so installs with an older `config.json` receive the instance; without it the instance never appears on upgrade:

```cpp
if (!core::configManager.conf["moduleInstances"].contains("<Instance Name>")) {
    core::configManager.conf["moduleInstances"]["<Instance Name>"] = defConfig["moduleInstances"]["<Instance Name>"];
}
```

### 2. Android: hardcoded .so list in the module manager

Android cannot enumerate the APK native library directory, so `core/src/core.cpp` keeps a hardcoded list (`#ifdef __ANDROID__`, ~lines 787-825):

```cpp
core::configManager.conf["modules"][modCount++] = "<name>.so";
```

- Add the module there, or the .so is packaged into the APK and never loaded.
- The bare filename is intentional. `core/src/module.cpp:41-52` skips the existence check on Android so `dlopen("<name>.so")` resolves from `lib/<abi>/`.
- AGP packages every shared library produced by `externalNativeBuild`, so no APK copy step is needed.
- The Android CMake invocation lives in `android/app/build.gradle` (`externalNativeBuild.cmake.arguments`). Touch it only when the option is `OFF` by default or needs a kit path (`SDR_KIT_ROOT`). Do not add a redundant `-DOPT_BUILD_<NAME>=ON` for an option that already defaults to `ON`.
- The same `#ifdef __ANDROID__` block also holds the `moduleInstances` defaults for Android-only modules (e.g. `Radiosonde Decoder`).

### 3. Packaging: the .so/.dll/.dylib must reach the modules directory

| Package | Script | Action |
|---|---|---|
| Windows x64 zip | `make_windows_package.ps1` (~line 109-130) | Add `cp $build_dir/decoder_modules/<name>/$RELDIR/<name>.dll sdrpp_windows_x64/modules/` |
| macOS .app | `make_macos_bundle.sh` (~line 108-135) | Add `bundle_install_binary $BUNDLE $BUNDLE/Contents/Plugins $BUILD_DIR/decoder_modules/<name>/<name>.dylib \|\| true` |
| Linux .deb | `make_debian_package.sh` | Nothing to do: runs `make install`, the `sdrpp_module.cmake` install rule copies the plugin |
| Local macOS/Linux install | `make install` | Nothing to do, same install rule |

Use the correct subdirectory for the module kind (`source_modules/`, `sink_modules/`, `decoder_modules/`, `misc_modules/`).

### 4. Other paths to check

- `tests/*.cpp`: auto-globbed (`core/CMakeLists.txt:26`, `BUILD_TESTS=ON`), no wiring needed. Register tests through `tests/test_runner.h` and run with `sdrpp -t <test_name>`.
- `e2e/test_<module>.py`: auto-discovered by `e2e/e2e_common.py` (`test_*.py` glob), no list to edit.
- `docs/index.md`: feature list mentions the decoders; add the module if the feature is user visible.
- `changelog.md`: entry for the new module.
- `.github/workflows/build_all.yml` and `docker_builds/`: no per-module lists, they call CMake and `make install`. Check them only if the module needs a new system dependency.
- Optional helper executables (e.g. `tinycw_cli`): guard with `if (NOT ANDROID)` so APK builds skip them.

### Order of work

1. Module + `CMakeLists.txt` (`include(${SDRPP_MODULE_CMAKE})`) → verify: builds and installs on the host.
2. Root `CMakeLists.txt` option + `add_subdirectory` → verify: `cmake` shows the target, `make install` puts the plugin in `lib/sdrpp/plugins`.
3. `core.cpp` `moduleInstances` default + repair block → verify: instance appears in the module list, `enabled=false` starts it off.
4. `core.cpp` Android `.so` list (`#ifdef __ANDROID__`) → verify: module loads in the Android app.
5. `make_windows_package.ps1` and `make_macos_bundle.sh` → verify: the packaged bundle contains `<name>.dll` / `<name>.dylib` under `modules/` / `Contents/Plugins`.

Raise a concern with the user instead of silently skipping a platform: state which platform is dropped, why (hardware/library/API), and what the user loses. Silicon- or OS-specific code inside shared sources must stay behind `#ifdef _WIN32` / `__APPLE__` / `__linux__` (Android also defines `__linux__`), so one source file still builds everywhere.

## General Notes
* The build process can be lengthy due to the number of dependencies and modules.
* The CI scripts in `.github/workflows/build_all.yml` and `docker_builds/` are good references for platform-specific dependencies and build commands.
* Many warnings may appear during compilation (e.g., from IT++, libmp3lame, deprecated C++ features). For this exercise, they were non-fatal for the selected build configuration. Investigation may be needed if they cause issues with specific functionalities.

IMPORTANT!!! Never change layout and formatting of C/C++ code unrelated to the task you're doing. NO braces edits in unrelated code, no spaces evenly distributed in array initializations. In existing C++ code, each position matter, NEVER change unless you fix bug related to that line. NEVER add changes to source that just alter code layout!

