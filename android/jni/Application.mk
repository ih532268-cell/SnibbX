# All four ABIs: armeabi-v7a and arm64-v8a cover real phones/tablets (32- and 64-bit),
# x86 and x86_64 cover emulators and x86 Chromebooks. Gradle's splits.abi block (build.gradle)
# turns these into one separate APK per ABI rather than one fat APK containing all four .so's.
APP_ABI := armeabi-v7a arm64-v8a x86 x86_64
APP_PLATFORM := android-21
APP_OPTIM := release
