#!/usr/bin/env bash
# Host checks for the kernel UI port: the libm subset, the frame allocator, the
# integer rasteriser, and a full ImGui + MD3 frame driven through all three.
# The module compiles these same sources; see docs/KERNEL_PORT.md for what this
# cannot cover (DRM plane binding, the input handler and the NEON guard).
set -euo pipefail
cd "$(dirname "$0")/.."
build=build/kernel-checks
mkdir -p "$build/objects"

includes=(-Iconfig -Ikernel/compat -Ikernel/raster -Ikernel/ui -Ikernel/src -Ithird_party/imgui
  -Ijni/include -Ijni/src -Itests)
cxxflags=(-std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
  '-DIMGUI_USER_CONFIG="qk_imconfig.h"' "${includes[@]}")
cflags=(-std=c11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
  "${includes[@]}")

printf '%s\n' "${CXX:-g++}" "${CC:-gcc}" "${cxxflags[@]}" "${cflags[@]}" > "$build/compiler.new"
if [[ -f "$build/compiler" ]] && ! cmp -s "$build/compiler" "$build/compiler.new"; then
  rm -f "$build"/objects/*.o
fi
mv "$build/compiler.new" "$build/compiler"

c_sources=(kernel/compat/qk_math.c kernel/compat/qk_arena.c kernel/raster/qk_raster.c
  kernel/src/qk_geometry.c kernel/src/qk_file.c)
cxx_sources=(kernel/ui/qk_ui.cpp kernel/ui/qk_app.cpp jni/src/ui/md3/*.cpp jni/src/ui/ui.cpp jni/src/ui/main_ui.cpp
  third_party/imgui/{imgui,imgui_draw,imgui_tables,imgui_widgets}.cpp)

objects=()
compile() { # compile <source> <compiler> <flags...>
  local source="$1" compiler="$2"; shift 2
  local object="$build/objects/$(basename "${source%.*}").o"
  local headers=(config kernel third_party/imgui tests)
  if [[ "$source" != third_party/imgui/* ]]; then
    headers+=(jni/include/ui jni/src/ui jni/src/ui/md3 jni/src/core)
  fi
  if [[ ! -f "$object" || "$source" -nt "$object" ]] ||
     [[ "$source" == */md3_shapes.cpp && third_party/material_shapes/material_shapes_data.inc -nt "$object" ]] ||
     [[ -n "$(find "${headers[@]}" -name '*.h' -newer "$object" -print -quit 2>/dev/null)" ]]; then
    "$compiler" "$@" -c "$source" -o "$object"
  fi
  objects+=("$object")
}

for source in "${c_sources[@]}"; do compile "$source" "${CC:-gcc}" "${cflags[@]}"; done
for source in "${cxx_sources[@]}"; do compile "$source" "${CXX:-g++}" "${cxxflags[@]}"; done

for test in kernel_math kernel_arena kernel_raster kernel_geometry kernel_ui; do
  "${CXX:-g++}" "${cxxflags[@]}" -c "tests/${test}_test.cpp" -o "$build/objects/$test.o"
  "${CXX:-g++}" -fsanitize=address,undefined "$build/objects/$test.o" "${objects[@]}" \
    -o "$build/$test"
  "$build/$test" "$@"
done

# Freestanding build: the UI sources compiled the way the module compiles them,
# against kernel/compat/include instead of the host's C and C++ libraries. The
# objects are then checked for undefined symbols, so a dependency the kernel
# cannot satisfy fails here rather than at modpost time on a device.
freestanding="$build/freestanding"
mkdir -p "$freestanding"
# arm64 turns round/trunc/sqrt into single instructions; on an x86 host SSE4.1
# does the same, so the check sees the same set of calls the module will make.
inline_math=(-fno-math-errno)
if echo 'int main(){return 0;}' | "${CXX:-g++}" -msse4.1 -x c++ - -o /dev/null 2>/dev/null; then
  inline_math+=(-msse4.1)
fi
kernelflags=(-std=c++17 -O1 -g -nostdinc++ -ffreestanding -fno-exceptions -fno-rtti
  -fno-threadsafe-statics -fno-use-cxa-atexit "${inline_math[@]}"
  '-DIMGUI_USER_CONFIG="qk_imconfig.h"' -Ikernel/compat/include "${includes[@]}")
freestanding_objects=()
for source in kernel/compat/qk_cxx.cpp "${cxx_sources[@]}"; do
  object="$freestanding/$(basename "${source%.cpp}").o"
  "${CXX:-g++}" "${kernelflags[@]}" -c "$source" -o "$object"
  freestanding_objects+=("$object")
done

# Everything the kernel already provides, plus what this module defines itself.
allowed='^(qk_|ImGui|Im[A-Z]|_Z|aimgui|operator |__gnu|GCC_except)'
provided='^(memcpy|memmove|memset|memcmp|memchr|strlen|strcmp|strncmp|strcpy|strncpy|strchr|strrchr|strstr|vsnprintf|snprintf|sscanf|_GLOBAL_OFFSET_TABLE_|__stack_chk_fail|__stack_chk_guard)$'
defined=$(nm --defined-only --no-demangle "${freestanding_objects[@]}" |
  awk 'NF > 2 {print $NF}' | sort -u)
missing=$(nm --undefined-only --no-demangle "${freestanding_objects[@]}" |
  awk 'NF == 2 && $1 == "U" {print $2}' | sort -u |
  grep -Ev "$allowed" | grep -Ev "$provided" |
  grep -Fxv -f <(printf '%s\n' "$defined") || true)
if [[ -n "$missing" ]]; then
  echo "freestanding build references symbols the kernel does not provide:" >&2
  echo "$missing" >&2
  exit 1
fi
echo "kernel freestanding: $(( ${#freestanding_objects[@]} )) objects, no unresolved host dependencies"
