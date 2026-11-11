# Frozen integer graphics assets

`shapes.inc` contains all 35 pinned AndroidX MaterialShapes presets as polygons
with coordinates in `0..4096`. Cubics are flattened offline with a normalized
chord-error bound of `1/1024`; quantization adds at most `1/8192` per axis.
These are approximations, not the original cubic geometry. Their error scales
with display size. Shape rotation, morphing and cubic editing are not provided
by this asset layer. The renderer can consume the immutable catalog directly.

`font.inc` contains Roboto 400 grayscale glyphs at 18 pixels for U+0020–U+007E.
No font parser, floating-point operation or external runtime library is needed
to draw them. UTF-8 characters outside this bundled range use a fallback glyph;
applications can provide additional sorted glyphs and their coverage bitmaps
through `struct qm_font`. The font data is governed by
[SIL OFL-1.1](../../../tests/md3_web/fonts/OFL.txt).

`icons.inc` contains flattened versions of the existing 26 Qimgui vector icons,
plus a check mark and a downward chevron. Sampling and software rasterization
can differ from the existing ImGui antialiasing. These are semantic Qimgui
symbols, not a copy of the full Material Symbols font.

Reproduce with Python 3, Pillow 12.3.0 and its FreeType 2.14.3 wheel:

```sh
python3 scripts/generate_kernel_md3_assets.py
python3 scripts/generate_kernel_md3_assets.py --check
```

Ordinary module builds consume these files without Python. Exact source hashes,
glyph settings and shape vertex counts are recorded in `provenance.json`.
MaterialShapes attribution and license are in
[NOTICE](../../../third_party/material_shapes/NOTICE) and
[LICENSE](../../../third_party/material_shapes/LICENSE).
