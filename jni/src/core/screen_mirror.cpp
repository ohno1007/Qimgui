#include "screen_mirror.h"

#include "platform/ANativeWindowCreator.h"

#include <android/hardware_buffer.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>

namespace aimgui {

bool ScreenMirror::Start(int width, int height, int srcWidth, int srcHeight) {
    if (m_Running) return true;
    if (width <= 0 || height <= 0 || srcWidth <= 0 || srcHeight <= 0) return false;
    if (!android::ANativeWindowCreator::ScreenCaptureSupported()) return false;

    // AIMAGE_FORMAT_PRIVATE keeps the buffers in whatever layout the GPU
    // prefers — we only ever sample them, never touch them from the CPU, so
    // there is no reason to force a linear RGBA layout the compositor would
    // have to convert into.
    AImageReader* reader = nullptr;
    if (AImageReader_newWithUsage(width, height, AIMAGE_FORMAT_PRIVATE,
                                  AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE,
                                  /*maxImages=*/3, &reader) != AMEDIA_OK || !reader) {
        return false;
    }

    ANativeWindow* window = nullptr;
    if (AImageReader_getWindow(reader, &window) != AMEDIA_OK || !window) {
        AImageReader_delete(reader);
        return false;
    }

    // The ANativeWindow an AImageReader hands out is an android::Surface, so
    // its producer end is what the display attaches to.
    const auto& fns = android::detail::Functionals::GetInstance();
    if (!fns.Surface__GetIGraphicBufferProducer) {
        AImageReader_delete(reader);
        return false;
    }
    android::detail::StrongPointer<void> producer =
        fns.Surface__GetIGraphicBufferProducer(window);
    if (!producer.get()) {
        AImageReader_delete(reader);
        return false;
    }

    auto& composer = android::ANativeWindowCreator::GetComposerInstance();
    // Non-secure: a secure display would refuse to mirror protected content
    // and we would rather show it blurred than fail outright.
    android::detail::StrongPointer<void> token =
        composer.CreateVirtualDisplay("AImGuiMirror", /*secure=*/false);
    if (!token.get()) {
        AImageReader_delete(reader);
        return false;
    }

    android::detail::SurfaceComposerClientTransaction t;
    t.SetDisplaySurface(token, producer);
    // Layer stack 0 is the built-in display's, i.e. everything the user sees.
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

    if (m_Image) {
        AImage_delete(static_cast<AImage*>(m_Image));
        m_Image = nullptr;
    }
    if (m_Token) {
        android::detail::StrongPointer<void> token{};
        token.pointer = m_Token;
        android::ANativeWindowCreator::GetComposerInstance().DestroyVirtualDisplay(token);
        m_Token = nullptr;
    }
    if (m_Reader) {
        AImageReader_delete(static_cast<AImageReader*>(m_Reader));
        m_Reader = nullptr;
    }
    m_Running = false;
}

AHardwareBuffer* ScreenMirror::AcquireLatest() {
    if (!m_Running || !m_Reader) return nullptr;

    AImage* image = nullptr;
    if (AImageReader_acquireLatestImage(static_cast<AImageReader*>(m_Reader), &image)
            != AMEDIA_OK || !image) {
        return nullptr;   // nothing new since last call
    }

    // The previous image's buffer may still be referenced by the frame in
    // flight, so it is only released once a newer one has arrived.
    if (m_Image) AImage_delete(static_cast<AImage*>(m_Image));
    m_Image = image;
    ++m_Frames;

    AHardwareBuffer* buffer = nullptr;
    if (AImage_getHardwareBuffer(image, &buffer) != AMEDIA_OK) return nullptr;
    return buffer;
}

} // namespace aimgui
