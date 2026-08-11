#!/usr/bin/env bash
# AImGui CMake build. Requires Android NDK (r25+). Set ANDROID_NDK_HOME.
set -euo pipefail

NDK="${ANDROID_NDK_HOME:-${NDK_ROOT:-}}"
if [[ -z "$NDK" ]]; then
    echo "error: set ANDROID_NDK_HOME to your NDK root" >&2
    exit 1
fi

cd "$(dirname "$0")"

ABI="${ABI:-arm64-v8a}"
BUILD="build/${ABI}"

# The Live2D layer uses AImageDecoder (NDK API 30+). Bump the platform
# automatically when it's enabled, unless the caller overrides it.
PLATFORM="android-24"
if [[ "$*" == *AIMGUI_LIVE2D=ON* && "$*" != *ANDROID_PLATFORM* ]]; then
    PLATFORM="android-30"
fi

cmake -S . -B "$BUILD" \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$ABI" \
    -DANDROID_PLATFORM="$PLATFORM" \
    -DANDROID_STL=c++_static \
    -DCMAKE_BUILD_TYPE=Release \
    "$@"

cmake --build "$BUILD" -j"$(nproc 2>/dev/null || echo 4)"

mkdir -p "libs/${ABI}"
cp "${BUILD}/AImGui" "libs/${ABI}/AImGui"

OUT="libs/${ABI}/AImGui"
if [[ -f "$OUT" ]]; then
    SIZE=$(stat -c '%s' "$OUT" 2>/dev/null || wc -c < "$OUT")
    printf '\n  ELF   : %s\n  size  : %s bytes (%.1f KB)\n' \
        "$OUT" "$SIZE" "$(awk -v s="$SIZE" 'BEGIN{print s/1024}')"
fi
