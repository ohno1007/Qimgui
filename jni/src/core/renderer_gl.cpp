#include "renderer.h"

#include "bloom_gl.h"
#include "glass_gl.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <cstdint>

#include "imgui.h"
#include "imgui_impl_opengl3.h"

#ifndef EGL_OPENGL_ES3_BIT
#define EGL_OPENGL_ES3_BIT 0x00000040
#endif

namespace aimgui {

namespace {

class GLRenderer final : public IRenderer {
public:
    bool Init(ANativeWindow* window, int width, int height) override {
        m_Window = window;
        m_Width = width;
        m_Height = height;

        const EGLint cfg_attribs[] = {
            EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8, EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_NONE,
        };
        const EGLint ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };

        m_Display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (m_Display == EGL_NO_DISPLAY) return false;
        if (!eglInitialize(m_Display, nullptr, nullptr)) return false;

        EGLConfig cfg;
        EGLint n = 0;
        if (!eglChooseConfig(m_Display, cfg_attribs, &cfg, 1, &n) || n < 1) return false;

        EGLint visual = 0;
        eglGetConfigAttrib(m_Display, cfg, EGL_NATIVE_VISUAL_ID, &visual);
        ANativeWindow_setBuffersGeometry(window, 0, 0, visual);

        m_Context = eglCreateContext(m_Display, cfg, EGL_NO_CONTEXT, ctx_attribs);
        m_Surface = eglCreateWindowSurface(m_Display, cfg, window, nullptr);
        if (m_Context == EGL_NO_CONTEXT || m_Surface == EGL_NO_SURFACE) return false;
        if (!eglMakeCurrent(m_Display, m_Surface, m_Surface, m_Context)) return false;

        // Lock to vsync: the panel becomes the frame clock. eglSwapBuffers
        // blocks until the next vblank, giving a flat FPS curve at the
        // panel refresh rate with zero CPU spin.
        eglSwapInterval(m_Display, 1);

        glViewport(0, 0, width, height);
        glClearColor(0.f, 0.f, 0.f, 0.f);

        if (!ImGui_ImplOpenGL3_Init("#version 300 es")) return false;
        m_Bloom.Init(width, height); // best-effort; renderer still works if it fails
        m_Glass.Init();              // ditto
        return true;
    }

    void NewFrame() override {
        ImGui_ImplOpenGL3_NewFrame();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)m_Width, (float)m_Height);
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    }

    void EndFrame() override {
        ImGui::Render();
        if (m_Bloom.Ready()) {
            if (m_ScenePreDraw) {
                // Draw the Live2D model as a background straight onto FB0 so it
                // is NOT part of the bloomed scene; the UI+bloom composites over
                // it. (BeginScene renders UI-only into the bloom scene FBO.)
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glViewport(0, 0, m_Width, m_Height);
                glClearColor(0.f, 0.f, 0.f, 0.f);
                glClear(GL_COLOR_BUFFER_BIT);
                m_ScenePreDraw();
            }
            m_Bloom.SetCompositeOverDest(m_ScenePreDraw != nullptr);
            m_Bloom.BeginScene();
            DrawGlass();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            m_Bloom.EndSceneAndComposite();
        } else {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, m_Width, m_Height);
            glClear(GL_COLOR_BUFFER_BIT);
            if (m_ScenePreDraw) m_ScenePreDraw();
            DrawGlass();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
        eglSwapBuffers(m_Display, m_Surface);
    }

    void SetScenePreDraw(void (*fn)()) override { m_ScenePreDraw = fn; }

    void SetGlassRects(const GlassRect* rects, int count,
                       int displayW, int displayH) override {
        m_GlassRects = rects;
        m_GlassCount = count;
        m_GlassW = displayW; m_GlassH = displayH;
    }

    void DrawGlass() {
        if (m_GlassCount > 0 && m_BackdropTex)
            m_Glass.Draw(m_BackdropTex, m_GlassW, m_GlassH, m_GlassRects, m_GlassCount);
    }

    void Shutdown() override {
        m_Bloom.Shutdown();
        m_Glass.Shutdown();
        if (m_BackdropTex) { glDeleteTextures(1, &m_BackdropTex); m_BackdropTex = 0; }
        ImGui_ImplOpenGL3_Shutdown();
        if (m_Display != EGL_NO_DISPLAY) {
            eglMakeCurrent(m_Display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (m_Context != EGL_NO_CONTEXT) eglDestroyContext(m_Display, m_Context);
            if (m_Surface != EGL_NO_SURFACE) eglDestroySurface(m_Display, m_Surface);
            eglTerminate(m_Display);
        }
        m_Display = EGL_NO_DISPLAY;
        m_Surface = EGL_NO_SURFACE;
        m_Context = EGL_NO_CONTEXT;
    }

    const char* Name() const override { return "OpenGL ES 3"; }

    void SetBloomIntensity(float i) override { m_Bloom.SetIntensity(i); }

    unsigned long long GetSceneSnapshotID() override {
        return (unsigned long long)(uintptr_t)m_Bloom.GetSnapshotTex();
    }

    void SetSnapshotFrozen(bool frozen) override { m_Bloom.SetSnapshotFrozen(frozen); }

    unsigned long long SetBackdropImage(const void* rgba, int w, int h) override {
        if (!rgba || w <= 0 || h <= 0) return 0;
        if (m_BackdropTex == 0) {
            glGenTextures(1, &m_BackdropTex);
            glBindTexture(GL_TEXTURE_2D, m_BackdropTex);
            // Linear + clamp: the capture is deliberately tiny and gets
            // stretched over the window, so the bilinear filter is doing a
            // lot of the smoothing for us.
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            m_BackdropW = m_BackdropH = 0;
        } else {
            glBindTexture(GL_TEXTURE_2D, m_BackdropTex);
        }

        if (w == m_BackdropW && h == m_BackdropH) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        } else {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            m_BackdropW = w; m_BackdropH = h;
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        return (unsigned long long)m_BackdropTex;
    }

private:
    ANativeWindow* m_Window = nullptr;
    EGLDisplay m_Display = EGL_NO_DISPLAY;
    EGLSurface m_Surface = EGL_NO_SURFACE;
    EGLContext m_Context = EGL_NO_CONTEXT;
    int m_Width = 0;
    int m_Height = 0;
    GLuint m_BackdropTex = 0;
    int    m_BackdropW = 0, m_BackdropH = 0;
    BloomGL m_Bloom;
    GlassGL m_Glass;
    const GlassRect* m_GlassRects = nullptr;
    int              m_GlassCount = 0;
    int              m_GlassW = 0, m_GlassH = 0;
    void (*m_ScenePreDraw)() = nullptr;
};

} // namespace

std::unique_ptr<IRenderer> MakeGLRenderer() {
    return std::unique_ptr<IRenderer>(new GLRenderer());
}

} // namespace aimgui
