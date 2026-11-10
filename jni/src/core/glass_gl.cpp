#include "glass_gl.h"
#include "glass_gl_sources.h"

#include <android/log.h>

namespace aimgui {
namespace {

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "AImGui", __VA_ARGS__)

using glass_gl_sources::kVS;
using glass_gl_sources::kFS;

GLuint Compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        LOGE("glass shader: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

}

bool GlassGL::Init() {
    GLuint vs = Compile(GL_VERTEX_SHADER, kVS);
    GLuint fs = Compile(GL_FRAGMENT_SHADER, kFS);
    if (!vs || !fs) { if (vs) glDeleteShader(vs); if (fs) glDeleteShader(fs); return false; }

    m_Prog = glCreateProgram();
    glAttachShader(m_Prog, vs);
    glAttachShader(m_Prog, fs);
    glLinkProgram(m_Prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(m_Prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(m_Prog, sizeof(log), nullptr, log);
        LOGE("glass link: %s", log);
        glDeleteProgram(m_Prog);
        m_Prog = 0;
        return false;
    }

    m_LocScreenTex = glGetUniformLocation(m_Prog, "uScreenTex");
    m_LocFlipY = glGetUniformLocation(m_Prog, "uFlipY");

    m_LocShapes    = glGetUniformLocation(m_Prog, "uShapes[0]");
    if (m_LocShapes < 0) m_LocShapes = glGetUniformLocation(m_Prog, "uShapes");
    if (m_LocShapes < 0) LOGE("glass: uShapes not found");
    m_LocScreen    = glGetUniformLocation(m_Prog, "uScreenSize");
    m_LocSurface   = glGetUniformLocation(m_Prog, "uSurface");
    m_LocParams    = glGetUniformLocation(m_Prog, "uParams");
    m_LocTint      = glGetUniformLocation(m_Prog, "uTint");
    m_LocParams2   = glGetUniformLocation(m_Prog, "uParams2");

    glGenVertexArrays(1, &m_VAO);
    m_Ready = true;
    return true;
}

void GlassGL::Shutdown() {
    if (m_VAO)  glDeleteVertexArrays(1, &m_VAO);
    if (m_Prog) glDeleteProgram(m_Prog);
    m_VAO = m_Prog = 0;
    m_Ready = false;
}

void GlassGL::Draw(GLuint screenTex, int screenW, int screenH,
                   int surfaceW, int surfaceH,
                   const GlassRect* rects, int count, bool framebufferSource) {
    if (!m_Ready || !screenTex || count <= 0 || screenW <= 0 || screenH <= 0) return;
    if (surfaceW <= 0 || surfaceH <= 0) return;

    glUseProgram(m_Prog);
    glBindVertexArray(m_VAO);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glDisable(GL_SCISSOR_TEST);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, screenTex);
    glUniform1i(m_LocScreenTex, 0);
    glUniform1i(m_LocFlipY, framebufferSource ? 1 : 0);
    glUniform2f(m_LocScreen, (float)screenW, (float)screenH);
    glUniform2f(m_LocSurface, (float)surfaceW, (float)surfaceH);

    for (int gi = 0; gi < kMaxGlassGroups; ++gi) {
        GlassGroup g;
        const GlassRect* lead = nullptr;
        if (!BuildGlassGroup(rects, count, &g, gi, &lead) || !lead) continue;
        const GlassRect& r = *lead;
        glUniform4f(m_LocParams, r.rounding, r.edgeWidth, r.bend, r.alpha);
        glUniform4f(m_LocTint, r.tintR, r.tintG, r.tintB, r.tintA);
        glUniform4f(m_LocParams2, r.blur, r.lightX, r.lightY, r.merge);
        glUniform4fv(m_LocShapes, kMaxMergedShapes, g.shapes);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    glBindVertexArray(0);
    glUseProgram(0);
}

}
