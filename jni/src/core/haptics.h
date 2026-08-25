#pragma once

namespace aimgui {

class Haptics {
public:
    bool Init();
    void Shutdown();
    bool Available() const { return m_Mode != Mode::None; }

    void Pulse(int ms, float strength);

private:
    enum class Mode { None, Evdev, SysfsLed, SysfsTimed };
    Mode m_Mode     = Mode::None;
    int  m_Fd       = -1;
    int  m_FdEnable = -1;
    int  m_FdDur    = -1;
    int  m_EffectId = -1;
};

namespace haptic {
void Install(Haptics* device, const bool* enabled);
void Tap();
void Step();
void Snap();
void Heavy();
}

}
