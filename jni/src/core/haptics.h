#pragma once

namespace aimgui {

// Vibration, driven straight at the kernel.
//
// The framework route to the vibrator is VibratorService over Binder, which
// wants a package identity this process does not have. What is left is what the
// HAL itself sits on, and there are three of those in the wild: force feedback
// through evdev, the LED-class sysfs node newer devices use, and the old
// timed_output node. All three are tried in that order — evdev first because it
// is the only one that carries an amplitude, so a light tap and a firm one can
// actually differ rather than only being long and short.
//
// Every path is optional. A device that offers none leaves Available() false
// and every Pulse() a no-op, which is the correct behaviour for something that
// is entirely feedback.
class Haptics {
public:
    bool Init();
    void Shutdown();
    bool Available() const { return m_Mode != Mode::None; }

    // Fire and forget. `strength` is 0..1 and is honoured only on the evdev
    // path; elsewhere the duration carries the whole message.
    void Pulse(int ms, float strength);

private:
    enum class Mode { None, Evdev, SysfsLed, SysfsTimed };
    Mode m_Mode     = Mode::None;
    int  m_Fd       = -1;   // evdev device, or the timed_output node
    int  m_FdEnable = -1;   // LED-class: activate
    int  m_FdDur    = -1;   // LED-class: duration
    int  m_EffectId = -1;
};

// A process-wide hook, so UI code can ask for feedback without the device being
// threaded through every function that might want it. `enabled` is read through
// on every call, which is what lets a settings toggle gate the lot.
namespace haptic {
void Install(Haptics* device, const bool* enabled);
void Tap();     // a control was pressed
void Step();    // the island moved to another rest state
void Snap();    // the strand let go, or found its way back
void Heavy();   // the window is coming apart
} // namespace haptic

} // namespace aimgui
