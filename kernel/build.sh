#!/usr/bin/env bash
# Builds qimgui.ko against a kernel source tree.
#
#   kernel/build.sh /path/to/kernel-source
#   KDIR=/path/to/kernel-source kernel/build.sh
#
# The module must be built against the source of the kernel it will be loaded
# into, with that kernel's own toolchain: CFI, the stack protector and the
# structure layouts all have to match, and modpost checks the version magic.
# Point this at the tree the device's kernel was built from.
#
# Toolchain: pass CC=clang LLVM=1 for a kernel built with clang, which is what
# Android kernels normally use. Otherwise a GCC cross compiler is found from
# CROSS_COMPILE.
set -euo pipefail
cd "$(dirname "$0")"

KDIR="${1:-${KDIR:-}}"
ARCH="${ARCH:-arm64}"
CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}"

if [[ -z "$KDIR" ]]; then
  cat >&2 <<'USAGE'
No kernel source given.

  kernel/build.sh /path/to/kernel-source

On a device-tree checkout that is the directory holding Makefile, include/ and
.config, already configured and prepared (make modules_prepare). A headers-only
package works too if it carries Module.symvers and the build scripts.
USAGE
  exit 2
fi
if [[ ! -f "$KDIR/Makefile" ]]; then
  echo "Not a kernel source tree (no Makefile): $KDIR" >&2
  exit 2
fi

# clang builds the kernel and the C++ sources with the same front end; GCC
# needs its matching g++ for the C++ half.
if [[ "${LLVM:-}" == "1" || "${CC:-}" == clang* ]]; then
  QK_CXX="${QK_CXX:-clang++}"
else
  QK_CXX="${QK_CXX:-${CROSS_COMPILE}g++}"
fi
if ! command -v "${QK_CXX%% *}" >/dev/null 2>&1; then
  echo "C++ compiler not found: $QK_CXX" >&2
  echo "Install it, or pass QK_CXX=<compiler> matching the kernel's toolchain." >&2
  exit 2
fi

echo "kernel : $KDIR"
echo "arch   : $ARCH"
echo "cc     : ${CC:-${CROSS_COMPILE}gcc}${LLVM:+ (LLVM=1)}"
echo "c++    : $QK_CXX"
echo

make KDIR="$KDIR" ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" QK_CXX="$QK_CXX" \
     ${LLVM:+LLVM=1} ${CC:+CC="$CC"} \
     ${PAHOLE:+PAHOLE="$PAHOLE"} ${RESOLVE_BTFIDS:+RESOLVE_BTFIDS="$RESOLVE_BTFIDS"} \
     modules

echo
echo "built: $(pwd)/.build/qimgui.ko"
echo "load:  adb push .build/qimgui.ko /data/local/tmp/ && adb shell su -c 'insmod /data/local/tmp/qimgui.ko'"
echo "watch: adb shell su -c 'cat /proc/qimgui'"
