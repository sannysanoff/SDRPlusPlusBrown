#!/bin/bash
# =============================================================================
# AppImage Builder for SDR++Brown
# =============================================================================
# Builds a portable AppImage that bundles all runtime dependencies.
# Designed to run inside an Ubuntu 20.04 (focal) Docker container.
#
# Usage:
#   ./make_appimage.sh              # Build from /root/SDRPlusPlus
#   SRC_DIR=/path ./make_appimage.sh # Build from custom source dir
#
# Output: /root/SDRPlusPlusBrown-x86_64.AppImage
# =============================================================================

set -e
SRC_DIR="${SRC_DIR:-/root/SDRPlusPlus}"
APP_NAME="SDRPlusPlusBrown"
ARCH="x86_64"
APPDIR="/AppDir"

echo "=== Building AppImage for $APP_NAME ==="
echo "Source: $SRC_DIR"
echo ""

# ---------------------------------------------------------------------------
# 1. Install build dependencies
# ---------------------------------------------------------------------------
echo "=== Step 1: Install build dependencies ==="
apt update
apt install -y \
    build-essential cmake git \
    libfftw3-dev libglfw3-dev libvolk2-dev libzstd-dev \
    libairspyhf-dev libairspy-dev libiio-dev libad9361-dev \
    librtaudio-dev libhackrf-dev librtlsdr-dev libbladerf-dev \
    liblimesuite-dev p7zip-full wget portaudio19-dev \
    libcodec2-dev autoconf libtool xxd libspdlog-dev \
    libpulse-dev file patchelf unzip liborc-0.4-dev

# ---------------------------------------------------------------------------
# 2. Install linuxdeploy (AppImage tool)
# ---------------------------------------------------------------------------
echo "=== Step 2: Install linuxdeploy ==="
wget -q -O /tmp/linuxdeploy-x86_64.AppImage \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
chmod +x /tmp/linuxdeploy-x86_64.AppImage

# linuxdeploy needs FUSE but inside Docker we use --appimage-extract
/tmp/linuxdeploy-x86_64.AppImage --appimage-extract > /dev/null 2>&1
mv squashfs-root /linuxdeploy
ln -sf /linuxdeploy/AppRun /usr/local/bin/linuxdeploy

