// The shell: one body that morphs pill -> card -> window, and the glass panes
// it submits for the renderer to refract.
//
// Everything drawn inside it lives elsewhere (sidebar.cpp, content.cpp,
// dialog.cpp); this file owns where the body is, what shape it is, and how it
// comes apart into panes.

#include "ui/ui_internal.h"
#include "ui/icons.h"
#include "core/haptics.h"

#include "imgui.h"
#include "platform/ANativeWindowCreator.h"

#ifdef AIMGUI_LIVE2D
#include "live2d/live2d_view.h"
#endif

#include <cfloat>
#include <cmath>
#include <cstdio>

namespace aimgui {
namespace {

// The slot between the nav column and the content, the surface tension across
// it, and the strand that spans it.
//
// The merge radius is a material constant, never animated. A smooth union
// closes at the midline only once the radius passes twice the gap, so 30
// against an 18px slot leaves it genuinely open — proximity is what does the
// work, and a third small shape straddling the divide is what keeps the two
// connected, flared into a neck by the same smoothing.
constexpr float kGlassGap      = 18.0f;
constexpr float kGlassMerge    = 30.0f;
constexpr float kGlassStrandAt = 0.46f;   // down the column, 0..1
constexpr float kGlassStrandH  = 96.0f;
constexpr float kStrandGrip    = 26.0f;   // how far it reaches into each side
// A liquid bridge thins as the bodies part and lets go when tension cannot hold
// it. Where it starts to neck, and where it snaps.
constexpr float kBridgeHold = 20.0f;
constexpr float kBridgeSnap = 31.0f;   // max retreat opens the slot to 32

// The nav column is its own body: this is the fraction of each frame's motion
// it fails to keep up with, and UpdateSpring brings it home. Pane and labels
// move together, which is what lets the throw be as far outward as inward.
constexpr float kNavFollow = 0.85f;
constexpr float kNavLagMax = 34.0f;

} // namespace

void DrawUi(UiState* state, bool* keep_running) {
    ApplyStyleOnce();

    ImGuiIO& io = ImGui::GetIO();
    const float dt = io.DeltaTime > 0.0f ? io.DeltaTime : 1.0f / 60.0f;

    // Past the click frame the window stops rendering entirely and the
    // particles — which sample the frozen pre-click snapshot — replace it.
    if (state->exit_anim_active && !state->exit_anim_first_frame) {
        const float now     = (float)ImGui::GetTime();
        const float t01     = (now - state->exit_anim_start) / 1.35f;
        const float clamped = t01 < 0.0f ? 0.0f : (t01 > 1.0f ? 1.0f : t01);

        // This path returns before the pane submission below, so the count has
        // to be cleared here or the renderer keeps drawing the last frame's
        // glass where the window used to be.
        state->glass_count = 0;

        ripple::DrawAll();
        dissolve::Step(dt, clamped,
                      (ImTextureID)(uintptr_t)state->scene_snapshot_id);

        if (t01 >= 1.0f) {
            state->exit_anim_active = false;
            *keep_running = false;
        }
        return;
    }

    // With Live2D loaded the character is the collapsed visual: a draggable
    // ball. A small press-release is a tap, a larger move is a drag.
    bool l2d_active = false;
#ifdef AIMGUI_LIVE2D
    l2d_active = live2d::IsLoaded();
    if (l2d_active) {
        const float bdw = state->display_w > 0 ? (float)state->display_w : io.DisplaySize.x;
        const float bdh = state->display_h > 0 ? (float)state->display_h : io.DisplaySize.y;
        constexpr float kBallHalf = 100.0f;               // keep-on-screen margin
        if (state->ball_pos.x < 0.0f)                     // first-use placement
            state->ball_pos = ImVec2(bdw * 0.18f, bdh * 0.28f);
        auto clampf = [](float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); };
        state->ball_pos.x = clampf(state->ball_pos.x, kBallHalf, bdw - kBallHalf);
        state->ball_pos.y = clampf(state->ball_pos.y, kBallHalf, bdh - kBallHalf);

        static bool   s_dragging = false, s_moved = false;
        static ImVec2 s_press(0, 0), s_off(0, 0);
        if (state->collapsed) {
            if (ImGui::IsMouseClicked(0) && live2d::HitCollapsed(io.MousePos.x, io.MousePos.y)) {
                s_dragging = true; s_moved = false; s_press = io.MousePos;
                s_off = ImVec2(state->ball_pos.x - io.MousePos.x, state->ball_pos.y - io.MousePos.y);
            }
            if (s_dragging && ImGui::IsMouseDown(0)) {
                float mdx = io.MousePos.x - s_press.x, mdy = io.MousePos.y - s_press.y;
                if (mdx * mdx + mdy * mdy > 24.0f * 24.0f) s_moved = true;
                if (s_moved) state->ball_pos = ImVec2(io.MousePos.x + s_off.x,
                                                      io.MousePos.y + s_off.y);
            }
            if (s_dragging && ImGui::IsMouseReleased(0)) {
                s_dragging = false;
                if (!s_moved) {
                    // One step per tap: ball -> card -> window.
                    if (state->stage < UiState::StageWindow) ++state->stage;
                    live2d::Poke();
                }
            }
        } else {
            s_dragging = false;
        }
    }
#endif

