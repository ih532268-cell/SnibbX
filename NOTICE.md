# Attribution

This repository combines code under three separate MIT copyrights. All three
carry the same permissions and conditions; this file exists only to make it
clear which lines apply to which files, since `LICENSE` itself now holds two
copyright blocks.

## 1. The original snibbetracker engine and demo songs

Copyright (c) 2019 Harry Lundström — https://github.com/lundstroem/snibbetracker

Unmodified, except for a small number of `#if defined(platform_android)` /
`#elif defined(__ANDROID__)` blocks added to `snibbetracker/src/main.c` to make
it run on Android (see the git history of that file for the exact diff). No
other original file was changed.

    res/
    snibbetracker/src/CAllocator.{c,h}
    snibbetracker/src/CEngine.{c,h}
    snibbetracker/src/CInput.{c,h}
    snibbetracker/src/CNoiseTable.h
    snibbetracker/src/CSynth.{c,h}
    snibbetracker/src/chars_gfx.h
    snibbetracker/src/dir_posix.{c,h}
    snibbetracker/src/dir_win.{c,h}
    snibbetracker/src/file_settings.h
    snibbetracker/src/main.c            (original, with Android-only additions)
    snibbetracker/src/osx_settings.{h,m}

## 2. cJSON

Copyright (c) 2009 Dave Gamble — https://github.com/DaveGamble/cJSON

Bundled unmodified by the original snibbetracker project; its own license
text is kept alongside it.

    snibbetracker/src/cJSON/

## 3. The Android port additions (this repository)

Copyright (c) 2026 A-C

A touch-input layer, Android-specific file handling, an in-app log viewer, and the Gradle/NDK
build scripts — added on top of the complete engine above so it runs on a phone.

    android/
    docs/
    tests/
    .github/workflows/

Full license text for all three: see `LICENSE`.
