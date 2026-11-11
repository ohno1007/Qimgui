#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/screen-mirror-checks
"${CXX:-g++}" -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -DSURFACE_LOG_ENABLE=0 -Itests/surface_stubs -Ijni/src \
  tests/screen_mirror_test.cpp jni/src/core/screen_mirror.cpp \
  -Wl,--wrap=dlopen -Wl,--wrap=dlsym -ldl \
  -o build/screen-mirror-checks/screen-mirror-test
build/screen-mirror-checks/screen-mirror-test
