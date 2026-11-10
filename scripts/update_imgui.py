#!/usr/bin/env python3
"""Vendor the official Dear ImGui sources and record their exact provenance."""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FILES = [
    'imgui.h', 'imgui_internal.h', 'imgui.cpp', 'imgui_draw.cpp',
    'imgui_tables.cpp', 'imgui_widgets.cpp', 'imconfig.h',
    'imstb_rectpack.h', 'imstb_textedit.h', 'imstb_truetype.h', 'LICENSE.txt',
    'backends/imgui_impl_opengl3.cpp', 'backends/imgui_impl_opengl3.h',
    'backends/imgui_impl_opengl3_loader.h',
    'backends/imgui_impl_vulkan.cpp', 'backends/imgui_impl_vulkan.h',
]

def install(source, tag):
    commit = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    tagged = subprocess.check_output(['git', '-C', str(source), 'rev-parse', f'{tag}^{{commit}}'], text=True).strip()
    if commit != tagged:
        raise SystemExit('Source checkout does not match the requested tag')
    version = re.search(r'#define IMGUI_VERSION\s+"([^"]+)"', (source/'imgui.h').read_text())[1]
    if tag != 'v' + version:
        raise SystemExit('Tag and IMGUI_VERSION disagree')
    destination = ROOT/'third_party/imgui'
    hashes = {}
    for name in FILES:
        target = destination/name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source/name, target)
        hashes[name] = hashlib.sha256(target.read_bytes()).hexdigest()
    (destination/'UPSTREAM.json').write_text(json.dumps({
        'repository': 'https://github.com/ocornut/imgui', 'tag': tag,
        'commit': commit, 'version': version, 'sha256': hashes,
    }, indent=2) + '\n')
    print(f'Vendored Dear ImGui {version} at {commit}')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tag', help='Official release tag, e.g. v1.93.0')
    parser.add_argument('--source', type=Path, help='Existing official source checkout')
    args = parser.parse_args()
    if not re.fullmatch(r'v[0-9]+\.[0-9]+\.[0-9]+[a-z]?', args.tag):
        parser.error('Use a stable release tag')
    if args.source:
        install(args.source.resolve(), args.tag)
    else:
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp)/'imgui'
            subprocess.run(['git', 'clone', '--depth', '1', '--branch', args.tag,
                            'https://github.com/ocornut/imgui.git', str(source)], check=True)
            install(source, args.tag)