    // First, so the NoMove flag below sees an up-to-date state->resizing.
    HandleResizeInput(state, io);

    if (!state->resizing) {
        UpdateSpring(&state->last_full_size.x, &state->resize_anim_vel.x,
                     state->resize_target_size.x, dt);
        UpdateSpring(&state->last_full_size.y, &state->resize_anim_vel.y,
                     state->resize_target_size.y, dt);
    } else {
        // Hold the live window at its pre-drag size; the preview frame reads
        // from resize_target_size.
        state->last_full_size = state->resize_drag_start_size;
    }

    state->collapsed = (state->stage == UiState::StageIsland);
    // One pulse per rest state actually changing, not per frame animating.
    if (state->stage != state->haptic_last_stage) {
        state->haptic_last_stage = state->stage;
        haptic::Step();
    }
    // The shell holds still while a modal is being re-issued: a modal can only
    // change which body it belongs to on the frame it has retracted to nothing,
    // and a shell moving across that frame is what would make it visible. So the
    // stage is taken but not acted on until the retract finishes.
    if (!dialog::StageMismatch(state->stage)) state->stage_shown = state->stage;
    const float target = (float)state->stage_shown * 0.5f;
    UpdateSpring(&state->expand, &state->expand_vel, target, dt);
    const float t = state->expand;
    const float lt = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);

    // The companion circle, and how far the lean carries the island. The dot's
    // gap is under half the merge radius, so at rest the two are joined by a
    // thread and it is the drag that breaks it. The lean is bounded so the
    // island cannot be tipped off screen.
    constexpr float kTiltRange = 70.0f;
    constexpr float kDotD      = 56.0f;
    constexpr float kDotGap    = 12.0f;
    constexpr float kDotMerge  = 30.0f;

    const float dw = state->display_w > 0 ? (float)state->display_w : io.DisplaySize.x;

    // A modal sharing this body is three of the four shapes one body gets, so
    // the shell has to come down to one: no parting, no dot. Ramped, and the
    // modal's own opening waits on it, so the island closes ranks first and the
    // question is drawn out of it after.
    {
        const float d = dt * 5.5f;
        state->modal_close += dialog::JoinedShell() ? d : -d;
        if (state->modal_close < 0.0f) state->modal_close = 0.0f;
        if (state->modal_close > 1.0f) state->modal_close = 1.0f;
    }
    const float mclose = state->modal_close;

    // What should sit centred on screen is the pair, so the island shifts left
    // by half the dot's reach for as long as the dot is there.
    const float dot_t = (lt < 0.22f ? 1.0f - lt / 0.22f : 0.0f) * (1.0f - mclose);
    const float pair_shift = (kDotD + kDotGap) * 0.5f * dot_t;

    // Sprung rather than tracked, so the island arrives with some weight. Faded
    // out well before the window opens, so an open window never wanders. Because
    // this moves win_pos, the velocity it produces feeds the same lag a drag
    // does and the dot swings behind the tilt for free.
    const float tilt_w = 1.0f - (lt < 0.55f ? lt / 0.55f : 1.0f);
    UpdateSpring(&state->island_tilt.x, &state->island_tilt_vel.x,
                 state->tilt_x * kTiltRange * tilt_w, dt);
    UpdateSpring(&state->island_tilt.y, &state->island_tilt_vel.y,
                 state->tilt_y * kTiltRange * tilt_w, dt);

    const ImVec2 island_base = l2d_active
        ? ImVec2(state->ball_pos.x - kIslandW * 0.5f,
                 state->ball_pos.y - kIslandH * 0.5f)
        : ImVec2(dw * 0.5f - kIslandW * 0.5f - pair_shift, kIslandTop);

    // Hold the lean on screen, dot included, and drop the spring's velocity at
    // the stop — otherwise it winds up against the edge and fires the island
    // across the display the moment the phone comes level.
    {
        const float dh_ = state->display_h > 0 ? (float)state->display_h : io.DisplaySize.y;
        const float margin = 12.0f;
        const float pair_w = kIslandW + (kDotGap + kDotD) * dot_t;
        auto hold = [](float* v, float* vel, float lo, float hi) {
            if (lo > hi) { *v = 0.0f; *vel = 0.0f; return; }
            if (*v < lo) { *v = lo; if (*vel < 0.0f) *vel = 0.0f; }
            if (*v > hi) { *v = hi; if (*vel > 0.0f) *vel = 0.0f; }
        };
        hold(&state->island_tilt.x, &state->island_tilt_vel.x,
             margin - island_base.x, dw - margin - pair_w - island_base.x);
        hold(&state->island_tilt.y, &state->island_tilt_vel.y,
             margin - island_base.y, dh_ - margin - kIslandH - island_base.y);
    }

    const ImVec2 island_pos(island_base.x + state->island_tilt.x,
                            island_base.y + state->island_tilt.y);
    const ImVec2 island_size(kIslandW, kIslandH);
    // Published for the modal, which is drawn out of this capsule. With Live2D
    // it is the ball's position, not the top centre.
    state->island_rect = ImVec4(island_pos.x, island_pos.y, kIslandW, kIslandH);

    // The card: enough to read at a glance, small enough that the island still
    // reads as having opened rather than the window as having arrived. Kept on
    // screen, so a ball dragged into a corner does not open half off the edge.
    const float dh = state->display_h > 0 ? (float)state->display_h : io.DisplaySize.y;
    const ImVec2 card_size(560.0f, 360.0f);
    ImVec2 card_pos(island_pos.x + kIslandW * 0.5f - card_size.x * 0.5f,
                    island_pos.y + kIslandH * 0.5f - card_size.y * 0.35f);
    if (card_pos.x < 16.0f) card_pos.x = 16.0f;
    if (card_pos.y < 16.0f) card_pos.y = 16.0f;
    if (card_pos.x + card_size.x > dw - 16.0f) card_pos.x = dw - 16.0f - card_size.x;
    if (card_pos.y + card_size.y > dh - 16.0f) card_pos.y = dh - 16.0f - card_size.y;

    // Where the shell will settle for the stage that has been *asked for*, so a
    // modal committing to a new attachment is already pressed by the destination
    // rather than by the shape the shell is passing through.
    state->shell_rest_rect =
        state->stage == UiState::StageIsland
            ? state->island_rect
            : (state->stage == UiState::StageCard
                   ? ImVec4(card_pos.x, card_pos.y, card_size.x, card_size.y)
                   : ImVec4(state->last_full_pos.x, state->last_full_pos.y,
                            state->last_full_size.x, state->last_full_size.y));

    auto lerp = [](ImVec2 a, ImVec2 b, float u) {
        return ImVec2(a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u);
    };
    // A rotation swaps the display's dimensions and can leave the window
    // entirely off-screen with no way to drag it back. Keep enough of it
    // reachable, and bring an oversized one in as well.
    if (state->display_w > 0 && state->display_h > 0) {
        const float keep = 120.0f;   // enough of the title bar to grab
        const float max_x = (float)state->display_w - keep;
        const float max_y = (float)state->display_h - keep;
        if (state->last_full_pos.x > max_x) state->last_full_pos.x = max_x;
        if (state->last_full_pos.y > max_y) state->last_full_pos.y = max_y;
        if (state->last_full_pos.x < 0.0f)  state->last_full_pos.x = 0.0f;
        if (state->last_full_pos.y < 0.0f)  state->last_full_pos.y = 0.0f;
        if (state->last_full_size.x > (float)state->display_w)
            state->last_full_size.x = (float)state->display_w;
        if (state->last_full_size.y > (float)state->display_h)
            state->last_full_size.y = (float)state->display_h;
    }

    // Two segments: island -> card over the first half of the spring, card ->
    // window over the second.
    ImVec2 win_pos, win_size;
    if (lt <= 0.5f) {
        const float k = lt * 2.0f;
        win_pos  = lerp(island_pos,  card_pos,  k);
        win_size = lerp(island_size, card_size, k);
    } else {
        const float k = (lt - 0.5f) * 2.0f;
        win_pos  = lerp(card_pos,  state->last_full_pos,  k);
        win_size = lerp(card_size, state->last_full_size, k);
    }

    // Squash and stretch straight off the spring's velocity: it stretches along
    // the direction of growth, pinches across it, and vanishes the moment the
    // spring settles. Overshoot alone reads as a bounce; the deformation is what
    // makes it read as soft.
    {
        float j = state->expand_vel * 0.055f;
        if (j >  0.16f) j =  0.16f;
        if (j < -0.16f) j = -0.16f;
        const ImVec2 c(win_pos.x + win_size.x * 0.5f, win_pos.y + win_size.y * 0.5f);
        win_size.x *= (1.0f + j * 0.55f);
        win_size.y *= (1.0f - j * 0.85f);
        win_pos.x = c.x - win_size.x * 0.5f;
        win_pos.y = c.y - win_size.y * 0.5f;
    }

    // The shell as it finally stands, squash included. Published here rather
    // than in the pane block below, which is skipped when the mirror is off —
    // the modal still has to know where the shell is.
    state->shell_rect = ImVec4(win_pos.x, win_pos.y, win_size.x, win_size.y);

    // Clear of the card's rest: the spring overshoots past 0.5 on its way there,
    // and a threshold any closer would flash the title bar during the bounce.
    const bool show_chrome    = (lt > 0.75f);
    const bool overriding_pos = (lt < 0.999f);

    if (overriding_pos) {
        ImGui::SetNextWindowPos (win_pos);
        ImGui::SetNextWindowSize(win_size);
    } else {
        // A content drag moves the window, so on those frames this position is
        // authoritative; otherwise ImGui owns it and this is initial placement.
        ImGui::SetNextWindowPos(state->last_full_pos,
                                state->content_moving ? ImGuiCond_Always
                                                      : ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(state->last_full_size, ImGuiCond_Always);
        ImGui::SetNextWindowSizeConstraints(ImVec2(700, 560), ImVec2(FLT_MAX, FLT_MAX));
    }

    // The pane is the background now, so ImGui contributes no fill at all —
    // not on the window, not on the title bar, not on the children. Every fill
    // it paints is a window-shaped rectangle, which would bridge the slot
    // between parted panes with a flat wash. The body they used to provide is
    // the shader's own, on the real silhouette, which glass_clarity accounts for.
    int pushed_glass_text = 0;
    if (state->screen_texture_id) {
        const ImVec4 kNone(0, 0, 0, 0);
        ImGui::PushStyleColor(ImGuiCol_WindowBg,         kNone);
        ImGui::PushStyleColor(ImGuiCol_TitleBg,          kNone);
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive,    kNone);
        ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, kNone);
        ImGui::PushStyleColor(ImGuiCol_ChildBg,          kNone);
        pushed_glass_text = 5;
    }
    const float rounding = (kIslandH * 0.5f) * (1.0f - lt) + 12.0f * lt;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, rounding);

    // ── Panes ────────────────────────────────────────────────────────────
    // One sheet for the whole window. The title bar and sidebar had panes of
    // their own once, but every pane carries its own rim, caustic and specular,
    // so their borders ran through the middle of the sheet and read as seams.
    // They are regions of one sheet, differentiated by density instead.
    //
    // Nothing is submitted once the window is coming apart: the panes are drawn
    // into the scene image the particles sample, so the refraction is already
    // baked into every particle.
    state->glass_count = 0;
    if (state->screen_texture_id && !state->exit_anim_active) {
        GlassRect base{};
        base.rounding = rounding;
        base.alpha    = 1.0f;
        base.tintA    = state->glass_clarity;
        base.lightX   = state->glass_light_x;
        base.lightY   = state->glass_light_y;
        // A 60px rim on a 56px pill reaches in from both sides and meets in the
        // middle, so scale the lensing with the shorter side.
        const float minSide = win_size.x < win_size.y ? win_size.x : win_size.y;
        if (minSide < 200.0f) {
            base.edgeWidth = minSide * 0.30f;
            base.blur      = 5.0f;
        }

        // The nav column trails the window and springs back: each frame's motion
        // is subtracted from where it has got to, then the spring pulls it home,
        // under-damped so it comes back through and briefly presses into its
        // neighbour. Nothing here touches the merge — it is all distance, and the
        // merge threshold turns that into contact.
        {
            const float dt = ImGui::GetIO().DeltaTime;
            if (state->glass_pos_valid) {
                state->glass_nav_lag.x -= (win_pos.x - state->glass_prev_pos.x) * kNavFollow;
                state->glass_nav_lag.y -= (win_pos.y - state->glass_prev_pos.y) * kNavFollow;
            }
            state->glass_prev_pos  = win_pos;
            state->glass_pos_valid = true;
            UpdateSpring(&state->glass_nav_lag.x, &state->glass_nav_lag_vel.x, 0.0f, dt);
            UpdateSpring(&state->glass_nav_lag.y, &state->glass_nav_lag_vel.y, 0.0f, dt);
            // Bounded, and the velocity dropped at the stop, or the spring winds
            // up against the cap and fires the column across the slot on release.
            auto hold = [](float* v, float* vel, float lim) {
                if (*v < -lim) { *v = -lim; if (*vel < 0.0f) *vel = 0.0f; }
                if (*v >  lim) { *v =  lim; if (*vel > 0.0f) *vel = 0.0f; }
            };
            hold(&state->glass_nav_lag.x, &state->glass_nav_lag_vel.x, kNavLagMax);
            hold(&state->glass_nav_lag.y, &state->glass_nav_lag_vel.y, kNavLagMax);
        }

        // Below the full-window stage there is nothing to part, and mclose
        // closes the slot again for a modal. Driving the width to zero rather
        // than switching the parting off is what makes that handover invisible:
        // at zero the four shapes tile the window exactly.
        const float split = (lt > 0.70f ? (lt - 0.70f) / 0.30f : 0.0f)
                          * (1.0f - mclose);
        GlassRect a = base, b = base, strand = base, title = base;
        bool parted = false;
        if (split > 0.01f) {
            // The title bar belongs to the content, not the column: it is its
            // own full-width shape butted against the content's top edge so the
            // two are one body, and the column is what comes away. A full-height
            // column instead put the window's title on the menu.
            const ImGuiStyle& stl = ImGui::GetStyle();
            const float divide  = win_pos.x + stl.WindowPadding.x + kSidebarW;
            const float title_h = ImGui::GetFontSize() + stl.FramePadding.y * 2.0f;
            const float gap     = kGlassGap * split;
            const float win_r   = win_pos.x + win_size.x;
            const float win_b   = win_pos.y + win_size.y;
            // Faded in with the stage and published for DrawSidebar, so pane and
            // widgets move by exactly one value.
            const ImVec2 lag(state->glass_nav_lag.x * split, state->glass_nav_lag.y * split);
            state->glass_nav_offset = lag;

            // Enough overlap that the title and content are unambiguously one
            // body. Kept small: the column's slot is measured from this edge, so
            // every pixel of overlap is a pixel the slot loses.
            constexpr float kTitleOverlap = 4.0f;
            title.x = win_pos.x;
            title.y = win_pos.y;
            title.w = win_size.x;
            title.h = title_h + kTitleOverlap;

            b.x = divide + gap * 0.5f;
            b.y = win_pos.y + title_h;
            b.w = win_r - b.x;
            b.h = win_b - b.y;

            // The whole column carries the lag at a fixed size — a slab that
            // trails, not a shape that stretches. DrawSidebar moves its widgets
            // by the same offset, so nothing it holds can come off it.
            const float nav_y0 = win_pos.y + title_h + kTitleOverlap + gap;
            a.x = win_pos.x + lag.x;
            a.y = nav_y0 + lag.y;
            a.w = (divide - gap * 0.5f) - win_pos.x;
            a.h = win_b - nav_y0;

            // What is left of the join once the bodies are too far apart for the
            // merge alone. The slot's width is a real distance, so where it necks
            // and where it snaps are too.
            const float nav_r = a.x + a.w;
            const float slot  = b.x - nav_r;
            const float u     = (slot - kBridgeHold) / (kBridgeSnap - kBridgeHold);
            const float uc    = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
            const float neck  = 1.0f - uc * uc * (3.0f - 2.0f * uc);
            // Letting go and finding its way back are the two moments the eye can
            // miss, so both get a nudge. Hysteresis, or it chatters while the
            // spring settles on the threshold.
            const bool joined = state->strand_joined ? neck > 0.06f : neck > 0.35f;
            if (joined != state->strand_joined) {
                state->strand_joined = joined;
                haptic::Snap();
            }
            strand.w = slot + kStrandGrip * neck;
            strand.h = kGlassStrandH * neck;
            strand.x = nav_r - kStrandGrip * neck * 0.5f;
            strand.y = a.y + (win_b - a.y) * kGlassStrandAt - strand.h * 0.5f;

            a.merge = b.merge = strand.merge = title.merge = kGlassMerge;
            parted = a.w > 2.0f && b.w > 2.0f && a.h > 2.0f;
        }
        if (parted) {
            state->glass_rects[state->glass_count++] = title;
            state->glass_rects[state->glass_count++] = b;
            state->glass_rects[state->glass_count++] = a;
            state->glass_rects[state->glass_count++] = strand;
        } else {
            state->glass_nav_offset = ImVec2(0, 0);
            GlassRect r = base;
            r.x = win_pos.x; r.y = win_pos.y; r.w = win_size.x; r.h = win_size.y;
            if (dot_t > 0.02f) {
                // Its own body, lagging the way the column does, so the thread
                // stretches when the island is thrown one way and the two run
                // together when it is thrown the other.
                GlassRect dot = base;
                dot.w = dot.h = kDotD * dot_t;
                dot.x = win_pos.x + win_size.x + kDotGap
                        + state->glass_nav_lag.x * dot_t;
                dot.y = win_pos.y + win_size.y * 0.5f - dot.h * 0.5f
                        + state->glass_nav_lag.y * dot_t;
                r.merge = dot.merge = kDotMerge;
                state->dot_center = ImVec2(dot.x + dot.w * 0.5f, dot.y + dot.h * 0.5f);
                state->dot_radius = dot.w * 0.5f;
                state->glass_rects[state->glass_count++] = r;
                state->glass_rects[state->glass_count++] = dot;
            } else {
                state->dot_radius = 0.0f;
                state->glass_rects[state->glass_count++] = r;
            }
        }
    }

    // A group takes its material from its first surviving pane, and which pane
    // that is depends on the branch above — so this is applied by index rather
    // than inside one of them. It is also where the merge radius comes from: a
    // shell on its own has nothing to merge with and carries none.
    if (dialog::JoinedShell()) {
        for (int i = 0; i < state->glass_count; ++i) {
            GlassRect& gr = state->glass_rects[i];
            if (gr.group != 0 || gr.w < 2.0f || gr.h < 2.0f || gr.alpha <= 0.001f)
                continue;
            dialog::BlendMaterial(&gr);
            break;
        }
    }

    // ── Window ───────────────────────────────────────────────────────────
    // With the character as the collapsed visual, fade ImGui's own chrome in as
    // the window expands so nothing boxes the tiny character in. The border goes
    // whenever the glass is up: it is a rectangle around the window, so once the
    // panes part it would run straight across the slot.
    const bool l2d_hidden_chrome = l2d_active && lt < 0.999f;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,
                        (l2d_hidden_chrome || state->screen_texture_id) ? 0.0f : 1.0f);
    if (l2d_hidden_chrome) ImGui::SetNextWindowBgAlpha(lt);

    // DrawResizeGrip owns resizing, so ImGui's own handle is suppressed. NoMove
    // during a grip drag stops ImGui reading the same touch as a window drag.
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoResize;
    if (!show_chrome || state->resizing) {
        flags |= ImGuiWindowFlags_NoMove;
    }
    if (!show_chrome) {
        flags |= ImGuiWindowFlags_NoTitleBar;
    }

    char title[64];
    std::snprintf(title, sizeof(title), "AImGui  v%s###aimgui_main", ImGui::GetVersion());

    if (ImGui::Begin(title, show_chrome ? keep_running : nullptr, flags)) {
        if (lt >= 0.999f && state->stage == UiState::StageWindow) {
            state->last_full_pos  = ImGui::GetWindowPos();
            state->last_full_size = ImGui::GetWindowSize();
        }

        // The pill's content, when there is no character to be it instead.
        const float island_alpha = 1.0f - (lt < 0.30f ? lt / 0.30f : 1.0f);
        if (!l2d_active && island_alpha > 0.01f) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, island_alpha);
            DrawIslandContent(state);
            DrawDotContent(state, island_alpha);
            ImGui::PopStyleVar();

            if (!show_chrome && ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) {
                if (state->stage < UiState::StageWindow) ++state->stage;
            }
        }

        // The card fills the gap the fades above and below leave: the island's
        // content is gone by 0.30 and the window's does not arrive until 0.70,
        // so without this the middle rest is an empty sheet of glass.
        const float card_alpha = (lt <= 0.30f || lt >= 0.72f) ? 0.0f
                               : (lt < 0.46f ? (lt - 0.30f) / 0.16f
                                             : (lt > 0.62f ? (0.72f - lt) / 0.10f : 1.0f));
        if (card_alpha > 0.01f) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, card_alpha);
            DrawCardContent(state);
            ImGui::PopStyleVar();

            // Tap opens the next rest; a flick upwards puts it away, up being
            // the direction the card came from. Same 26px slop as the content
            // gesture, or the card swallows taps that wandered.
            static bool   s_press   = false;
            static ImVec2 s_from    = ImVec2(0, 0);
            static bool   s_handled = false;
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) {
                s_press = true; s_handled = false; s_from = io.MousePos;
            }
            if (s_press && io.MouseDown[0] && !s_handled) {
                if (io.MousePos.y - s_from.y < -46.0f) {
                    state->stage = UiState::StageIsland;
                    s_handled = true;
                }
            }
            if (s_press && !io.MouseDown[0]) {
                const float dy = io.MousePos.y - s_from.y;
                const float dx = io.MousePos.x - s_from.x;
                if (!s_handled && std::fabs(dy) < 26.0f && std::fabs(dx) < 26.0f &&
                    state->stage < UiState::StageWindow) {
                    ++state->stage;
                }
                s_press = false;
            }
        }

        const float full_alpha = lt > 0.70f ? (lt - 0.70f) / 0.30f : 0.0f;
        if (full_alpha > 0.01f) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, full_alpha);

            Page page = (Page)state->nav_page;
            // No SameLine: DrawSidebar hands the cursor back itself. SameLine
            // would recompute it from the child's advance, which carries the
            // column's lag and would drag the content along with it.
            DrawSidebar(page, keep_running, state);
            state->nav_page = (int)page;
            DrawContent(state, page);

            ImGui::PopStyleVar();
        }
    }
    ImGui::End();

    ImGui::PopStyleVar(2);   // WindowRounding + WindowBorderSize
    if (pushed_glass_text) ImGui::PopStyleColor(pushed_glass_text);

    ripple::DrawAll();

    // Last, so its panes land on top of the window's.
    dialog::Draw(state);

    // On the click frame the particles advance while the real UI is still under
    // them, so the surface itself is what appears to come apart. After this
    // frame the early-return path above takes over.
    if (state->exit_anim_active && state->exit_anim_first_frame) {
        const float now     = (float)ImGui::GetTime();
        const float t01     = (now - state->exit_anim_start) / 1.35f;
        const float clamped = t01 < 0.0f ? 0.0f : (t01 > 1.0f ? 1.0f : t01);
        dissolve::Step(dt, clamped,
                      (ImTextureID)(uintptr_t)state->scene_snapshot_id);
        state->exit_anim_first_frame = false;
    }
}

} // namespace aimgui
