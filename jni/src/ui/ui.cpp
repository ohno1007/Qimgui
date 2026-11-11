#include "ui/ui.h"
#include "ui/main_ui.h"
#include "ui/md3.h"
#include "ui/md3_layout.h"

#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace aimgui {
namespace {
namespace md = md3;

float UiScale(const UiState& state, ImVec2 display) {
    float automatic = std::clamp(std::min(display.x, display.y) / 480.f, 1.f, 1.25f);
    // A floating window sets its own density; a tall phone display should not
    // enlarge a compact saved window into a nearly full-screen panel.
    if (std::isfinite(state.last_full_size.x) && state.last_full_size.x > 0 &&
        std::isfinite(state.last_full_size.y) && state.last_full_size.y > 0)
        automatic = std::min({automatic, state.last_full_size.x / 560.f,
                              state.last_full_size.y / 456.f});
    const float requested = std::isfinite(state.ui_scale) && state.ui_scale > 0 ? state.ui_scale : automatic;
    // Keep room for both columns and five labelled destinations, even on a
    // narrow or rotated display. Navigation always stays on the left.
    const float fitting = std::min({2.5f, (display.x - 24.f) / 560.f, (display.y - 24.f) / 456.f});
    return std::clamp(requested, .5f, std::max(.5f, fitting));
}

void ClampWindow(UiState& state, ImVec2 display, float scale) {
    const float margin = std::min(12.f * scale, std::min(display.x, display.y) * .025f);
    const ImVec2 maximum(std::max(1.f, display.x - margin * 2), std::max(1.f, display.y - margin * 2));
    const ImVec2 minimum(std::min(maximum.x, 520.f * scale), std::min(maximum.y, 440.f * scale));
    const bool bad_size = !std::isfinite(state.last_full_size.x) || state.last_full_size.x <= 0 ||
                          !std::isfinite(state.last_full_size.y) || state.last_full_size.y <= 0;
    if (!state.ui_initialized) {
        const bool original_default = state.last_full_size.x == 900.f && state.last_full_size.y == 620.f &&
                                      state.last_full_pos.x == 60.f && state.last_full_pos.y == 100.f;
        const bool legacy_tall = !bad_size && state.last_full_size.y > state.last_full_size.x * 1.2f;
        if (bad_size || original_default || legacy_tall) {
            const float fit = std::min({1.f, maximum.x / 900.f, maximum.y / 620.f});
            state.last_full_size = ImVec2(900.f * fit, 620.f * fit);
            state.request_center_window = true;
        }
        state.ui_initialized = true;
    }
    if (!std::isfinite(state.last_full_size.x) || !std::isfinite(state.last_full_size.y))
        state.last_full_size = minimum;
    state.last_full_size.x = std::clamp(state.last_full_size.x, minimum.x, maximum.x);
    state.last_full_size.y = std::clamp(state.last_full_size.y, minimum.y, maximum.y);
    if (state.request_center_window || !std::isfinite(state.last_full_pos.x) || !std::isfinite(state.last_full_pos.y)) {
        state.last_full_pos = ImVec2((display.x - state.last_full_size.x) * .5f,
                                    (display.y - state.last_full_size.y) * .5f);
        state.request_center_window = false;
    }
    state.last_full_pos.x = std::clamp(state.last_full_pos.x, margin, std::max(margin, display.x - margin - state.last_full_size.x));
    state.last_full_pos.y = std::clamp(state.last_full_pos.y, margin, std::max(margin, display.y - margin - state.last_full_size.y));
}

bool GlassLive(const UiState& state) {
    return state.screen_mirror && state.glass_available && !state.glass_failed &&
           state.screen_texture_id != 0;
}

void SubmitGlass(UiState& state, const ImVec4& rect, float rounding) {
    if (!GlassLive(state) || rect.z <= 1 || rect.w <= 1 || state.glass_count >= kMaxGlassRects) return;
    GlassRect glass{};
    glass.x = rect.x; glass.y = rect.y; glass.w = rect.z; glass.h = rect.w;
    glass.rounding = rounding;
    glass.edgeWidth = std::clamp(std::min(rect.z, rect.w) * .12f, 24.f, 80.f);
    glass.blur = 6.f;
    glass.bend = .65f;
    glass.lightX = state.glass_light_x;
    glass.lightY = state.glass_light_y;
    const auto& theme = md::GetTheme();
    glass.tintR = theme.surface.x;
    glass.tintG = theme.surface.y;
    glass.tintB = theme.surface.z;
    glass.tintA = std::clamp(std::isfinite(state.glass_clarity) ? state.glass_clarity : .11f, 0.f, .35f);
    state.glass_rects[state.glass_count++] = glass;
}

void AppBar(UiState& state, ImVec2 display, float scale) {
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const char* actions[] = {"@minimize", "@close"};
    char title[96];
    std::snprintf(title, sizeof(title), "Qimgui · %s", kPages[state.nav_page].label);
    // Inherit the rounded window surface instead of covering the glass with
    // an opaque square. Navigation, text and action tokens remain unchanged.
    auto& theme = md::GetTheme();
    const float surface_alpha = theme.surface.w;
    theme.surface.w = 0;
    const int action = md::TopAppBar("app-bar", title, md::AppBarStyle::Small, nullptr, actions, 2);
    theme.surface.w = surface_alpha;
    if (action == 0) state.stage = UiState::StageIsland;
    if (action == 1) state.request_exit = true;

    const ImVec2 next = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(start.x + 16.f * scale, start.y + 8.f * scale));
    ImGui::InvisibleButton("window-drag", ImVec2(std::max(1.f, width - 128.f * scale), 48.f * scale));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        state.last_full_pos.x += ImGui::GetIO().MouseDelta.x;
        state.last_full_pos.y += ImGui::GetIO().MouseDelta.y;
        ClampWindow(state, display, scale);
    }
    ImGui::SetCursorScreenPos(next);
}

