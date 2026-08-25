#pragma once

#include "glass.h"

#include <GLES3/gl3.h>

namespace aimgui {

class GlassGL {
public:
    bool Init();
    void Shutdown();
    bool Ready() const { return m_Ready; }

    void Draw(GLuint screenTex, int screenW, int screenH,
              int surfaceW, int surfaceH,
              const GlassRect* rects, int count);

private:
    bool   m_Ready   = false;
    GLuint m_Prog    = 0;
    GLuint m_VAO     = 0;
    GLint  m_LocScreenTex = -1;
    GLint  m_LocShapes    = -1;
    GLint  m_LocScreen    = -1;
    GLint  m_LocSurface   = -1;
    GLint  m_LocParams    = -1;
    GLint  m_LocTint      = -1;
    GLint  m_LocParams2   = -1;
};

}
