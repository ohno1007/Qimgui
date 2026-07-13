// Access to a Live2D model embedded in the binary (see scripts/embed_live2d_model.py).
// When no model is embedded, the stub build returns nullptr for both.
#pragma once

namespace aimgui {
namespace live2d {

// Return the embedded bytes for a path relative to the model dir (e.g.
// "Hiyori.moc3", "Hiyori.2048/texture_00.png"), or nullptr if not embedded.
const unsigned char* EmbeddedGet(const char* relpath, unsigned* outSize);

// Return the relative path of the embedded *.model3.json, or nullptr if none.
const char* EmbeddedFindModel3();

} // namespace live2d
} // namespace aimgui
