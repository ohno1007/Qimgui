#include "ui/ui_internal.h"
#include "core/haptics.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace aimgui {

void ContentGesture(const char* id, UiState* state) {
    enum class Mode { Undecided, Scroll, Move, Widget };

    constexpr int   kVelSamples = 8;
    constexpr float kVelWindow  = 0.09f;
    struct Drag {
        bool   active   = false;
        Mode   mode     = Mode::Undecided;
        ImVec2 start    = ImVec2(0, 0);
        float  velocity = 0.0f;
        float  dy[kVelSamples] = {};
        float  dt[kVelSamples] = {};
        int    head  = 0;
        int    count = 0;
    };

    static std::vector<std::pair<const char*, Drag>> states;
    Drag* d = nullptr;
    for (auto& e : states) if (e.first == id) { d = &e.second; break; }
    if (!d) { states.push_back({id, Drag{}}); d = &states.back().second; }

    ImGuiIO&    io = ImGui::GetIO();
    const float dt = io.DeltaTime > 0.0f ? io.DeltaTime : 1.0f / 60.0f;
    const bool  hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows |
                                                 ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    if (io.MouseDown[0] && (d->active || hovered)) {
        if (!d->active) {
            d->active = true;
            d->start  = io.MousePos;
            d->count  = 0;
            d->head   = 0;
            d->velocity = 0.0f;

            d->mode   = ImGui::IsAnyItemActive() ? Mode::Widget : Mode::Undecided;
        }

        float dx = io.MousePos.x - d->start.x;
        float dy = io.MousePos.y - d->start.y;

        constexpr float kTeleport = 140.0f;
        if (d->mode == Mode::Undecided &&
            (std::fabs(io.MouseDelta.x) > kTeleport || std::fabs(io.MouseDelta.y) > kTeleport)) {
            d->start = io.MousePos;
            dx = dy = 0.0f;
        }

        constexpr float kSlop = 26.0f;
        if (d->mode == Mode::Undecided && (std::fabs(dx) > kSlop || std::fabs(dy) > kSlop)) {
            const bool vertical  = std::fabs(dy) > std::fabs(dx);
            const bool canScroll = ImGui::GetScrollMaxY() > 1.0f;
            d->mode = (vertical && canScroll) ? Mode::Scroll : Mode::Move;
        }

        if (d->mode == Mode::Scroll) {
            ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
            d->dy[d->head] = -io.MouseDelta.y;
            d->dt[d->head] = dt;
            d->head = (d->head + 1) % kVelSamples;
            if (d->count < kVelSamples) ++d->count;
        } else if (d->mode == Mode::Move) {
            state->last_full_pos.x += io.MouseDelta.x;
            state->last_full_pos.y += io.MouseDelta.y;
            g_ui.content_moving = true;
        }
    } else {
        g_ui.content_moving = false;
        if (d->active && d->mode == Mode::Scroll) {

            if (d->count > 0) {
                float sum_dy = 0.0f, sum_dt = 0.0f;
                for (int i = 0; i < d->count && sum_dt < kVelWindow; ++i) {
                    const int k = (d->head - 1 - i + kVelSamples * 2) % kVelSamples;
                    sum_dy += d->dy[k];
                    sum_dt += d->dt[k];
                }
                d->velocity = sum_dt > 1e-4f ? sum_dy / sum_dt : 0.0f;
                d->count = 0;
            }

            d->velocity *= std::exp(-4.5f * dt);
            if (std::fabs(d->velocity) > 8.0f) {
                ImGui::SetScrollY(ImGui::GetScrollY() + d->velocity * dt);
            } else {
                d->velocity = 0.0f;
                d->active   = false;
                d->mode     = Mode::Undecided;
            }
        } else {
            d->active   = false;
            d->mode     = Mode::Undecided;
            d->velocity = 0.0f;
        }
    }
}

ImVec2 GripMin(const UiState* state) {
    constexpr float kGrip = 40.0f;
    return ImVec2(state->last_full_pos.x + state->last_full_size.x - kGrip,
                  state->last_full_pos.y + state->last_full_size.y - kGrip);
}
ImVec2 GripMax(const UiState* state) {
    return ImVec2(state->last_full_pos.x + state->last_full_size.x,
                  state->last_full_pos.y + state->last_full_size.y);
}

void HandleResizeInput(UiState* state, const ImGuiIO& io) {
    const ImVec2 grip_min = GripMin(state);
    const ImVec2 grip_max = GripMax(state);

    const bool inside = io.MousePos.x >= grip_min.x && io.MousePos.x < grip_max.x &&
                        io.MousePos.y >= grip_min.y && io.MousePos.y < grip_max.y;

    if (io.MouseClicked[0] && !g_ui.resizing && inside) {
        g_ui.resizing                = true;
        g_ui.resize_drag_start_mouse = io.MousePos;
        g_ui.resize_drag_start_size  = state->last_full_size;
        g_ui.resize_target_size      = state->last_full_size;
    }
    if (g_ui.resizing && io.MouseDown[0]) {
        const ImVec2 d(io.MousePos.x - g_ui.resize_drag_start_mouse.x,
                       io.MousePos.y - g_ui.resize_drag_start_mouse.y);
        g_ui.resize_target_size = ImVec2(
            std::max(700.0f, g_ui.resize_drag_start_size.x + d.x),
            std::max(560.0f, g_ui.resize_drag_start_size.y + d.y));
    }
    if (g_ui.resizing && !io.MouseDown[0]) {
        g_ui.resizing        = false;
        g_ui.resize_anim_vel = ImVec2(0, 0);
    }
}

void DrawResizeGrip(const UiState* state) {
    const ImVec2 grip_min = GripMin(state);
    const ImVec2 grip_max = GripMax(state);

    const bool inside = ImGui::IsMouseHoveringRect(grip_min, grip_max);
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const ImU32 col = ImGui::GetColorU32(
        g_ui.resizing ? ImGuiCol_ResizeGripActive
                        : (inside ? ImGuiCol_ResizeGripHovered : ImGuiCol_ResizeGrip));

    for (int i = 0; i < 3; ++i) {
        const float o = 8.0f + i * 7.0f;
        fg->AddLine(ImVec2(grip_max.x - o, grip_max.y - 5),
                    ImVec2(grip_max.x - 5, grip_max.y - o),
                    col, 3.0f);
    }

    if (g_ui.resizing) {
        const ImVec2 a = state->last_full_pos;
        const ImVec2 b(a.x + g_ui.resize_target_size.x,
                       a.y + g_ui.resize_target_size.y);
        fg->AddRect(a, b,
                    ImGui::GetColorU32(ImVec4(0.30f, 0.62f, 1.0f, 0.95f)),
                    12.0f, 5.0f, 0);
    }
}
}
