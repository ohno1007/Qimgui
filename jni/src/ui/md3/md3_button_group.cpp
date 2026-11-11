#include "ui/md3_button_group.h"
#include "ui/md3_icons.h"
#include "md3_internal.h"

#include <array>
#include <cmath>

namespace aimgui::md3 {
namespace {
using namespace detail;

// ConnectedButtonGroupSmallTokens / ButtonGroupDefaults, AndroidX revision
// 587b8da057e3dfa7fb908e4b62b1bb84271eb20d: 40dp high, 2dp gaps, 8dp inner
// corners, 4dp pressed inner corners, full checked corners, .15 width expansion.
// Native immediate-mode interaction/layout implementation; upstream provenance
// is recorded in third_party/material_expressive/button_group/README.md.
struct Segment {
    ImGuiID id = 0;
    ImRect rect;
    float pressed = 0.f;
    bool hovered = false, held = false, visible = false;
};

void RoundedPath(ImDrawList* draw, const ImRect& rect, float left, float right) {
    const float limit = std::max(0.f, std::min(rect.GetWidth(), rect.GetHeight()) * .5f);
    left = std::clamp(left, 0.f, limit); right = std::clamp(right, 0.f, limit);
    draw->PathArcTo(ImVec2(rect.Min.x + left, rect.Min.y + left), left, IM_PI, IM_PI * 1.5f);
    draw->PathArcTo(ImVec2(rect.Max.x - right, rect.Min.y + right), right, IM_PI * 1.5f, IM_PI * 2.f);
    draw->PathArcTo(ImVec2(rect.Max.x - right, rect.Max.y - right), right, 0.f, IM_PI * .5f);
    draw->PathArcTo(ImVec2(rect.Min.x + left, rect.Max.y - left), left, IM_PI * .5f, IM_PI);
    // Narrow segments can become capsules with coincident arc endpoints.
    for (int i = draw->_Path.Size - 1; i > 0; --i) {
        const ImVec2 delta = Sub(draw->_Path[i], draw->_Path[i - 1]);
        if (delta.x * delta.x + delta.y * delta.y < 1.e-8f) draw->_Path.erase(draw->_Path.Data + i);
    }
    if (draw->_Path.Size > 1) {
        const ImVec2 delta = Sub(draw->_Path.front(), draw->_Path.back());
        if (delta.x * delta.x + delta.y * delta.y < 1.e-8f) draw->_Path.pop_back();
    }
}

void Fill(ImDrawList* draw, const ImRect& rect, float left, float right, ImU32 color) {
    if (!(color & IM_COL32_A_MASK)) return;
    RoundedPath(draw, rect, left, right); draw->PathFillConvex(color);
}

void Stroke(ImDrawList* draw, const ImRect& rect, float left, float right, ImU32 color, float width) {
    const float inset = std::min(width * .5f, std::min(rect.GetWidth(), rect.GetHeight()) * .25f);
    const ImRect inner(ImVec2(rect.Min.x + inset, rect.Min.y + inset),
                       ImVec2(rect.Max.x - inset, rect.Max.y - inset));
    RoundedPath(draw, inner, std::max(0.f, left - inset), std::max(0.f, right - inset));
    draw->PathStroke(color, width, ImDrawFlags_Closed);
}

void Content(ImDrawList* draw, const ImRect& rect, const ButtonGroupItem& item,
             bool selected, ImVec4 color) {
    const ImVec2 size = Measure(item.label, 14.f, true);
    const float inset = std::min(Dp(12), rect.GetWidth() * .25f);
    const ImRect clip(ImVec2(rect.Min.x + inset, rect.Min.y),
                      ImVec2(rect.Max.x - inset, rect.Max.y));
    if (clip.GetWidth() <= 0.f) return;
    const bool showIcon = (selected || item.icon) && clip.GetWidth() >= size.x + Dp(24);
    const float total = size.x + (showIcon ? Dp(24) : 0.f);
    float x = rect.GetCenter().x - total * .5f;
    draw->PushClipRect(clip.Min, clip.Max, true);
    if (showIcon) {
        const ImVec2 center(x + Dp(9), rect.GetCenter().y);
        if (selected) DrawCheck(center, color, 12.f);
        else if (!icons::DrawSymbol(draw, center, Dp(18), Color(color), item.icon))
            CenterText(ImRect(ImVec2(x, rect.Min.y), ImVec2(x + Dp(18), rect.Max.y)), item.icon, color, 18.f);
        x += Dp(24);
    }
    if (item.label) {
        const ImVec4 clip4(clip.Min.x, clip.Min.y, clip.Max.x, clip.Max.y);
        draw->AddText(Font(true), Dp(14), ImVec2(x, rect.GetCenter().y - size.y * .5f),
                      Color(color), item.label, VisibleEnd(item.label), 0.f, &clip4);
    }
    draw->PopClipRect();
}

bool Group(const char* id, int* single, std::uint64_t* multi,
           const ButtonGroupItem* items, int count, bool allowEmpty, const ButtonGroupOptions& options) {
    if (!id || (!single && !multi) || !items || count < 1 || count > 64 ||
        !std::isfinite(options.width) || !std::isfinite(options.height) ||
        !std::isfinite(options.spacing) || !std::isfinite(options.expandedRatio) ||
        options.width < 0.f || options.height <= 0.f || options.spacing < 0.f ||
        (options.style != ButtonGroupStyle::Filled && options.style != ButtonGroupStyle::Tonal &&
         options.style != ButtonGroupStyle::Outlined)) return false;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    const float width = options.width > 0.f ? Dp(options.width) : ImGui::GetContentRegionAvail().x;
    const float height = Dp(options.height);
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.f || height <= 0.f) return false;
    const float spacing = count > 1 ? std::min(Dp(options.spacing), width / (count * 2.f)) : 0.f;
    const float base = (width - spacing * (count - 1)) / count;
    const float expansion = std::clamp(options.expandedRatio, 0.f, 1.f) * base;
    const float compression = std::min(Dp(16), base * .25f);
    const bool parentEnabled = !Disabled();
    const auto& theme = GetTheme();
    const std::uint64_t mask = count == 64 ? UINT64_MAX : (std::uint64_t(1) << count) - 1;
    const auto selected = [&](int i) { return single ? *single == i : (*multi & (std::uint64_t(1) << i)) != 0; };
    bool changed = false;
    auto toggle = [&](int i) {
        if (single) {
            if (*single == i) return false;
            *single = i;
        } else {
            const auto next = *multi ^ (std::uint64_t(1) << i);
            if (!allowEmpty && !(next & mask)) return false;
            *multi = next;
        }
        changed = true; return true;
    };
    ImGui::PushID(id); ImGui::BeginGroup();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    std::array<Segment, 64> segments{};
    std::array<float, 64> widths{};
    for (int i = 0; i < count; ++i) {
        ImGui::PushID(i); segments[i].id = window->GetID("button"); ImGui::PopID();
        const float target = parentEnabled && items[i].enabled && GImGui->ActiveId == segments[i].id ? 1.f : 0.f;
        segments[i].pressed = options.animated ? Animate(segments[i].id, 51, target, 24.f) : target;
        widths[i] = base;
    }
    // Transfer only bounded padding from immediate neighbors. Even during
    // overlapping press/release animations every width remains >= base / 2.
    for (int i = 0; i < count; ++i) {
        const int neighbors = (i > 0 ? 1 : 0) + (i + 1 < count ? 1 : 0);
        if (!neighbors) continue;
        const float transfer = std::min(expansion / neighbors, compression) * segments[i].pressed;
        if (i > 0) { widths[i - 1] -= transfer; widths[i] += transfer; }
        if (i + 1 < count) { widths[i + 1] -= transfer; widths[i] += transfer; }
    }
    float x = origin.x;
    for (int i = 0; i < count; ++i) {
        segments[i].rect = ImRect(ImVec2(x, origin.y), ImVec2(i + 1 == count ? origin.x + width : x + widths[i], origin.y + height));
        x += widths[i] + spacing;
    }
    bool navigationHandled = false;
    for (int i = 0; i < count; ++i) {
        auto& segment = segments[i];
        const bool enabled = parentEnabled && items[i].enabled;
        ImGui::PushID(i); ImGui::BeginDisabled(!items[i].enabled);
        ImGui::SetCursorScreenPos(segment.rect.Min);
        ImGui::ItemSize(segment.rect);
        segment.visible = ImGui::ItemAdd(segment.rect, segment.id);
        if (segment.visible) {
            if (ImGui::ButtonBehavior(segment.rect, segment.id, &segment.hovered, &segment.held) && enabled && toggle(i))
                ImGui::MarkItemEdited(segment.id);
            if (enabled && !navigationHandled && ImGui::IsItemFocused()) {
                int next = i, direction = 0;
                if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) direction = -1;
                else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) direction = 1;
                else if (ImGui::IsKeyPressed(ImGuiKey_Home)) { next = count - 1; direction = 1; }
                else if (ImGui::IsKeyPressed(ImGuiKey_End)) { next = 0; direction = -1; }
                if (direction) {
                    for (int attempt = 0; attempt < count; ++attempt) {
                        next = (next + direction + count) % count;
                        if (items[next].enabled) break;
                    }
                    navigationHandled = true;
                    ImGui::NavMoveRequestCancel();
                    if (next != i) {
                        if (single && toggle(next)) ImGui::MarkItemEdited(segment.id);
                        ImGui::SetNavID(segments[next].id, ImGuiNavLayer_Main, GImGui->CurrentFocusScopeId,
                                       ImGui::WindowRectAbsToRel(window, segments[next].rect));
                        ImGui::SetNavCursorVisibleAfterMove();
                    }
                }
            }
        }
        ImGui::EndDisabled(); ImGui::PopID();
    }
    auto* draw = ImGui::GetWindowDrawList();
    for (int i = 0; i < count; ++i) {
        auto& segment = segments[i];
        if (!segment.visible) continue;
        const bool enabled = parentEnabled && items[i].enabled;
        const bool on = selected(i);
        const float checked = options.animated ? Animate(segment.id, 52, on ? 1.f : 0.f, 18.f) : (on ? 1.f : 0.f);
        const float half = height * .5f;
        const float restingInner = Dp(8) + (half - Dp(8)) * checked;
        // Like ToggleButtonShapes, the pressed shape takes precedence over
        // checked state; releasing returns to the fully rounded selection.
        const float inner = restingInner + (Dp(4) - restingInner) * segment.pressed;
        const float left = i == 0 ? half : inner;
        const float right = i + 1 == count ? half : inner;
        ImVec4 background = on ? theme.primary : theme.primaryContainer;
        ImVec4 foreground = on ? theme.onPrimary : theme.onPrimaryContainer;
        if (options.style == ButtonGroupStyle::Tonal) {
            background = on ? theme.secondaryContainer : theme.surfaceContainerHighest;
            foreground = on ? theme.onSecondaryContainer : theme.onSurfaceVariant;
        } else if (options.style == ButtonGroupStyle::Outlined) {
            background = on ? theme.secondaryContainer : ImVec4(0, 0, 0, 0);
            foreground = on ? theme.onSecondaryContainer : theme.onSurface;
        }
        if (!enabled) {
            if (background.w > 0.f) { background = theme.onSurface; background.w *= .12f; }
            foreground = theme.onSurface; foreground.w *= .38f;
        }
        Fill(draw, segment.rect, left, right, Color(background));
        if (options.style == ButtonGroupStyle::Outlined)
            Stroke(draw, segment.rect, left, right, Color(enabled ? theme.outline : theme.onSurface, enabled ? 1.f : .12f), Dp(1));
        const float overlay = enabled ? (segment.held ? .12f : segment.hovered ? .08f : 0.f) : 0.f;
        if (overlay > 0.f) Fill(draw, segment.rect, left, right, Color(foreground, overlay));
        Content(draw, segment.rect, items[i], on, foreground);
        if (enabled && GImGui->NavId == segment.id && GImGui->NavCursorVisible)
            Stroke(draw, segment.rect, left, right,
                   Color(background.w > 0.f ? foreground : theme.primary), Dp(2));
    }
    ImGui::SetCursorScreenPos(origin); ImGui::Dummy(ImVec2(width, height));
    ImGui::EndGroup(); ImGui::PopID();
    return changed;
}
} // namespace

bool ButtonGroup(const char* id, int* selected, const ButtonGroupItem* items, int count, const ButtonGroupOptions& options) {
    return Group(id, selected, nullptr, items, count, false, options);
}
bool MultiButtonGroup(const char* id, std::uint64_t* selected, const ButtonGroupItem* items,
                      int count, bool allowEmpty, const ButtonGroupOptions& options) {
    return Group(id, nullptr, selected, items, count, allowEmpty, options);
}
} // namespace aimgui::md3
