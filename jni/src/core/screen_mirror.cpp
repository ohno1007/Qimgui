#include "screen_mirror.h"

#include "platform/ANativeWindowCreator.h"

#include <android/native_window.h>

#include <chrono>
#include <cstdio>
#include <dlfcn.h>

namespace aimgui {
namespace {

// Only failures are reported, and only once each — this path is quiet when it
// works. stderr because the binary is run from a shell.
#define MIRROR_FAIL(fmt, ...) \
    std::fprintf(stderr, "[mirror] " fmt "\n" __VA_OPT__(,) __VA_ARGS__)


// AImageReader is loaded at first use rather than linked. Linking libmediandk
// would put a DT_NEEDED on this binary, dragging libmedia/libbinder into every
// launch of what is a bare root executable rather than an app — the situation
// this project already hit with linker namespaces. Loading it lazily also
// keeps the build at API 24 instead of forcing 26 on everyone, and turns "this
// device can't do it" into a disabled feature rather than a failure.
constexpr int32_t  kMediaOk         = 0;
// RGBA_8888 rather than PRIVATE. PRIVATE (IMPLEMENTATION_DEFINED) lets the
// allocator pick a GPU-friendly layout, which is ideal for a sample-only
// buffer — but SurfaceFlinger has to negotiate a format it can composite a
// virtual display into, and with PRIVATE it silently settled on producing
// nothing. RGBA_8888 is the combination every MediaProjection + ImageReader
// screen-capture path uses, and it is unambiguous for both writer and reader.
constexpr int32_t  kFormatRgba8888 = 0x1;       // AIMAGE_FORMAT_RGBA_8888
constexpr uint64_t kUsageGpuSampled = 1ULL << 8; // GPU_SAMPLED_IMAGE
constexpr uint64_t kUsageGpuFramebuffer = 1ULL << 9; // GPU_FRAMEBUFFER (colour output)

// Both halves are required. SAMPLED is for us — we read these buffers as a
// texture. FRAMEBUFFER is for SurfaceFlinger, which composites *into* them and
// therefore needs them usable as a render target. Requesting only SAMPLED
// leaves the compositor with nothing it can draw to, and the display sits
// there producing no frames at all rather than reporting an error.
constexpr uint64_t kMirrorUsage = kUsageGpuSampled | kUsageGpuFramebuffer;

struct MediaNdk {
    int32_t (*ReaderNewWithUsage)(int32_t w, int32_t h, int32_t fmt,
                                  uint64_t usage, int32_t maxImages, void** out) = nullptr;
    int32_t (*ReaderGetWindow)(void* reader, ANativeWindow** out)                = nullptr;
    int32_t (*ReaderAcquireLatest)(void* reader, void** outImage)                = nullptr;
    void    (*ReaderDelete)(void* reader)                                        = nullptr;
    void    (*ImageDelete)(void* image)                                          = nullptr;
    int32_t (*ImageGetHardwareBuffer)(void* image, AHardwareBuffer** out)        = nullptr;
    bool    ok = false;
};

const MediaNdk& Media() {
    static MediaNdk m = [] {
        MediaNdk r;
        void* lib = ::dlopen("libmediandk.so", RTLD_NOW | RTLD_LOCAL);
        if (!lib) return r;
        auto sym = [&](const char* n) { return ::dlsym(lib, n); };
        r.ReaderNewWithUsage     = (decltype(r.ReaderNewWithUsage))     sym("AImageReader_newWithUsage");
        r.ReaderGetWindow        = (decltype(r.ReaderGetWindow))        sym("AImageReader_getWindow");
        r.ReaderAcquireLatest    = (decltype(r.ReaderAcquireLatest))    sym("AImageReader_acquireLatestImage");
        r.ReaderDelete           = (decltype(r.ReaderDelete))           sym("AImageReader_delete");
        r.ImageDelete            = (decltype(r.ImageDelete))            sym("AImage_delete");
        r.ImageGetHardwareBuffer = (decltype(r.ImageGetHardwareBuffer)) sym("AImage_getHardwareBuffer");
        r.ok = r.ReaderNewWithUsage && r.ReaderGetWindow && r.ReaderAcquireLatest &&
               r.ReaderDelete && r.ImageDelete && r.ImageGetHardwareBuffer;
        return r;
    }();
    return m;
}

} // namespace

bool ScreenMirror::Available() {
    return Media().ok && android::ANativeWindowCreator::ScreenCaptureSupported();
}

bool ScreenMirror::Start(int width, int height, int srcWidth, int srcHeight) {
    if (m_Running) return true;
    if (width <= 0 || height <= 0 || srcWidth <= 0 || srcHeight <= 0) return false;

    const MediaNdk& media = Media();
    if (!media.ok) return false;
    if (!android::ANativeWindowCreator::ScreenCaptureSupported()) return false;

    void* reader = nullptr;
    if (media.ReaderNewWithUsage(width, height, kFormatRgba8888, kMirrorUsage,
                                 /*maxImages=*/5, &reader) != kMediaOk || !reader) {
        MIRROR_FAIL("AImageReader_newWithUsage failed (%dx%d)", width, height);
        return false;
    }

    ANativeWindow* window = nullptr;
    if (media.ReaderGetWindow(reader, &window) != kMediaOk || !window) {
        media.ReaderDelete(reader);
        return false;
    }

    // The ANativeWindow an AImageReader hands out is an android::Surface, so
    // its producer end is what the virtual display attaches to.
    const auto& fns = android::detail::Functionals::GetInstance();
    if (!fns.Surface__GetIGraphicBufferProducer) {
        media.ReaderDelete(reader);
        return false;
    }
    // An ANativeWindow* is not the Surface's address. android::Surface derives
    // from ANativeObjectBase<ANativeWindow, Surface, RefBase>, and RefBase's
    // vtable sits first, so the ANativeWindow subobject lives 16 bytes into
    // the Surface. ANativeWindowCreator::Create() already relies on this in
    // the other direction — SurfaceControl::GetSurface() adds the same 16 to
    // turn a Surface* into the ANativeWindow* it hands out.
    //
    // Passing the window straight through as `this` made the callee read its
    // members at the wrong offsets and then incStrong the garbage it found
    // there, which is the segfault.
    constexpr size_t kSurfaceToWindow = sizeof(std::max_align_t) / 2;
    void* surface = reinterpret_cast<char*>(window) - kSurfaceToWindow;
    android::detail::StrongPointer<void> producer =
        fns.Surface__GetIGraphicBufferProducer(surface);
    if (!producer.get()) {
        media.ReaderDelete(reader);
        return false;
    }

    auto& composer = android::ANativeWindowCreator::GetComposerInstance();
    // Non-secure: a secure display refuses to mirror protected content, and
    // showing that blurred beats failing outright.
    android::detail::StrongPointer<void> token =
        composer.CreateVirtualDisplay("AImGuiMirror", /*secure=*/false);
    if (!token.get()) {
        MIRROR_FAIL("createVirtualDisplay returned no token");
        media.ReaderDelete(reader);
        return false;
    }

    // Read the primary display's actual layer stack rather than assuming 0.
    // Mirroring the wrong stack yields a display that composites nothing, and
    // reports success at every step while doing it.
    // Give the mirror its own layer stack rather than sharing the physical
    // display's. Sharing one stack between two displays used to be how
    // mirroring worked, and it is what screenrecord still looks like it does,
    // but on this build SurfaceFlinger stores the shared stack faithfully
    // (readback confirms layerStack=0) and then assigns no layers to the
    // second display. Modern SurfaceFlinger mirrors by way of a mirror layer
    // instead, so make a stack that only this display sees and put one there.
    constexpr uint32_t kMirrorLayerStack = 0x41493344;  // arbitrary, ours alone
    const uint32_t layerStack = kMirrorLayerStack;

    android::detail::SurfaceComposerClientTransaction t;
    const bool okSurf = t.SetDisplaySurface(token, producer);
    const bool okStack = t.SetDisplayLayerStack(token, layerStack);
    const android::detail::ui::Rect src{0, 0, srcWidth, srcHeight};
    const android::detail::ui::Rect dst{0, 0, width, height};
    const bool okProj = t.SetDisplayProjection(token, /*orientation=*/0, src, dst);
    // Not one-way: this transaction brings a display into existence, and a
    // fire-and-forget binder call gives SurfaceFlinger no way to report that
    // it rejected any of it. The status was being discarded, which is why a
    // rejected transaction has looked identical to an accepted one all along.
    const int32_t applyRc = t.Apply(false, false);

    // A virtual display that is configured but not powered on composites
    // nothing, which from outside looks exactly like a layer-stack mismatch.
    // SurfaceFlinger does not turn these on by itself here, and it only
    // attaches to our producer once one is on.
    composer.SetDisplayPowerMode(token, /*ON=*/2);

    // Mirror the physical display into a layer parked on our stack, so this
    // display has exactly one thing to composite and it is the screen. Two
    // displays sharing a layer stack used to be enough to mirror; this
    // SurfaceFlinger stores the shared stack faithfully and then assigns no
    // layers to the second display, so it needs a mirror layer instead.
    android::detail::ui::PhysicalDisplayId pid{};
    if (android::ANativeWindowCreator::GetPrimaryPhysicalDisplayId(&pid) &&
        android::detail::SurfaceComposerClient::MirrorDisplaySupported()) {
        m_MirrorLayer = composer.MirrorDisplay(pid).data;
        if (m_MirrorLayer) {
            android::detail::SurfaceComposerClientTransaction mt;
            android::detail::StrongPointer<void> mp{};
            mp.pointer = m_MirrorLayer;
            mt.SetLayer(mp, 0);
            mt.SetLayerStack(mp, layerStack);
            mt.Show(mp);
            mt.Apply(false, false);
        } else {
            MIRROR_FAIL("mirrorDisplay returned no layer; nothing to composite");
        }
    }

    m_Window = window;

    m_Reader  = reader;
    m_Token   = token.get();
    m_Width   = width;
    m_Height  = height;
    m_SrcW    = srcWidth;
    m_SrcH    = srcHeight;
    m_Frames  = 0;
    m_Running = true;
    return true;
}

void ScreenMirror::Stop() {
    if (!m_Running) return;
    const MediaNdk& media = Media();

    if (m_Image && media.ok) { media.ImageDelete(m_Image); }
    m_Image = nullptr;

    // The mirror layer has to go with the display it fed. Leaving it behind
    // would strand one on our layer stack per restart, and rotation restarts
    // the mirror every time.
    if (m_MirrorLayer) {
        android::detail::SurfaceComposerClientTransaction t;
        android::detail::StrongPointer<void> mp{};
        mp.pointer = m_MirrorLayer;
        t.Hide(mp);
        t.Apply(false, false);
        m_MirrorLayer = nullptr;
    }

    if (m_Token) {
        android::detail::StrongPointer<void> token{};
        token.pointer = m_Token;
        android::ANativeWindowCreator::GetComposerInstance().DestroyVirtualDisplay(token);
        m_Token = nullptr;
    }
    if (m_Reader && media.ok) { media.ReaderDelete(m_Reader); }
    m_Reader     = nullptr;
    m_SrcW       = 0;
    m_SrcH       = 0;
    m_Running    = false;
}

AHardwareBuffer* ScreenMirror::AcquireLatest() {
    if (!m_Running || !m_Reader) return nullptr;
    const MediaNdk& media = Media();
    if (!media.ok) return nullptr;

    void* image = nullptr;
    const int32_t rc = media.ReaderAcquireLatest(m_Reader, &image);
    if (rc != kMediaOk || !image) {
        // Report the code periodically. NO_BUFFER_AVAILABLE means the
        // compositor simply hasn't produced anything yet; any other code is a
        // different failure and would otherwise look identical from the UI.
        // Nothing new since the last call — normal between frames.
        return nullptr;
    }

    // The previous image's buffer may still be referenced by the frame in
    // flight, so it is only released once a newer one has arrived.
    if (m_Image) media.ImageDelete(m_Image);
    m_Image = image;
    ++m_Frames;

    AHardwareBuffer* buffer = nullptr;
    if (media.ImageGetHardwareBuffer(image, &buffer) != kMediaOk) return nullptr;
    return buffer;
}

} // namespace aimgui
