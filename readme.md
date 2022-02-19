# SDR++ (brown fork), The bloat-free SDR software<br>

Please see [upstream project page](https://github.com/AlexandreRouma/SDRPlusPlus) for the main list of its features.

This brown fork has several goals:

* to maintain compatibility with upstream project (regular merges from upstream)
* to add Hermes Lite 2 support 
* to add various features that are not in the upstream and have hard time being merged there due to various reasons.

## Installing the fork

Note: Fork may or may not work alongside the original project at present moment. In any case, try. On Windows, should be simpler to run it alongside the original project.

go to the  [recent builds page](https://github.com/sannysanoff/SDRPlusPlus/actions/workflows/build_all.yml), find topmost green build,
click on it, then go to the bottom of the page and find artifact for your system (Linux, Windows, MacOS). It looks like this:
![Example of artifact](https://i.imgur.com/iq8t0Fa.png)

## Features

2022.02.19 (initial release):

* Hermes Lite 2 support (receive only)
* Noise reduction to benefit SSB/AM - wideband and audio frequency. Wideband is visible on the waterfall. Can turn on both. ***Logmmse*** algorithm is used.
* Mouse wheel scrolling of sliders
* Multilanguage support in fonts, filenames and installation path (UTF-8)
* Saving of zoom parameter between sessions
* Interface scaling (seems upstream decided to implement it)
* Audio AGC controllable via slider (default/original is agressive)
* SNR meter charted below SNR meter - good for comparing antennas
* noise floor calculation differs from original.

## Feedback

Have an issue? Works worse than original? File an [issue](https://github.com/sannysanoff/SDRPlusPlus/issues).

Good luck.

## Thanks

Thanks and due respect to original author. 
