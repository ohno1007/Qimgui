#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/recording-mirror-checks
"${CXX:-g++}" -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -DSURFACE_LOG_ENABLE=0 -Itests/surface_stubs -Ijni/src \
    tests/recording_mirror_test.cpp -ldl -o build/recording-mirror-checks/recording-mirror-test
for scenario in lifecycle mirror-1 mirror-3 missing-mirror unknown-mirror; do
    build/recording-mirror-checks/recording-mirror-test "$scenario"
done