void GlassSwitch(UiState& state, ImVec2 position, float scale) {
    ImGui::SetCursorScreenPos(ImVec2(position.x + 16.f * scale, position.y + 64.f * scale));
    // A saved request must always be switchable off, even if capture support
    // disappears after a system update or an initialization failure.
    ImGui::BeginDisabled(!state.glass_available && !state.screen_mirror);
    if (md::Switch(u8"液体玻璃##glass-toggle", &state.screen_mirror)) state.glass_failed = false;
    const ImVec2 switch_min = ImGui::GetItemRectMin(), switch_max = ImGui::GetItemRectMax();
    ImGui::EndDisabled();
    md::Tooltip(u8"切换窗口材质，导航和控件使用同一套 MD3 样式。");
    ImGui::SameLine(0, 16.f * scale);
    ImGui::PushStyleColor(ImGuiCol_Text, md::GetTheme().onSurfaceVariant);
    const char* status = !state.glass_available ? u8"当前设备不可用"
        : !state.screen_mirror ? u8"标准外观"
        : state.glass_failed ? u8"启用失败"
        : GlassLive(state) ? u8"已开启" : u8"正在启用…";
    ImVec2 text_pos = ImGui::GetCursorScreenPos();
    text_pos.y = (switch_min.y + switch_max.y - ImGui::GetFontSize()) * .5f;
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - 16.f * scale;
    ImGui::SetCursorScreenPos(text_pos);
    ImGui::Dummy(ImVec2(std::max(0.f, right - text_pos.x), ImGui::GetFontSize()));
    if (right > text_pos.x)
        ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), text_pos,
            ImVec2(right, text_pos.y + ImGui::GetFontSize()), right, status, nullptr, nullptr);
    ImGui::PopStyleColor();
}

