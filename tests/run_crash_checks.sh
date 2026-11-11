#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/crash-checks
"${CXX:-g++}" -std=c++17 -O1 -g -shared -fPIC \
    tests/crash_diagnostics_module.cpp -o build/crash-checks/libcrash-diagnostics-test-module.so
# Intentional fatal signals test disposition, without accessing invalid memory.
"${CXX:-g++}" -std=c++17 -O1 -g -pthread -Ijni/src \
    tests/crash_diagnostics_test.cpp jni/src/core/crash_diagnostics.cpp \
    -ldl -o build/crash-checks/crash-diagnostics-test
build/crash-checks/crash-diagnostics-test ./build/crash-checks/libcrash-diagnostics-test-module.so
