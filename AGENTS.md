# Agent Instructions for SDR++ Brown Fork

This document provides guidance for AI agents working with the SDR++ Brown Fork codebase.

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

## HTTP Debug Server (PENDING)

SDR++ includes an embedded HTTP debug server from [EmbeddableWebServer](https://github.com/hellerf/EmbeddableWebServer) for debugging and remote control.

**Command Line Options:**
- `--http <port>` - Start HTTP server on port (0 to disable, default 8080)
- `--debug-wait <file>` - Wait for file to exist before continuing (for debugger attachment)

**Endpoints:**
- `GET /status` - Returns JSON with `ready`, `httpListening`, `mainLoopStarted`

- `GET /stop` or `/exit` - Stop the application
- `GET /windows` - List ImGui windows
- `GET /click?x=<>&y=<>` - Queue mouse click
- `GET /mouse?x=<>&y=<>` - Queue mouse move
- `GET /key?key=<>` - Queue key press
- `GET /type?text=<>` - Queue text input

**Usage Example:**
```bash
/Users/san/Fun/SDRPlusPlus/root_dev/inst/bin/sdrpp -r /Users/san/Fun/SDRPlusPlus/root_dev --http 8080
# Then access http://localhost:8080/status
```

## EmbeddableWebServer (EWS) Nuances

**Surprise:** The header file at `core/src/EmbeddableWebServer.h` contains a hardcoded `#define EWS_HEADER_ONLY` (line 72) that was added during editing. This breaks the build because EWS_HEADER_ONLY excludes all implementation!

**Overcome:** The solution is to NOT define EWS_HEADER_ONLY - include the header directly in `http_debug_server_impl.cpp` without it. The header uses `#ifndef EWS_HEADER_ONLY` to guard implementation, so without the define, all functions are included with `static` linkage. A separate `.cpp` file must compile the header to provide the implementation.

## SDR++ CLI Tool (sdrpp-cli)

A convenience script for managing SDR++ during development and testing.

**Location:** `./sdrpp-cli` (in project root)

**Commands:**
- `./sdrpp-cli start` - Start SDR++ with HTTP debug server on port 8080, waits for main loop to start
- `./sdrpp-cli stop` - Stop SDR++ (reports "Was not running" if not running)
- `./sdrpp-cli status` - Returns "up" or "down"
- `./sdrpp-cli build` - Rebuild SDR++ and install to `root_dev/inst/`

**Usage in Edit/Test/Debug Loop:**

```bash
# Build and test changes
./sdrpp-cli build
./sdrpp-cli start
curl http://localhost:8080/status
./sdrpp-cli stop
```

**Key Notes:**
- Uses a clean config directory (`/tmp/sdrpp_config`) to avoid loading user settings
- Log file: `/tmp/sdrpp_config/sdrpp.log`
- HTTP debug server runs on port 8080 when started
- The `mainLoopStarted` flag in `/status` endpoint indicates the app is fully initialized
- Uses `nohup` to run in background, preventing signal propagation to child process

## General Notes
* The build process can be lengthy due to the number of dependencies and modules.
* The CI scripts in `.github/workflows/build_all.yml` and `docker_builds/` are good references for platform-specific dependencies and build commands.
* Many warnings may appear during compilation (e.g., from IT++, libmp3lame, deprecated C++ features). For this exercise, they were non-fatal for the selected build configuration. Investigation may be needed if they cause issues with specific functionalities.
