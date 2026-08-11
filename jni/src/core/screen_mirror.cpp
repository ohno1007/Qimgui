#include "screen_mirror.h"

#include "platform/ANativeWindowCreator.h"

#include <dlfcn.h>

namespace aimgui {
namespace {

// AImageReader is loaded at first use rather than linked.
//
// Linking libmediandk would put a DT_NEEDED on this binary, dragging
// libmedia/libbinder and friends into every launch — and this is a bare root
// executable, not an app, which is exactly the situation this project has
// already been bitten by once (see the clns-1 linker-namespace commit).
// Linking it crashed the process at startup before main did anything. Loading
// it lazily keeps startup untouched, keeps the build at API 24 instead of
// forcing 26 on everyone, and turns "this device can't do it" into a disabled
// feature rather than a dead process.
constexpr int32_t  kMediaOk            = 0;
constexpr int32_t  kFormatPrivate      = 0x22;      // AIMAGE_FORMAT_PRIVATE
constexpr uint64_t kUsageGpuSampled    = 1ULL << 8; // GPU_SAMPLED_IMAGE

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

    // PRIVATE format keeps the buffers in whatever layout the GPU prefers — we
    // only ever sample them, never touch them from the CPU, so there is no
    // reason to make the compositor convert into a linear layout.
    void* reader = nullptr;
    if (media.ReaderNewWithUsage(width, height, kFormatPrivate, kUsageGpuSampled,
                                 /*maxImages=*/3, &reader) != kMediaOk || !reader) {
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
    android::detail::StrongPointer<void> producer =
        fns.Surface__GetIGraphicBufferProducer(window);
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
        media.ReaderDelete(reader);
        return false;
    }

    android::detail::SurfaceComposerClientTransaction t;
    t.SetDisplaySurface(token, producer);
    // Layer stack 0 is the built-in display's — everything the user sees.
    t.SetDisplayLayerStack(token, 0);
    const android::detail::ui::Rect src{0, 0, srcWidth, srcHeight};
    const android::detail::ui::Rect dst{0, 0, width, height};
    t.SetDisplayProjection(token, /*orientation=*/0, src, dst);
    t.Apply(false, true);

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
    if (media.ReaderAcquireLatest(m_Reader, &image) != kMediaOk || !image) {
        return nullptr;   // nothing new since last call
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
