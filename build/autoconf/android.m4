dnl This Source Code Form is subject to the terms of the Mozilla Public
dnl License, v. 2.0. If a copy of the MPL was not distributed with this
dnl file, You can obtain one at http://mozilla.org/MPL/2.0/.

AC_DEFUN([MOZ_ANDROID_NDK],
[

MOZ_ARG_WITH_STRING(android-ndk,
[  --with-android-ndk=DIR
                          location where the Android NDK can be found],
    android_ndk=$withval)

MOZ_ARG_WITH_STRING(android-toolchain,
[  --with-android-toolchain=DIR
                          location of the android toolchain],
    android_toolchain=$withval)

MOZ_ARG_WITH_STRING(android-gnu-compiler-version,
[  --with-android-gnu-compiler-version=VER
                          gnu compiler version to use],
    android_gnu_compiler_version=$withval)

MOZ_ARG_ENABLE_BOOL(android-libstdcxx,
[  --enable-android-libstdcxx
                          use GNU libstdc++ instead of STLPort],
    MOZ_ANDROID_LIBSTDCXX=1,
    MOZ_ANDROID_LIBSTDCXX= )

define([MIN_ANDROID_VERSION], [9])
android_version=MIN_ANDROID_VERSION

MOZ_ARG_WITH_STRING(android-version,
[  --with-android-version=VER
                          android platform version, default] MIN_ANDROID_VERSION,
    android_version=$withval)

if test $android_version -lt MIN_ANDROID_VERSION ; then
    AC_MSG_ERROR([--with-android-version must be at least MIN_ANDROID_VERSION.])
fi

case "$target" in
arm-linux*-android*|*-linuxandroid*)
    android_tool_prefix="arm-linux-androideabi"
    ;;
i?86-*android*)
    android_tool_prefix="i686-linux-android"
    ;;
mipsel-*android*)
    android_tool_prefix="mipsel-linux-android"
    ;;
*)
    android_tool_prefix="$target_os"
    ;;
esac

