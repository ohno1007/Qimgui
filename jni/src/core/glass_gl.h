#pragma once

#include "glass.h"

#include <GLES3/gl3.h>

namespace aimgui {

// Refracts the live screen through a set of panes, for the OpenGL ES backend.
// The GLSL here is the ES translation of jni/src/core/shaders/glass.frag —
// keep the two in step, since the Vulkan backend compiles that one to SPIR-V.
class GlassGL {
public:
    bool Init();
    void Shutdown();
    bool Ready() const { return m_Ready; }

    // `screenTex` is the mirrored screen; rects are in screen pixels and
    // sample it by their own position, so a pane shows what is behind it.
    // `screenW/H` is the visible display, which the panes sample against;
    // `surfaceW/H` is the render target, which is the square surface.
    void Draw(GLuint screenTex, int screenW, int screenH,
              int surfaceW, int surfaceH,
              const GlassRect* rects, int count);

private:
    bool   m_Ready   = false;
    GLuint m_Prog    = 0;
    GLuint m_VAO     = 0;
    GLint  m_LocScreenTex = -1;
    GLint  m_LocRect      = -1;
    GLint  m_LocScreen    = -1;
    GLint  m_LocSurface   = -1;
    GLint  m_LocParams    = -1;
    GLint  m_LocTint      = -1;
    GLint  m_LocParams2   = -1;
};

} // namespace aimgui
