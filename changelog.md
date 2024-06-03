# Changelog

Newest entries are at the top.

2024.06.02

* SDR++Brown server: baseband compression, TX capability (Hermes Lite 2), buffering, password authentication.
* File replay - combobox file selection, when in server mode.

2024.05.12

* Added extra UHF/VHF modes module, refactored work of cropinghigh.

2024.04.07

* SDR++ server: client disconnection is detected, stream is stopped. Client IP address is logged properly.
* soapysdr sources: merged work of @alexander-sholohov with extended UI config of soapy sdr sources.
* bluetooth mic / playback on android is working ok
* airspy hf+ fixed/made it properly: fill-in option 
 
2024.04.03

* Android only: added Audio Device selection (both mic and playback).
 
2024.03.01

* Reduced trx parts which are not relevant during receive mode (was: microphone full processing)
* Macos: speed up FFT, check new modes in "FFT Window Size"
* Other performance improvements: option to reduce framerate, switch off oscilloscope. 
* Oscilloscope made simpler (and faster)
* Added battery indicator on display, added time display (todo: fix collision with RDS)
* Added preliminary SWR scan + results plotting (Hermes Lite 2)
* SDR View cleanup (removed mostly all text)
* Merged recent changes from upstream.

2024.02.05

* Hermes Lite 2 driver: added configuration for band filters per band. Automatic band switching.
* Merged recent changes from upstream.

2023.11.28

* added rtlsdr v4 library to the android driver core.

2023.09.30

* fixed MacOS build/packagine, so it includes hermes, ft8, noise reduction and other Brown features. Note: ft8 is still having issues on mac.
* added microphone input to portaudio driver, which is default on MacOS. 

2023.09.17

* Removed clicks when switching radio modes (e.g AM to SSB)
* Frequency manager - non-overlapping rendering labels on the FFT
* QSO Logging / spotting - added temporary frequency labels on FFT.
* Android: added explicit permission request button in "Debug" section.

2023.06.18

* Added one-shot beacon (call CQ) transmit for WSPR/FT8/CW
* Added connection to RBN/PSKReporter/SWPRnet to obtain reports.
* WebSDR view plugin allows to add set of Kiwisdr servers and view (only) them below waterfall, with SNR estimation.

2023.06.05

* Added QSO audio recording. Click on the recorded QSO, and in the dialog it is possible to change text, to listen to the record.
* Added "Call CQ" recorders/player. It is possible to record "CQ 20 meters, CQ 20 meters, ..." and replay many times on the air.
* Added the Kiwisdr dialog to select the KiwiSDR server. No two finger zoom, alas. Only buttons. 

2023.05.06

* Multithreaded decoding option for FT8/FT4, however works only marginally better.

2023.05.03

* Improved TX pipeline (lowpass/compression part). Fixed issue with random non-start of hermes lite 2 driver.
* Added QSO logging
* Modified TX button to have both PTT and on/off functionality.

2023.05.01

* Various TX issues fixed. Added Microphone squelch, so it does not transmit noise between words. When working over wifi, it still occasionally underflows the transciever, but I made everything from my side.

2023.04.22

* Added OMLSA_MCRA Noise Reduction (NR2 on the menu). It's only AF NR, but it really rocks!! Carefully choose what you select.
* Improved TRX UI and audio pipeline. As part of audio configuration, added Record/Playback functionality to check settings. Added small waterfall for audio frequency.
* Added TX button (volume up)
* Added noise reduction, DX compression to TX sound pipeline. Note, on android, noise reduction is not needed because android does it as part of microphone input API. However, for cheap desktop microphones, it rocks. DX compression now only has some defaults, it is not finalized and it's definitely an area for future development.

2023.03.25

* Added parallel (multithreaded) waterfall update. 2 threads give the best result (maybe number memory channels?).
* Added ALL.TXT log output from FT8/FT4 decoder.

2023.03.13

* Added kiwisdr source, no good UI yet. 

2023.03.05

* rewrote ft8 layout, animations. Added bandwidth slider as one user suggested on discord. 
* Added UI scale less than 100% as another user suggested on discord.

2023.03.01

* was playing with slow resizing (zooming) of the waterfall. Found a place to optimize it using SSE vector operations in most deep loop.
  Gained 300% speedup. Still single-threaded, multithreaded will come later.

2023.02.15

* added experimental FT4/FT8 decoding from MSHV code, tweaked for SDR++. All credit goes to MSHV author and down the line.


2023.01.07

* added transmit mode for Hermes Lite 2. Note: using my own driver (hl2_), not the one from upstream.
* added mobile screen mode for android/landscape. With big buttons. Has big ambitions, but in early stage.

2022.09.06

* by this time, merge of upstream has been already completed. The log reducion has been greatly optimized compared to previous version.
* you need to enable plugins to use most of fork functionality: noise_reduction_logmmse and hl2_source
* improved waterfall performance


2022.06.05

* merging from upstream. Project is still on hold because war in my village (grid myGrid KO80ce).



2022.02.20

* for Airspy HF+ Discovery, added Fill-In option which cuts off attenuated far sides of the spectrum.
* added secondary audio stream, so you can output same radio to multiple audio cards / virtual cables
* added single-channel (left/right) option for output audio


2022.02.19 initial release.

* Audio AGC controllable via slider (note: default/original is aggressive) - later original author reimplemented it in a different way
* Interface scaling - later original author reimplemented it in a different way
* Batch of things from features list (above)
