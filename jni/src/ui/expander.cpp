#include "ui/ui_internal.h"
#include "core/haptics.h"

#include "imgui.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace aimgui {
namespace expander {
namespace {

constexpr float kRowH     = 54.0f;
constexpr float kPadX     = 22.0f;
constexpr float kBodyPadT = 6.0f;
constexpr float kBodyPadB = 16.0f;
constexpr float kPanelR   = 18.0f;
constexpr float kGapBelow = 10.0f;

// Under-damped, so it comes back through the open height rather than easing
// onto it. That overshoot is the whole of the elasticity — the panel arrives,
// goes slightly past, and settles.
constexpr float kOmega = 14.0f;
constexpr float kZeta  = 0.58f;

struct Ex {
    ImGuiID id     = 0;
    bool    open   = false;
    float   t      = 0.0f;
    float   vel    = 0.0f;
    float   body_h = 0.0f;
    float   y0     = 0.0f;   // cursor at the top of the body, for measuring
    bool    child  = false;  // a child was opened and End must close it
};

std::vector<Ex> g_all;

Ex* Get(ImGuiID id) {
    for (Ex& e : g_all) if (e.id == id) return &e;
    if (g_all.size() >= 64) g_all.erase(g_all.begin());
    g_all.push_back(Ex{});
    g_all.back().id = id;
    return &g_all.back();
}

// Ids, not pointers, and a stack rather than one slot: a body is free to hold
// another expander, and that would both nest and push_back into the vector the
// outer one's pointer came from.
std::vector<ImGuiID> g_stack;

void Spring(float* pos, float* vel, float target, float dt) {
    const float diff  = target - *pos;
    const float accel = kOmega * kOmega * diff - 2.0f * kZeta * kOmega * (*vel);
    *vel += accel * dt;
    *pos += (*vel) * dt;
    if (std::fabs(diff) < 0.0005f && std::fabs(*vel) < 0.003f) { *pos = target; *vel = 0.0f; }
}

// Two strokes rather than a glyph, because it has to turn: a chevron drawn by
// hand can be rotated to any angle, and the font's cannot.
void Chevron(ImDrawList* dl, ImVec2 c, float r, float turn, ImU32 col) {
    const float a = turn * 3.14159265f;          // 0 = pointing down, 1 = up
    const float s = std::sin(a), k = std::cos(a);
    auto rot = [&](float x, float y) {
        return ImVec2(c.x + x * k - y * s, c.y + x * s + y * k);
    };
    const ImVec2 l = rot(-r, -r * 0.45f);
    const ImVec2 m = rot(0.0f, r * 0.45f);
    const ImVec2 t = rot(r, -r * 0.45f);
    dl->PathLineTo(l);
    dl->PathLineTo(m);
    dl->PathLineTo(t);
    dl->PathStroke(col, 2.6f);
}

}

bool Begin(const char* label, const char* icon) {
    const ImGuiID id = ImGui::GetID(label);
    Ex* e = Get(id);
    g_stack.push_back(id);

    const float dt = ImGui::GetIO().DeltaTime > 0.0f ? ImGui::GetIO().DeltaTime
                                                     : 1.0f / 60.0f;
    Spring(&e->t, &e->vel, e->open ? 1.0f : 0.0f, dt);
    const float t = e->t < 0.0f ? 0.0f : e->t;

    const float w  = ImGui::GetContentRegionAvail().x;
    const float bh = e->body_h + kBodyPadT + kBodyPadB;
    const float h  = kRowH + t * bh;

    const ImVec2 p = ImGui::GetCursorScreenPos();

    // The header is the hit area; the body below it never toggles anything.
    ImGui::PushID(id);
    const bool hit = ImGui::InvisibleButton("##hdr", ImVec2(w, kRowH));
    const bool hovered = ImGui::IsItemHovered();
    const bool active  = ImGui::IsItemActive();
    if (hit) {
        e->open = !e->open;
        haptic::Tap();
    }

    // One body for both states. The rounding travels with it — a capsule when
    // it is a row, a panel when it is open — so there is never a moment where
    // the row is replaced by something else. Pinched at the sides while the
    // spring is moving fastest, which is what a thing being stretched does.
    float pinch = e->vel * 0.35f;
    if (pinch >  5.0f) pinch =  5.0f;
    if (pinch < -5.0f) pinch = -5.0f;
    const float r = kRowH * 0.5f + (kPanelR - kRowH * 0.5f) * t;
    chrome::Rect(ImVec2(p.x + pinch, p.y),
                 ImVec2(p.x + w - pinch, p.y + h), r, hovered, active);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cy = p.y + kRowH * 0.5f;
    const ImU32 col = ImGui::GetColorU32(
        ImVec4(1, 1, 1, active ? 1.0f : (hovered ? 0.95f : 0.82f)));

    float x = p.x + kPadX;
    if (icon && *icon) {
        const ImVec2 is = ImGui::CalcTextSize(icon);
        dl->AddText(ImVec2(x, cy - is.y * 0.5f), col, icon);
        x += is.x + 14.0f;
    }
    const char* hash = std::strstr(label, "##");
    const char* end  = hash ? hash : label + std::strlen(label);
    const ImVec2 ls  = ImGui::CalcTextSize(label, end);
    dl->AddText(ImVec2(x, cy - ls.y * 0.5f), col, label, end);
    Chevron(dl, ImVec2(p.x + w - kPadX - 8.0f, cy), 8.0f, t, col);

    // Opened at all, or still on the way back: the body has to be submitted so
    // it can be measured, even on the frame the height is still zero — nothing
    // knows how tall it is until it has been laid out once.
    const bool live = e->open || e->t > 0.002f;
    if (!live) {
        e->child = false;
        return false;
    }

    // Exactly at the bottom of the header. Left to itself ImGui would insert a
    // row of item spacing here, and the body would sit that much below the pane
    // drawn to contain it.
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + kRowH));

    float ch = h - kRowH;
    if (ch < 1.0f) ch = 1.0f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##body", ImVec2(w, ch), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    e->child = true;
    // Not a Dummy: that would carry a row of item spacing with it, and the pad
    // is measured out of the panel's height exactly.
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + kBodyPadT);
    ImGui::Indent(kPadX);
    e->y0 = ImGui::GetCursorPosY();

    // The words arrive on a surface that is already there.
    const float a = t < 0.35f ? 0.0f : (t - 0.35f) / 0.65f;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, a * a);
    return true;
}

void End() {
    if (g_stack.empty()) return;
    Ex* e = Get(g_stack.back());
    g_stack.pop_back();
    if (e->child) {
        ImGui::PopStyleVar();
        const float used = ImGui::GetCursorPosY() - e->y0;
        if (used > 0.0f) e->body_h = used;
        ImGui::Unindent(kPadX);
        ImGui::EndChild();
        ImGui::PopStyleColor();
        e->child = false;
    }
    ImGui::PopID();
    ImGui::Dummy(ImVec2(0, kGapBelow));
}

bool IsOpen(const char* label) {
    return Get(ImGui::GetID(label))->open;
}

}
}
