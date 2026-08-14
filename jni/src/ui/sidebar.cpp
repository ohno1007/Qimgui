#include "ui/ui_internal.h"
#include "ui/icons.h"
#include "core/haptics.h"

#include "imgui.h"

#include <cmath>
#include <cstdio>

namespace aimgui {

// The nav column. Its own body: the pane and everything on it carry the same
// lag, so the column can be thrown as far outward as inward.
void DrawSidebar(Page& current, bool* keep_running, UiState* state) {
    // Enough that the labels clear the lensed band on both edges. The slot eats
    // half its width out of the right one, which is what sets the number.
    constexpr float kInnerPadX     = 30.0f;
    // The pane's top edge is lensed too, so the first entry has to clear it.
    constexpr float kInnerPadY     = 30.0f;
    constexpr float kSelectableH   = 48.0f;
    // Measured from the capsule's own edge. SelectableTextAlign cannot express
    // this — it is a fraction of the *leftover* space, so the inset would move
    // with each label's width. The row lays itself out instead.
    constexpr float kRowPadX       = 24.0f;
    constexpr float kRowIconGap    = 16.0f;
    constexpr float kFooterH       = 168.0f;
    constexpr float kBottomMargin  = 34.0f;

    // The selected entry is drawn below as a capsule, so ImGui's own Header
    // fills stay out of it entirely — a slab of flat blue was the single most
    // out-of-place thing on the sheet.
    const ImVec4 sel_bg(0, 0, 0, 0);
    // This child's own fill is opaque, and it is pushed after the glass code
    // has cleared ChildBg — so it was painting the nav column solid black over
    // the pane behind it. The pane is the background whenever there is one.
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
                          state->screen_texture_id ? ImVec4(0, 0, 0, 0)
                                                   : ImVec4(0.07f, 0.08f, 0.10f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Header,        sel_bg);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, sel_bg);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  sel_bg);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,       ImVec2(kInnerPadX, kInnerPadY));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,         ImVec2(0, 8));
    // Unused now that the row draws its own content, but the Selectable still
    // reads it, so keep it neutral.
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,        ImVec2(0, 6));

    // Ride the column's lag, so the labels stay put on the pane while the whole
    // slab trails the window and springs back. Without this the pane would slide
    // out from under them and the outward throw would have to be capped short.
    const ImVec2 nav_origin = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(nav_origin.x + g_ui.glass_nav_offset.x,
                                     nav_origin.y + g_ui.glass_nav_offset.y));

    // The border is a rectangle around the child, which cuts across the pane
    // and re-draws the seam the parting just removed.
    ImGui::BeginChild("##sidebar", ImVec2(kSidebarW, 0),
                      (state->screen_texture_id ? 0 : ImGuiChildFlags_Borders) |
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);

    const ImU32 accent = ImGui::GetColorU32(ImVec4(0.30f, 0.62f, 1.0f, 1.0f));
    for (int i = 0; i < kPagesCount; ++i) {
        const PageItem& p = kPages[i];
        const bool selected = (current == p.id);
        // The Selectable is the hit area and nothing else — an empty visible
        // label, because the row places its own icon and text below.
        ImGui::PushID(i);
        if (ImGui::Selectable("##nav", selected, 0, ImVec2(0, kSelectableH))) {
            current = p.id;
        }
        ripple::TouchLastItem();

        // A capsule with the accent *in* it: a stripe alongside would collide
        // with the rounded end, and the colour reads better as the pill being
        // lit than as a marker stuck to its edge.
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

        // Icon and label on a fixed pixel grid, both centred on the row's
        // middle so a tall icon and a short label share one baseline.
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

    // On the same grid as the rows, so the rule and the backend name line up
    // with the icons above rather than starting at the column's edge.
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
    // Asked, not done: the window coming apart is not for a mistaken tap.
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
            // UVs normalise against the *snapshot texture*, which is
            // io.DisplaySize — display_w/h are physical pixels and would
            // mis-map most particles off-frame.
            const ImGuiIO& io2 = ImGui::GetIO();
            // The modal has to come apart too: from the next frame DrawUi
            // returns early and nothing draws it, so a region with no particles
            // is a region where the question simply stops existing.
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

    // Hand the cursor back where the content expects it. The lag is this
    // column's alone, so leaving it in the cursor would drag the content pane
    // along with it and there would be no relative motion at all.
    ImGui::SetCursorScreenPos(ImVec2(nav_origin.x + kSidebarW, nav_origin.y));

    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(4);
}

// Forward decl — body lives further down, but DrawContent invokes it.
void DrawResizeGrip(const UiState* state);
} // namespace aimgui
