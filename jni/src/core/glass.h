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
};

constexpr int kMaxGlassRects = 8;

} // namespace aimgui
