#pragma once

namespace aimgui {

struct GlassRect {
    float x = 0, y = 0, w = 0, h = 0;
    float rounding  = 12.0f;
    float edgeWidth = 60.0f;
    float bend      = 1.15f;
    float alpha     = 1.0f;
    float tintR = 0.0f, tintG = 0.0f, tintB = 0.0f;
    float tintA = 0.0f;

    float blur  = 7.0f;

    float lightX = -0.6f, lightY = -0.8f;

    float merge = 0.0f;

    int   group = 0;
};

constexpr int kMaxGlassRects = 8;

// Controls get glass of their own, one small pane each, in a second pass that
// samples what the first pass already put on screen. They never merge with each
// other, so they are not bound by kMaxMergedShapes — one draw apiece.
constexpr int kMaxWidgetGlass = 48;

constexpr int kMaxMergedShapes = 4;

struct GlassGroup {
    float shapes[kMaxMergedShapes * 4] = {};
};

constexpr int kMaxGlassGroups = 4;

inline bool BuildGlassGroup(const GlassRect* rects, int count, GlassGroup* out,
                            int group = 0, const GlassRect** lead = nullptr) {
    int n = 0;
    for (int i = 0; i < count && n < kMaxMergedShapes; ++i) {
        const GlassRect& r = rects[i];
        if (r.group != group) continue;
        if (r.w < 2.0f || r.h < 2.0f || r.alpha <= 0.001f) continue;
        if (lead && n == 0) *lead = &r;
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

}
