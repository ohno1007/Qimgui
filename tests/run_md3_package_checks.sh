#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
repo="$PWD"
build="$repo/build/md3-package-checks"
mkdir -p "$build"
cmake_bin="${CMAKE:-cmake}"
if ! command -v "$cmake_bin" >/dev/null 2>&1; then
  printf 'CMake is required (or set CMAKE to its executable path).\n' >&2
  exit 1
fi

# Build from an external application's CMake root. The component subdirectory
# must never use CMAKE_SOURCE_DIR to locate its own sources or force Android.
"$cmake_bin" -S examples/md3_consumer -B "$build/subdirectory" \
  -DCMAKE_BUILD_TYPE=Release -DMD3_CONSUMER_SHOWCASE=OFF
"$cmake_bin" --build "$build/subdirectory" --parallel 3
"$build/subdirectory/md3_consumer"

# A separate install is moved before consumption: absolute checkout/build paths
# in exported targets would fail this check or point to the wrong header files.
"$cmake_bin" -S libraries/md3 -B "$build/library" \
  -DCMAKE_BUILD_TYPE=Release -DQIMGUI_MD3_BUILD_SHOWCASE=ON \
  -DQIMGUI_MD3_INSTALL=ON -DCMAKE_INSTALL_LIBDIR=lib
"$cmake_bin" --build "$build/library" --parallel 3
stage="$(mktemp -d "$build/stage.XXXXXX")"
relocated="${stage}.relocated"
trap 'rm -rf "$stage" "$relocated"' EXIT
"$cmake_bin" --install "$build/library" --prefix "$stage"
mv "$stage" "$relocated"
if rg --fixed-strings "$repo" "$relocated/lib/cmake/QimguiMD3"; then
  printf 'Installed package retains a checkout/build path.\n' >&2
  exit 1
fi
"$cmake_bin" -S examples/md3_consumer -B "$build/installed" \
  -DCMAKE_BUILD_TYPE=Release -DMD3_CONSUMER_INSTALLED=ON \
  -DMD3_CONSUMER_SHOWCASE=ON -DQimguiMD3_DIR="$relocated/lib/cmake/QimguiMD3"
"$cmake_bin" --build "$build/installed" --parallel 3
"$build/installed/md3_consumer"

if nm -u "$build/library/libaimgui_md3.a" | rg 'ImGui_Impl|ANativeWindow|ASensor|SurfaceComposer'; then
  printf 'Unexpected platform/backend dependency in component library.\n' >&2
  exit 1
fi
printf 'PASS: standalone core, optional showcase, relocated install/find_package consumer and platform-free linkage\n'
