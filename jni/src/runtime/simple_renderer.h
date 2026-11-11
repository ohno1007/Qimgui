#pragma once

#include <EGL/egl.h>
#include "imgui.h"

struct ANativeWindow;

namespace qimgui::android::detail {

// Internal GLES presenter shared by applications using RunOverlay.
// No backdrop capture, bloom, refraction, or scene snapshot resources.
class SimpleRenderer {
public:
    ~SimpleRenderer() { Shutdown(); }
    bool Init(ANativeWindow* window, int width, int height);
    void NewFrame(ImVec2 display_size);
    bool EndFrame();
    void Shutdown();

    SimpleRenderer() = default;
    SimpleRenderer(const SimpleRenderer&) = delete;
    SimpleRenderer& operator=(const SimpleRenderer&) = delete;

private:
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLContext context_ = EGL_NO_CONTEXT;
    bool imgui_ready_ = false;
    int width_ = 0, height_ = 0;
};

}
