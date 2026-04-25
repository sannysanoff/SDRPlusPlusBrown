FROM ubuntu:focal
ENV DEBIAN_FRONTEND=noninteractive
RUN apt update && apt install -y \
    build-essential cmake git \
    libfftw3-dev libglfw3-dev libvolk2-dev libzstd-dev \
    libairspyhf-dev libairspy-dev libiio-dev libad9361-dev \
    librtaudio-dev libhackrf-dev librtlsdr-dev libbladerf-dev \
    liblimesuite-dev p7zip-full wget portaudio19-dev \
    libcodec2-dev autoconf libtool xxd libspdlog-dev \
    libpulse-dev file patchelf unzip liborc-0.4-dev \
    && rm -rf /var/lib/apt/lists/*

# The source code is mounted at /root/SDRPlusPlus at runtime
# make_appimage.sh is baked in for convenience
COPY make_appimage.sh /root/make_appimage.sh
RUN chmod +x /root/make_appimage.sh
