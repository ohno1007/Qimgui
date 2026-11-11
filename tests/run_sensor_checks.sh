#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
ndk="${ANDROID_NDK_HOME:-${NDK_ROOT:-}}"
headers="$ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/android"
if [[ ! -f "$headers/sensor.h" || ! -f "$headers/looper.h" ]]; then
  echo "Set ANDROID_NDK_HOME to the pinned Android NDK; tests use its actual sensor ABI." >&2
  exit 1
fi
build=build/sensor-checks
mkdir -p "$build/ndk-headers/android"
# These two official headers explicitly support glibc. Copy only them so the
# host sanitizer build keeps its own libc headers instead of the Android sysroot.
cp "$headers/sensor.h" "$headers/looper.h" "$build/ndk-headers/android/"
"${CXX:-g++}" -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Itests/sensor_stubs -I"$build/ndk-headers" -Ijni/src \
  tests/sensor_tilt_test.cpp jni/src/core/sensor_tilt.cpp \
  -Wl,--wrap=dlopen -Wl,--wrap=dlsym -Wl,--wrap=dlclose -ldl \
  -o "$build/sensor-tilt-test"
"$build/sensor-tilt-test"
