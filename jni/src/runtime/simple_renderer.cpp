#include "simple_renderer.h"

#include <android/native_window.h>
#include <GLES3/gl3.h>
#include <cstdio>
#include "imgui_impl_opengl3.h"

#ifndef EGL_OPENGL_ES3_BIT
#define EGL_OPENGL_ES3_BIT 0x00000040
#endif

namespace qimgui::android::detail {

bool SimpleRenderer::Init(ANativeWindow* window, int width, int height) {
    if (!window || width <= 0 || height <= 0) return false;
    width_ = width;
    height_ = height;
    auto fail = [&](const char* operation) {
        std::fprintf(stderr, "[overlay] %s failed: EGL 0x%x\n", operation, eglGetError());
        Shutdown();
        return false;
    };
    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY) return fail("eglGetDisplay");
    if (!eglInitialize(display_, nullptr, nullptr)) return fail("eglInitialize");
    if (!eglBindAPI(EGL_OPENGL_ES_API)) return fail("eglBindAPI");
    const EGLint attributes[] = {
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_NONE,
    };
    EGLConfig config = nullptr;
    EGLint count = 0;
    if (!eglChooseConfig(display_, attributes, &config, 1, &count) || count == 0)
        return fail("eglChooseConfig");
    EGLint visual = 0;
    if (!eglGetConfigAttrib(display_, config, EGL_NATIVE_VISUAL_ID, &visual))
        return fail("eglGetConfigAttrib");
    if (ANativeWindow_setBuffersGeometry(window, 0, 0, visual) != 0) {
        std::fprintf(stderr, "[overlay] ANativeWindow_setBuffersGeometry failed\n");
        Shutdown();
        return false;
    }
    const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, context_attributes);
    if (context_ == EGL_NO_CONTEXT) return fail("eglCreateContext");
    surface_ = eglCreateWindowSurface(display_, config, window, nullptr);
    if (surface_ == EGL_NO_SURFACE) return fail("eglCreateWindowSurface");
    if (!eglMakeCurrent(display_, surface_, surface_, context_)) return fail("eglMakeCurrent");
    eglSwapInterval(display_, 1);
    if (!ImGui_ImplOpenGL3_Init("#version 300 es")) {
        std::fprintf(stderr, "[overlay] ImGui OpenGL ES 3 initialization failed\n");
        Shutdown();
        return false;
    }
    imgui_ready_ = true;
    return true;
}

void SimpleRenderer::NewFrame(ImVec2 display_size) {
    ImGui_ImplOpenGL3_NewFrame();
    auto& io = ImGui::GetIO();
    io.DisplaySize = display_size;
    io.DisplayFramebufferScale = ImVec2(1, 1);
}

bool SimpleRenderer::EndFrame() {
    ImGui::Render();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width_, height_);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    // The native surface stays square across rotations. Layout and popup
    // bounds use the real display size, while rendering maps those vertices
    // to the top-left of the full framebuffer (including landscape mode).
    auto* draw = ImGui::GetDrawData();
    const ImVec2 layout_size = draw->DisplaySize;
    draw->DisplaySize = ImVec2(static_cast<float>(width_), static_cast<float>(height_));
    ImGui_ImplOpenGL3_RenderDrawData(draw);
    draw->DisplaySize = layout_size;
    if (eglSwapBuffers(display_, surface_)) return true;
    std::fprintf(stderr, "[overlay] eglSwapBuffers failed: EGL 0x%x\n", eglGetError());
    return false;
}

void SimpleRenderer::Shutdown() {
    if (imgui_ready_) ImGui_ImplOpenGL3_Shutdown();
    imgui_ready_ = false;
    if (display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
        if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
        eglTerminate(display_);
    }
    display_ = EGL_NO_DISPLAY;
    surface_ = EGL_NO_SURFACE;
    context_ = EGL_NO_CONTEXT;
}

}
