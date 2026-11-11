#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/host-checks
"${CXX:-g++}" -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -DIMGUI_USER_CONFIG='"aimgui_imconfig.h"' -Iconfig -Ithird_party/imgui -Ijni/src \
    tests/ui_render_test.cpp jni/src/core/text_outline.cpp \
    third_party/imgui/{imgui,imgui_draw,imgui_tables,imgui_widgets}.cpp \
    -o build/host-checks/ui-render
build/host-checks/ui-render
"${CXX:-g++}" -std=c++17 -O1 -g -pthread -fsanitize=address,undefined -fno-omit-frame-pointer \
    -ffunction-sections -fdata-sections -Wl,--gc-sections -Ithird_party/imgui \
    tests/keyboard_input_test.cpp -o build/host-checks/keyboard-input
build/host-checks/keyboard-input
