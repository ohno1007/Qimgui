#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
build=build/overlay-runtime-checks
mkdir -p "$build/objects"
flags=(-std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
  '-DIMGUI_USER_CONFIG="aimgui_imconfig.h"'
  -Itests/overlay_stubs -Ijni/include -Ithird_party/imgui -Iconfig -Itests)
printf '%s\n' "${CXX:-g++}" "${flags[@]}" > "$build/compiler.new"
if [[ -f "$build/compiler" ]] && ! cmp -s "$build/compiler" "$build/compiler.new"; then
  rm -f "$build"/objects/*.o
fi
mv "$build/compiler.new" "$build/compiler"
sources=(tests/overlay_runtime_test.cpp jni/src/runtime/android_overlay.cpp
  third_party/imgui/{imgui,imgui_draw,imgui_tables,imgui_widgets}.cpp)
objects=();pids=()
for source in "${sources[@]}"; do
  object="$build/objects/$(basename "${source%.cpp}").o"
  if [[ ! -f "$object" || "$source" -nt "$object" ]] ||
    [[ -n "$(find tests/overlay_stubs config third_party/imgui jni/include/qimgui \( -name '*.h' -o -name '*.hpp' \) -newer "$object" -print -quit 2>/dev/null)" ]]; then
    "${CXX:-g++}" "${flags[@]}" -c "$source" -o "$object" &
    pids+=("$!")
    if [[ ${#pids[@]} -ge 3 ]]; then
      for pid in "${pids[@]}"; do wait "$pid"; done
      pids=()
    fi
  fi
  objects+=("$object")
done
for pid in "${pids[@]}"; do wait "$pid"; done
"${CXX:-g++}" -fsanitize=address,undefined "${objects[@]}" -o "$build/overlay-runtime-test"
"$build/overlay-runtime-test"
