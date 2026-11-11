#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
build=build/md3-checks
mkdir -p "$build/objects"
flags=(-std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
  '-DIMGUI_USER_CONFIG="aimgui_imconfig.h"' -Iconfig -Ithird_party/imgui -Ijni/include -Ijni/src)
printf '%s\n' "${CXX:-g++}" "${flags[@]}" > "$build/compiler.new"
if [[ -f "$build/compiler" ]] && ! cmp -s "$build/compiler" "$build/compiler.new"; then
  rm -f "$build"/objects/*.o
fi
mv "$build/compiler.new" "$build/compiler"
sources=(tests/md3_test.cpp jni/src/ui/md3/*.cpp third_party/imgui/{imgui,imgui_draw,imgui_tables,imgui_widgets}.cpp)
objects=()
for source in "${sources[@]}"; do
  object="$build/objects/$(basename "${source%.cpp}").o"
  if [[ ! -f "$object" || "$source" -nt "$object" ]] || [[ -n "$(find config third_party/imgui jni/include/ui jni/src/ui/md3 tests -name '*.h' -newer "$object" -print -quit 2>/dev/null)" ]]; then
    "${CXX:-g++}" "${flags[@]}" -c "$source" -o "$object"
  fi
  objects+=("$object")
done
"${CXX:-g++}" -fsanitize=address,undefined "${objects[@]}" -o "$build/md3-test"
"$build/md3-test" "$@"