void DrawNavigation(UiState& state, float width) {
    md::NavigationItem items[5];
    const int count = std::min(kPagesCount, 5);
    for (int i = 0; i < count; ++i) items[i] = {kPages[i].label, kPages[i].icon, 0, true};
    auto& theme = md::GetTheme();
    const float surface_alpha = theme.surface.w, container_alpha = theme.surfaceContainer.w;
    if (GlassLive(state)) {
        theme.surface.w = .30f;
        theme.surfaceContainer.w = .55f;
    }
    md::Navigation("main-navigation", &state.nav_page, items, count, md::NavigationStyle::Drawer, width);
    theme.surface.w = surface_alpha;
    theme.surfaceContainer.w = container_alpha;
}

void Content(UiState& state, ImVec2 pos, ImVec2 size, float scale, bool compact) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushID(state.nav_page);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2((compact ? 16.f : 24.f) * scale, 16.f * scale));
    ImVec4 surface = md::GetTheme().surfaceContainerLow;
    if (GlassLive(state)) surface.w = .60f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, surface);
    if (ImGui::BeginChild("main-content", size, ImGuiChildFlags_AlwaysUseWindowPadding))
        DrawPage(&state, static_cast<Page>(state.nav_page));
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::PopID();
}

void ResizeGrip(UiState& state, ImVec2 display, float scale) {
    const ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    const float side = 20.f * scale;
    const ImVec2 corner(pos.x + size.x - side, pos.y + size.y - side);
    ImGui::SetCursorScreenPos(corner);
    ImGui::InvisibleButton("window-resize", ImVec2(side, side));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        state.last_full_size.x += ImGui::GetIO().MouseDelta.x;
        state.last_full_size.y += ImGui::GetIO().MouseDelta.y;
        ClampWindow(state, display, scale);
    }
    auto* draw = ImGui::GetWindowDrawList();
    const ImU32 color = ImGui::GetColorU32(md::GetTheme().outline);
    for (int i = 0; i < 2; ++i) {
        const float inset = (7.f + i * 5.f) * scale;
        draw->AddLine(ImVec2(pos.x + size.x - inset, pos.y + size.y - 5.f * scale),
                      ImVec2(pos.x + size.x - 5.f * scale, pos.y + size.y - inset), color, scale);
    }
}

void ExitDialog(UiState& state, bool* keep_running) {
    if (md::BeginDialog("exit-confirmation", &state.request_exit, u8"退出 Qimgui", false, 360.f)) {
        ImGui::TextWrapped(u8"关闭窗口并保存当前设置？");
        if (md::Button(u8"取消##exit-cancel", md::ButtonStyle::Text)) {
            state.request_exit = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (md::Button(u8"退出##exit-confirm")) {
            state.request_exit = false;
            if (keep_running) *keep_running = false;
            ImGui::CloseCurrentPopup();
        }
        md::EndDialog();
    }
}

void CompactWindow(UiState& state, ImVec2 display, float scale) {
    // A hidden modal must not intercept taps on the restore button. Controlled
    // page dialogs may reopen when their page becomes visible again.
    ImGui::ClosePopupsOverWindow(nullptr, false);
    state.request_exit = false;
    const ImVec2 size(std::min(224.f * scale, std::max(1.f, display.x - 16.f)), std::min(80.f * scale, display.y));
    const ImVec2 pos((display.x - size.x) * .5f, std::min(16.f * scale, std::max(0.f, display.y - size.y)));
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowBgAlpha(GlassLive(state) ? .35f : 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.f * scale, 12.f * scale));
    if (ImGui::Begin("Qimgui compact###aimgui_compact", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse)) {
        if (md::Fab("restore-window", "@expand", md::FabSize::Medium, u8"展开 Qimgui"))
            state.stage = UiState::StageWindow;
        state.interaction_rect = ImVec4(pos.x, pos.y, size.x, size.y);
        SubmitGlass(state, state.interaction_rect, 28.f * scale);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}
} // namespace

