// Used when no model is embedded at build time (disk-load path only).
#include "live2d/live2d_embedded.h"

namespace aimgui {
namespace live2d {

const unsigned char* EmbeddedGet(const char*, unsigned*) { return nullptr; }
const char* EmbeddedFindModel3() { return nullptr; }

} // namespace live2d
} // namespace aimgui
