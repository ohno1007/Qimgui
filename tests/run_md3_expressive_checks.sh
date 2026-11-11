#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
python3 - <<'PY'
from pathlib import Path
import hashlib, json
count = 0
for component in ("toolbar_split", "button_group", "progress"):
    base = Path("third_party/material_expressive") / component
    provenance = json.loads((base / "provenance.json").read_text())
    for source in provenance["sources"]:
        path = base / source.get("path", source.get("file"))
        assert hashlib.sha256(path.read_bytes()).hexdigest() == source["sha256"], str(path)
        count += 1
print(f"PASS: {count} pinned Expressive source/token reference hashes")
PY
build=build/md3-expressive-checks
mkdir -p "$build/objects"
flags=(-std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
  '-DIMGUI_USER_CONFIG="aimgui_imconfig.h"' -DIMGUI_ENABLE_TEST_ENGINE
  -Iconfig -Ithird_party/imgui -Ijni/include -Ijni/src)
printf '%s\n' "${CXX:-g++}" "${flags[@]}" > "$build/compiler.new"
if [[ -f "$build/compiler" ]] && ! cmp -s "$build/compiler" "$build/compiler.new"; then
  rm -f "$build"/objects/*.o
fi
mv "$build/compiler.new" "$build/compiler"
sources=(tests/md3_expressive_test.cpp tests/md3_toolbar_split_test.cpp
  tests/md3_button_group_test.cpp tests/md3_progress_test.cpp jni/src/ui/md3/*.cpp
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
"${CXX:-g++}" -fsanitize=address,undefined "${objects[@]}" -o "$build/md3-expressive-test"
"$build/md3-expressive-test" "$@"
