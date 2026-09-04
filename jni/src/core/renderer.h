#pragma once

#include "glass.h"

#include <android/native_window.h>
#include <memory>

#ifdef AIMGUI_LIVE2D
#include "live2d_vk_bridge.h"
#endif

namespace aimgui {

class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual bool Init(ANativeWindow* window, int width, int height) = 0;
    virtual void NewFrame() = 0;
    virtual void EndFrame() = 0;
    virtual void Shutdown() = 0;
    virtual const char* Name() const = 0;

    virtual void SetBloomIntensity(float intensity) = 0;

    virtual unsigned long long GetSceneSnapshotID() = 0;

    virtual void SetSnapshotFrozen(bool frozen) = 0;

    virtual void SetScenePreDraw(void (*fn)()) { (void)fn; }

    virtual unsigned long long SetBackdropImage(const void* rgba,
                                                int width, int height) {
        (void)rgba; (void)width; (void)height;
        return 0;
    }

    virtual void SetGlassRects(const GlassRect* rects, int count,
                               int displayW, int displayH) {
        (void)rects; (void)count; (void)displayW; (void)displayH;
    }

    virtual void SetWidgetGlass(const GlassRect* rects, int count) {
        (void)rects; (void)count;
    }

    virtual bool SupportsWidgetGlass() const { return false; }

    virtual unsigned long long ImportHardwareBuffer(struct AHardwareBuffer* ahb,
                                                    int width, int height) {
        (void)ahb; (void)width; (void)height;
        return 0;
    }

#ifdef AIMGUI_LIVE2D

    virtual const Live2DVkContext* GetLive2DVkContext() { return nullptr; }
#endif
};

enum class Backend { Auto, Vulkan, OpenGL };

std::unique_ptr<IRenderer> MakeRenderer(ANativeWindow* window,
                                        int width, int height,
                                        Backend preferred = Backend::Auto);

std::unique_ptr<IRenderer> MakeGLRenderer();
std::unique_ptr<IRenderer> MakeVKRenderer();

}