case "$target" in
*-android*|*-linuxandroid*)
    if test -z "$android_ndk" ; then
        AC_MSG_ERROR([You must specify --with-android-ndk=/path/to/ndk when targeting Android.])
    fi

    if test -z "$android_toolchain" ; then
        AC_MSG_CHECKING([for android toolchain directory])

        kernel_name=`uname -s | tr "[[:upper:]]" "[[:lower:]]"`

        for version in $android_gnu_compiler_version 4.7 4.6 4.4.3 ; do
            case "$target_cpu" in
            arm)
                target_name=arm-linux-androideabi-$version
                ;;
            i?86)
                target_name=x86-$version
                ;;
            mipsel)
                target_name=mipsel-linux-android-$version
                ;;
            *)
                AC_MSG_ERROR([target cpu is not supported])
                ;;
            esac
            case "$host_cpu" in
            i*86)
                android_toolchain="$android_ndk"/toolchains/$target_name/prebuilt/$kernel_name-x86
                ;;
            x86_64)
                android_toolchain="$android_ndk"/toolchains/$target_name/prebuilt/$kernel_name-x86_64
                if ! test -d "$android_toolchain" ; then
                    android_toolchain="$android_ndk"/toolchains/$target_name/prebuilt/$kernel_name-x86
                fi
                ;;
            *)
                AC_MSG_ERROR([No known toolchain for your host cpu])
                ;;
            esac
            if test -d "$android_toolchain" ; then
                android_gnu_compiler_version=$version
                break
            elif test -n "$android_gnu_compiler_version" ; then
                AC_MSG_ERROR([not found. Your --with-android-gnu-compiler-version may be wrong.])
            fi
        done

        if test -z "$android_gnu_compiler_version" ; then
            AC_MSG_ERROR([not found. You have to specify --with-android-toolchain=/path/to/ndk/toolchain.])
        else
            AC_MSG_RESULT([$android_toolchain])
        fi
        NSPR_CONFIGURE_ARGS="$NSPR_CONFIGURE_ARGS --with-android-toolchain=$android_toolchain"
    fi

    NSPR_CONFIGURE_ARGS="$NSPR_CONFIGURE_ARGS --with-android-version=$android_version"

    AC_MSG_CHECKING([for android platform directory])

    case "$target_cpu" in
    arm)
        target_name=arm
        ;;
    i?86)
        target_name=x86
        ;;
    mipsel)
        target_name=mips
        ;;
    esac

    android_platform="$android_ndk"/platforms/android-"$android_version"/arch-"$target_name"

    if test -d "$android_platform" ; then
        AC_MSG_RESULT([$android_platform])
    else
        AC_MSG_ERROR([not found. Please check your NDK. With the current configuration, it should be in $android_platform])
    fi

    dnl Old NDK support. If minimum requirement is changed to NDK r8b,
    dnl please remove this.
    case "$target_cpu" in
    i?86)
        if ! test -e "$android_toolchain"/bin/"$android_tool_prefix"-gcc; then
            dnl Old NDK toolchain name
            android_tool_prefix="i686-android-linux"
        fi
        ;;
    esac

    dnl set up compilers
    TOOLCHAIN_PREFIX="$android_toolchain/bin/$android_tool_prefix-"
    AS="$android_toolchain"/bin/"$android_tool_prefix"-as
    if test -z "$CC"; then
        CC="$android_toolchain"/bin/"$android_tool_prefix"-gcc
    fi
    if test -z "$CXX"; then
        CXX="$android_toolchain"/bin/"$android_tool_prefix"-g++
    fi
    if test -z "$CPP"; then
        CPP="$android_toolchain"/bin/"$android_tool_prefix"-cpp
    fi
    LD="$android_toolchain"/bin/"$android_tool_prefix"-ld
    AR="$android_toolchain"/bin/"$android_tool_prefix"-ar
    RANLIB="$android_toolchain"/bin/"$android_tool_prefix"-ranlib
    STRIP="$android_toolchain"/bin/"$android_tool_prefix"-strip
    OBJCOPY="$android_toolchain"/bin/"$android_tool_prefix"-objcopy

    CPPFLAGS="-idirafter $android_platform/usr/include $CPPFLAGS"
    CFLAGS="-mandroid -fno-short-enums -fno-exceptions $CFLAGS"
    CXXFLAGS="-mandroid -fno-short-enums -fno-exceptions -Wno-psabi $CXXFLAGS"
    ASFLAGS="-idirafter $android_platform/usr/include -DANDROID $ASFLAGS"

    dnl Add -llog by default, since we use it all over the place.
    dnl Add --allow-shlib-undefined, because libGLESv2 links to an
    dnl undefined symbol (present on the hardware, just not in the
    dnl NDK.)
    LDFLAGS="-mandroid -L$android_platform/usr/lib -Wl,-rpath-link=$android_platform/usr/lib --sysroot=$android_platform -llog -Wl,--allow-shlib-undefined $LDFLAGS"
    dnl prevent cross compile section from using these flags as host flags
    if test -z "$HOST_CPPFLAGS" ; then
        HOST_CPPFLAGS=" "
    fi
    if test -z "$HOST_CFLAGS" ; then
        HOST_CFLAGS=" "
    fi
    if test -z "$HOST_CXXFLAGS" ; then
        HOST_CXXFLAGS=" "
    fi
    if test -z "$HOST_LDFLAGS" ; then
        HOST_LDFLAGS=" "
    fi

    ANDROID_NDK="${android_ndk}"
    ANDROID_TOOLCHAIN="${android_toolchain}"
    ANDROID_PLATFORM="${android_platform}"

    AC_DEFINE(ANDROID)
    AC_SUBST(ANDROID_NDK)
    AC_SUBST(ANDROID_TOOLCHAIN)
    AC_SUBST(ANDROID_PLATFORM)

    ;;
esac

])
    
