#include "ui/ui_internal.h"

#include "imgui.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace aimgui {

namespace chrome {
namespace {

constexpr int   kShadowLayers = 3;
constexpr float kShadowStep   = 2.0f;
constexpr float kRimThickness = 1.5f;

// A control's own glass adds very little of its own, because what it is
// refracting has already been washed, blurred and lit by the pane underneath.
// A second full-strength sheet on top of that is what makes glass-on-glass look
// like mud. So: no wash to speak of, a rim narrow enough that the two sides do
// not meet across a 48px control, and only a light bend.
constexpr float kEdgeShare = 0.30f;   // of the shorter side, same guard as the pill
constexpr float kEdgeMax   = 18.0f;
constexpr float kBend      = 0.95f;
constexpr float kBlur      = 2.0f;

bool      g_live = false;             // the second pass is running this frame
GlassRect g_rects[kMaxWidgetGlass];
int       g_count = 0;

// Everything drawn into a scrolling child can be scrolled out of it, and the
// glass pass knows nothing about ImGui's clip rects — so a control leaving the
// view would otherwise keep its pane, spilling over the edge of the child. The
// pane is trimmed to what is actually visible and faded by how much of it
// survived, so it slides away instead of popping.
bool Submit(const ImVec2& a, const ImVec2& b, float r, bool hovered, bool active) {
    if (g_count >= kMaxWidgetGlass) return false;
    const ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 cmin = dl->GetClipRectMin();
    const ImVec2 cmax = dl->GetClipRectMax();
    const float x0 = a.x > cmin.x ? a.x : cmin.x;
    const float y0 = a.y > cmin.y ? a.y : cmin.y;
    const float x1 = b.x < cmax.x ? b.x : cmax.x;
    const float y1 = b.y < cmax.y ? b.y : cmax.y;
    if (x1 - x0 < 2.0f || y1 - y0 < 2.0f) return true;

    const float full = (b.x - a.x) * (b.y - a.y);
    const float vis  = (x1 - x0) * (y1 - y0);
    const float share = full > 0.0f ? vis / full : 0.0f;

    const float minSide = (x1 - x0) < (y1 - y0) ? (x1 - x0) : (y1 - y0);
    float edge = minSide * kEdgeShare;
    if (edge > kEdgeMax) edge = kEdgeMax;
    if (edge < 4.0f)     edge = 4.0f;

    GlassRect p{};
    p.x = x0; p.y = y0; p.w = x1 - x0; p.h = y1 - y0;
    p.rounding  = r;
    p.edgeWidth = edge;
    p.bend      = kBend;
    p.blur      = kBlur;
    p.alpha     = share;
    p.merge     = 0.0f;
    p.group     = 0;
    // The only thing state changes: a touch more light held at the edge.
    p.tintA = active ? 0.10f : (hovered ? 0.06f : 0.0f);
    p.tintR = p.tintG = p.tintB = 1.0f;
    p.lightX = -0.6f;
    p.lightY = -0.8f;
    g_rects[g_count++] = p;
    return true;
}

// What a control looked like before it had glass, and what it falls back to
// when there is no mirror to refract: a contact shadow, a wash thin enough to
// separate it, and a rim lit from the same up-and-left key.
void Painted(const ImVec2& a, const ImVec2& b, float r, bool hovered, bool active) {
    ImDrawList* dl = ImGui::GetWindowDrawList();

    for (int i = kShadowLayers; i >= 1; --i) {
        const float o = (float)i * kShadowStep;
        dl->AddRectFilled(ImVec2(a.x - o * 0.3f, a.y + o * 0.4f),
                          ImVec2(b.x + o * 0.3f, b.y + o),
                          IM_COL32(0, 0, 0, 11), r + o * 0.3f);
    }

    const float fill = active ? 0.13f : (hovered ? 0.09f : 0.05f);
    dl->AddRectFilled(a, b, ImGui::GetColorU32(ImVec4(1, 1, 1, fill)), r);

    const float mid = (a.y + b.y) * 0.5f;
    const float top = active ? 0.34f : (hovered ? 0.28f : 0.20f);
    const ImVec2 pad(2.0f, 2.0f);
    dl->PushClipRect(ImVec2(a.x - pad.x, a.y - pad.y), ImVec2(b.x + pad.x, mid), true);
    dl->AddRect(a, b, ImGui::GetColorU32(ImVec4(1, 1, 1, top)), r, kRimThickness);
    dl->PopClipRect();
    dl->PushClipRect(ImVec2(a.x - pad.x, mid), ImVec2(b.x + pad.x, b.y + pad.y), true);
    dl->AddRect(a, b, ImGui::GetColorU32(ImVec4(1, 1, 1, top * 0.35f)), r, kRimThickness);
    dl->PopClipRect();
}

}

void BeginFrame(bool glass_live) {
    g_live  = glass_live;
    g_count = 0;
}

int Drain(GlassRect* out, int max) {
    const int n = g_count < max ? g_count : max;
    for (int i = 0; i < n; ++i) out[i] = g_rects[i];
    g_count = 0;
    return n;
}

void Rect(const ImVec2& a, const ImVec2& b, float rounding,
          bool hovered, bool active) {
    const float h = b.y - a.y;
    const float w = b.x - a.x;
    if (h < 2.0f || w < 2.0f) return;
    float r = rounding < 0.0f ? h * 0.5f : rounding;
    const float rmax = (h < w ? h : w) * 0.5f;
    if (r > rmax) r = rmax;

    // The pane is drawn by the renderer before any of ImGui's data, so the
    // control's own label lands on top of it with nothing else to arrange.
    if (g_live && Submit(a, b, r, hovered, active)) return;
    Painted(a, b, r, hovered, active);
}

namespace {

struct Press { ImGuiID id; float v; float vel; };
std::vector<Press> g_press;

float PressAmount(ImGuiID id, bool active, float dt) {
    Press* p = nullptr;
    for (Press& e : g_press) if (e.id == id) { p = &e; break; }
    if (!p) {

        if (g_press.size() >= 96) g_press.erase(g_press.begin());
        g_press.push_back({id, 0.0f, 0.0f});
        p = &g_press.back();
    }

    constexpr float kOmega = 30.0f, kZeta = 1.0f;
    const float diff  = (active ? 1.0f : 0.0f) - p->v;
    const float accel = kOmega * kOmega * diff - 2.0f * kZeta * kOmega * p->vel;
    p->vel += accel * dt;
    p->v   += p->vel * dt;
    if (p->v < 0.0f) p->v = 0.0f;
    return p->v;
}

void ApplySquash(ImVec2* a, ImVec2* b, float press) {
    const float d = 2.0f * press;
    a->x += d; a->y += d; b->x -= d; b->y -= d;
}
}

void LastItem(float rounding) {
    ImVec2 a = ImGui::GetItemRectMin();
    ImVec2 b = ImGui::GetItemRectMax();
    const float dt = ImGui::GetIO().DeltaTime > 0.0f ? ImGui::GetIO().DeltaTime : 1.0f / 60.0f;
    ApplySquash(&a, &b, PressAmount(ImGui::GetItemID(), ImGui::IsItemActive(), dt));
    Rect(a, b, rounding, ImGui::IsItemHovered(), ImGui::IsItemActive());
}

void LastItemFrame(const char* label, float rounding) {
    ImVec2 a = ImGui::GetItemRectMin();
    ImVec2 b = ImGui::GetItemRectMax();
    if (label) {
        const char* hash = std::strstr(label, "##");
        const char* end  = hash ? hash : label + std::strlen(label);
        const ImVec2 ls  = ImGui::CalcTextSize(label, end);
        if (ls.x > 0.0f) b.x -= ls.x + ImGui::GetStyle().ItemInnerSpacing.x;
    }
    const float dt = ImGui::GetIO().DeltaTime > 0.0f ? ImGui::GetIO().DeltaTime : 1.0f / 60.0f;
    ApplySquash(&a, &b, PressAmount(ImGui::GetItemID(), ImGui::IsItemActive(), dt));
    Rect(a, b, rounding, ImGui::IsItemHovered(), ImGui::IsItemActive());
}

}
}
