#include "screen_mirror.h"

#include "platform/ANativeWindowCreator.h"
#if defined(AIMGUI_DIAGNOSTICS)
#include "crash_diagnostics.h"
#endif

#include <android/native_window.h>

#include <chrono>
#include <cstdio>
#include <dlfcn.h>

namespace aimgui {
namespace {

#define MIRROR_FAIL(fmt, ...) \
    std::fprintf(stderr, "[mirror] " fmt "\n" __VA_OPT__(,) __VA_ARGS__)

#if defined(AIMGUI_DIAGNOSTICS)
#define MIRROR_STAGE(name) diagnostics::SetStage(name)
#define MIRROR_TRACE(name) do { \
    MIRROR_STAGE(name); \
    std::fprintf(stderr, "[mirror] %s\n", name); \
} while (false)
#else
#define MIRROR_STAGE(name) ((void)0)
#define MIRROR_TRACE(name) ((void)0)
#endif

constexpr int32_t  kMediaOk         = 0;

constexpr int32_t  kFormatRgba8888 = 0x1;
constexpr uint64_t kUsageGpuSampled = 1ULL << 8;
constexpr uint64_t kUsageGpuFramebuffer = 1ULL << 9;

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

}

bool ScreenMirror::Available() {
    return Media().ok && android::ANativeWindowCreator::ScreenCaptureSupported();
}

bool ScreenMirror::Start(int width, int height, int srcWidth, int srcHeight) {
    if (m_Running) return true;
    if (width <= 0 || height <= 0 || srcWidth <= 0 || srcHeight <= 0) return false;

    MIRROR_TRACE("mirror-media-load");
    const MediaNdk& media = Media();
    if (!media.ok) {
        MIRROR_FAIL("required AImageReader interfaces unavailable");
        return false;
    }
#if defined(AIMGUI_DIAGNOSTICS)
    diagnostics::RefreshLoadedModules();
#endif
    std::string missing;
    if (!android::ANativeWindowCreator::ScreenCaptureSupported(&missing)) {
        MIRROR_FAIL("required capture interface unavailable: %s", missing.c_str());
        return false;
    }

    void* reader = nullptr;
    MIRROR_TRACE("mirror-reader-create");
    if (media.ReaderNewWithUsage(width, height, kFormatRgba8888, kMirrorUsage,
                                 5, &reader) != kMediaOk || !reader) {
        MIRROR_FAIL("AImageReader_newWithUsage failed (%dx%d)", width, height);
        return false;
    }

    ANativeWindow* window = nullptr;
    MIRROR_TRACE("mirror-reader-window");
    if (media.ReaderGetWindow(reader, &window) != kMediaOk || !window) {
        MIRROR_STAGE("mirror-reader-delete");
        media.ReaderDelete(reader);
        return false;
    }

    const auto& fns = android::detail::Functionals::GetInstance();
    if (!fns.Surface__GetIGraphicBufferProducer) {
        MIRROR_STAGE("mirror-reader-delete");
        media.ReaderDelete(reader);
        return false;
    }

    constexpr size_t kSurfaceToWindow = sizeof(std::max_align_t) / 2;
    void* surface = reinterpret_cast<char*>(window) - kSurfaceToWindow;
    MIRROR_TRACE("mirror-get-producer");
    android::detail::StrongPointer<void> producer =
        fns.Surface__GetIGraphicBufferProducer(surface);
    if (!producer.get()) {
        MIRROR_FAIL("getIGraphicBufferProducer returned no producer");
        MIRROR_STAGE("mirror-reader-delete");
        media.ReaderDelete(reader);
        return false;
    }

    MIRROR_TRACE("mirror-get-composer");
    auto& composer = android::ANativeWindowCreator::GetComposerInstance();

    MIRROR_TRACE("mirror-create-display");
    android::detail::StrongPointer<void> token =
        composer.CreateVirtualDisplay("AImGuiMirror", false);
    if (!token.get()) {
        MIRROR_FAIL("createVirtualDisplay returned no token");
        MIRROR_STAGE("mirror-reader-delete");
        media.ReaderDelete(reader);
        return false;
    }

    const auto failStart = [&](const char* operation) {
        MIRROR_FAIL("%s failed; stopping virtual display", operation);
        MIRROR_STAGE("mirror-destroy-display");
        composer.DestroyVirtualDisplay(token);
        MIRROR_STAGE("mirror-reader-delete");
        media.ReaderDelete(reader);
        return false;
    };

    android::detail::ui::DisplayState ds{};
    MIRROR_TRACE("mirror-query-display");
    if (!composer.GetDisplayInfo(&ds)) return failStart("getDisplayInfo");
    const uint32_t layerStack = ds.layerStack.id;

    MIRROR_TRACE("mirror-transaction-create");
    android::detail::SurfaceComposerClientTransaction t;
    MIRROR_TRACE("mirror-set-display-surface");
    if (!t.SetDisplaySurface(token, producer)) return failStart("setDisplaySurface");
    MIRROR_TRACE("mirror-set-display-layer-stack");
    if (!t.SetDisplayLayerStack(token, layerStack)) return failStart("setDisplayLayerStack");
    const android::detail::ui::Rect src{0, 0, srcWidth, srcHeight};
    const android::detail::ui::Rect dst{0, 0, width, height};
    MIRROR_TRACE("mirror-set-display-projection");
    if (!t.SetDisplayProjection(token, 0, src, dst)) return failStart("setDisplayProjection");

    MIRROR_TRACE("mirror-transaction-apply");
    const int32_t applyRc = t.Apply(false, false);
    if (applyRc != 0) {
        MIRROR_FAIL("display transaction returned %d", applyRc);
        return failStart("transaction apply");
    }

    MIRROR_TRACE("mirror-display-power");
    composer.SetDisplayPowerMode(token, 2);
    m_Started = std::chrono::steady_clock::now();

    m_Window = window;
    m_LayerStack = layerStack;

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

    if (m_Image && media.ok) {
        MIRROR_STAGE("mirror-image-delete");
        media.ImageDelete(m_Image);
    }
    m_Image = nullptr;

    if (m_MirrorLayer) {
        MIRROR_STAGE("mirror-stop-transaction-create");
        android::detail::SurfaceComposerClientTransaction t;
        android::detail::StrongPointer<void> mp{};
        mp.pointer = m_MirrorLayer;
        MIRROR_STAGE("mirror-stop-hide");
        t.Hide(mp);
        MIRROR_STAGE("mirror-stop-transaction-apply");
        t.Apply(false, false);
        m_MirrorLayer = nullptr;
    }

    if (m_Token) {
        android::detail::StrongPointer<void> token{};
        token.pointer = m_Token;
        MIRROR_STAGE("mirror-destroy-display");
        android::ANativeWindowCreator::GetComposerInstance().DestroyVirtualDisplay(token);
        m_Token = nullptr;
    }
    if (m_Reader && media.ok) {
        MIRROR_STAGE("mirror-reader-delete");
        media.ReaderDelete(m_Reader);
    }
    m_Reader     = nullptr;
    m_Window     = nullptr;
    m_SrcW       = 0;
    m_SrcH       = 0;
    m_Running    = false;
}

