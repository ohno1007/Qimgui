#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
build=build/md3-main-checks
mkdir -p "$build/objects"
flags=(-std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
  '-DIMGUI_USER_CONFIG="aimgui_imconfig.h"' -DIMGUI_ENABLE_TEST_ENGINE
  -Iconfig -Ithird_party/imgui -Ijni/include -Ijni/src)
printf '%s\n' "${CXX:-g++}" "${flags[@]}" > "$build/compiler.new"
if [[ -f "$build/compiler" ]] && ! cmp -s "$build/compiler" "$build/compiler.new"; then
  rm -f "$build"/objects/*.o
fi
mv "$build/compiler.new" "$build/compiler"
sources=(tests/md3_main_test.cpp jni/src/ui/ui.cpp jni/src/ui/main_ui.cpp jni/src/ui/md3/*.cpp
  third_party/imgui/{imgui,imgui_draw,imgui_tables,imgui_widgets}.cpp)
objects=()
pids=()
for source in "${sources[@]}"; do
  object="$build/objects/$(basename "${source%.cpp}").o"
  headers=(config third_party/imgui)
  if [[ "$source" != third_party/imgui/* ]]; then headers+=(jni/include/ui jni/src/ui/md3 tests); fi
  if [[ ! -f "$object" || "$source" -nt "$object" ]] ||
     [[ "$source" == */md3_shapes.cpp && third_party/material_shapes/material_shapes_data.inc -nt "$object" ]] ||
     [[ -n "$(find "${headers[@]}" -name '*.h' -newer "$object" -print -quit 2>/dev/null)" ]]; then
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
"${CXX:-g++}" -fsanitize=address,undefined "${objects[@]}" -o "$build/md3-main-test"
"$build/md3-main-test" "$@"
