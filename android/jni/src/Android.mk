LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

# Must be "main": SnibbeActivity.getLibraries() loads "SDL2" then "main",
# and SDL calls SDL_main() inside it.
LOCAL_MODULE := main

SDL_PATH := ../SDL
SRC := engine

# The engine does #include <SDL2/SDL.h>, but SDL ships its headers flat in include/.
# The workflow creates jni/src/sdl2_prefix/SDL2 -> ../../SDL/include so both spellings resolve.
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/sdl2_prefix \
    $(LOCAL_PATH)/$(SDL_PATH)/include \
    $(LOCAL_PATH)/$(SRC) \
    $(LOCAL_PATH)/$(SRC)/cJSON \
    $(LOCAL_PATH)

# NOTE: dir_posix.c only (dir_win.c is Windows-only); osx_settings.m is macOS-only.
LOCAL_SRC_FILES := \
    $(SRC)/CAllocator.c \
    $(SRC)/CEngine.c \
    $(SRC)/CInput.c \
    $(SRC)/CSynth.c \
    $(SRC)/cJSON/cJSON.c \
    $(SRC)/dir_posix.c \
    $(SRC)/main.c \
    android_log.c

LOCAL_CFLAGS += -std=gnu99 -Wall -Wno-unused-function -Wno-format-truncation -Wno-stringop-truncation

LOCAL_SHARED_LIBRARIES := SDL2

LOCAL_LDLIBS := -lGLESv1_CM -lGLESv2 -lOpenSLES -llog -landroid -lm

include $(BUILD_SHARED_LIBRARY)
