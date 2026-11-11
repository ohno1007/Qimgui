#include "ui/md3_split_button.h"
#include "ui/md3_icons.h"
#include "md3_internal.h"
#include <array>

// Small split-button dimensions follow AndroidX SplitButtonSmallTokens.
// Apache-2.0 upstream tokens are pinned in third_party/material_expressive/toolbar_split/.
namespace aimgui::md3 {
namespace {
using namespace detail;
constexpr float Pi = 3.14159265358979323846f;
struct Outline {
    std::array<ImVec2, 44> points{};
    int count = 0;
};
Outline RoundedOutline(const ImRect& rect, float left, float right) {
    const float limit = std::min(rect.GetWidth(), rect.GetHeight()) * .5f;
    left = std::clamp(left, 0.f, limit); right = std::clamp(right, 0.f, limit);
    Outline result;
    const float radii[] = {left, right, right, left};
    const ImVec2 centers[] = {{rect.Min.x+left,rect.Min.y+left}, {rect.Max.x-right,rect.Min.y+right},
                             {rect.Max.x-right,rect.Max.y-right}, {rect.Min.x+left,rect.Max.y-left}};
    for (int corner = 0; corner < 4; ++corner)
        for (int step = 0; step <= 10; ++step) {
            const float angle = (180.f + corner * 90.f + step * 9.f) * Pi / 180.f;
            result.points[result.count++] = ImVec2(centers[corner].x + std::cos(angle) * radii[corner],
                                                   centers[corner].y + std::sin(angle) * radii[corner]);
        }
    return result;
}
void PaintSegment(const ItemState& item, ButtonStyle style, float left, float right,
                  ImVec4 background, ImVec4 foreground) {
    if (!item.visible) return;
    auto* draw = ImGui::GetWindowDrawList();
    const auto outline = RoundedOutline(item.rect, left, right);
    if (background.w > 0) draw->AddConvexPolyFilled(outline.points.data(), outline.count, Color(background));
    if (style == ButtonStyle::Outlined)
        draw->AddPolyline(outline.points.data(), outline.count,
                          Color(Disabled() ? GetTheme().onSurface : GetTheme().outline, Disabled() ? .12f : 1.f),
                          Dp(1), ImDrawFlags_Closed);
    if (!Disabled()) {
        const float opacity = Animate(item.id, 0, item.held ? .12f : item.hovered ? .08f : 0.f);
        if (opacity > .001f)
            draw->AddConvexPolyFilled(outline.points.data(), outline.count, Color(foreground, opacity));
        if (GImGui->NavId == item.id && GImGui->NavCursorVisible)
            draw->AddPolyline(outline.points.data(), outline.count,
                              Color(background.w > 0 ? foreground : GetTheme().primary), Dp(2), ImDrawFlags_Closed);
    }
}
}

SplitButtonAction SplitButton(const char* id, const char* label, bool* expanded,
                              ButtonStyle style, const char* icon, float width, bool enabled) {
    using namespace detail;
    if (!id || !label || !expanded || !std::isfinite(width) || width < 0) return SplitButtonAction::None;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return SplitButtonAction::None;
    const Theme& theme = GetTheme();
    const float natural = std::max(Dp(48), Measure(label, 14, true).x + Dp(28 + (icon ? 26 : 0))) + Dp(50);
    const float total = std::min(width > 0 ? Dp(width) : natural, std::max(0.f, ImGui::GetContentRegionAvail().x));
    if (!std::isfinite(total) || total <= 0) return SplitButtonAction::None;
    const float spacing = std::min(Dp(2), total * .05f);
    const float inner = total - spacing;
    const float trailing = inner >= Dp(72) ? Dp(48) : inner * .5f;
    const float leading = inner - trailing;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size(total, Dp(40));
    const ImRect bounds(origin, Add(origin, size));
    ImGui::PushID(id); ImGui::BeginGroup(); ImGui::BeginDisabled(!enabled);
    const bool active = !Disabled();
    const ImGuiID primaryId = window->GetID("primary"), menuId = window->GetID("menu");
    const auto primary = Item("primary", ImVec2(leading, size.y));
    ImGui::SetCursorScreenPos(ImVec2(origin.x + leading + spacing, origin.y));
    const auto menu = Item("menu", ImVec2(trailing, size.y));
    SplitButtonAction action = SplitButtonAction::None;
    if (primary.pressed) { action = SplitButtonAction::Primary; ImGui::MarkItemEdited(primary.id); }
    if (menu.pressed) {
        *expanded = !*expanded; action = SplitButtonAction::ToggleMenu; ImGui::MarkItemEdited(menu.id);
    }
    if (active && (GImGui->NavId == primaryId || GImGui->NavId == menuId) &&
        (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_RightArrow))) {
        const bool nextIsMenu = ImGui::IsKeyPressed(ImGuiKey_RightArrow);
        ImGui::NavMoveRequestCancel();
        ImGui::SetNavID(nextIsMenu ? menuId : primaryId, ImGuiNavLayer_Main, GImGui->CurrentFocusScopeId,
                        ImGui::WindowRectAbsToRel(window, nextIsMenu ? menu.rect : primary.rect));
        ImGui::SetNavCursorVisibleAfterMove();
    }
    ImVec4 background(0,0,0,0), foreground = theme.primary;
    if (style == ButtonStyle::Filled) { background = theme.primary; foreground = theme.onPrimary; }
    else if (style == ButtonStyle::Tonal) { background = theme.secondaryContainer; foreground = theme.onSecondaryContainer; }
    else if (style == ButtonStyle::Elevated) background = theme.surfaceContainerLow;
    if (!active) {
        foreground = theme.onSurface; foreground.w *= .38f;
        if (background.w > 0) { background = theme.onSurface; background.w *= .12f; }
    }
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(Sub(bounds.Min, ImVec2(Dp(1), Dp(1))), Add(bounds.Max, ImVec2(Dp(1), Dp(1))), true);
    const float selection = Animate(menu.id, 21, *expanded ? 1.f : 0.f);
    const float primaryInner = Dp(Animate(primary.id, 22, active && (primary.held || primary.hovered) ? 12.f : 4.f));
    const float menuInner = Dp(Animate(menu.id, 22, *expanded ? 20.f : active && (menu.held || menu.hovered) ? 12.f : 4.f));
    PaintSegment(primary, style, Dp(20), primaryInner, background, foreground);
    PaintSegment(menu, style, menuInner, Dp(20), background, foreground);
    if (primary.visible) {
        draw->PushClipRect(primary.rect.Min, primary.rect.Max, true);
        ImRect labelRect = primary.rect;
        labelRect.Min.x += std::min(Dp(12), leading * .125f);
        labelRect.Max.x -= std::min(Dp(8), leading * .125f);
        if (icon && leading >= Dp(40)) {
            const float contentWidth = std::min(labelRect.GetWidth(), Measure(label, 14, true).x + Dp(26));
            const float contentLeft = primary.rect.GetCenter().x - contentWidth * .5f;
            const ImVec2 center(contentLeft + Dp(9), primary.rect.GetCenter().y);
            if (!icons::DrawSymbol(draw, center, Dp(18), Color(foreground), icon))
                CenterText(ImRect(Sub(center, ImVec2(Dp(9), Dp(9))), Add(center, ImVec2(Dp(9), Dp(9)))),
                           icon, foreground, 18);
            labelRect.Min.x = contentLeft + Dp(26);
            labelRect.Max.x = contentLeft + contentWidth;
        }
        if (labelRect.Max.x > labelRect.Min.x) {
            draw->PushClipRect(labelRect.Min, labelRect.Max, true);
            CenterText(labelRect, label, foreground, 14, true);
            draw->PopClipRect();
        }
        draw->PopClipRect();
    }
    if (menu.visible) {
        draw->PushClipRect(menu.rect.Min, menu.rect.Max, true);
        const float angle = selection * Pi, cosine = std::cos(angle), sine = std::sin(angle);
        const float unit = std::min(Dp(1), trailing / 16.f);
        const ImVec2 center = menu.rect.GetCenter();
        const ImVec2 local[] = {{-5,-2.5f},{0,2.5f},{5,-2.5f}};
        ImVec2 arrow[3];
        for (int i = 0; i < 3; ++i)
            arrow[i] = ImVec2(center.x + unit * (cosine * local[i].x - sine * local[i].y),
                              center.y + unit * (sine * local[i].x + cosine * local[i].y));
        draw->AddPolyline(arrow, 3, Color(foreground), std::max(.5f, 2.f * unit));
        draw->PopClipRect();
    }
    draw->PopClipRect();
    ImGui::EndDisabled(); ImGui::SetCursorScreenPos(origin); ImGui::Dummy(size);
    ImGui::EndGroup(); ImGui::PopID();
    return action;
}
} // namespace aimgui::md3
