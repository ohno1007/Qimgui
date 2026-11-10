#!/usr/bin/env python3
"""Check that all vendored ImGui sources match the recorded upstream import."""
import hashlib
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1] / 'third_party/imgui'
manifest = json.loads((root / 'UPSTREAM.json').read_text())
for name, expected in manifest['sha256'].items():
    actual = hashlib.sha256((root / name).read_bytes()).hexdigest()
    if actual != expected:
        raise SystemExit(f'Unexpected change to upstream source: {name}')
version = re.search(r'#define IMGUI_VERSION\s+"([^"]+)"', (root/'imgui.h').read_text())[1]
if version != manifest['version'] or manifest['tag'] != 'v' + version:
    raise SystemExit('ImGui header and manifest disagree')
print(f"Verified Dear ImGui {version}, {manifest['commit']}")
