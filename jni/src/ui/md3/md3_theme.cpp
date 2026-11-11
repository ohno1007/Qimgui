#include "md3_internal.h"
#include "ui/md3_motion.h"
#include <array>
namespace aimgui::md3 {
namespace {
ImVec4 Rgb(unsigned c) {
    return ImVec4(((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f, 1.f);
}
Theme Palette(bool dark, float scale) {
    Theme t;
    t.dark = dark;
    t.scale = std::isfinite(scale) ? std::clamp(scale, .5f, 4.f) : 1.f;
    const unsigned light[] = {0x6750A4, 0xFFFFFF, 0xEADDFF, 0x21005D, 0xE8DEF8, 0x1D192B, 0xFEF7FF,
                              0xF3EDF7, 0xF7F2FA, 0xECE6F0, 0xE7E0EC, 0x1D1B20, 0x49454F, 0x79747E,
                              0xCAC4D0, 0xB3261E, 0xFFFFFF, 0x322F35, 0xF5EFF7, 0xD0BCFF};
    const unsigned night[] = {0xD0BCFF, 0x381E72, 0x4F378B, 0xEADDFF, 0x4A4458, 0xE8DEF8, 0x141218,
                              0x211F26, 0x1D1B20, 0x2B2930, 0x49454F, 0xE6E0E9, 0xCAC4D0, 0x938F99,
                              0x49454F, 0xF2B8B5, 0x601410, 0xE6E0E9, 0x322F35, 0x6750A4};
    const unsigned *c = dark ? night : light;
    ImVec4 *f[] = {&t.primary,
                   &t.onPrimary,
                   &t.primaryContainer,
                   &t.onPrimaryContainer,
                   &t.secondaryContainer,
                   &t.onSecondaryContainer,
                   &t.surface,
                   &t.surfaceContainer,
                   &t.surfaceContainerLow,
                   &t.surfaceContainerHigh,
                   &t.surfaceVariant,
                   &t.onSurface,
                   &t.onSurfaceVariant,
                   &t.outline,
                   &t.outlineVariant,
                   &t.error,
                   &t.onError,
                   &t.inverseSurface,
                   &t.inverseOnSurface,
                   &t.inversePrimary};
    for (unsigned i = 0; i < 20; ++i)
        *f[i] = Rgb(c[i]);
    t.surfaceContainerHighest = Rgb(dark ? 0x36343B : 0xE6E0E9);
    return t;
}
struct Animation {
    ImGuiID id = 0;
    int channel = 0, frame = -1;
    float value = 0;
};
struct Ripple {
    ImGuiID id = 0;
    int frame = -1;
    double started = 0, released = -1;
    ImVec2 origin; // Normalized position follows a moving/resized widget.
};
struct Context {
    ImGuiContext *owner = nullptr;
    Theme theme = Palette(false, 1);
    MotionSettings motion;
    ImFont *body = nullptr;
    ImFont *label = nullptr;
    std::array<Animation, 256> animations{};
    std::array<Ripple, 64> ripples{};
    Context *next = nullptr;
};
Context *contexts = nullptr;
void Shutdown(ImGuiContext *owner, ImGuiContextHook *) {
    Context **p = &contexts;
    while (*p) {
        if ((*p)->owner == owner) {
            Context *dead = *p;
            *p = dead->next;
            IM_DELETE(dead);
            return;
        }
        p = &(*p)->next;
    }
}
Context &Current() {
    IM_ASSERT(GImGui);
    for (Context *c = contexts; c; c = c->next)
        if (c->owner == GImGui)
            return *c;
    Context *c = IM_NEW(Context)();
    c->owner = GImGui;
    c->next = contexts;
    contexts = c;
    ImGuiContextHook h;
    h.Type = ImGuiContextHookType_Shutdown;
    h.Callback = Shutdown;
    ImGui::AddContextHook(GImGui, &h);
    return *c;
}
} // namespace
Theme &GetTheme() {
    return Current().theme;
}
void SetMotionSettings(const MotionSettings& settings) {
    auto& motion = Current().motion;
    motion.reduced = settings.reduced;
    motion.durationScale = std::isfinite(settings.durationScale)
        ? std::clamp(settings.durationScale, 0.f, 10.f) : 1.f;
}
const MotionSettings& GetMotionSettings() {
    return Current().motion;
}
void SetTheme(bool dark, float scale) {
    Current().theme = Palette(dark, scale);
}
void SetFonts(ImFont *body, ImFont *label) {
    Current().body = body;
    Current().label = label ? label : body;
}
void PushTheme() {
    const Theme &t = GetTheme();
    // Ordinary ImGui content inside MD3 uses the same body font as custom
    // controls; the Android shell's default font is restored on PopTheme.
    ImGui::PushFont(detail::Font(), detail::Dp(14));
    ImGui::PushStyleColor(ImGuiCol_Text, t.onSurface);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled,
                          ImVec4(t.onSurface.x, t.onSurface.y, t.onSurface.z, .38f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, t.surface);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, t.surfaceContainer);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, t.surfaceContainer);
    ImGui::PushStyleColor(ImGuiCol_Border, t.outlineVariant);
    ImGui::PushStyleColor(ImGuiCol_Header, t.secondaryContainer);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, t.surfaceContainerHigh);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, t.primaryContainer);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, t.primary);
    ImGui::PushStyleColor(ImGuiCol_NavCursor, t.primary);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, detail::Dp(28));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, detail::Dp(12));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, detail::Dp(12));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(detail::Dp(24), detail::Dp(24)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(detail::Dp(8), detail::Dp(8)));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(detail::Dp(16), detail::Dp(8)));
    ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.f);
}
void PopTheme() {
    ImGui::PopFont();
    ImGui::PopStyleVar(7);
    ImGui::PopStyleColor(11);
}
namespace detail {
const char *VisibleEnd(const char *t) {
    return ImGui::FindRenderedTextEnd(t ? t : "");
}
ImFont *Font(bool label) {
    auto &c = Current();
    ImFont *f = label ? c.label : c.body;
    return f ? f : ImGui::GetFont();
}
ImVec2 Measure(const char *text, float size, bool label) {
    return text ? Font(label)->CalcTextSizeA(Dp(size), FLT_MAX, 0, text, VisibleEnd(text))
                : ImVec2(0, 0);
}
void Text(ImVec2 p, const char *text, ImVec4 color, float size, bool label) {
    if (text)
        ImGui::GetWindowDrawList()->AddText(Font(label), Dp(size), p, Color(color), text,
                                            VisibleEnd(text));
}
void CenterText(const ImRect &r, const char *text, ImVec4 c, float size, bool label) {
    ImVec2 s = Measure(text, size, label);
    Text(ImVec2((r.Min.x + r.Max.x - s.x) * .5f, (r.Min.y + r.Max.y - s.y) * .5f), text, c, size,
         label);
}
ItemState Item(const char *label, ImVec2 size, ImGuiButtonFlags flags) {
    ItemState s;
    ImGuiWindow *w = ImGui::GetCurrentWindow();
    if (w->SkipItems)
        return s;
    s.id = w->GetID(label);
    s.rect = ImRect(w->DC.CursorPos, Add(w->DC.CursorPos, size));
    ImGui::ItemSize(s.rect);
    s.visible = ImGui::ItemAdd(s.rect, s.id);
    if (s.visible) {
        s.pressed = ImGui::ButtonBehavior(s.rect, s.id, &s.hovered, &s.held, flags);
        ImGui::RenderNavCursor(s.rect, s.id);
    }
    return s;
}
float Animate(ImGuiID id, int channel, float target, float rate) {
    auto &states = Current().animations;
    const auto& motion = Current().motion;
    Animation *choice = &states[0];
    int frame = ImGui::GetFrameCount();
    for (auto &s : states) {
        if (s.id == id && s.channel == channel) {
            choice = &s;
            break;
        }
        if (s.frame < choice->frame)
            choice = &s;
    }
    if (motion.reduced || motion.durationScale <= 0.f ||
        choice->id != id || choice->channel != channel || choice->frame < frame - 2) {
        choice->id = id;
        choice->channel = channel;
        choice->value = target;
    } else if (choice->frame != frame)
        choice->value +=
            (target - choice->value) * (1.f - std::exp(-rate * ImGui::GetIO().DeltaTime /
                                                     std::max(.01f, motion.durationScale)));
    choice->frame = frame;
    return choice->value;
}
namespace {
// Convex polygon intersection lets the ripple use ordinary ImGui draw data.
constexpr int RippleVertices = 128;
using Polygon = std::array<ImVec2, RippleVertices>;
float Cross(ImVec2 a, ImVec2 b, ImVec2 p) {
    return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
}
int RoundedOutline(const ImRect &rect, float radius, ImDrawFlags flags, Polygon &out) {
    if ((flags & ImDrawFlags_RoundMask_) == 0)
        flags |= ImDrawFlags_RoundAll;
    if (flags & ImDrawFlags_RoundNone)
        radius = 0;
    // Match Dear ImGui's PathRect limit, including partially rounded segments.
    const bool topBottom = (flags & ImDrawFlags_RoundTop) == ImDrawFlags_RoundTop ||
                           (flags & ImDrawFlags_RoundBottom) == ImDrawFlags_RoundBottom;
    const bool leftRight = (flags & ImDrawFlags_RoundLeft) == ImDrawFlags_RoundLeft ||
                           (flags & ImDrawFlags_RoundRight) == ImDrawFlags_RoundRight;
    radius =
        std::max(0.f, std::min(radius, std::min(rect.GetWidth() * (topBottom ? .5f : 1.f) - 1.f,
                                                rect.GetHeight() * (leftRight ? .5f : 1.f) - 1.f)));
    const ImVec2 corner[] = {
        {rect.Max.x, rect.Min.y}, rect.Max, {rect.Min.x, rect.Max.y}, rect.Min};
    const ImDrawFlags masks[] = {ImDrawFlags_RoundTopRight, ImDrawFlags_RoundBottomRight,
                                 ImDrawFlags_RoundBottomLeft, ImDrawFlags_RoundTopLeft};
    const ImVec2 signs[] = {{-1, 1}, {-1, -1}, {1, -1}, {1, 1}};
    int count = 0;
    for (int c = 0; c < 4; ++c) {
        const float r = (flags & masks[c]) ? radius : 0;
        if (r < .5f) {
            out[count++] = corner[c];
            continue;
        }
        const ImVec2 center(corner[c].x + signs[c].x * r, corner[c].y + signs[c].y * r);
        for (int segment = 0; segment <= 8; ++segment) {
            const float angle = (-1.f + c + segment / 8.f) * IM_PI * .5f;
            out[count++] = ImVec2(center.x + std::cos(angle) * r, center.y + std::sin(angle) * r);
        }
    }
    return count;
}
int ClipPolygon(Polygon &subject, int count, const Polygon &outline, int outlineCount) {
    Polygon next;
    for (int edge = 0; edge < outlineCount && count > 0; ++edge) {
        const ImVec2 a = outline[edge], b = outline[(edge + 1) % outlineCount];
        int nextCount = 0;
        ImVec2 previous = subject[count - 1];
        float previousDistance = Cross(a, b, previous);
        for (int i = 0; i < count; ++i) {
            const ImVec2 current = subject[i];
            const float distance = Cross(a, b, current);
            const bool inside = distance >= 0, previousInside = previousDistance >= 0;
            if (inside != previousInside) {
                const float denominator = previousDistance - distance;
                if (std::abs(denominator) > 1e-8f && nextCount < RippleVertices) {
                    const float t = previousDistance / denominator;
                    next[nextCount++] = ImVec2(previous.x + (current.x - previous.x) * t,
                                               previous.y + (current.y - previous.y) * t);
                }
            }
            if (inside && nextCount < RippleVertices)
                next[nextCount++] = current;
            previous = current;
            previousDistance = distance;
        }
        count = nextCount;
        for (int i = 0; i < count; ++i)
            subject[i] = next[i];
    }
    return count;
}
} // namespace
void RippleLayer(ImGuiID id, const ImRect &rect, ImVec4 color, float radius, bool held,
                 bool enabled, ImDrawFlags corners) {
    auto &pulses = Current().ripples;
    const auto& motion = Current().motion;
    Ripple *pulse = nullptr;
    for (auto &p : pulses)
        if (p.id == id) {
            pulse = &p;
            break;
        }
    if (!enabled || Disabled() || motion.reduced || motion.durationScale <= 0.f ||
        id == 0 || rect.GetWidth() <= 0 || rect.GetHeight() <= 0) {
        if (pulse)
            pulse->id = 0;
        return;
    }
    const int frame = ImGui::GetFrameCount();
    const double duration = std::max(.01f, motion.durationScale);
    if (pulse && pulse->frame < frame - 1) {
        pulse->id = 0;
        pulse = nullptr;
    }
    const bool pointerPress = held && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool keyPress = GImGui->NavActivatePressedId == id;
    const double now = ImGui::GetTime();
    if (pointerPress || keyPress) {
        if (!pulse) {
            pulse = &pulses[0];
            for (auto &p : pulses) {
                if (p.id == 0) {
                    pulse = &p;
                    break;
                }
                if (p.started < pulse->started)
                    pulse = &p;
            }
        }
        pulse->id = id;
        pulse->started = now;
        pulse->released = -1;
        pulse->origin =
            keyPress
                ? ImVec2(.5f, .5f)
                : ImVec2(std::clamp((ImGui::GetIO().MousePos.x - rect.Min.x) / rect.GetWidth(), 0.f,
                                    1.f),
                         std::clamp((ImGui::GetIO().MousePos.y - rect.Min.y) / rect.GetHeight(),
                                    0.f, 1.f));
    }
    if (!pulse)
        return;
    pulse->frame = frame;
    if (!held && pulse->released < 0)
        pulse->released = std::max(now, pulse->started + .18 * duration);
    const float fade =
        pulse->released < 0 ? 1.f : 1.f - float(std::max(0., now - pulse->released) / (.20 * duration));
    if (fade <= 0) {
        pulse->id = 0;
        return;
    }
    const float growth = std::clamp(float((now - pulse->started) / (.30 * duration)), 0.f, 1.f);
    const float eased = 1.f - std::pow(1.f - growth, 3.f);
    const ImVec2 origin(rect.Min.x + rect.GetWidth() * pulse->origin.x,
                        rect.Min.y + rect.GetHeight() * pulse->origin.y);
    const float dx = std::max(origin.x - rect.Min.x, rect.Max.x - origin.x);
    const float dy = std::max(origin.y - rect.Min.y, rect.Max.y - origin.y);
    const float extent = std::max(Dp(.5f), std::sqrt(dx * dx + dy * dy) * eased);
    Polygon circle, outline;
    constexpr int circleCount = 48;
    for (int i = 0; i < circleCount; ++i) {
        float angle = 2.f * IM_PI * i / circleCount;
        circle[i] =
            ImVec2(origin.x + std::cos(angle) * extent, origin.y + std::sin(angle) * extent);
    }
    const int outlineCount = RoundedOutline(rect, radius, corners, outline);
    const int count = ClipPolygon(circle, circleCount, outline, outlineCount);
    if (count >= 3) {
        ImDrawList *draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(rect.Min, rect.Max, true);
        draw->AddConvexPolyFilled(circle.data(), count, Color(color, .12f * fade));
        draw->PopClipRect();
    }
}
void StateLayer(const ItemState &item, ImVec4 color, float radius) {
    RippleLayer(item.id, item.rect, color, radius, item.held, !Disabled());
    if (Disabled())
        return;
    float opacity = Animate(item.id, 0, item.held ? .12f : item.hovered ? .08f : 0.f);
    if (opacity > .001f)
        ImGui::GetWindowDrawList()->AddRectFilled(item.rect.Min, item.rect.Max,
                                                  Color(color, opacity), radius);
    if (GImGui->NavId == item.id && GImGui->NavCursorVisible)
        ImGui::GetWindowDrawList()->AddRect(item.rect.Min, item.rect.Max, Color(GetTheme().primary),
                                            radius, Dp(2));
}
void DrawCheck(ImVec2 c, ImVec4 color, float size) {
    float s = Dp(size);
    auto *dl = ImGui::GetWindowDrawList();
    dl->AddLine(ImVec2(c.x - s * .45f, c.y), ImVec2(c.x - s * .1f, c.y + s * .3f), Color(color),
                Dp(2));
    dl->AddLine(ImVec2(c.x - s * .1f, c.y + s * .3f), ImVec2(c.x + s * .5f, c.y - s * .35f),
                Color(color), Dp(2));
}
void DrawClose(ImVec2 c, ImVec4 color, float size) {
    float s = Dp(size) * .4f;
    auto *dl = ImGui::GetWindowDrawList();
    dl->AddLine(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), Color(color), Dp(1.8f));
    dl->AddLine(ImVec2(c.x + s, c.y - s), ImVec2(c.x - s, c.y + s), Color(color), Dp(1.8f));
}
} // namespace detail
} // namespace aimgui::md3
