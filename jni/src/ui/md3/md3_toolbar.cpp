#include "ui/md3_toolbar.h"
#include "ui/md3_layout.h"
#include "ui/md3_icons.h"
#include "md3_internal.h"
#include <array>

// Dimensions and color roles follow AndroidX FloatingToolbarTokens (Apache-2.0).
// See third_party/material_expressive/toolbar_split/ for pinned sources.
namespace aimgui::md3 {
namespace {
using namespace detail;
void FocusToolbarItem(ImGuiWindow* window, ImGuiID id, const ImRect& rect) {
    ImGui::NavMoveRequestCancel();
    ImGui::SetNavID(id, ImGuiNavLayer_Main, GImGui->CurrentFocusScopeId,
                    ImGui::WindowRectAbsToRel(window, rect));
    ImGui::SetNavCursorVisibleAfterMove();
}
}

int Toolbar(const char* id, const ToolbarItem* items, int count,
            ToolbarOrientation orientation, ToolbarStyle style, float extent) {
    using namespace detail;
    if (!id || !items || count < 1 || count > 64 || !std::isfinite(extent) || extent < 0 ||
        (orientation != ToolbarOrientation::Horizontal && orientation != ToolbarOrientation::Vertical) ||
        (style != ToolbarStyle::Standard && style != ToolbarStyle::Vibrant)) return -1;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return -1;
    const bool horizontal = orientation == ToolbarOrientation::Horizontal;
    const bool parentEnabled = !Disabled();
    const Theme& theme = GetTheme();
    std::array<float, 64> natural{};
    float total = 0.f, widest = Dp(48);
    for (int i = 0; i < count; ++i) {
        const float text = items[i].label ? Measure(items[i].label, 14, true).x : 0.f;
        const float width = text > 0 ? text + Dp(24 + (items[i].icon ? 26 : 0)) : Dp(48);
        widest = std::max(widest, width);
        natural[i] = horizontal ? std::max(Dp(48), width) : Dp(48);
        total += natural[i];
    }
    const float availableWidth = std::max(0.f, ImGui::GetContentRegionAvail().x);
    const float naturalLength = total + Dp(16 + 4.f * (count - 1));
    float length = extent > 0 ? std::min(naturalLength, Dp(extent)) : naturalLength;
    if (horizontal) length = std::min(length, availableWidth);
    const float cross = std::min(availableWidth, horizontal ? Dp(64) : widest + Dp(16));
    if (!std::isfinite(length) || !std::isfinite(cross) || length <= 0 || cross <= 0) return -1;
    const float pad = std::min(Dp(8), length * .125f);
    const float crossPad = std::min(Dp(8), cross * .125f);
    const float gap = count > 1 ? std::min(Dp(4), (length - 2 * pad) / (count * 4.f)) : 0;
    const float ratio = std::max(0.f, length - 2 * pad - gap * (count - 1)) / total;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = horizontal ? ImVec2(length, Dp(64)) : ImVec2(cross, length);
    const ImRect container(origin, Add(origin, size));
    const ImVec4 base = style == ToolbarStyle::Vibrant ? theme.primaryContainer : theme.surfaceContainer;
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(container.Min, container.Max, Color(base), std::min(size.x, size.y) * .5f);
    draw->PushClipRect(container.Min, container.Max, true);
    ImGui::PushID(id);
    std::array<ImRect, 64> rects;
    std::array<ImGuiID, 64> ids{};
    float along = pad;
    for (int i = 0; i < count; ++i) {
        const float itemLength = natural[i] * ratio;
        if (horizontal) rects[i] = ImRect(Add(origin, ImVec2(along, Dp(8))),
                                          Add(origin, ImVec2(along + itemLength, Dp(56))));
        else rects[i] = ImRect(Add(origin, ImVec2(crossPad, along)),
                               Add(origin, ImVec2(cross - crossPad, along + itemLength)));
        ImGui::PushID(i); ids[i] = window->GetID(items[i].id ? items[i].id : "##tool"); ImGui::PopID();
        along += itemLength + gap;
    }
    int activated = -1;
    bool navigationHandled = false;
    ImGui::BeginGroup();
    for (int i = 0; i < count; ++i) {
        ImGui::PushID(i); ImGui::BeginDisabled(!items[i].enabled);
        ImGui::SetCursorScreenPos(rects[i].Min);
        ItemState item = Item(items[i].id ? items[i].id : "##tool", rects[i].GetSize());
        if (item.pressed) {
            activated = i;
            if (items[i].selected) *items[i].selected = !*items[i].selected;
            ImGui::MarkItemEdited(item.id);
        }
        if (parentEnabled && items[i].enabled && !navigationHandled && ImGui::IsItemFocused()) {
            const ImGuiKey previous = horizontal ? ImGuiKey_LeftArrow : ImGuiKey_UpArrow;
            const ImGuiKey next = horizontal ? ImGuiKey_RightArrow : ImGuiKey_DownArrow;
            int candidate = i, direction = 0;
            if (ImGui::IsKeyPressed(previous)) direction = -1;
            else if (ImGui::IsKeyPressed(next)) direction = 1;
            else if (ImGui::IsKeyPressed(ImGuiKey_Home)) { candidate = count - 1; direction = 1; }
            else if (ImGui::IsKeyPressed(ImGuiKey_End)) { candidate = 0; direction = -1; }
            if (direction) {
                for (int tries = 0; tries < count; ++tries) {
                    candidate = (candidate + direction + count) % count;
                    if (items[candidate].enabled) break;
                }
                FocusToolbarItem(window, ids[candidate], rects[candidate]);
                navigationHandled = true;
            }
        }
        if (item.visible) {
            const bool selected = items[i].selected && *items[i].selected;
            ImVec4 foreground = style == ToolbarStyle::Vibrant ? theme.onPrimaryContainer : theme.onSurface;
            if (selected) foreground = style == ToolbarStyle::Vibrant ? theme.onSurface : theme.onSecondaryContainer;
            if (Disabled()) foreground.w *= .38f;
            ImRect visual = item.rect;
            visual.Expand(-std::min(Dp(4), std::min(visual.GetWidth(), visual.GetHeight()) * .125f));
            const float radius = std::min(visual.GetWidth(), visual.GetHeight()) * .5f;
            if (selected) draw->AddRectFilled(visual.Min, visual.Max,
                Color(style == ToolbarStyle::Vibrant ? theme.surfaceContainer : theme.secondaryContainer), radius);
            ItemState feedback = item; feedback.rect = visual;
            StateLayer(feedback, foreground, radius);
            draw->PushClipRect(visual.Min, visual.Max, true);
            const float iconSize = std::min(Dp(24), std::max(0.f, std::min(visual.GetWidth(), visual.GetHeight()) - Dp(4)));
            const bool hasLabel = items[i].label && *items[i].label;
            ImRect labelRect = visual;
            if (items[i].icon && iconSize > 0) {
                const ImVec2 center = hasLabel ? ImVec2(visual.Min.x + Dp(16), visual.GetCenter().y) : visual.GetCenter();
                if (!icons::DrawSymbol(draw, center, iconSize, Color(foreground), items[i].icon))
                    CenterText(ImRect(Sub(center, ImVec2(iconSize*.5f, iconSize*.5f)),
                                      Add(center, ImVec2(iconSize*.5f, iconSize*.5f))),
                               items[i].icon, foreground, iconSize / theme.scale, true);
                if (hasLabel) labelRect.Min.x += Dp(30);
            }
            if (hasLabel && labelRect.Max.x > labelRect.Min.x)
                CenterText(labelRect, items[i].label, foreground, 14, true);
            draw->PopClipRect();
            if (items[i].tooltip) Tooltip(items[i].tooltip);
        }
        ImGui::EndDisabled(); ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(origin); ImGui::Dummy(size); ImGui::EndGroup();
    ImGui::PopID(); draw->PopClipRect();
    return activated;
}
} // namespace aimgui::md3
