#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
python3 - <<'PY'
import importlib.util
import json
spec = importlib.util.spec_from_file_location("material_shapes_export", "scripts/material_shapes/export.py")
export = importlib.util.module_from_spec(spec)
spec.loader.exec_module(export)
for source in json.loads((export.DATA / "provenance.json").read_text())["sources"]:
    assert export.digest(export.DATA / source["path"]) == source["sha256"], source["path"]
shapes = json.loads((export.DATA / "material_shapes.json").read_text())
assert export.cpp_data(shapes) == (export.DATA / "material_shapes_data.inc").read_bytes()
print("PASS: pinned source hashes and JSON-to-C++ data generation (offline, no JVM)")
PY
build=build/md3-shapes-checks
mkdir -p "$build/objects"
flags=(-std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
  '-DIMGUI_USER_CONFIG="aimgui_imconfig.h"'
  -Iconfig -Ithird_party/imgui -Ijni/include -Ijni/src)
printf '%s\n' "${CXX:-g++}" "${flags[@]}" > "$build/compiler.new"
if [[ -f "$build/compiler" ]] && ! cmp -s "$build/compiler" "$build/compiler.new"; then
  rm -f "$build"/objects/*.o
fi
mv "$build/compiler.new" "$build/compiler"
sources=(tests/md3_shapes_test.cpp jni/src/ui/md3/md3_shapes.cpp
  jni/src/ui/md3/md3_theme.cpp
  third_party/imgui/{imgui,imgui_draw,imgui_tables,imgui_widgets}.cpp)
objects=()
pids=()
for source in "${sources[@]}"; do
  object="$build/objects/$(basename "${source%.cpp}").o"
  headers=(config third_party/imgui)
  if [[ "$source" != third_party/imgui/* ]]; then
    headers+=(jni/include/ui jni/src/ui/md3 tests third_party/material_shapes)
  fi
  if [[ ! -f "$object" || "$source" -nt "$object" ]] || [[ -n "$(find "${headers[@]}" -type f \( -name '*.h' -o -name '*.inc' \) -newer "$object" -print -quit 2>/dev/null)" ]]; then
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
"${CXX:-g++}" -fsanitize=address,undefined "${objects[@]}" -o "$build/md3-shapes-test"
"$build/md3-shapes-test" "$@"
