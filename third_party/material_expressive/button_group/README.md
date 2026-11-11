# Connected button group reference

Pinned AndroidX revision: `587b8da057e3dfa7fb908e4b62b1bb84271eb20d`.
The original Apache-2.0 source and generated tokens are preserved alongside
`provenance.json` and `LICENSE`.

Native `md3_button_group.cpp` uses the connected small group's 40dp height,
2dp spacing, 8dp inner corners, 4dp pressed inner corners, full selected
corners and 0.15 default expansion ratio. Layout, mouse/keyboard interaction,
text clipping and transitions are implemented for Dear ImGui.

The C++ API implements connected horizontal single/multiple selection groups.
It has bounded equal-width items and compresses adjacent padding when pressed.
It does not port Compose's generic arbitrary-child layout, vertical layout or
automatic overflow menu. Supply fewer items or a wider container for readable
labels; narrow widths clip text without overflowing the group.
