#!/bin/bash
# Rebuild SDR++ Brown (ws_debug branch)
# Run from the repo root: ./rebuild.sh

set -e

export PATH="/opt/data/.local/cmake/bin:/opt/data/.local/build_deps/usr/bin:$PATH"
export PKG_CONFIG_PATH="/opt/data/.local/build_deps/usr/lib/x86_64-linux-gnu/pkgconfig"
export LD_LIBRARY_PATH="/opt/data/.local/build_deps/usr/lib/x86_64-linux-gnu"
export CMAKE_ROOT="/opt/data/.local/cmake/share/cmake-3.30"

cd "$(dirname "$0")"
mkdir -p build
cd build

cmake .. \
  -DCMAKE_PREFIX_PATH="/opt/data/.local/build_deps/usr" \
  -DCMAKE_LIBRARY_PATH="/opt/data/.local/build_deps/usr/lib/x86_64-linux-gnu" \
  -DCMAKE_EXE_LINKER_FLAGS="-L/opt/data/.local/build_deps/usr/lib/x86_64-linux-gnu" \
  -DCMAKE_SHARED_LINKER_FLAGS="-L/opt/data/.local/build_deps/usr/lib/x86_64-linux-gnu" \
  -DOPT_BUILD_AIRSPY_SOURCE=OFF \
  -DOPT_BUILD_AIRSPYHF_SOURCE=OFF \
  -DOPT_BUILD_BLADERF_SOURCE=OFF \
  -DOPT_BUILD_HACKRF_SOURCE=OFF \
  -DOPT_BUILD_RTL_SDR_SOURCE=OFF \
  -DOPT_BUILD_LIMESDR_SOURCE=OFF \
  -DOPT_BUILD_PLUTOSDR_SOURCE=OFF \
  -DOPT_BUILD_SDRPLAY_SOURCE=OFF \
  -DOPT_BUILD_PERSEUS_SOURCE=OFF \
  -DOPT_BUILD_RFNM_SOURCE=OFF \
  -DOPT_BUILD_FOBOSSDR_SOURCE=OFF \
  -DOPT_BUILD_M17_DECODER=OFF \
  -DOPT_BUILD_DISCORD_PRESENCE=OFF \
  -DOPT_BUILD_DAB_DECODER=OFF \
  -DOPT_BUILD_KG_SSTV_DECODER=OFF \
  -DOPT_BUILD_CH_TETRA_DEMODULATOR=ON \
  -DCMAKE_BUILD_TYPE=Release

make -j$(nproc)

echo ""
echo "=== Build complete ==="
echo "Binary: $(pwd)/sdrpp"
echo "Core:   $(pwd)/core/libsdrpp_brown_core.so"
echo "Modules: $(find . -name '*.so' -not -path './core/*' | wc -l) .so files"
