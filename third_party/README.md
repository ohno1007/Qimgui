# Third-party dependencies

## Dear ImGui

`imgui/` contains the required core and OpenGL/Vulkan backend sources from the
pinned official release. `imgui/UPSTREAM.json` records the tag, exact commit and
per-file SHA-256 hashes. Its MIT license is preserved in `imgui/LICENSE.txt`.

Project configuration lives in `config/aimgui_imconfig.h` and is passed through
`IMGUI_USER_CONFIG` to every target. Do not edit the vendored upstream defaults.

Update with `python3 scripts/update_imgui.py <stable-tag>`, then run the checks
and Android build before committing. The ordinary build needs no network.

## Live2D Cubism (optional)

Copy `Core/` and `CubismNativeFramework/` from the same Cubism SDK for Native
package into this directory. These SDK directories are ignored by Git.
See [docs/LIVE2D.md](../docs/LIVE2D.md) for the full setup and licensing notes.
