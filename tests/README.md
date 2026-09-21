# Tests

These run on a normal computer (no phone needed). They cover the parts of the port that are pure
logic; anything that talks to Android (file picker, share screens) can only be checked on a device.

## Song name / import validation (Java)
Needs a JDK (11+).

    mkdir -p /tmp/out && javac -d /tmp/out android/java/org/libsdl/app/SongFiles.java tests/java/SongFilesTest.java
    java -Dfile.encoding=UTF-8 -cp /tmp/out SongFilesTest      # run from the repository root

`SongFilesTest` reads the bundled demo songs from `res/demos`, so run it from the repository root.

## Touch keyboard (C)
Needs SDL2 development files (`libsdl2-dev`).

    gcc -std=gnu99 -Wall -Iandroid/jni/src $(sdl2-config --cflags) tests/native/touch_kb_test.c \
        android/jni/src/touch_kb.c $(sdl2-config --libs) -o /tmp/tkb_test
    SDL_VIDEODRIVER=dummy /tmp/tkb_test
