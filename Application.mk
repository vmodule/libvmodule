
# Uncomment this if youre using STL in your project
# See CPLUSPLUS-SUPPORT.html in the NDK documentation for more information
# APP_STL := stlport_static 
APP_ABI := armeabi-v7a armeabi arm64-v8a
NDK_TOOLCHAIN_VERSION := 4.9
APP_PLATFORM := android-L
APP_STL := gnustl_static
NDK_PROJECT_PATH=./
APP_BUILD_SCRIPT=./Android.mk
NDK_APPLICATION_MK=./Application.mk
#APP_CFLAGS += -DTARGET_POSIX -DTARGET_ANDROID
#APP_CPPFLAGS += -std=gnu++11 -Wextra -fexceptions -DTARGET_POSIX -DTARGET_ANDROID
#ndk-build NDK_PROJECT_PATH=./ APP_BUILD_SCRIPT=./Android.mk NDK_APPLICATION_MK=./Application.mk
#ndk-build NDK_PROJECT_PATH=./ APP_BUILD_SCRIPT=./Android.mk NDK_APPLICATION_MK=./Application.mk clean
