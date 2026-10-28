# RX888 MkII firmware and attribution

This folder holds `SDDC_FX3.img`, the USB firmware of the RX888 MkII (Cypress FX3). The module
copies it to `share/sdrpp/firmware/` on install; `SDRPP_RX888_FIRMWARE` overrides the path at run
time.

A Cypress FX3 has no persistent program: the host writes the image into the **RAM** of the chip each
time it is plugged in (nothing is flashed, and unplugging clears it). This is the same image the SDDC
tools and HDSDR load.

## Where the code and the image come from

- **SDR++ module** (`source_modules/rx888_source`): a port of the native RX888 driver of the OnAir
  receiver, <https://github.com/adswill/OnAir> (MIT). The USB protocol, the firmware upload and the
  gain mapping are OnAir's work: `core/src/source/source_rx888.cpp` and `core/src/source/rx888_usb.h`.
  The DSP (tuning and decimation) is SDR++'s own.
- **Protocol and firmware**: the SDDC project — Oscar Steila ik1xpv's
  [ExtIO_sddc](https://github.com/ik1xpv/ExtIO_sddc) / libsddc, and
  [renardspark/SDDC_Driver](https://github.com/renardspark/SDDC_Driver) (MIT).
- **`SDDC_FX3.img`**: the release asset of SDDC_Driver **v2.0.0**, 145868 bytes,
  <https://github.com/renardspark/SDDC_Driver/releases/tag/v2.0.0>, MIT (`LICENSE-SDDC_Driver.txt`
  next to it). The image contains Cypress/Infineon FX3 SDK object code, which that SDK's licence
  allows to be redistributed in firmware for FX3 devices; it is data loaded into the radio's RAM and
  never linked into SDR++.

To use a different image, download it from the release link above and point
`SDRPP_RX888_FIRMWARE` at it. Do not replace this file with an image from elsewhere.

## Driver on Windows

SDR++ talks to the FX3 through libusb, so the radio needs the WinUSB driver for `04B4:00F3`
(boot loader) and `04B4:00F1` (running), installed once with [Zadig](https://zadig.akeo.ie)
("List All Devices" on). The Cypress driver that the official RX-888 installer binds to
`04B4:00F1` cannot be used; HDSDR and ExtIO keep working with it, so rebind only when you want SDR++
to drive the radio. No driver is shipped here.