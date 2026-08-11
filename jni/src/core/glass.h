#pragma once

namespace aimgui {

// One pane of glass to be refracted this frame.
//
// Panes are submitted by the UI and drawn by the backend before ImGui's draw
// data, so ImGui's own widgets composite on top of the refracted screen. Doing
// it in the backend rather than on ImGui's draw list is what allows per-pixel
// lensing and per-channel dispersion; a draw-list mesh can only displace UVs
// per-vertex, which cannot express dispersion at all and quantises the bend
// exactly where it changes fastest — at the border.
struct GlassRect {
    float x = 0, y = 0, w = 0, h = 0;   // screen px
    float rounding  = 12.0f;
    float edgeWidth = 60.0f;            // how far in the lensing reaches, px
    float bend      = 1.15f;            // rim lensing strength
    float alpha     = 1.0f;
    float tintR = 0.0f, tintG = 0.0f, tintB = 0.0f;
    float tintA = 0.0f;                 // wash strength; keep low or it reads as a panel
    // Softens the transmitted image, in screen px. Glass this thick does not
    // transmit sharply, and UI text over a busy photo is unreadable however the
    // contrast is tuned — this is what buys the legibility back.
    float blur  = 7.0f;
    // Key-light direction in screen space, normalised by the shader. Driven by
    // the accelerometer where one is reachable, so the rim highlight sweeps as
    // the panel leans; falls back to a fixed up-and-left key otherwise.
    float lightX = -0.6f, lightY = -0.8f;
    // Surface-tension radius, px. Panes closer together than this grow a neck
    // joining them, exactly the way two drops of liquid brought near each other
    // do; further apart than this they are left alone, so the neck thins out
    // and lets go on its own rather than being switched off. 0 = no merging.
    float merge = 0.0f;
};

constexpr int kMaxGlassRects = 8;

// The shader merges at most this many panes in one pass. The limit is Vulkan's
// guaranteed 128 bytes of push constants: four vec4s of shared state plus four
// shapes come to exactly that. The quad's bounds used to take a fifth vec4 of
// shared state; the vertex shader derives them from the shapes instead, which
// is what bought the fourth shape.
constexpr int kMaxMergedShapes = 4;

// A group of panes packed the way both backends upload it: the shapes whose
// smooth union the distance field is.
struct GlassGroup {
    float shapes[kMaxMergedShapes * 4] = {};     // xy = centre, zw = half size
};

// Packs submitted rects into one merged group. Returns false when there is
// nothing to draw. Panes past kMaxMergedShapes are dropped; a slot with a
// non-positive half-width is how the shader knows where the list ends.
inline bool BuildGlassGroup(const GlassRect* rects, int count, GlassGroup* out) {
    int n = 0;
    for (int i = 0; i < count && n < kMaxMergedShapes; ++i) {
        const GlassRect& r = rects[i];
        if (r.w < 2.0f || r.h < 2.0f || r.alpha <= 0.001f) continue;
        const float hw = r.w * 0.5f, hh = r.h * 0.5f;
        out->shapes[n * 4 + 0] = r.x + hw;
        out->shapes[n * 4 + 1] = r.y + hh;
        out->shapes[n * 4 + 2] = hw;
        out->shapes[n * 4 + 3] = hh;
        ++n;
    }
    if (n == 0) return false;
    for (int i = n; i < kMaxMergedShapes; ++i)
        out->shapes[i * 4 + 0] = out->shapes[i * 4 + 1] =
        out->shapes[i * 4 + 2] = out->shapes[i * 4 + 3] = 0.0f;
    return true;
}

} // namespace aimgui
