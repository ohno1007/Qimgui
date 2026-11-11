#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/display-dump-checks
"${CXX:-g++}" -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Ijni/src tests/display_dump_test.cpp -o build/display-dump-checks/display-dump-test
build/display-dump-checks/display-dump-test