AC_DEFUN([MOZ_ANDROID_STLPORT],
[

if test "$OS_TARGET" = "Android" -a -z "$gonkdir"; then
    case "${CPU_ARCH}-${MOZ_ARCH}" in
    arm-armv7*)
        ANDROID_CPU_ARCH=armeabi-v7a
        ;;
    arm-*)
        ANDROID_CPU_ARCH=armeabi
        ;;
    x86-*)
        ANDROID_CPU_ARCH=x86
        ;;
    mips-*) # When target_cpu is mipsel, CPU_ARCH is mips
        ANDROID_CPU_ARCH=mips
        ;;
    esac

    AC_SUBST(ANDROID_CPU_ARCH)

    if test -z "$STLPORT_CPPFLAGS$STLPORT_LDFLAGS$STLPORT_LIBS"; then
        if test -n "$MOZ_ANDROID_LIBSTDCXX" ; then
            if test -e "$android_ndk/sources/cxx-stl/gnu-libstdc++/$android_gnu_compiler_version/libs/$ANDROID_CPU_ARCH/libgnustl_static.a"; then
                # android-ndk-r8b
                STLPORT_LIBS="-L$android_ndk/sources/cxx-stl/gnu-libstdc++/$android_gnu_compiler_version/libs/$ANDROID_CPU_ARCH/ -lgnustl_static"
                STLPORT_CPPFLAGS="-I$android_ndk/sources/cxx-stl/gnu-libstdc++/$android_gnu_compiler_version/include -I$android_ndk/sources/cxx-stl/gnu-libstdc++/$android_gnu_compiler_version/libs/$ANDROID_CPU_ARCH/include -I$android_ndk/sources/cxx-stl/gnu-libstdc++/$android_gnu_compiler_version/include/backward"
            elif test -e "$android_ndk/sources/cxx-stl/gnu-libstdc++/libs/$ANDROID_CPU_ARCH/libgnustl_static.a"; then
                # android-ndk-r7, android-ndk-r7b, android-ndk-r8
                STLPORT_LIBS="-L$android_ndk/sources/cxx-stl/gnu-libstdc++/libs/$ANDROID_CPU_ARCH/ -lgnustl_static"
                STLPORT_CPPFLAGS="-I$android_ndk/sources/cxx-stl/gnu-libstdc++/include -I$android_ndk/sources/cxx-stl/gnu-libstdc++/libs/$ANDROID_CPU_ARCH/include"
            elif test -e "$android_ndk/sources/cxx-stl/gnu-libstdc++/libs/$ANDROID_CPU_ARCH/libstdc++.a"; then
                # android-ndk-r5c, android-ndk-r6, android-ndk-r6b
                STLPORT_CPPFLAGS="-I$android_ndk/sources/cxx-stl/gnu-libstdc++/include -I$android_ndk/sources/cxx-stl/gnu-libstdc++/libs/$ANDROID_CPU_ARCH/include"
                STLPORT_LIBS="-L$android_ndk/sources/cxx-stl/gnu-libstdc++/libs/$ANDROID_CPU_ARCH/ -lstdc++"
            else
                AC_MSG_ERROR([Couldn't find path to gnu-libstdc++ in the android ndk])
            fi
        else
            STLPORT_CPPFLAGS="-isystem $_topsrcdir/build/stlport/stlport -isystem $android_ndk/sources/cxx-stl/system/include"
            STLPORT_LIBS="$_objdir/build/stlport/libstlport_static.a -static-libstdc++"
        fi
    fi
    CXXFLAGS="$CXXFLAGS $STLPORT_CPPFLAGS"
fi
AC_SUBST([MOZ_ANDROID_LIBSTDCXX])
AC_SUBST([STLPORT_LIBS])

])

