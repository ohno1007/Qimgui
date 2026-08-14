#include "ui/ui_internal.h"
#include "core/haptics.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace aimgui {

// ─── Content gestures ────────────────────────────────────────────────────
// A drag in the content means either "scroll the page" or "move the window",
// decided from the first few pixels of travel:
//
//   mostly vertical, and the page has somewhere to scroll  -> scroll
//   anything else                                          -> move the window
//
// So a short page drags the window from anywhere in it, a long page scrolls,
// and a sideways drag moves the window either way. Decided once and held until
// release — a gesture that changes meaning halfway through feels broken.
void ContentGesture(const char* id, UiState* state) {
    enum class Mode { Undecided, Scroll, Move, Widget };

    // Throw speed over a short window of recent motion, not a running average.
    // An average includes the frames just before the finger lifts, and those
    // lie: a finger that pauses before letting go reads as no throw at all, and
    // one that drifts back a pixel flips the sign.
    constexpr int   kVelSamples = 8;
    constexpr float kVelWindow  = 0.09f;   // seconds of history that count
    struct Drag {
        bool   active   = false;
        Mode   mode     = Mode::Undecided;
        ImVec2 start    = ImVec2(0, 0);
        float  velocity = 0.0f;   // scroll momentum, px/s
        float  dy[kVelSamples] = {};
        float  dt[kVelSamples] = {};
        int    head  = 0;
        int    count = 0;
    };
    // Keyed by id: the sidebar and the content pane both scroll and must not
    // share momentum or a decision.
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
            // A press that lands on a widget belongs to that widget for the
            // whole gesture; taking it back partway would need ClearActiveID,
            // which is not in the vendored public headers, and stealing a
            // slider's drag halfway would be worse than not scrolling.
            d->mode   = ImGui::IsAnyItemActive() ? Mode::Widget : Mode::Undecided;
        }

        float dx = io.MousePos.x - d->start.x;
        float dy = io.MousePos.y - d->start.y;

        // A touch point that moves further than this between two frames is not
        // a finger travelling — at 120 Hz nothing human covers it in 8ms. It is
        // the press and the position arriving on different frames, which leaves
        // `start` sitting wherever the pointer happened to be last. Re-seed
        // from the real position instead of reading the jump as a throw, which
        // is how a tap sometimes came out as a scroll.
        constexpr float kTeleport = 140.0f;
        if (d->mode == Mode::Undecided &&
            (std::fabs(io.MouseDelta.x) > kTeleport || std::fabs(io.MouseDelta.y) > kTeleport)) {
            d->start = io.MousePos;
            dx = dy = 0.0f;
        }

        // Android's own touch slop is 8dp, which on this panel is nearer thirty
        // pixels than six. Six is under a tenth of a millimetre: no finger
        // presses that precisely, so a tap that drifted while landing was being
        // read as a drag and the button under it never got its release.
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
            // On the frame of release, work the throw out from the window of
            // recent samples: total distance over total time. Frames where the
            // finger had already stopped contribute their duration but no
            // distance, so a pause before letting go damps the throw towards
            // zero on its own instead of needing a rule.
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
            // Then glide on, shedding speed exponentially, coming to rest in
            // about a second so it reads as friction rather than the list being
            // yanked away.
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

// Resize input is detected BEFORE Begin so that the ImGuiWindowFlags_NoMove
// can be applied this very frame to stop ImGui from also interpreting the
// touch as a window-drag-start. Without this, a press on the grip would
// kick off both the grip drag (our code) and the main window's title-bar
// move (ImGui's built-in), and the window would slide around under the
// finger as the size grew.
//
// Hit-test is done with raw math (not ImGui::IsMouseHoveringRect) — that
// helper defaults to clipping against the *current window*'s ClipRect, and
// we're called outside any Begin/End so its clip rect is empty / wrong,
// which silently produces "no hit" forever.
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

// Visual-only: pips at the corner + preview frame while resizing. Input
// is handled by HandleResizeInput at the top of DrawUi.
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
} // namespace aimgui
