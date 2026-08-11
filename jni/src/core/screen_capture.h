#pragma once

#include <cstdint>
#include <vector>

namespace aimgui {

// A small RGBA image of the current screen, used as the source texture for the
// frosted / liquid-glass backdrop.
struct ScreenShot {
    int                  width  = 0;
    int                  height = 0;
    std::vector<uint8_t> rgba;              // width*height*4, tightly packed
    bool valid() const { return width > 0 && height > 0 && !rgba.empty(); }
};

// Captures the screen via /system/bin/screencap and box-downscales it so its
// longer side is about `target_long_side` px, optionally running a separable
// Gaussian over the result.
//
// Capture rate is not frame rate. This forks screencap and reads a whole
// framebuffer over a pipe, so a call costs on the order of 100-300 ms and must
// never run on the render thread — but the texture it produces is then sampled
// every frame by the shader, so the *effect* still animates at full rate. Call
// it on a worker when the backdrop needs refreshing, not per frame.
//
// Unlike SurfaceFlinger's background blur this works on any Android version
// and hands back real pixels, which is what refraction-style effects need.
//
// Returns an invalid ScreenShot if screencap is unavailable or unparseable.
ScreenShot CaptureScreen(int target_long_side = 256, int blur_radius = 2);

} // namespace aimgui