# ---------------------------------------------------------------------------
# 3. Install SDRPlay API (proprietary, bundled into AppImage)
# ---------------------------------------------------------------------------
echo "=== Step 3: Install SDRPlay API ==="
SDRPLAY_ARCH=$(dpkg --print-architecture)
wget -q https://www.sdrplay.com/software/SDRplay_RSP_API-Linux-3.15.2.run -O /tmp/sdrplay.run || true
if [ -f /tmp/sdrplay.run ] && [ -s /tmp/sdrplay.run ]; then
    7z x /tmp/sdrplay.run -o/tmp/sdrplay_extracted > /dev/null
    7z x /tmp/sdrplay_extracted -o/tmp/sdrplay_pkg > /dev/null
    cp /tmp/sdrplay_pkg/$SDRPLAY_ARCH/libsdrplay_api.so.3.15 /usr/lib/libsdrplay_api.so
    cp /tmp/sdrplay_pkg/inc/* /usr/include/
    ldconfig
    echo "SDRPlay API installed"
else
    echo "WARNING: SDRPlay API download failed, building without SDRPlay support"
fi

# ---------------------------------------------------------------------------
# 4. Install libperseus-sdr
# ---------------------------------------------------------------------------
echo "=== Step 4a: Install libperseus-sdr ==="
cd /tmp
git clone --depth=1 https://github.com/Microtelecom/libperseus-sdr || true
if [ -d /tmp/libperseus-sdr ]; then
    cd /tmp/libperseus-sdr
    autoreconf -i || true
    ./configure || true
    make -j$(nproc) || true
    make install || true
    ldconfig
    cd /tmp
fi

# ---------------------------------------------------------------------------
# 5. Install librfnm
# ---------------------------------------------------------------------------
echo "=== Step 5: Install librfnm ==="
cd /tmp
git clone --depth=1 https://github.com/AlexandreRouma/librfnm || true
if [ -d /tmp/librfnm ]; then
    cd /tmp/librfnm
    mkdir -p build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr || true
    make -j$(nproc) || true
    make install || true
    cd /tmp
fi

# ---------------------------------------------------------------------------
# 6. Install libfobos
# ---------------------------------------------------------------------------
echo "=== Step 6: Install libfobos ==="
cd /tmp
git clone --depth=1 https://github.com/AlexandreRouma/libfobos || true
if [ -d /tmp/libfobos ]; then
    cd /tmp/libfobos
    mkdir -p build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr || true
    make -j$(nproc) || true
    make install || true
    cd /tmp
fi

# ---------------------------------------------------------------------------
# 7. Install libhydrasdr
# ---------------------------------------------------------------------------
echo "=== Step 7: Install libhydrasdr ==="
cd /tmp
git clone --depth=1 https://github.com/hydrasdr/rfone_host || true
if [ -d /tmp/rfone_host ]; then
    cd /tmp/rfone_host
    mkdir -p build && cd build
    cmake .. || true
    make -j$(nproc) || true
    make install || true
    cd /tmp
fi

# ---------------------------------------------------------------------------
# 8. Install libdlcr (DragonLabs)
# ---------------------------------------------------------------------------
echo "=== Step 8: Install libdlcr ==="
cd /tmp
wget -q https://dragnlabs.com/host-tools/dlcr_host_v0.3.0.zip -O dlcr_host.zip || true
if [ -f /tmp/dlcr_host.zip ] && [ -s /tmp/dlcr_host.zip ]; then
    unzip -qo dlcr_host.zip -d dlcr_host
    cd dlcr_host
    mkdir -p build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr || true
    make -j$(nproc) || true
    make install || true
    cd /tmp
fi

# ---------------------------------------------------------------------------
# 9. Pre-download ETSI TETRA codec
# ETSI URL may be dead; if so, we'll compile without TETRA decoder
# ---------------------------------------------------------------------------
echo "=== Step 9: Pre-download ETSI TETRA codec ==="
ETSI_CODEC_DIR="$SRC_DIR/build/decoder_modules/ch_tetra_demodulator/etsi_codec"
ETSI_CODEC_URL="https://www.etsi.org/deliver/etsi_en/300300_300399/30039502/01.03.01_60/en_30039502v010301p0.zip"
mkdir -p "$ETSI_CODEC_DIR"
if wget -q --no-check-certificate "$ETSI_CODEC_URL" -O "${ETSI_CODEC_DIR}/etsi_tetra_codec.zip.tmp" 2>/dev/null; then
    if unzip -tq "${ETSI_CODEC_DIR}/etsi_tetra_codec.zip.tmp" >/dev/null 2>&1; then
        mv "${ETSI_CODEC_DIR}/etsi_tetra_codec.zip.tmp" "$ETSI_CODEC_DIR/etsi_tetra_codec.zip"
        echo "ETSI TETRA codec downloaded successfully"
    else
        echo "ETSI TETRA codec download invalid"
        rm -f "${ETSI_CODEC_DIR}/etsi_tetra_codec.zip.tmp"
    fi
else
    echo "WARNING: ETSI TETRA codec download failed, CMake will try"
fi

# ---------------------------------------------------------------------------
# 10. Build SDR++ with CMAKE_INSTALL_PREFIX=/usr
#     (Important: /usr prefix ensures correct relative paths in AppDir)
# ---------------------------------------------------------------------------
echo "=== Step 10: Build SDR++Brown ==="
cd "$SRC_DIR"
mkdir -p build && cd build

# Set options - enable everything that's available
CMAKE_OPTS=(
    -DCMAKE_INSTALL_PREFIX=/usr
    -DCMAKE_BUILD_TYPE=Release
    -DOPT_BUILD_BLADERF_SOURCE=ON
    -DOPT_BUILD_LIMESDR_SOURCE=ON
    -DOPT_BUILD_NEW_PORTAUDIO_SINK=ON
    -DOPT_BUILD_M17_DECODER=ON
    -DOPT_BUILD_APPIMAGE=ON
)

# Conditionally enable sources based on what we installed
[ -f /usr/lib/libsdrplay_api.so ] && CMAKE_OPTS+=(-DOPT_BUILD_SDRPLAY_SOURCE=ON)
[ -d /usr/include/perseus-sdr ] && CMAKE_OPTS+=(-DOPT_BUILD_PERSEUS_SOURCE=ON)
[ -f /usr/lib/librfnm.so ] && CMAKE_OPTS+=(-DOPT_BUILD_RFNM_SOURCE=ON)
[ -f /usr/lib/libfobos.so ] && CMAKE_OPTS+=(-DOPT_BUILD_FOBOSSDR_SOURCE=ON)
[ -f /usr/lib/libiio.so ] && CMAKE_OPTS+=(-DOPT_BUILD_PLUTOSDR_SOURCE=ON)

echo "CMake options: ${CMAKE_OPTS[@]}"

cmake "$SRC_DIR" "${CMAKE_OPTS[@]}" 2>&1 || {
    echo "=== CMake failed. Retrying with minimal config... ==="
    cmake "$SRC_DIR" \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_BUILD_TYPE=Release \
        -DOPT_BUILD_BLADERF_SOURCE=ON \
        -DOPT_BUILD_LIMESDR_SOURCE=ON \
        -DOPT_BUILD_NEW_PORTAUDIO_SINK=ON \
        -DOPT_BUILD_M17_DECODER=ON \
        -DOPT_BUILD_SDRPLAY_SOURCE=OFF \
        -DOPT_BUILD_PERSEUS_SOURCE=OFF \
        -DOPT_BUILD_RFNM_SOURCE=OFF \
        -DOPT_BUILD_FOBOSSDR_SOURCE=OFF \
        -DOPT_BUILD_APPIMAGE=ON 2>&1
}

echo "=== Building (this may take a while) ==="
make -j$(nproc) 2>&1

echo "=== Build complete ==="
ls -la "$SRC_DIR/build/sdrpp_brown_appimage"

# ---------------------------------------------------------------------------
# 11. Install to AppDir
# ---------------------------------------------------------------------------
echo "=== Step 11: Install to AppDir ==="
make install DESTDIR="$APPDIR"

# ---------------------------------------------------------------------------
# 12. Copy icon to AppDir root (linuxdeploy requirement)
# ---------------------------------------------------------------------------
echo "=== Step 12: Copy icon to AppDir root ==="
if [ -f "$APPDIR/usr/share/sdrpp/icons/sdrpp.png" ]; then
    cp "$APPDIR/usr/share/sdrpp/icons/sdrpp.png" "$APPDIR/sdrpp.png"
fi

# ---------------------------------------------------------------------------
# 13. Fix .desktop file for AppImage
# ---------------------------------------------------------------------------
echo "=== Step 13: Fix .desktop file for AppImage ==="
# The desktop file is generated by CMake with @CMAKE_INSTALL_PREFIX@/usr/bin/ paths
DESKTOP_FILE="$APPDIR/usr/share/applications/sdrpp.desktop"
if [ -f "$DESKTOP_FILE" ]; then
    # Fix paths for AppImage (Exec should be just the binary name, no path)
    # CMake now generates the correct executable name (sdrpp_brown_appimage for AppImage)
    sed -i \
        -e 's|Exec=/usr/bin/sdrpp_brown_appimage|Exec=sdrpp_brown_appimage|' \
        -e 's|Icon=/.*|Icon=sdrpp|' \
        "$DESKTOP_FILE"
    # Copy to AppDir root (linuxdeploy requirement)
    cp "$DESKTOP_FILE" "$APPDIR/sdrpp.desktop"
fi

# ---------------------------------------------------------------------------
# 14. Fix rpath in all installed binaries
# ---------------------------------------------------------------------------
echo "=== Step 14: Skipping custom rpath (linuxdeploy handles it) ==="

# ---------------------------------------------------------------------------
# 15. Bundle with linuxdeploy
# ---------------------------------------------------------------------------
echo "=== Step 15: Bundle with linuxdeploy (populate AppDir only) ==="

# First run linuxdeploy to populate the AppDir with dependencies
cd "$APPDIR"

# Ensure linuxdeploy can find inter-plugin dependencies
export LD_LIBRARY_PATH="$APPDIR/usr/lib:$LD_LIBRARY_PATH"

# Exclude GPU/graphics libraries that should come from host (for NVIDIA/AMD/Intel compatibility)
# These are ABI-dependent and must match the host's GPU drivers
linuxdeploy \
    --appdir "$APPDIR" \
    --desktop-file "$APPDIR/sdrpp.desktop" \
    --icon-file "$APPDIR/sdrpp.png" \
    --exclude-library "*libGL.so*" \
    --exclude-library "*libGLX.so*" \
    --exclude-library "*libEGL.so*" \
    --exclude-library "*libGLdispatch.so*" \
    --exclude-library "*libOpenGL.so*" \
    --exclude-library "*libdrm.so*" \
    --exclude-library "*libd3dadapter9.so*" \
    --exclude-library "*libvulkan.so*" \
    --exclude-library "*libxcb*" \
    --exclude-library "*libX11*" \
    --exclude-library "*libXext*" \
    --exclude-library "*libXrender*" \
    --exclude-library "*libnvidia*" \
    --exclude-library "*libvdpau*" \
    --exclude-library "*libLLVM*" \
    --exclude-library "*swrast*" \
    --exclude-library "*iris*" \
    --exclude-library "*radeonsi*" \
    --exclude-library "*nouveau*" \
    --exclude-library "*vmwgfx*" \
    --exclude-library "*libc.so*" \
    --exclude-library "*libglfw*" \
    2>&1 || echo "WARNING: linuxdeploy failed (non-critical). AppDir is partially populated."

# Now install appimagetool and create the AppImage
echo "=== Step 15b: Create AppImage with appimagetool ==="
wget -q -O /tmp/appimagetool-x86_64.AppImage \
    "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage" || true
if [ -f /tmp/appimagetool-x86_64.AppImage ] && [ -s /tmp/appimagetool-x86_64.AppImage ]; then
    chmod +x /tmp/appimagetool-x86_64.AppImage
    /tmp/appimagetool-x86_64.AppImage --appimage-extract > /dev/null 2>&1
    mv squashfs-root /appimagetool 2>/dev/null || true
    /appimagetool/AppRun "$APPDIR" /root/"$APP_NAME-$ARCH.AppImage" 2>&1 || \
    /tmp/appimagetool-x86_64.AppImage "$APPDIR" /root/"$APP_NAME-$ARCH.AppImage" 2>&1 || \
    APPIMAGE_GENERATED=true
else
    # Fallback: try linuxdeploy --output appimage
    echo "appimagetool download failed, falling back to linuxdeploy --output appimage"
    linuxdeploy \
        --appdir "$APPDIR" \
        --desktop-file "$APPDIR/sdrpp.desktop" \
        --icon-file "$APPDIR/sdrpp.png" \
        --exclude-library "*libglfw*" \
        --output appimage 2>&1 || true
fi

# ---------------------------------------------------------------------------
# 16. Move the final AppImage to a predictable location
# ---------------------------------------------------------------------------
echo "=== Step 16: Finalize ==="
# Find the generated AppImage (linuxdeploy creates it in CWD)
APPIMAGE_FILE=$(find "$APPDIR" -maxdepth 1 -name "*.AppImage" -print -quit 2>/dev/null)
if [ -n "$APPIMAGE_FILE" ] && [ -f "$APPIMAGE_FILE" ]; then
    mv -f "$APPIMAGE_FILE" /root/"$APP_NAME-$ARCH.AppImage"
elif ls /root/*.AppImage >/dev/null 2>&1; then
    # Already in /root
    :
else
    # Search wider
    FOUND=$(find / -maxdepth 3 -name "*.AppImage" -print -quit 2>/dev/null)
    if [ -n "$FOUND" ]; then
        mv -f "$FOUND" /root/"$APP_NAME-$ARCH.AppImage" 2>/dev/null || true
    fi
fi

echo ""
echo "=== Done! ==="
if [ -f /root/"$APP_NAME-$ARCH.AppImage" ]; then
    ls -lh /root/"$APP_NAME-$ARCH.AppImage"
    echo "AppImage ready at /root/$APP_NAME-$ARCH.AppImage"
else
    echo "WARNING: AppImage not found at expected location"
    find / -name "*.AppImage" 2>/dev/null
    echo ""
    echo "Checking AppDir contents:"
    ls -la "$APPDIR/" 2>/dev/null | head -20
fi
