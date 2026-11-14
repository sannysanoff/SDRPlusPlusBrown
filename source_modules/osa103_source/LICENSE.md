# OSA103 source module — licence, provenance and disclaimer

Non-commercial, educational use only. Read this file before using, modifying or
redistributing anything in this directory.

## What this code is, and where it comes from

This is an independently written SDR++ source plugin for the OSA103 / OSA103 Mini USB
measuring device (FTDI FT232RL used as a UART, Xilinx Spartan-6 FPGA, AD9288 ADC,
ADF4351 or MAX2871 synthesizer). The device and its Windows software are products of
the OSA103 project, published at http://www.osa103.ru.

The USB protocol implemented here was reconstructed by static analysis of the vendor's
ExtIO driver:

    file     ExtIO_Osa.dll
    format   PE32 (x86), Borland C++Builder, compiled 2022-05-16
    version  3.09
    md5      bcab0dfd74186c2b99b8b2b68ccbc57c
    vendor   OSA103 project (osa103.ru); ExtIO plugin ABI originally by Alberto di
             Bene for Winrad and adopted by SDR#/HDSDR/SDRuno

**No vendor code, no decompiled listing, no vendor binary and no vendor FPGA
bitstream is included in, or redistributed with, this directory.** The only things
carried over are functional facts — command type bytes, field offsets, constant words,
formulas — which are the interface itself. If the FPGA image is needed at runtime it
must be extracted by the user from their own copy of the vendor driver; this module
never ships it.

This project is not affiliated with, authorised by, or endorsed by the OSA103 vendor.

## Permitted use

Permission is granted, free of charge, to any person obtaining a copy of this software
and associated files, to use, copy, modify, merge and redistribute it, subject to all
of the following:

1. **Non-commercial only.** Use is limited to private, educational, research and other
   non-commercial purposes. Commercial use — including bundling into a product,
   selling, or offering it as part of a paid service — requires prior written
   permission from the copyright holder.
2. **Attribution.** The copyright notice and this file must be retained in all copies
   and in substantial portions of the software, including modified versions.
3. **No vendor material.** You may not use this project as a vehicle to redistribute
   the vendor's DLL, driver package, FPGA bitstream or documentation.
4. **No warranty and no liability.** The software is provided "as is", without warranty
   of any kind, express or implied, including but not limited to the warranties of
   merchantability, fitness for a particular purpose and non-infringement. In no event
   shall the authors or copyright holders be liable for any claim, damages or other
   liability, whether in an action of contract, tort or otherwise, arising from, out
   of or in connection with the software or its use. This code talks to hardware it
   could not be tested against; misconfiguration may leave the device in an unexpected
   state, and you run it entirely at your own risk.

## Third-party components

* **FTDI D2XX** — not distributed here. The module loads the library at runtime from
  whatever the user installed; FTDI's own licence governs that library.
* **SDR++ / `sdrpp_core`** — the host application and its module API are by Ryzerth
  (credited verbatim as such in SDR++'s own sources), released under GPLv3. This module
  is loaded by SDR++ and links against `libsdrpp_core`. Be aware that GPLv3 does not permit additional non-commercial
  restrictions on a covered work that is distributed as a whole: the non-commercial
  terms above apply to this plugin on its own, and a build of it that is distributed
  together with SDR++ should be treated as GPLv3. For that reason the module is
  intended for personal and educational use, and is not submitted upstream.
* **ExtIO** — the plugin ABI this driver family implements is a de-facto standard;
  no ExtIO SDK code is used here, the interface is reimplemented from its public
  description.

## Trademarks

"OSA103", "OSA103 Mini" and "OSA103 F" identify the vendor's hardware. "SDR++" is the
name of the SDR++ project. All trademarks belong to their respective owners and are
used here only descriptively.
