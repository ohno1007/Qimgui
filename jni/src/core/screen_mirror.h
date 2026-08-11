#pragma once

#include <cstdint>

struct AHardwareBuffer;

namespace aimgui {

// Live screen content, delivered as GPU buffers.
//
// Asks SurfaceFlinger to composite a layer stack into an AImageReader we own:
// the compositor writes straight into our buffers, so there is no capture
// call, no readback and no per-frame cost on this side — frames simply arrive
// at display refresh rate. That is what makes a refracting glass backdrop
// possible, which SurfaceFlinger's own background blur can't do (it exposes
// no sampleable pixels) and screencap can't do either (100-300 ms per grab).
//
// Our own overlay is created as a trusted overlay, so SurfaceFlinger leaves it
// out of this composition — the mirrored image is the screen *behind* us, with
// no feedback loop.
class ScreenMirror {
public:
    ~ScreenMirror() { Stop(); }

    // Whether libmediandk loaded and the display symbols resolved. Checked
    // before Start() so an unsupported device disables the feature instead
    // of failing mid-setup.
    static bool Available();

    // `width`/`height` size the mirror buffers; passing the display size
    // divided by 2-4 is plenty for a blurred backdrop and cuts the
    // compositor's scaling work. Returns false if the display couldn't be
    // created (see ANativeWindowCreator::ScreenCaptureSupported).
    bool Start(int width, int height, int srcWidth, int srcHeight);
    void Stop();
    bool running() const { return m_Running; }

    // Newest buffer, or nullptr if no frame has arrived since the last call.
    // The returned buffer stays valid until the following AcquireLatest().
    AHardwareBuffer* AcquireLatest();

    int  width()  const { return m_Width; }
    int  height() const { return m_Height; }
    // Frames taken delivery of so far — a cheap liveness signal for the UI.
    uint64_t frames() const { return m_Frames; }

private:
    void*    m_Reader   = nullptr;   // AImageReader*
    void*    m_Window   = nullptr;   // ANativeWindow*, owned by the reader
    void*    m_Image    = nullptr;   // AImage*, held while its buffer is in use
    void*    m_Token    = nullptr;   // display token (StrongPointer payload)
    bool     m_Running  = false;
    int      m_Width    = 0;
    int      m_Height   = 0;
    uint64_t m_Frames   = 0;
    uint64_t m_AcquireMisses = 0;
};

} // namespace aimgui
