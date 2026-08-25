#pragma once

#include <chrono>
#include <cstdint>

struct AHardwareBuffer;

namespace aimgui {

class ScreenMirror {
public:
    ~ScreenMirror() { Stop(); }

    static bool Available();

    bool Start(int width, int height, int srcWidth, int srcHeight);
    void Stop();
    bool running() const { return m_Running; }

    void Update();

    AHardwareBuffer* AcquireLatest();

    bool NeedsRestart(int srcWidth, int srcHeight) const {
        return m_Running && (srcWidth != m_SrcW || srcHeight != m_SrcH);
    }

    int  width()  const { return m_Width; }
    int  height() const { return m_Height; }

    uint64_t frames() const { return m_Frames; }

private:
    void*    m_Reader   = nullptr;
    void*    m_Window   = nullptr;
    void*    m_Image    = nullptr;
    void*    m_Token    = nullptr;
    void*    m_MirrorLayer = nullptr;
    uint32_t m_LayerStack   = 0;
    std::chrono::steady_clock::time_point m_Started{};
    bool     m_Running  = false;
    int      m_Width    = 0;
    int      m_Height   = 0;
    int      m_SrcW     = 0;
    int      m_SrcH     = 0;
    uint64_t m_Frames   = 0;
};

}
