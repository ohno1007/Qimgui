#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I kernel/md3/include tests/kernel_md3_test.c kernel/md3/src/core.c \
  kernel/md3/src/canvas.c kernel/md3/src/assets.c kernel/md3/src/widgets.c \
  kernel/md3/src/layout.c -o /tmp/qimgui-kernel-md3-test
/tmp/qimgui-kernel-md3-test
if python3 - <<'PY2'
try:
 import PIL
 from PIL import features
 raise SystemExit(0 if PIL.__version__ == "12.3.0" and features.version("freetype2") == "2.14.3" else 1)
except Exception:
 raise SystemExit(1)
PY2
then
  python3 scripts/generate_kernel_md3_assets.py --check
else
  echo "SKIP: frozen asset regeneration check requires Pillow 12.3.0 / FreeType 2.14.3"
fi
