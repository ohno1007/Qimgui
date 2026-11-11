#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/touch-checks
"${CXX:-g++}" -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Ijni/src tests/touch_slots_test.cpp -o build/touch-checks/touch-slots-test
build/touch-checks/touch-slots-test
