#include "screen_mirror.h"

#include "platform/ANativeWindowCreator.h"

#include <android/native_window.h>

#include <chrono>
#include <cstdio>
#include <dlfcn.h>

namespace aimgui {
namespace {

#define MIRROR_FAIL(fmt, ...) \
    std::fprintf(stderr, "[mirror] " fmt "\n" __VA_OPT__(,) __VA_ARGS__)

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

    const MediaNdk& media = Media();
    if (!media.ok) return false;
    if (!android::ANativeWindowCreator::ScreenCaptureSupported()) return false;

    void* reader = nullptr;
    if (media.ReaderNewWithUsage(width, height, kFormatRgba8888, kMirrorUsage,
                                 5, &reader) != kMediaOk || !reader) {
        MIRROR_FAIL("AImageReader_newWithUsage failed (%dx%d)", width, height);
        return false;
    }

    ANativeWindow* window = nullptr;
    if (media.ReaderGetWindow(reader, &window) != kMediaOk || !window) {
        media.ReaderDelete(reader);
        return false;
    }

    const auto& fns = android::detail::Functionals::GetInstance();
    if (!fns.Surface__GetIGraphicBufferProducer) {
        media.ReaderDelete(reader);
        return false;
    }

    constexpr size_t kSurfaceToWindow = sizeof(std::max_align_t) / 2;
    void* surface = reinterpret_cast<char*>(window) - kSurfaceToWindow;
    android::detail::StrongPointer<void> producer =
        fns.Surface__GetIGraphicBufferProducer(surface);
    if (!producer.get()) {
        media.ReaderDelete(reader);
        return false;
    }

    auto& composer = android::ANativeWindowCreator::GetComposerInstance();

    android::detail::StrongPointer<void> token =
        composer.CreateVirtualDisplay("AImGuiMirror", false);
    if (!token.get()) {
        MIRROR_FAIL("createVirtualDisplay returned no token");
        media.ReaderDelete(reader);
        return false;
    }

    uint32_t layerStack = 0;
    {
        android::detail::ui::DisplayState ds{};
        if (composer.GetDisplayInfo(&ds)) layerStack = ds.layerStack.id;
    }
    m_LayerStack = layerStack;

    android::detail::SurfaceComposerClientTransaction t;
    const bool okSurf = t.SetDisplaySurface(token, producer);
    const bool okStack = t.SetDisplayLayerStack(token, layerStack);
    const android::detail::ui::Rect src{0, 0, srcWidth, srcHeight};
    const android::detail::ui::Rect dst{0, 0, width, height};
    const bool okProj = t.SetDisplayProjection(token, 0, src, dst);

    const int32_t applyRc = t.Apply(false, false);

    composer.SetDisplayPowerMode(token, 2);
    m_Started = std::chrono::steady_clock::now();

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

void ScreenMirror::Update() {

    if (!m_Running || m_Frames > 0 || m_MirrorLayer) return;
    if (std::chrono::steady_clock::now() - m_Started < std::chrono::milliseconds(1000)) return;
    if (!android::detail::SurfaceComposerClient::MirrorDisplaySupported()) return;

    android::detail::ui::PhysicalDisplayId pid{};
    if (!android::ANativeWindowCreator::GetPrimaryPhysicalDisplayId(&pid)) return;

    auto& composer = android::ANativeWindowCreator::GetComposerInstance();
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

    android::detail::SurfaceComposerClientTransaction t;
    t.SetDisplayLayerStack(token, kPrivateStack);
    t.SetLayer(mp, 0);
    t.SetLayerStack(mp, kPrivateStack);
    t.Show(mp);
    t.Apply(false, false);
    m_LayerStack = kPrivateStack;
}

AHardwareBuffer* ScreenMirror::AcquireLatest() {
    if (!m_Running || !m_Reader) return nullptr;
    const MediaNdk& media = Media();
    if (!media.ok) return nullptr;

    void* image = nullptr;
    const int32_t rc = media.ReaderAcquireLatest(m_Reader, &image);
    if (rc != kMediaOk || !image) {

        return nullptr;
    }

    if (m_Image) media.ImageDelete(m_Image);
    m_Image = image;
    ++m_Frames;

    AHardwareBuffer* buffer = nullptr;
    if (media.ImageGetHardwareBuffer(image, &buffer) != kMediaOk) return nullptr;
    return buffer;
}

}
