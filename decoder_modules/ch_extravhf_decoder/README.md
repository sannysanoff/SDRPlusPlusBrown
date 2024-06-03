# sdrpp-vhfvoiceradio

======== REFERENCE =====

This code has been used by gentle permission of author, https://github.com/cropinghigh/sdrpp-vhfvoiceradio 

======== ORIGINAL README =====

VHF voice radio  plugin for SDR++

Includes:
- Increased max.bandwidths for demodulators(affects performance, i know)
- Integrated DSD(DMR,P25,NXDN demodulators and decoders)

Binary installing:

Visit the Actions page, find latest commit build artifacts, download vhfvoiceradio.so and put it to /usr/lib/sdrpp/plugins/, skipping to the step 3. Don't forget to install ITPP!

Building:

  1.  Install SDR++ core headers to /usr/include/sdrpp_core/, if not installed. Refer to https://cropinghigh.github.io/sdrpp-moduledb/headerguide.html about how to do that

      OR if you don't want to use my header system, add -DSDRPP_MODULE_CMAKE="/path/to/sdrpp_build_dir/sdrpp_module.cmake" to cmake launch arguments

      Install ITPP package.

  2.  Build:

          mkdir build
          cd build
          cmake ..
          make
          sudo make install

  3.  Enable new module by adding it via Module manager
