#include "ui/ui_internal.h"
#include "ui/icons.h"
#include "core/haptics.h"

#include "imgui.h"

#include <cmath>
#include <cstdio>

namespace aimgui {

void DrawSidebar(Page& current, bool* keep_running, UiState* state) {

    constexpr float kInnerPadX     = 30.0f;

    constexpr float kInnerPadY     = 30.0f;
    constexpr float kSelectableH   = 48.0f;

    constexpr float kRowPadX       = 24.0f;
    constexpr float kRowIconGap    = 16.0f;
    constexpr float kFooterH       = 168.0f;
    constexpr float kBottomMargin  = 34.0f;

    const ImVec4 sel_bg(0, 0, 0, 0);

    ImGui::PushStyleColor(ImGuiCol_ChildBg,
                          state->screen_texture_id ? ImVec4(0, 0, 0, 0)
                                                   : ImVec4(0.07f, 0.08f, 0.10f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Header,        sel_bg);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, sel_bg);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  sel_bg);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,       ImVec2(kInnerPadX, kInnerPadY));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,         ImVec2(0, 8));

    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,        ImVec2(0, 6));

    const ImVec2 nav_origin = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(nav_origin.x + g_ui.glass_nav_offset.x,
                                     nav_origin.y + g_ui.glass_nav_offset.y));

    ImGui::BeginChild("##sidebar", ImVec2(kSidebarW, 0),
                      (state->screen_texture_id ? 0 : ImGuiChildFlags_Borders) |
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);

    const ImU32 accent = ImGui::GetColorU32(ImVec4(0.30f, 0.62f, 1.0f, 1.0f));
    for (int i = 0; i < kPagesCount; ++i) {
        const PageItem& p = kPages[i];
        const bool selected = (current == p.id);

        ImGui::PushID(i);
        if (ImGui::Selectable("##nav", selected, 0, ImVec2(0, kSelectableH))) {
            current = p.id;
        }
        ripple::TouchLastItem();

        const ImVec2 a = ImGui::GetItemRectMin();
        const ImVec2 b = ImGui::GetItemRectMax();
        const bool hovered = ImGui::IsItemHovered();
        if (selected) {
            ImGui::GetWindowDrawList()->AddRectFilled(
                a, b, (accent & ~IM_COL32_A_MASK) | (46u << IM_COL32_A_SHIFT),
                (b.y - a.y) * 0.5f);
            chrome::Rect(a, b, -1.0f, false, true);
        } else if (hovered) {
            chrome::Rect(a, b, -1.0f, true, false);
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float  cy  = (a.y + b.y) * 0.5f;
        const ImU32  col = ImGui::GetColorU32(
            ImVec4(1, 1, 1, selected ? 1.0f : (hovered ? 0.92f : 0.78f)));
        float x = a.x + kRowPadX;
        if (p.icon && *p.icon) {
            const ImVec2 is = ImGui::CalcTextSize(p.icon);
            dl->AddText(ImVec2(x, cy - is.y * 0.5f), col, p.icon);
            x += is.x + kRowIconGap;
        }
        const ImVec2 ls = ImGui::CalcTextSize(p.label);
        dl->AddText(ImVec2(x, cy - ls.y * 0.5f), col, p.label);
        ImGui::PopID();
    }

    float remaining = ImGui::GetWindowHeight() - ImGui::GetCursorPosY() - kFooterH - kBottomMargin;
    if (remaining > 0) ImGui::Dummy(ImVec2(0, remaining));

    ImGui::Indent(kRowPadX);
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 10));
    ImGui::TextDisabled("%s", state->renderer_name ? state->renderer_name : "?");
    ImGui::Unindent(kRowPadX);
    ImGui::Dummy(ImVec2(0, 14));

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 14));
    const bool exit_pressed = ImGui::Button(ICON_FA_POWER u8"  退出", ImVec2(-1, 0));
    chrome::LastItem();

    if (exit_pressed && !state->exit_anim_active) {
        dialog::Open(dialog::KindConfirm, u8"退出 AImGui",
                     u8"窗口会碎成粒子飘散，设置会先保存。");
        g_ui.pending_exit = true;
    }
    if (g_ui.pending_exit) {
        const dialog::Result r = dialog::Take();
        if (r == dialog::ResultCancel) g_ui.pending_exit = false;
        if (r == dialog::ResultOk) {
            g_ui.pending_exit = false;

            const ImGuiIO& io2 = ImGui::GetIO();

            ImVec2 dp = state->last_full_pos;
            ImVec2 ds = state->last_full_size;
            const ImVec4& m = g_ui.modal_rect;
            if (m.z > 2.0f && m.w > 2.0f) {
                const float x1 = (dp.x + ds.x > m.x + m.z) ? dp.x + ds.x : m.x + m.z;
                const float y1 = (dp.y + ds.y > m.y + m.w) ? dp.y + ds.y : m.y + m.w;
                if (m.x < dp.x) dp.x = m.x;
                if (m.y < dp.y) dp.y = m.y;
                ds = ImVec2(x1 - dp.x, y1 - dp.y);
            }
            dissolve::Begin(dp, ds, io2.DisplaySize.x, io2.DisplaySize.y);
            haptic::Heavy();
            state->exit_anim_active      = true;
            g_ui.exit_anim_first_frame = true;
            g_ui.exit_anim_start       = (float)ImGui::GetTime();
        }
    }
    ripple::TouchLastItem();
    ImGui::PopStyleVar();

    ImGui::Dummy(ImVec2(0, kBottomMargin));

    ImGui::EndChild();

    ImGui::SetCursorScreenPos(ImVec2(nav_origin.x + kSidebarW, nav_origin.y));

    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(4);
}

void DrawResizeGrip(const UiState* state);
}