void DrawUi(UiState* state, bool* keep_running) {
    if (!state) return;
    state->glass_count = 0;
    state->widget_count = 0;
    state->interaction_rect = ImVec4();
    state->modal_open = false;
    state->exit_anim_active = false;
    const ImVec2 display(state->display_w > 0 ? static_cast<float>(state->display_w) : ImGui::GetIO().DisplaySize.x,
                         state->display_h > 0 ? static_cast<float>(state->display_h) : ImGui::GetIO().DisplaySize.y);
    if (display.x <= 0 || display.y <= 0) return;
    const float scale = UiScale(*state, display);
    ClampWindow(*state, display, scale);
    state->nav_page = std::clamp(state->nav_page, 0, std::max(0, kPagesCount - 1));
    state->collapsed = state->stage != UiState::StageWindow;
    state->expand = state->collapsed ? 0.f : 1.f;
    md::SetTheme(state->md3_dark, scale);
    md::PushTheme();
    const auto& theme = md::GetTheme();
    ImVec4 scrollbar_hovered = theme.outline, scrollbar_active = theme.primary;
    scrollbar_hovered.w = .8f;
    scrollbar_active.w = .8f;
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, theme.outlineVariant);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, scrollbar_hovered);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, scrollbar_active);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 12.f * scale);
    if (state->collapsed) {
        CompactWindow(*state, display, scale);
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(4);
        md::PopTheme();
        return;
    }

    ImGui::SetNextWindowPos(state->last_full_pos);
    ImGui::SetNextWindowSize(state->last_full_size);
    if (ImGui::Begin("Qimgui###aimgui_main", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoScrollWithMouse)) {
        const ImVec2 position = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        auto* background = ImGui::GetWindowDrawList();
        background->ChannelsSplit(2);
        background->ChannelsSetCurrent(1);
        AppBar(*state, display, scale);
        GlassSwitch(*state, position, scale);
        const float logical_width = size.x / scale;
        const float inset = 16.f * scale;
        const float body_top = position.y + 120.f * scale;
        const float body_bottom = position.y + size.y - inset;
        const float body_height = std::max(1.f, body_bottom - body_top);
        const float nav_left = position.x + inset;
        const float nav_width = std::clamp(logical_width * .25f, 128.f, 230.f) * scale;
        const float content_left = nav_left + nav_width + inset;
        const float content_right = position.x + size.x - inset;
        ImGui::SetCursorScreenPos(ImVec2(nav_left, body_top));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        if (ImGui::BeginChild("navigation-panel", ImVec2(nav_width, body_height), 0,
                              ImGuiWindowFlags_NoBackground))
            DrawNavigation(*state, ImGui::GetContentRegionAvail().x / scale);
        ImGui::EndChild();
        ImGui::PopStyleVar();
        Content(*state, ImVec2(content_left, body_top),
                ImVec2(std::max(1.f, content_right - content_left), body_height), scale,
                (content_right - content_left) / scale < 600.f);
        ResizeGrip(*state, display, scale);
        ExitDialog(*state, keep_running);
        state->interaction_rect = ImVec4(position.x, position.y, size.x, size.y);
        SubmitGlass(*state, state->interaction_rect, 28.f * scale);

        // Evaluate after the switch. Disabling glass paints an opaque MD3
        // surface immediately, even while the previous capture texture exists.
        background->ChannelsSetCurrent(0);
        ImVec4 fill = md::GetTheme().surface;
        if (GlassLive(*state)) fill.w = state->md3_dark ? .42f : .32f;
        background->AddRectFilled(position, ImVec2(position.x + size.x, position.y + size.y),
                                  ImGui::GetColorU32(fill), 28.f * scale);
        background->ChannelsMerge();
        state->modal_open = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
        if (state->modal_open) state->interaction_rect = ImVec4(0, 0, display.x, display.y);
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(4);
    md::PopTheme();
}
} // namespace aimgui
