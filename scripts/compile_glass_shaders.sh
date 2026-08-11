#!/usr/bin/env bash
# Regenerate jni/src/core/glass_vk_spv.h from jni/src/core/shaders/glass.{vert,frag}.
# Requires the glslc that ships with Android NDK r25+ under
# $ANDROID_NDK_HOME/shader-tools/<host>/glslc.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NDK="${ANDROID_NDK_HOME:-${NDK_ROOT:-}}"
if [[ -z "$NDK" ]]; then
    echo "error: set ANDROID_NDK_HOME to your NDK root" >&2
    exit 1
fi
GLSLC="$(find "$NDK/shader-tools" -name glslc -type f | head -n1)"
if [[ -z "$GLSLC" || ! -x "$GLSLC" ]]; then
    echo "error: glslc not found under $NDK/shader-tools" >&2
    exit 1
fi

SRC="$ROOT/jni/src/core/shaders"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

"$GLSLC" "$SRC/glass.vert" -o "$TMP/vert.spv"
"$GLSLC" "$SRC/glass.frag" -o "$TMP/frag.spv"

python3 - "$TMP" > "$ROOT/jni/src/core/glass_vk_spv.h" <<'PY'
import sys, struct, os
tmp = sys.argv[1]
print("// AUTO-GENERATED from jni/src/core/shaders/glass.{vert,frag} via glslc.")
print("// Regenerate via scripts/compile_glass_shaders.sh.")
print("#pragma once")
print("#include <cstdint>")
print()
print("namespace aimgui::glass_vk_spv {")
for name, f in (("kVS", "vert.spv"), ("kFS", "frag.spv")):
    data = open(os.path.join(tmp, f), "rb").read()
    words = struct.unpack("<%dI" % (len(data) // 4), data)
    print()
    print("inline constexpr uint32_t %s[%d] = {" % (name, len(words)))
    for i in range(0, len(words), 8):
        print("    " + " ".join("0x%08x," % w for w in words[i:i + 8]))
    print("};")
print()
print("} // namespace aimgui::glass_vk_spv")
PY
echo "wrote jni/src/core/glass_vk_spv.h"