void ScreenMirror::Update() {

    if (!m_Running || m_Frames > 0 || m_MirrorLayer) return;
    if (std::chrono::steady_clock::now() - m_Started < std::chrono::milliseconds(1000)) return;
    if (!android::detail::SurfaceComposerClient::MirrorDisplaySupported()) return;

    android::detail::ui::PhysicalDisplayId pid{};
    MIRROR_TRACE("mirror-fallback-query-display");
    if (!android::ANativeWindowCreator::GetPrimaryPhysicalDisplayId(&pid)) return;

    auto& composer = android::ANativeWindowCreator::GetComposerInstance();
    MIRROR_TRACE("mirror-fallback-create-layer");
    m_MirrorLayer = composer.MirrorDisplay(pid).data;
    if (!m_MirrorLayer) {
        MIRROR_FAIL("no frames on the shared layer stack, and mirrorDisplay gave no layer");
        return;
    }

    constexpr uint32_t kPrivateStack = 0x41493344;
    android::detail::StrongPointer<void> token{};
    token.pointer = m_Token;
    android::detail::StrongPointer<void> mp{};
    mp.pointer = m_MirrorLayer;

    MIRROR_TRACE("mirror-fallback-transaction-create");
    android::detail::SurfaceComposerClientTransaction t;
    MIRROR_TRACE("mirror-fallback-set-display-layer-stack");
    t.SetDisplayLayerStack(token, kPrivateStack);
    MIRROR_TRACE("mirror-fallback-set-layer");
    t.SetLayer(mp, 0);
    MIRROR_TRACE("mirror-fallback-set-layer-stack");
    t.SetLayerStack(mp, kPrivateStack);
    MIRROR_TRACE("mirror-fallback-show");
    t.Show(mp);
    MIRROR_TRACE("mirror-fallback-transaction-apply");
    t.Apply(false, false);
    m_LayerStack = kPrivateStack;
}

AHardwareBuffer* ScreenMirror::AcquireLatest() {
    if (!m_Running || !m_Reader) return nullptr;
    const MediaNdk& media = Media();
    if (!media.ok) return nullptr;

    void* image = nullptr;
    MIRROR_STAGE("mirror-acquire-image");
    const int32_t rc = media.ReaderAcquireLatest(m_Reader, &image);
    if (rc != kMediaOk || !image) {

        return nullptr;
    }

    if (m_Image) {
        MIRROR_STAGE("mirror-image-delete");
        media.ImageDelete(m_Image);
    }
    m_Image = image;
    ++m_Frames;

    AHardwareBuffer* buffer = nullptr;
    MIRROR_STAGE("mirror-get-hardware-buffer");
    if (media.ImageGetHardwareBuffer(image, &buffer) != kMediaOk) return nullptr;
    return buffer;
}

}
