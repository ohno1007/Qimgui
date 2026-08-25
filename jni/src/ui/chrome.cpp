#include "ui/ui_internal.h"

#include "imgui.h"

#include <cmath>
#include <vector>

namespace aimgui {

namespace chrome {
namespace {

constexpr int   kShadowLayers = 3;
constexpr float kShadowStep   = 2.0f;
constexpr float kRimThickness = 1.5f;
}

void Rect(const ImVec2& a, const ImVec2& b, float rounding,
          bool hovered, bool active) {
    const float h = b.y - a.y;
    const float w = b.x - a.x;
    if (h < 2.0f || w < 2.0f) return;
    float r = rounding < 0.0f ? h * 0.5f : rounding;
    const float rmax = (h < w ? h : w) * 0.5f;
    if (r > rmax) r = rmax;

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
