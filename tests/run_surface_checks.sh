#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/surface-checks
"${CXX:-g++}" -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -DSURFACE_LOG_ENABLE=0 -Itests/surface_stubs -Ijni/src \
    tests/surface_startup_test.cpp -ldl -o build/surface-checks/surface-startup-test
for scenario in missing-library missing-display-symbols missing-display-state missing-surface-symbols null-surface legacy-create-surface const-metadata-create-surface unknown-create-surface; do
    build/surface-checks/surface-startup-test "$scenario"
done
