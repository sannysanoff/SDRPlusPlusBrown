# RX888 MkII source module — attribution

The source code in this module is based on the original OnAir receiver source:
<https://github.com/adswill/OnAir>

The module is part of SDR++ and is distributed under the GNU General Public License version 3. The
repository `LICENSE` file holds the full text.

## OnAir

- Copyright (C) 2026 Kristijonas Adomaitis and OnAir contributors
- Licence: GPL-3.0-or-later
- Files taken: `core/src/source/source_rx888.cpp`, `core/src/source/rx888_usb.h`,
  `core/src/source/native_realddc.h`, `core/src/dsp/dsp_compat.cpp` (the `desamp` kernel),
  `core/include/dect2/common/window.h` (`besselI0`), `core/src/source/native_common.h` (USB error
  texts)
- Kristijonas Adomaitis (OnAir) is listed on the Credits screen of SDR++ (`core/src/credits.cpp`).
- If OnAir code of 4.1 or later is ported, the credit line required by its `ADDITIONAL-TERMS.md`
  must also be shown on the Credits screen.
- OnAir adds attribution terms under GPL section 7(b) (`ADDITIONAL-TERMS.md`). These two lines must
  stay with the code, and both are reproduced in the header of every file named above:

  > Based on OnAir by Kristijonas Adomaitis - https://on-air.dev
  > OnAir contributors: https://github.com/adswill/OnAir/blob/main/CONTRIBUTORS.md

## SDDC project (USB protocol and firmware image)

- The MIT License (MIT), Copyright (c) 2017-2020 Oscar Steila ik1xpv<at>gmail.com
- Files taken: the USB request protocol of `renardspark/SDDC_Driver` (`Interface.h`, `Core/usb`,
  `Core/radio/RX888R2Radio.cpp`), which is ik1xpv's ExtIO_sddc / libsddc
- `firmware/SDDC_FX3.img` is the SDDC_Driver v2.0.0 release asset. It contains Cypress/Infineon FX3
  SDK object code, redistributable as FX3 firmware under that SDK's licence. It is loaded into the
  RAM of the radio and never linked into SDR++.
- The full MIT text sits in `firmware/LICENSE-SDDC_Driver.txt` and is installed with the module.

## What SDR++ supplies

The module wrapper, the menu, the sample stream, the firmware search order and the Windows driver
diagnostics. See the header of `src/main.cpp`.