AC_DEFUN([MOZ_ANDROID_SDK],
[

MOZ_ARG_WITH_STRING(android-sdk,
[  --with-android-sdk=DIR
                          location where the Android SDK can be found (base directory, e.g. .../android/platforms/android-6)],
    android_sdk=$withval)

android_sdk_root=${withval%/platforms/android-*}

case "$target" in
*-android*|*-linuxandroid*)
    if test -z "$android_sdk" ; then
        AC_MSG_ERROR([You must specify --with-android-sdk=/path/to/sdk when targeting Android.])
    else
        if ! test -e "$android_sdk"/source.properties ; then
            AC_MSG_ERROR([The path in --with-android-sdk isn't valid (source.properties hasn't been found).])
        fi

        # Get the api level from "$android_sdk"/source.properties.
        android_api_level=`$AWK -F = changequote(<<, >>)'<<$>>1 == "AndroidVersion.ApiLevel" {print <<$>>2}'changequote([, ]) "$android_sdk"/source.properties`

        if test -z "$android_api_level" ; then
            AC_MSG_ERROR([Unexpected error: no AndroidVersion.ApiLevel field has been found in source.properties.])
        fi

        if ! test "$android_api_level" -eq "$android_api_level" ; then
            AC_MSG_ERROR([Unexpected error: the found android api value isn't a number! (found $android_api_level)])
        fi

        if test $android_api_level -lt $1 ; then
            AC_MSG_ERROR([The given Android SDK provides API level $android_api_level ($1 or higher required).])
        fi
    fi

    android_tools="$android_sdk_root"/tools
    android_platform_tools="$android_sdk_root"/platform-tools
    if test ! -d "$android_platform_tools" ; then
        android_platform_tools="$android_sdk"/tools # SDK Tools < r8
    fi
    # The build tools got moved around to different directories in
    # SDK Tools r22.  Try to locate them.
    android_build_tools=""
    for suffix in android-4.4 android-4.3 android-4.2.2 19.0.0 18.1.0 18.0.1 18.0.0 17.0.0; do
        tools_directory="$android_sdk_root/build-tools/$suffix"
        if test -d "$tools_directory" ; then
            android_build_tools="$tools_directory"
            break
        fi
    done
    if test -z "$android_build_tools" ; then
        android_build_tools="$android_platform_tools" # SDK Tools < r22
    fi
    ANDROID_SDK="${android_sdk}"
    ANDROID_SDK_ROOT="${android_sdk_root}"
    if test -e "${ANDROID_SDK_ROOT}/extras/android/compatibility/v4/android-support-v4.jar" ; then
        ANDROID_COMPAT_LIB="${ANDROID_SDK_ROOT}/extras/android/compatibility/v4/android-support-v4.jar"
    else
        ANDROID_COMPAT_LIB="${ANDROID_SDK_ROOT}/extras/android/support/v4/android-support-v4.jar";
    fi
    ANDROID_TOOLS="${android_tools}"
    ANDROID_PLATFORM_TOOLS="${android_platform_tools}"
    ANDROID_BUILD_TOOLS="${android_build_tools}"
    AC_SUBST(ANDROID_SDK_ROOT)
    AC_SUBST(ANDROID_SDK)
    AC_SUBST(ANDROID_COMPAT_LIB)
    if ! test -e $ANDROID_COMPAT_LIB ; then
        AC_MSG_ERROR([You must download the Android support library when targeting Android.   Run the Android SDK tool and install Android Support Library under Extras.  See https://developer.android.com/tools/extras/support-library.html for more info. (looked for $ANDROID_COMPAT_LIB)])
    fi

    MOZ_PATH_PROG(ZIPALIGN, zipalign, :, [$ANDROID_TOOLS])
    MOZ_PATH_PROG(DX, dx, :, [$ANDROID_BUILD_TOOLS])
    MOZ_PATH_PROG(AAPT, aapt, :, [$ANDROID_BUILD_TOOLS])
    MOZ_PATH_PROG(AIDL, aidl, :, [$ANDROID_BUILD_TOOLS])
    MOZ_PATH_PROG(ADB, adb, :, [$ANDROID_PLATFORM_TOOLS])

    if test -z "$ZIPALIGN" -o "$ZIPALIGN" = ":"; then
      AC_MSG_ERROR([The program zipalign was not found.  Use --with-android-sdk={android-sdk-dir}.])
    fi
    if test -z "$DX" -o "$DX" = ":"; then
      AC_MSG_ERROR([The program dx was not found.  Use --with-android-sdk={android-sdk-dir}.])
    fi
    if test -z "$AAPT" -o "$AAPT" = ":"; then
      AC_MSG_ERROR([The program aapt was not found.  Use --with-android-sdk={android-sdk-dir}.])
    fi
    if test -z "$AIDL" -o "$AIDL" = ":"; then
      AC_MSG_ERROR([The program aidl was not found.  Use --with-android-sdk={android-sdk-dir}.])
    fi
    if test -z "$ADB" -o "$ADB" = ":"; then
      AC_MSG_ERROR([The program adb was not found.  Use --with-android-sdk={android-sdk-dir}.])
    fi
    ;;
esac

])
