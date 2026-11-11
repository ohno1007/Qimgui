#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/mirror-checks
"${CXX:-g++}" -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -DSURFACE_LOG_ENABLE=0 -Itests/surface_stubs -Ijni/src \
    tests/mirror_abi_test.cpp -ldl -o build/mirror-checks/mirror-abi-test
for scenario in create-4 create-5 create-6 create-legacy unknown-create producer-only surface-only both-surface-overloads surface-error legacy-layout unknown-layout; do
    build/mirror-checks/mirror-abi-test "$scenario"
done
