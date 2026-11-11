#include "md3_internal.h"
#include <cstdio>
namespace aimgui::md3 {
using namespace detail;
namespace {
ImVec4 Faded(ImVec4 c, float a) {
    c.w *= a;
    return c;
}
void Background(const ItemState &s, ImVec4 bg, ImVec4 fg, float r, bool outline = false,
                bool elevated = false) {
    auto *d = ImGui::GetWindowDrawList();
    if (elevated && !Disabled())
        d->AddRectFilled(Add(s.rect.Min, ImVec2(0, Dp(2))), Add(s.rect.Max, ImVec2(0, Dp(2))),
                         IM_COL32(0, 0, 0, 30), r);
    if (bg.w > 0)
        d->AddRectFilled(s.rect.Min, s.rect.Max, Color(bg), r);
    if (outline)
        d->AddRect(
            s.rect.Min, s.rect.Max,
            Color(Disabled() ? GetTheme().onSurface : GetTheme().outline, Disabled() ? .12f : 1), r,
            Dp(1));
    StateLayer(s, fg, r);
}
void Mark(const ItemState &s) {
    if (s.pressed)
        ImGui::MarkItemEdited(s.id);
}
} // namespace
bool Button(const char *label, ButtonStyle style, ImVec2 size, const char *icon) {
    const Theme &t = GetTheme();
    ImVec2 text = Measure(label, 14, true);
    float iw = icon ? Dp(26) : 0, pad = style == ButtonStyle::Text ? 12 : 24;
    ItemState s =
        Item(label, ImVec2(size.x > 0 ? Dp(size.x) : std::max(Dp(48), text.x + Dp(pad * 2) + iw),
                           size.y > 0 ? Dp(size.y) : Dp(40)));
    if (!s.visible)
        return false;
    ImVec4 bg(0, 0, 0, 0), fg = t.primary;
    if (style == ButtonStyle::Filled) {
        bg = t.primary;
        fg = t.onPrimary;
    }
    if (style == ButtonStyle::Tonal) {
        bg = t.secondaryContainer;
        fg = t.onSecondaryContainer;
    }
    if (style == ButtonStyle::Elevated)
        bg = t.surfaceContainerLow;
    if (Disabled()) {
        fg = Faded(t.onSurface, .38f);
        if (bg.w > 0)
            bg = Faded(t.onSurface, .12f);
    }
    Background(s, bg, fg, Dp(20), style == ButtonStyle::Outlined, style == ButtonStyle::Elevated);
    float x = (s.rect.Min.x + s.rect.Max.x - text.x - iw) * .5f;
    if (icon) {
        Text(ImVec2(x, s.rect.GetCenter().y - Dp(9)), icon, fg, 18);
        x += iw;
    }
    Text(ImVec2(x, s.rect.GetCenter().y - text.y * .5f), label, fg, 14, true);
    Mark(s);
    return s.pressed;
}
bool IconButton(const char *id, const char *icon, IconButtonStyle style, bool *selected) {
    const Theme &t = GetTheme();
    ItemState s = Item(id, ImVec2(Dp(48), Dp(48)));
    if (!s.visible)
        return false;
    if (s.pressed && selected)
        *selected = !*selected;
    bool on = !selected || *selected;
    ImVec4 bg(0, 0, 0, 0), fg = on ? t.primary : t.onSurfaceVariant;
    if (style == IconButtonStyle::Filled) {
        bg = on ? t.primary : t.surfaceContainerHigh;
        fg = on ? t.onPrimary : t.primary;
    }
    if (style == IconButtonStyle::Tonal) {
        bg = on ? t.secondaryContainer : t.surfaceContainerHigh;
        fg = on ? t.onSecondaryContainer : t.onSurfaceVariant;
    }
    if (style == IconButtonStyle::Outlined && on && selected) {
        bg = t.inverseSurface;
        fg = t.inverseOnSurface;
    }
    if (Disabled()) {
        fg = Faded(t.onSurface, .38f);
        if (bg.w > 0)
            bg = Faded(t.onSurface, .12f);
    }
    ItemState visual = s;
    visual.rect.Expand(-Dp(4));
    Background(visual, bg, fg, Dp(20), style == IconButtonStyle::Outlined);
    CenterText(visual.rect, icon, fg, 24);
    Mark(s);
    return s.pressed;
}
bool Fab(const char *id, const char *icon, FabSize size, const char *label) {
    const Theme &t = GetTheme();
    float h = size == FabSize::Small   ? 40
              : size == FabSize::Large ? 96
                                       : 56,
          r = size == FabSize::Small   ? 12
              : size == FabSize::Large ? 28
                                       : 16;
    ItemState s = Item(
        id, ImVec2(label ? std::max(Dp(80), Measure(label, 14, true).x + Dp(64)) : Dp(h), Dp(h)));
    if (!s.visible)
        return false;
    ImVec4 bg = Disabled() ? Faded(t.onSurface, .12f) : t.primaryContainer,
           fg = Disabled() ? Faded(t.onSurface, .38f) : t.onPrimaryContainer;
    Background(s, bg, fg, Dp(r), false, true);
    if (label) {
        CenterText(ImRect(s.rect.Min, ImVec2(s.rect.Min.x + Dp(48), s.rect.Max.y)), icon, fg, 24);
        Text(ImVec2(s.rect.Min.x + Dp(48), s.rect.GetCenter().y - Dp(7)), label, fg, 14, true);
    } else
        CenterText(s.rect, icon, fg, size == FabSize::Large ? 36 : 24);
    Mark(s);
    return s.pressed;
}
bool Checkbox(const char *label, bool *value, bool mixed) {
    IM_ASSERT(value);
    const Theme &t = GetTheme();
    ImVec2 txt = Measure(label);
    ItemState s = Item(label, ImVec2(Dp(48) + (txt.x > 0 ? txt.x + Dp(4) : 0), Dp(48)));
    if (!s.visible)
        return false;
    if (s.pressed) {
        *value = mixed ? true : !*value;
        Mark(s);
    }
    auto *d = ImGui::GetWindowDrawList();
    ImVec2 c(s.rect.Min.x + Dp(24), s.rect.GetCenter().y);
    ImVec4 fg =
        Disabled() ? Faded(t.onSurface, .38f) : (*value || mixed ? t.primary : t.onSurfaceVariant);
    ItemState state = s;
    state.rect = ImRect(Sub(c, ImVec2(Dp(20), Dp(20))), Add(c, ImVec2(Dp(20), Dp(20))));
    StateLayer(state, fg, Dp(20));
    ImVec2 p = Sub(c, ImVec2(Dp(9), Dp(9))), q = Add(c, ImVec2(Dp(9), Dp(9)));
    if (*value || mixed)
        d->AddRectFilled(p, q, Color(fg), Dp(2));
    else
        d->AddRect(p, q, Color(fg), Dp(2), Dp(2));
    ImVec4 mark = Disabled() ? t.surface : t.onPrimary;
    if (mixed)
        d->AddLine(ImVec2(c.x - Dp(5), c.y), ImVec2(c.x + Dp(5), c.y), Color(mark), Dp(2));
    else if (*value)
        DrawCheck(c, mark);
    Text(ImVec2(s.rect.Min.x + Dp(52), c.y - txt.y * .5f), label,
         Disabled() ? Faded(t.onSurface, .38f) : t.onSurface);
    return s.pressed;
}
bool RadioButton(const char *label, int *value, int option) {
    IM_ASSERT(value);
    const Theme &t = GetTheme();
    ImVec2 txt = Measure(label);
    ItemState s = Item(label, ImVec2(Dp(48) + (txt.x > 0 ? txt.x + Dp(4) : 0), Dp(48)));
    if (!s.visible)
        return false;
    bool changed = s.pressed && *value != option;
    if (changed) {
        *value = option;
        Mark(s);
    }
    ImVec2 c(s.rect.Min.x + Dp(24), s.rect.GetCenter().y);
    ImVec4 fg =
        Disabled() ? Faded(t.onSurface, .38f) : (*value == option ? t.primary : t.onSurfaceVariant);
    ItemState state = s;
    state.rect = ImRect(Sub(c, ImVec2(Dp(20), Dp(20))), Add(c, ImVec2(Dp(20), Dp(20))));
    StateLayer(state, fg, Dp(20));
    auto *d = ImGui::GetWindowDrawList();
    d->AddCircle(c, Dp(9), Color(fg), 0, Dp(2));
    if (*value == option)
        d->AddCircleFilled(c, Dp(5), Color(fg));
    Text(ImVec2(s.rect.Min.x + Dp(52), c.y - txt.y * .5f), label,
         Disabled() ? Faded(t.onSurface, .38f) : t.onSurface);
    return changed;
}
bool Switch(const char *label, bool *value, bool show_icons) {
    IM_ASSERT(value);
    const Theme &t = GetTheme();
    ImVec2 txt = Measure(label);
    ItemState s = Item(label, ImVec2(Dp(52) + (txt.x > 0 ? txt.x + Dp(16) : 0), Dp(48)));
    if (!s.visible)
        return false;
    if (s.pressed) {
        *value = !*value;
        Mark(s);
    }
    auto *d = ImGui::GetWindowDrawList();
    float y = s.rect.GetCenter().y;
    ImRect track(ImVec2(s.rect.Min.x, y - Dp(16)), ImVec2(s.rect.Min.x + Dp(52), y + Dp(16)));
    ImVec4 bg = *value ? t.primary : t.surfaceVariant, handle = *value ? t.onPrimary : t.outline;
    if (Disabled()) {
        bg = Faded(t.onSurface, .12f);
        handle = *value ? t.surface : Faded(t.onSurface, .38f);
    }
    d->AddRectFilled(track.Min, track.Max, Color(bg), Dp(16));
    if (!*value)
        d->AddRect(track.Min, track.Max,
                   Color(Disabled() ? t.onSurface : t.outline, Disabled() ? .12f : 1), Dp(16),
                   Dp(2));
    float progress = Animate(s.id, 1, *value ? 1.f : 0.f),
          radius = Animate(s.id, 2, s.held ? 14.f : (show_icons || *value ? 12.f : 8.f));
    ImVec2 c(s.rect.Min.x + Dp(16 + 20 * progress), y);
    ItemState state = s;
    state.rect = ImRect(Sub(c, ImVec2(Dp(20), Dp(20))), Add(c, ImVec2(Dp(20), Dp(20))));
    StateLayer(state, t.primary, Dp(20));
    d->AddCircleFilled(c, Dp(radius), Color(handle));
    if (show_icons) {
        if (*value)
            DrawCheck(c, Disabled() ? Faded(t.onSurface, .38f) : t.onPrimaryContainer, 10);
        else
            DrawClose(c, Disabled() ? t.surface : t.surfaceVariant, 10);
    }
    Text(ImVec2(s.rect.Min.x + Dp(68), y - txt.y * .5f), label,
         Disabled() ? Faded(t.onSurface, .38f) : t.onSurface);
    return s.pressed;
}
bool Chip(const char *label, ChipStyle style, bool *selected, bool *removed, const char *icon) {
    const Theme &t = GetTheme();
    bool on = selected && *selected, leading = icon || (style == ChipStyle::Filter && on);
    // Official chip tokens: leading icon 8 + 18 + 8dp; plain leading 16dp.
    // A trailing removal target keeps 8 + 18 + 8dp, including when disabled.
    const float leadingWidth = leading ? 34.f : 16.f;
    const float trailingWidth = removed ? 34.f : 16.f;
    float w = Measure(label, 14, true).x + Dp(leadingWidth + trailingWidth);
    // Split the trailing remove target from the body so keyboard and pointer input
    // never toggle a chip when the user activates its close icon.
    ImGuiWindow *win = ImGui::GetCurrentWindow();
    if (win->SkipItems)
        return false;
    ImVec2 start = win->DC.CursorPos;
    ImGui::BeginGroup();
    ItemState s = Item(label, ImVec2(w - (removed ? Dp(trailingWidth) : 0), Dp(32)));
    if (!s.visible && !removed) {
        ImGui::EndGroup();
        return false;
    }
    ImRect full(start, Add(start, ImVec2(w, Dp(32))));
    bool remove_hit = false;
    if (removed) {
        ImGui::SameLine(0, 0);
        ImGui::PushID(label);
        ItemState close = Item("##remove", ImVec2(Dp(trailingWidth), Dp(32)));
        ImGui::PopID();
        remove_hit = close.pressed;
        if (remove_hit)
            *removed = true;
    }
    s.rect = full;
    if (s.pressed && selected) {
        *selected = !*selected;
        on = *selected;
    }
    ImVec4 bg = on ? t.secondaryContainer : ImVec4(0, 0, 0, 0),
           fg = on ? t.onSecondaryContainer : t.onSurfaceVariant;
    if (Disabled()) {
        fg = Faded(t.onSurface, .38f);
        if (on)
            bg = Faded(t.onSurface, .12f);
    }
    Background(s, bg, fg, Dp(8), !on);
    float x = s.rect.Min.x + Dp(leadingWidth);
    if (leading) {
        if (style == ChipStyle::Filter && on)
            DrawCheck(ImVec2(s.rect.Min.x + Dp(17), s.rect.GetCenter().y), fg, 12);
        else
            Text(ImVec2(s.rect.Min.x + Dp(8), s.rect.GetCenter().y - Dp(9)), icon, fg, 18);
    }
    Text(ImVec2(x, s.rect.GetCenter().y - Dp(7)), label, fg, 14, true);
    if (removed)
        DrawClose(ImVec2(s.rect.Max.x - Dp(17), s.rect.GetCenter().y), fg, 12);
    Mark(s);
    ImGui::EndGroup();
    return s.pressed || remove_hit;
}
namespace {
float Fraction(float value, float lo, float hi) {
    return hi > lo ? std::clamp((value - lo) / (hi - lo), 0.f, 1.f) : 0.f;
}
float Snap(float v, float lo, float hi, int steps) {
    v = std::clamp(v, lo, hi);
    if (steps > 0 && hi > lo)
        v = lo + std::round((v - lo) / (hi - lo) * steps) * (hi - lo) / steps;
    return std::clamp(v, lo, hi);
}
bool SliderKeys(ImGuiID id, float *value, float lo, float hi, int steps) {
    if (Disabled() || GImGui->NavId != id)
        return false;
    float old = *value;
    ImGui::SetKeyOwner(ImGuiKey_Home, id);
    ImGui::SetKeyOwner(ImGuiKey_End, id);
    if (ImGui::IsKeyPressed(ImGuiKey_Home, ImGuiInputFlags_None, id))
        *value = lo;
    if (ImGui::IsKeyPressed(ImGuiKey_End, ImGuiInputFlags_None, id))
        *value = hi;
    *value = Snap(*value, lo, hi, steps);
    return *value != old;
}
void DrawTrack(const ImRect &r, float low, float high, bool disabled, int steps) {
    const Theme &t = GetTheme();
    auto *d = ImGui::GetWindowDrawList();
    // Official web track starts 40/2 - 2 = 18dp from either edge;
    // thumb/tick centers use the 20dp state-layer inset.
    float y = r.GetCenter().y, x0 = r.Min.x + Dp(18), x1 = r.Max.x - Dp(18);
    const float center0 = r.Min.x + Dp(20), span = r.GetWidth() - Dp(40);
    ImVec4 inactive = disabled ? Faded(t.onSurface, .12f) : t.secondaryContainer,
           active = disabled ? Faded(t.onSurface, .38f) : t.primary;
    d->AddRectFilled(ImVec2(x0, y - Dp(2)), ImVec2(x1, y + Dp(2)), Color(inactive), Dp(2));
    d->AddRectFilled(ImVec2(low <= 0 ? x0 : center0 + span * low, y - Dp(2)),
                     ImVec2(high >= 1 ? x1 : center0 + span * high, y + Dp(2)), Color(active),
                     Dp(2));
    if (steps > 0 && steps <= 100)
        for (int i = 0; i <= steps; ++i) {
            float f = float(i) / steps;
            d->AddCircleFilled(ImVec2(center0 + span * f, y), Dp(1),
                               Color(f >= low && f <= high ? t.onPrimary : t.onSecondaryContainer,
                                     disabled ? .38f : 1));
        }
}
void DrawThumb(ImGuiID id, const ImRect &r, float ratio, bool hovered, bool held) {
    const Theme &t = GetTheme();
    ImVec2 c(r.Min.x + Dp(20) + (r.GetWidth() - Dp(40)) * ratio, r.GetCenter().y);
    auto *d = ImGui::GetWindowDrawList();
    if (!Disabled() && (hovered || held || GImGui->NavId == id))
        d->AddCircleFilled(c, Dp(20), Color(t.primary, held ? .12f : .08f));
    d->AddCircleFilled(c, Dp(10), Color(Disabled() ? Faded(t.onSurface, .38f) : t.primary));
}
} // namespace
bool Slider(const char *label, float *value, float minimum, float maximum, float width, int steps,
            const char *format) {
    IM_ASSERT(value);
    ImGuiWindow *w = ImGui::GetCurrentWindow();
    if (w->SkipItems)
        return false;
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || maximum < minimum)
        return false;
    const Theme &t = GetTheme();
    const ImGuiID id = w->GetID(label);
    const bool has_label = VisibleEnd(label) != label;
    ImVec2 start = w->DC.CursorPos;
    ImRect frame(Add(start, ImVec2(0, has_label ? Dp(20) : 0)),
                 Add(start, ImVec2(Dp(std::max(width, 48.f)), Dp(has_label ? 68 : 48))));
    ImGui::ItemSize(ImRect(start, frame.Max));
    if (!ImGui::ItemAdd(ImRect(start, frame.Max), id, &frame))
        return false;
    bool disabled = Disabled(),
         hovered = ImGui::ItemHoverable(frame, id, GImGui->LastItemData.ItemFlags), changed = false;
    if (!disabled) {
        if (!std::isfinite(*value))
            *value = minimum;
        bool click = hovered && ImGui::IsMouseClicked(0, ImGuiInputFlags_None, id);
        if (click || GImGui->NavActivateId == id) {
            if (click)
                ImGui::SetKeyOwner(ImGuiKey_MouseLeft, id);
            ImGui::SetActiveID(id, w);
            ImGui::SetFocusID(id, w);
            ImGui::FocusWindow(w);
            GImGui->ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);
        }
        ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, Dp(16));
        ImRect grab;
        // ImGui adds a fixed 2px grab padding. Offset its behavior rectangle
        // so the input endpoints exactly match the density-scaled 20dp centers.
        ImRect behavior_frame = frame;
        behavior_frame.Min.x += Dp(12) - 2.f;
        behavior_frame.Max.x -= Dp(12) - 2.f;
        float old = *value;
        changed = ImGui::SliderBehavior(behavior_frame, id, ImGuiDataType_Float, value, &minimum,
                                        &maximum, format ? format : "%.0f",
                                        ImGuiSliderFlags_NoInput | ImGuiSliderFlags_NoRoundToFormat,
                                        &grab);
        ImGui::PopStyleVar();
        if (changed)
            *value = Snap(*value, minimum, maximum, steps);
        changed = (*value != old) || SliderKeys(id, value, minimum, maximum, steps);
    }
    float ratio = Fraction(std::isfinite(*value) ? *value : minimum, minimum, maximum);
    DrawTrack(frame, 0, ratio, disabled, steps);
    DrawThumb(id, frame, ratio, hovered, GImGui->ActiveId == id);
    ImGui::RenderNavCursor(frame, id);
    if (has_label) {
        Text(start, label, disabled ? Faded(t.onSurface, .38f) : t.onSurface);
        char buf[64];
        std::snprintf(buf, sizeof(buf), format ? format : "%.0f", *value);
        Text(ImVec2(frame.Max.x - Measure(buf).x, start.y), buf,
             disabled ? Faded(t.onSurface, .38f) : t.onSurfaceVariant);
    }
    if (changed)
        ImGui::MarkItemEdited(id);
    return changed;
}
bool RangeSlider(const char *label, float *lower, float *upper, float minimum, float maximum,
                 float width, int steps) {
    IM_ASSERT(lower && upper);
    ImGuiWindow *w = ImGui::GetCurrentWindow();
    if (w->SkipItems)
        return false;
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || maximum < minimum)
        return false;
    ImGui::PushID(label);
    ImVec2 start = w->DC.CursorPos;
    bool has_label = VisibleEnd(label) != label;
    ImRect frame(Add(start, ImVec2(0, has_label ? Dp(20) : 0)),
                 Add(start, ImVec2(Dp(std::max(width, 48.f)), Dp(has_label ? 68 : 48))));
    ImGui::ItemSize(ImRect(start, frame.Max));
    float l = Fraction(std::isfinite(*lower) ? *lower : minimum, minimum, maximum),
          u = Fraction(std::isfinite(*upper) ? *upper : maximum, minimum, maximum);
    float x0 = frame.Min.x + Dp(20), span = frame.GetWidth() - Dp(40);
    bool disabled = Disabled(), changed = false;
    int nearest = std::abs(ImGui::GetIO().MousePos.x - (x0 + l * span)) <
                          std::abs(ImGui::GetIO().MousePos.x - (x0 + u * span))
                      ? 0
                      : 1;
    ImGuiID ids[] = {w->GetID("lower"), w->GetID("upper")};
    bool hover[2] = {}, held[2] = {};
    for (int i = 0; i < 2; ++i) {
        float ratio = i ? u : l;
        ImVec2 c(x0 + ratio * span, frame.GetCenter().y);
        ImRect thumb(Sub(c, ImVec2(Dp(12), Dp(24))), Add(c, ImVec2(Dp(12), Dp(24))));
        if (!ImGui::ItemAdd(thumb, ids[i]))
            continue;
        ImRect hit = (i == nearest) ? frame : thumb;
        if (i == nearest || GImGui->ActiveId == ids[i] || GImGui->NavActivateId == ids[i] ||
            GImGui->NavId == ids[i])
            ImGui::ButtonBehavior(hit, ids[i], &hover[i], &held[i],
                                  ImGuiButtonFlags_PressedOnClick);
        float *v = i ? upper : lower;
        float lo = i ? std::max(minimum, std::isfinite(*lower) ? *lower : minimum) : minimum,
              hi = i ? maximum : std::min(maximum, std::isfinite(*upper) ? *upper : maximum);
        if (hi < lo)
            hi = lo;
        if (!disabled) {
            float old = *v;
            if (held[i] && GImGui->ActiveIdSource == ImGuiInputSource_Mouse)
                *v = Snap(minimum + Fraction(ImGui::GetIO().MousePos.x, x0, x0 + span) *
                                        (maximum - minimum),
                          minimum, maximum, steps);
            if (GImGui->NavId == ids[i] && (GImGui->ActiveId == 0 || GImGui->ActiveId == ids[i])) {
                float step = (maximum - minimum) / (steps > 0 ? steps : 100.f);
                ImGui::SetKeyOwner(ImGuiKey_LeftArrow, ids[i]);
                ImGui::SetKeyOwner(ImGuiKey_RightArrow, ids[i]);
                if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, ImGuiInputFlags_Repeat, ids[i]))
                    *v -= step;
                if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, ImGuiInputFlags_Repeat, ids[i]))
                    *v += step;
                SliderKeys(ids[i], v, lo, hi, 0);
            }
            if (!std::isfinite(*v))
                *v = lo;
            *v = std::clamp(*v, lo, hi);
            if (*v != old) {
                changed = true;
                ImGui::MarkItemEdited(ids[i]);
            }
        }
    }
    l = Fraction(std::isfinite(*lower) ? *lower : minimum, minimum, maximum);
    u = Fraction(std::isfinite(*upper) ? *upper : maximum, minimum, maximum);
    DrawTrack(frame, l, u, disabled, steps);
    for (int i = 0; i < 2; ++i)
        DrawThumb(ids[i], frame, i ? u : l, hover[i], held[i]);
    if (has_label)
        Text(start, label, disabled ? Faded(GetTheme().onSurface, .38f) : GetTheme().onSurface);
    ImGui::PopID();
    return changed;
}
bool TextField(const char *label, char *buffer, std::size_t capacity, TextFieldStyle style,
               float width, const char *supporting_text, bool error, ImGuiInputTextFlags flags) {
    IM_ASSERT(buffer && capacity > 0);
    ImGuiWindow *w = ImGui::GetCurrentWindow();
    if (w->SkipItems)
        return false;
    const Theme &t = GetTheme();
    bool disabled = Disabled();
    ImVec2 p = w->DC.CursorPos;
    ImGui::BeginGroup();
    float ww = Dp(std::max(width, 80.f));
    ImRect field(p, Add(p, ImVec2(ww, Dp(56))));
    auto *d = ImGui::GetWindowDrawList();
    ImVec4 fg = disabled ? Faded(t.onSurface, .38f) : t.onSurface,
           bg = style == TextFieldStyle::Filled ? t.surfaceContainerHighest : t.surface;
    if (disabled && style == TextFieldStyle::Filled)
        bg = Faded(t.onSurface, .04f);
    d->AddRectFilled(field.Min, field.Max, Color(bg), Dp(4),
                     style == TextFieldStyle::Filled ? ImDrawFlags_RoundTop : ImDrawFlags_RoundAll);
    ImGui::PushID(label);
    ImGuiID id = w->GetID("##input");
    bool old_focus = GImGui->ActiveId == id;
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, fg);
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, Faded(t.primary, .28f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(Dp(16), Dp(style == TextFieldStyle::Filled ? 24 : 20)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Dp(4));
    ImGui::PushFont(Font(), 16.f * t.scale);
    bool changed =
        ImGui::InputTextEx("##input", nullptr, buffer, int(capacity), ImVec2(ww, Dp(56)), flags);
    ImGui::PopFont();
    bool focused = GImGui->ActiveId == id, hovered = ImGui::IsItemHovered();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(6);
    ImGuiLastItemData input_item = GImGui->LastItemData;
    ImGui::PopID();
    ImVec4 edge = disabled  ? Faded(t.onSurface, style == TextFieldStyle::Filled ? .38f : .12f)
                  : error   ? t.error
                  : focused ? t.primary
                  : hovered ? t.onSurface
                  : style == TextFieldStyle::Filled ? t.onSurfaceVariant
                                                    : t.outline;
    ImVec4 label_color = disabled  ? Faded(t.onSurface, .38f)
                         : error   ? t.error
                         : focused ? t.primary
                                   : t.onSurfaceVariant;
    bool floating = buffer[0] != 0 || focused || old_focus;
    float label_size = floating ? 12.f : 16.f;
    if (style == TextFieldStyle::Outlined) {
        d->AddRect(field.Min, field.Max, Color(edge), Dp(4), Dp(focused ? 2 : 1));
        if (floating) {
            ImVec2 sz = Measure(label, label_size);
            d->AddRectFilled(ImVec2(p.x + Dp(12), p.y - Dp(7)),
                             ImVec2(p.x + Dp(20) + sz.x, p.y + Dp(7)), Color(t.surface));
        }
    } else {
        // Keep the solid indicator inside the field, matching the web border;
        // a centered AA stroke would split a 1px line over two pixel rows.
        const float height = Dp(focused ? 3 : 1);
        d->AddRectFilled(ImVec2(p.x, field.Max.y - height), field.Max, Color(edge));
    }
    Text(ImVec2(p.x + Dp(16),
                p.y + Dp(floating ? (style == TextFieldStyle::Outlined ? -6 : 8) : 20)),
         label, label_color, label_size);
    if (supporting_text && *supporting_text) {
        ImVec2 support_pos(p.x + Dp(16), field.Max.y + Dp(4));
        d->PushClipRect(ImVec2(p.x, field.Max.y), ImVec2(field.Max.x, field.Max.y + Dp(20)), true);
        Text(support_pos, supporting_text,
             error && !disabled ? t.error
             : disabled         ? Faded(t.onSurface, .38f)
                                : t.onSurfaceVariant,
             12);
        d->PopClipRect();
        // Register the supporting line as layout, then let the group restore
        // SameLine baselines. Moving the cursor alone cannot extend an item.
        ImGui::SetCursorScreenPos(ImVec2(p.x, field.Max.y));
        ImGui::Dummy(ImVec2(ww, Dp(20)));
    }
    ImGui::EndGroup();
    input_item.Rect = GImGui->LastItemData.Rect;
    GImGui->LastItemData = input_item;
    return changed;
}
} // namespace aimgui::md3
