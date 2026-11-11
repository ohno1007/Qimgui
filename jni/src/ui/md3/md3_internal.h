#pragma once
#include "imgui_internal.h"
#include "ui/md3.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace aimgui::md3::detail {
inline float Dp(float x) {
    return x * GetTheme().scale;
}
inline ImVec2 Add(ImVec2 a, ImVec2 b) {
    return ImVec2(a.x + b.x, a.y + b.y);
}
inline ImVec2 Sub(ImVec2 a, ImVec2 b) {
    return ImVec2(a.x - b.x, a.y - b.y);
}
inline bool Disabled() {
    return (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
}
inline ImU32 Color(ImVec4 c, float alpha = 1.f) {
    c.w *= alpha;
    return ImGui::GetColorU32(c);
}
const char *VisibleEnd(const char *text);
ImFont *Font(bool label = false);
ImVec2 Measure(const char *text, float size = 14.f, bool label = false);
void Text(ImVec2 position, const char *text, ImVec4 color, float size = 14.f, bool label = false);
void CenterText(const ImRect &rect, const char *text, ImVec4 color, float size = 14.f,
                bool label = false);
struct ItemState {
    ImRect rect;
    ImGuiID id = 0;
    bool visible = false, pressed = false, hovered = false, held = false;
};
ItemState Item(const char *label, ImVec2 size, ImGuiButtonFlags flags = 0);
float Animate(ImGuiID id, int channel, float target, float rate = 18.f);
void StateLayer(const ItemState &item, ImVec4 color, float radius);
void DrawCheck(ImVec2 center, ImVec4 color, float size = 12.f);
void DrawClose(ImVec2 center, ImVec4 color, float size = 12.f);
} // namespace aimgui::md3::detail
