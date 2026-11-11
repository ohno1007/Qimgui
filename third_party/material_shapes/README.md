MaterialShapes geometry from AndroidX

`material_shapes.json` contains all 35 official normalized `RoundedPolygon`
presets. Each cubic has eight binary32 coordinates in this order:
`start.x, start.y, control1.x, control1.y, control2.x, control2.y, end.x, end.y`.
`center` is the polygon's declared centroid and `bounds` is the result of the
official `calculateBounds(approximate = false)` method. Shapes are ordered as
the public properties in `MaterialShapes.kt`.

`material_shapes_data.inc` exposes the same geometry to the native renderer.
The complete upstream `MaterialShapes` class body is evaluated unchanged using
the official graphics-shapes JVM implementation. The exporter substitutes only
the Compose value types used for point arithmetic and 2D matrices; their
formulas are retained in `scripts/material_shapes/Adapter.kt`. Upstream source
snapshots, including those math helpers, are included for review. No hand-drawn
silhouettes, fitted curves, or sampled polygon coordinates replace the cubics.

Regenerate with Python 3 and Java 17 or newer:

```sh
python3 scripts/material_shapes/export.py
python3 scripts/material_shapes/export.py --check
```

The exporter downloads pinned, hash-verified development dependencies to
`build/material-shapes-export`. Subsequent runs use that cache. The first run
requires access to Google Maven and Maven Central. Kotlin, Java, and Compose
are not native application runtime dependencies; ordinary C++ builds use the
checked-in `.inc` file directly.

Source revision, artifact versions, and SHA-256 digests are recorded in
`provenance.json` and `scripts/material_shapes/dependencies.lock.json`. The
catalogue is from Material 3 Expressive's experimental `MaterialShapes` API;
it is separate from Material 3's rounded-corner size tokens. Only static
predefined geometry is exported; the AndroidX `Morph` API is not exported.

See `LICENSE` and `NOTICE` for attribution.
