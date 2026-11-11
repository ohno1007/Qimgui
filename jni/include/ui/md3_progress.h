#pragma once

namespace aimgui::md3 {

// Material 3 Expressive progress defaults; lengths are logical dp.
struct WavyProgressStyle {
    float amplitude = 1.f;       // 0..1 of the default peak: linear 3dp, circular 1.6dp.
    float wavelength = 0.f;      // 0 selects 40/20dp linear determinate/indeterminate, 15dp circular.
    float strokeWidth = 4.f;
    float trackWidth = 4.f;
    float gap = 4.f;             // Clear space between active and remaining track.
    float stopSize = 4.f;        // Linear endpoint dot; zero hides it.
    float waveSpeed = 1.f;       // Wave cycles per second; zero freezes the wave.
    bool animate = true;        // False also freezes indeterminate motion.
};

// Caller owns progress. [0,1] is determinate; >1 clamps to complete.
// Negative/non-finite values display an indeterminate indicator.
// No animation is applied between determinate progress values. The wave becomes
// a straight line at <=10% and >=95%, following the expressive defaults.
// width=0 fills the available content region. The linear item reserves at least
// 10dp height, expanding for custom stroke/track/stop widths. Circular diameter
// is the complete reserved square. Non-positive/invalid geometry draws nothing.
// Primary and secondaryContainer provide indicator/track colors. These display
// items do not capture input or provide platform accessibility semantics.
// Reduced motion or durationScale=0 freezes animation at a stable readable state.
void WavyLinearProgress(const char* id, float value, float width = 240.f,
                        const WavyProgressStyle& style = {});
void WavyCircularProgress(const char* id, float value, float diameter = 48.f,
                          const WavyProgressStyle& style = {});

} // namespace aimgui::md3
