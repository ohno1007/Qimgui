#include "ui/ui_internal.h"
#include "core/haptics.h"

#include "imgui.h"

#include <vector>

namespace aimgui {

// MD3 ripples. TouchLastItem captures the tap position and rect of the item
// just drawn if it was activated; DrawAll paints each as an expanding clipped
// white tint on the foreground list, once a frame after everything that could
// have recorded one.
namespace ripple {

namespace {
struct Entry {
    ImGuiID id;
    ImVec2  origin;
    ImVec2  rect_min;
    ImVec2  rect_max;
    float   start;
};
std::vector<Entry> g_ripples;
} // namespace

void TouchLastItem() {
    if (!ImGui::IsItemActivated()) return;
    // Every rippling control is also every pressable control, so this is the
    // one place a tap pulse belongs — adding it per widget would miss some.
    haptic::Tap();
    Entry e;
    e.id       = ImGui::GetItemID();
    e.origin   = ImGui::GetIO().MousePos;
    e.rect_min = ImGui::GetItemRectMin();
    e.rect_max = ImGui::GetItemRectMax();
    e.start    = (float)ImGui::GetTime();
    g_ripples.push_back(e);
}

void DrawAll() {
    const float now      = (float)ImGui::GetTime();
    const float duration = 0.55f;

    auto& v = g_ripples;
    for (auto it = v.begin(); it != v.end(); ) {
        const float t = (now - it->start) / duration;
        if (t >= 1.0f) { it = v.erase(it); continue; }

        const float ease   = 1.0f - (1.0f - t) * (1.0f - t); // quadratic ease-out
        const float dx     = it->rect_max.x - it->rect_min.x;
        const float dy     = it->rect_max.y - it->rect_min.y;
        const float max_r  = std::sqrt(dx * dx + dy * dy);
        const float radius = ease * max_r;
        const float alpha  = (1.0f - t) * 0.22f;

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        dl->PushClipRect(it->rect_min, it->rect_max, true);
        dl->AddCircleFilled(it->origin, radius,
                            ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, alpha)),
                            48);
        dl->PopClipRect();
        ++it;
    }
}

} // namespace ripple
} // namespace aimgui
