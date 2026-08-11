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

    // Post-process bloom intensity (0 = off, ~1 = strong). No-op if the
    // backend's bloom isn't ready.
    virtual void SetBloomIntensity(float intensity) = 0;

    // Sampleable snapshot of the previous frame's scene image as an opaque
    // handle suitable for casting to ImTextureID (used by the exit dissolve
    // animation to draw real pieces of UI). Returns 0 if not available.
    virtual unsigned long long GetSceneSnapshotID() = 0;

    // Freeze / unfreeze snapshot refresh. While frozen the renderer keeps
    // serving the same prev-frame scene image instead of overwriting it
    // with this frame's output — used during the exit dissolve so every
    // particle samples the same clean pre-dissolve UI.
    virtual void SetSnapshotFrozen(bool frozen) = 0;

    // Register a callback invoked each frame into the scene framebuffer,
    // before the ImGui draw data — used to draw a background layer (Live2D)
    // that the UI then composites on top of. Default: no-op.
    virtual void SetScenePreDraw(void (*fn)()) { (void)fn; }

    // Uploads a small RGBA8 image and returns a handle castable to
    // ImTextureID, for drawing as the window's glass backdrop. Replaces any
    // previous one — a single slot, so the caller never frees. Returns 0 if
    // the backend can't provide one.
    //
    // Sized for a downscaled screen capture (a few hundred px on the long
    // side), so an upload is tens of KB. This is not a general texture API:
    // no batching, no mipmaps, and re-uploading at the same size reuses the
    // existing allocation.
    virtual unsigned long long SetBackdropImage(const void* rgba,
                                                int width, int height) {
        (void)rgba; (void)width; (void)height;
        return 0;
    }

    // Panes of glass to refract the live screen through, drawn by the backend
    // before ImGui's draw data so widgets composite on top. Submitted per
    // frame; an empty list draws nothing. The source is whatever was last
    // handed to ImportHardwareBuffer.
    // `displayW/H` is the visible screen, which is NOT the surface size: the
    // surface is square (max(w,h) on a side) so it survives rotation without
    // being rebuilt, while the mirrored screen texture covers the display. The
    // shader converts pixel positions to texture coordinates, so it needs the
    // display's dimensions or the image lands scaled and offset.
    virtual void SetGlassRects(const GlassRect* rects, int count,
                               int displayW, int displayH) {
        (void)rects; (void)count; (void)displayW; (void)displayH;
    }

    // Imports a screen-mirror AHardwareBuffer as a sampled texture and returns
    // an ImTextureID-compatible handle, or 0 if the backend can't. No copy is
    // made — the texture aliases the memory SurfaceFlinger composited into.
    // Dimensions come from the caller because AHardwareBuffer_describe is
    // API 26 and this builds against 24.
    virtual unsigned long long ImportHardwareBuffer(struct AHardwareBuffer* ahb,
                                                    int width, int height) {
        (void)ahb; (void)width; (void)height;
        return 0;
    }

#ifdef AIMGUI_LIVE2D
    // Vulkan objects the Live2D Cubism renderer needs, or nullptr if this
    // backend can't host Live2D (e.g. the GL renderer). Valid after Init().
    virtual const Live2DVkContext* GetLive2DVkContext() { return nullptr; }
#endif
};

enum class Backend { Auto, Vulkan, OpenGL };

// Creates a renderer. With Backend::Auto it tries Vulkan first and falls back
// to OpenGL ES 3 if Vulkan is unavailable or fails to initialize.
// Returns nullptr if no backend works.
std::unique_ptr<IRenderer> MakeRenderer(ANativeWindow* window,
                                        int width, int height,
                                        Backend preferred = Backend::Auto);

// Factories for the individual backends (mostly for internal use).
std::unique_ptr<IRenderer> MakeGLRenderer();
std::unique_ptr<IRenderer> MakeVKRenderer();

} // namespace aimgui
