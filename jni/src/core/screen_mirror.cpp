#include "screen_mirror.h"

#include "platform/ANativeWindowCreator.h"

#include <android/native_window.h>

#include <cstdio>
#include <dlfcn.h>

namespace aimgui {
namespace {

// Each risky step is reported before it runs. Every call below goes through a
// symbol resolved out of libgui by name, so a wrong binding takes the process
// down with it, and the last line printed is then the only thing that says
// which one. stderr because it is unbuffered — a buffered stdout would lose
// the very line that matters when the process dies.
#define MIRROR_STEP(fmt, ...) \
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

// Print SurfaceFlinger's own view of the displays it knows about. When every
// setup call reports success but nothing is ever composited, the question that
// actually matters is whether SF created the display at all — and only SF can
// answer that.
void DumpSurfaceFlingerDisplays() {
    FILE* pipe = ::popen("dumpsys SurfaceFlinger --display-id 2>/dev/null", "r");
    if (!pipe) { MIRROR_STEP("dumpsys unavailable"); return; }
    char line[512];
    int printed = 0;
    while (std::fgets(line, sizeof(line), pipe) && printed < 8) {
        std::fprintf(stderr, "[sf] %s", line);
        ++printed;
    }
    ::pclose(pipe);
}

} // namespace

bool ScreenMirror::Available() {
    return Media().ok && android::ANativeWindowCreator::ScreenCaptureSupported();
}

// Diagnostic: point the virtual display at a plain visible SurfaceControl
// instead of an AImageReader. If SurfaceFlinger composites into that (the
// mirrored screen becomes visible on top, feedback loop and all), then the
// virtual-display mechanism works here and the problem is specific to
// AImageReader's producer. If it stays blank, SF refuses to drive
// caller-created virtual displays on this ROM at all and no amount of
// parameter fiddling will change that.
bool ScreenMirror::StartVisibleProbe(int width, int height, int srcWidth, int srcHeight) {
    if (m_Running) return true;
    if (!android::ANativeWindowCreator::ScreenCaptureSupported()) return false;

    ANativeWindow* win = android::ANativeWindowCreator::Create("AImGuiMirrorProbe",
                                                              width, height, false);
    if (!win) { MIRROR_STEP("probe: layer create failed"); return false; }

    const auto& fns = android::detail::Functionals::GetInstance();
    constexpr size_t kSurfaceToWindow = sizeof(std::max_align_t) / 2;
    void* surface = reinterpret_cast<char*>(win) - kSurfaceToWindow;
    android::detail::StrongPointer<void> producer =
        fns.Surface__GetIGraphicBufferProducer(surface);
    if (!producer.get()) { MIRROR_STEP("probe: no producer"); return false; }

    auto& composer = android::ANativeWindowCreator::GetComposerInstance();
    android::detail::StrongPointer<void> token =
        composer.CreateVirtualDisplay("AImGuiProbe", false);
    if (!token.get()) { MIRROR_STEP("probe: no display token"); return false; }

    uint32_t layerStack = 0;
    { android::detail::ui::DisplayState ds{};
      if (composer.GetDisplayInfo(&ds)) layerStack = ds.layerStack.id; }

    android::detail::SurfaceComposerClientTransaction t;
    t.SetDisplaySurface(token, producer);
    t.SetDisplayLayerStack(token, layerStack);
    const android::detail::ui::Rect src{0, 0, srcWidth, srcHeight};
    const android::detail::ui::Rect dst{0, 0, width, height};
    t.SetDisplayProjection(token, 0, src, dst);
    const int32_t rc = t.Apply(false, false);
    MIRROR_STEP("probe: visible layer %dx%d attached to virtual display, apply rc=%d",
                width, height, rc);
    MIRROR_STEP("probe: if the screen appears mirrored in a box, SF drives caller-made "
                "virtual displays and AImageReader's producer is the problem");

    m_Token   = token.get();
    m_Width   = width;
    m_Height  = height;
    m_Running = true;
    return true;
}

bool ScreenMirror::Start(int width, int height, int srcWidth, int srcHeight) {
    if (m_Running) return true;
    if (width <= 0 || height <= 0 || srcWidth <= 0 || srcHeight <= 0) return false;

    const MediaNdk& media = Media();
    if (!media.ok) return false;
    if (!android::ANativeWindowCreator::ScreenCaptureSupported()) return false;

    MIRROR_STEP("1/6 AImageReader_newWithUsage %dx%d fmt=RGBA_8888 usage=0x%llx",
                width, height, (unsigned long long)kMirrorUsage);
    void* reader = nullptr;
    if (media.ReaderNewWithUsage(width, height, kFormatRgba8888, kMirrorUsage,
                                 /*maxImages=*/3, &reader) != kMediaOk || !reader) {
        return false;
    }

    MIRROR_STEP("2/6 AImageReader_getWindow");
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
    MIRROR_STEP("3/6 Surface::getIGraphicBufferProducer(window=%p surface=%p)",
                (void*)window, surface);
    android::detail::StrongPointer<void> producer =
        fns.Surface__GetIGraphicBufferProducer(surface);
    if (!producer.get()) {
        media.ReaderDelete(reader);
        return false;
    }

    auto& composer = android::ANativeWindowCreator::GetComposerInstance();
    // Non-secure: a secure display refuses to mirror protected content, and
    // showing that blurred beats failing outright.
    MIRROR_STEP("4/6 createVirtualDisplay");
    android::detail::StrongPointer<void> token =
        composer.CreateVirtualDisplay("AImGuiMirror", /*secure=*/false);
    if (!token.get()) {
        media.ReaderDelete(reader);
        return false;
    }

    // Read the primary display's actual layer stack rather than assuming 0.
    // Mirroring the wrong stack yields a display that composites nothing, and
    // reports success at every step while doing it.
    uint32_t layerStack = 0;
    {
        android::detail::ui::DisplayState ds{};
        if (composer.GetDisplayInfo(&ds)) layerStack = ds.layerStack.id;
    }

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
    MIRROR_STEP("5/6 transaction: surface=%d stack=%d(%u) proj=%d src=%dx%d dst=%dx%d -> apply rc=%d",
                okSurf, okStack, layerStack, okProj,
                srcWidth, srcHeight, width, height, applyRc);

    MIRROR_STEP("6/6 running");
    DumpSurfaceFlingerDisplays();
    m_Window = window;
    m_Reader  = reader;
    m_Token   = token.get();
    m_Width   = width;
    m_Height  = height;
    m_Frames  = 0;
    m_Running = true;
    return true;
}

void ScreenMirror::Stop() {
    if (!m_Running) return;
    const MediaNdk& media = Media();

    if (m_Image && media.ok) { media.ImageDelete(m_Image); }
    m_Image = nullptr;

    if (m_Token) {
        android::detail::StrongPointer<void> token{};
        token.pointer = m_Token;
        android::ANativeWindowCreator::GetComposerInstance().DestroyVirtualDisplay(token);
        m_Token = nullptr;
    }
    if (m_Reader && media.ok) { media.ReaderDelete(m_Reader); }
    m_Reader  = nullptr;
    m_Running = false;
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
        if (m_Frames == 0 && ++m_AcquireMisses % 240 == 0) {
            // The queue's negotiated state answers whether SurfaceFlinger ever
            // connected as a producer at all. If it had connected and dequeued,
            // these would reflect what it asked for; unchanged values mean the
            // display exists and the transaction was accepted, but nothing on
            // SF's side ever touched our buffer queue.
            ANativeWindow* w = static_cast<ANativeWindow*>(m_Window);
            MIRROR_STEP("acquire still empty after %llu tries, rc=%d; queue now %dx%d fmt=%d",
                        (unsigned long long)m_AcquireMisses, rc,
                        w ? ANativeWindow_getWidth(w) : -1,
                        w ? ANativeWindow_getHeight(w) : -1,
                        w ? ANativeWindow_getFormat(w) : -1);
        }
        return nullptr;
    }

    // The previous image's buffer may still be referenced by the frame in
    // flight, so it is only released once a newer one has arrived.
    if (m_Image) media.ImageDelete(m_Image);
    m_Image = image;
    if (m_Frames == 0) MIRROR_STEP("first frame acquired");
    ++m_Frames;

    AHardwareBuffer* buffer = nullptr;
    if (media.ImageGetHardwareBuffer(image, &buffer) != kMediaOk) return nullptr;
    return buffer;
}

} // namespace aimgui
