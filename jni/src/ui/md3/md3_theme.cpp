#include "md3_internal.h"
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
struct Context {
    ImGuiContext *owner = nullptr;
    Theme theme = Palette(false, 1);
    ImFont *body = nullptr;
    ImFont *label = nullptr;
    std::array<Animation, 256> animations{};
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
void SetTheme(bool dark, float scale) {
    Current().theme = Palette(dark, scale);
}
void SetFonts(ImFont *body, ImFont *label) {
    Current().body = body;
    Current().label = label ? label : body;
}
void PushTheme() {
    const Theme &t = GetTheme();
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
    if (choice->id != id || choice->channel != channel || choice->frame < frame - 2) {
        choice->id = id;
        choice->channel = channel;
        choice->value = target;
    } else if (choice->frame != frame)
        choice->value +=
            (target - choice->value) * (1.f - std::exp(-rate * ImGui::GetIO().DeltaTime));
    choice->frame = frame;
    return choice->value;
}
void StateLayer(const ItemState &item, ImVec4 color, float radius) {
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
