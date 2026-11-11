#pragma once

namespace aimgui::md3 {

// Per ImGui context. Theme palette/scale changes preserve these preferences.
// reduced=true or durationScale=0 settles transitions immediately and uses a
// still presentation for indeterminate progress. Otherwise 1 is normal speed,
// 2 takes twice as long. Values are clamped to 0..10; non-finite values use 1.
struct MotionSettings {
    bool reduced = false;
    float durationScale = 1.f;
};
void SetMotionSettings(const MotionSettings& settings);
const MotionSettings& GetMotionSettings();

} // namespace aimgui::md3
