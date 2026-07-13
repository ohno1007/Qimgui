// PNG → GL texture loader using the NDK's AImageDecoder (API 30+). Live2D model
// textures are PNGs; this avoids vendoring a decoder.
#pragma once

#include <GLES3/gl3.h>

namespace aimgui {
namespace live2d {

// Decode the PNG at `path` and upload it as an RGBA GL texture. Returns the GL
// texture id, or 0 on failure. Premultiplied alpha is applied (Cubism expects
// premultiplied textures for its default blend setup).
GLuint LoadTexture(const char* path);

} // namespace live2d
} // namespace aimgui
