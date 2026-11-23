#!/bin/bash
set -e
cd /root

# Install dependencies and tools
apt update
apt install -y unzip ccache build-essential cmake git libfftw3-dev libglfw3-dev libvolk2-dev libzstd-dev libairspyhf-dev libairspy-dev \
            libiio-dev libad9361-dev librtaudio-dev libhackrf-dev librtlsdr-dev libbladerf-dev liblimesuite-dev p7zip-full wget portaudio19-dev \
            libcodec2-dev autoconf libtool xxd libspdlog-dev liborc-0.4-dev

# Install SDRPlay libraries
SDRPLAY_ARCH=$(dpkg --print-architecture)
wget https://www.sdrpp.org/ci/SDRplay_RSP_API-Linux-3.15.2.run
7z x ./SDRplay_RSP_API-Linux-3.15.2.run
7z x ./SDRplay_RSP_API-Linux-3.15.2
cp $SDRPLAY_ARCH/libsdrplay_api.so.3.15 /usr/lib/libsdrplay_api.so
cp inc/* /usr/include/

# Install libperseus
git clone https://github.com/Microtelecom/libperseus-sdr
cd libperseus-sdr
autoreconf -i
./configure
make
make install
ldconfig
cd ..

# Install librfnm
git clone https://github.com/AlexandreRouma/librfnm
cd librfnm
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
make -j2
make install
cd ../../

# Install libfobos
git clone https://github.com/AlexandreRouma/libfobos
cd libfobos
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
make -j2
make install
cd ../../

# Install libhydrasdr
git clone https://github.com/hydrasdr/rfone_host
cd rfone_host
mkdir build
cd build
cmake ..
make -j2
make install
cd ../../

# Install libdlcr
wget https://dragnlabs.com/host-tools/dlcr_host_v0.3.0.zip
mkdir dlcr_host
cd dlcr_host
7z x ../dlcr_host_v0.3.0.zip
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
make -j2
make install
cd ../../

# ccache: reuse compiled objects across CI runs. The workflow restores .ccache
# from the actions cache, so the directory must stay inside the mounted repo.
mkdir -p /usr/local/lib/ccache
for t in cc gcc c++ g++; do ln -sf /usr/bin/ccache /usr/local/lib/ccache/$t; done
export PATH=/usr/local/lib/ccache:$PATH
export CCACHE_DIR=/root/SDRPlusPlus/.ccache
export CCACHE_MAXSIZE=1G
# The compiler binary is reinstalled on every run, so its mtime changes.
# Key the cache on the compiler version string instead of mtime.
export CCACHE_COMPILERCHECK="string:$(gcc -dumpmachine)-$(gcc -dumpfullversion)"

cd SDRPlusPlus
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DOPT_BUILD_BLADERF_SOURCE=ON -DOPT_BUILD_LIMESDR_SOURCE=ON -DOPT_BUILD_SDRPLAY_SOURCE=ON -DOPT_BUILD_NEW_PORTAUDIO_SINK=ON -DOPT_BUILD_M17_DECODER=ON -DOPT_BUILD_PERSEUS_SOURCE=ON -DOPT_BUILD_RFNM_SOURCE=ON -DOPT_BUILD_FOBOSSDR_SOURCE=ON -DOPT_BUILD_HYDRASDR_SOURCE=ON -DOPT_BUILD_DRAGONLABS_SOURCE=ON -DOPT_BUILD_CH_EXTRAVHF_DECODER=ON -DOPT_BUILD_CH_TETRA_DEMODULATOR=ON
make VERBOSE=1 -j$(nproc)
ccache -s

cd ..
sh make_debian_package.sh ./build 'libfftw3-dev, libglfw3-dev, libvolk2-dev, librtaudio-dev, libzstd-dev, liborc-0.4-dev, libusb-1.0-0'