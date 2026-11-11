#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/config-checks
"${CXX:-g++}" -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Itests/sensor_stubs -Ithird_party/imgui -Ijni/include -Ijni/src \
  tests/config_test.cpp jni/src/core/config.cpp -o build/config-checks/config-test
build/config-checks/config-test
