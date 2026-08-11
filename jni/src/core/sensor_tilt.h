#pragma once

namespace aimgui {

// Device tilt, used to steer the glass's key light.
//
// Real glass gives itself away by how the highlight sweeps when you move the
// slab, not by the highlight itself — a stationary rim light reads as painted
// on. The accelerometer is enough for that: at rest it measures gravity, so its
// x/y tell us which way the panel is leaning without needing to integrate a
// gyroscope.
//
// The NDK sensor API lives in libandroid.so and is reached through dlopen
// rather than linked: this process is a bare ELF with no package identity, and
// SensorManager is entitled to refuse it. Every entry point is therefore
// optional and failure is silent — Available() stays false and callers keep
// the fixed light direction.
class SensorTilt {
public:
    bool Init();
    void Shutdown();

    // Drains pending samples and advances the smoothing. Cheap; call per frame.
    void Update(float dt);

    bool  Available() const { return m_Ok; }
    // Lean in screen space, roughly -1..1 per axis, already smoothed.
    // (0,0) when the panel is face-up and level.
    float x() const { return m_X; }
    float y() const { return m_Y; }

private:
    void* m_Lib   = nullptr;
    void* m_Queue = nullptr;
    void* m_Looper = nullptr;
    bool  m_Ok = false;
    float m_X = 0.0f, m_Y = 0.0f;
};

} // namespace aimgui
