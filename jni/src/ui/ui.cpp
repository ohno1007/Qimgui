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

Shell g_ui;

namespace {

constexpr float kGlassGap      = 18.0f;
constexpr float kGlassMerge    = 30.0f;
constexpr float kGlassStrandAt = 0.46f;
constexpr float kGlassStrandH  = 96.0f;
constexpr float kStrandGrip    = 26.0f;

constexpr float kBridgeHold = 20.0f;
constexpr float kBridgeSnap = 31.0f;

constexpr float kNavFollow = 0.85f;
constexpr float kNavLagMax = 34.0f;

}

void DrawUi(UiState* state, bool* keep_running) {
    ApplyStyleOnce();
    state->widget_count = 0;
    chrome::BeginFrame(state->widget_glass && state->widget_glass_ok &&
                       state->screen_texture_id != 0 && !state->exit_anim_active);

    ImGuiIO& io = ImGui::GetIO();
    const float dt = io.DeltaTime > 0.0f ? io.DeltaTime : 1.0f / 60.0f;

    if (state->exit_anim_active && !g_ui.exit_anim_first_frame) {
        const float now     = (float)ImGui::GetTime();
        const float t01     = (now - g_ui.exit_anim_start) / 1.35f;
        const float clamped = t01 < 0.0f ? 0.0f : (t01 > 1.0f ? 1.0f : t01);

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

    bool l2d_active = false;
#ifdef AIMGUI_LIVE2D
    l2d_active = live2d::IsLoaded();
    if (l2d_active) {
        const float bdw = state->display_w > 0 ? (float)state->display_w : io.DisplaySize.x;
        const float bdh = state->display_h > 0 ? (float)state->display_h : io.DisplaySize.y;
        constexpr float kBallHalf = 100.0f;
        if (state->ball_pos.x < 0.0f)
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

                    if (state->stage < UiState::StageWindow) ++state->stage;
                    live2d::Poke();
                }
            }
        } else {
            s_dragging = false;
        }
    }
#endif

    if (!g_ui.resize_valid) {
        g_ui.resize_target_size = state->last_full_size;
        g_ui.resize_valid = true;
    }

    HandleResizeInput(state, io);

    if (!g_ui.resizing) {
        UpdateSpring(&state->last_full_size.x, &g_ui.resize_anim_vel.x,
                     g_ui.resize_target_size.x, dt);
        UpdateSpring(&state->last_full_size.y, &g_ui.resize_anim_vel.y,
                     g_ui.resize_target_size.y, dt);
    } else {

        state->last_full_size = g_ui.resize_drag_start_size;
    }

    state->collapsed = (state->stage == UiState::StageIsland);

    if (state->stage != g_ui.haptic_last_stage) {
        g_ui.haptic_last_stage = state->stage;
        haptic::Step();
    }

    if (!dialog::StageMismatch(state->stage)) g_ui.stage_shown = state->stage;
    const float target = (float)g_ui.stage_shown * 0.5f;
    UpdateSpring(&state->expand, &g_ui.expand_vel, target, dt);
    const float t = state->expand;
    const float lt = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);

    constexpr float kTiltRange = 70.0f;
    constexpr float kDotD      = 56.0f;
    constexpr float kDotGap    = 12.0f;
    constexpr float kDotMerge  = 30.0f;

    const float dw = state->display_w > 0 ? (float)state->display_w : io.DisplaySize.x;

    {
        const float d = dt * 5.5f;
        g_ui.modal_close += dialog::JoinedShell() ? d : -d;
        if (g_ui.modal_close < 0.0f) g_ui.modal_close = 0.0f;
        if (g_ui.modal_close > 1.0f) g_ui.modal_close = 1.0f;
    }
    const float mclose = g_ui.modal_close;

    const float dot_t = (lt < 0.22f ? 1.0f - lt / 0.22f : 0.0f) * (1.0f - mclose);
    const float pair_shift = (kDotD + kDotGap) * 0.5f * dot_t;

    const float tilt_w = 1.0f - (lt < 0.55f ? lt / 0.55f : 1.0f);
    UpdateSpring(&g_ui.island_tilt.x, &g_ui.island_tilt_vel.x,
                 state->tilt_x * kTiltRange * tilt_w, dt);
    UpdateSpring(&g_ui.island_tilt.y, &g_ui.island_tilt_vel.y,
                 state->tilt_y * kTiltRange * tilt_w, dt);

    const ImVec2 island_base = l2d_active
        ? ImVec2(state->ball_pos.x - kIslandW * 0.5f,
                 state->ball_pos.y - kIslandH * 0.5f)
        : ImVec2(dw * 0.5f - kIslandW * 0.5f - pair_shift, kIslandTop);

    {
        const float dh_ = state->display_h > 0 ? (float)state->display_h : io.DisplaySize.y;
        const float margin = 12.0f;
        const float pair_w = kIslandW + (kDotGap + kDotD) * dot_t;
        auto hold = [](float* v, float* vel, float lo, float hi) {
            if (lo > hi) { *v = 0.0f; *vel = 0.0f; return; }
            if (*v < lo) { *v = lo; if (*vel < 0.0f) *vel = 0.0f; }
            if (*v > hi) { *v = hi; if (*vel > 0.0f) *vel = 0.0f; }
        };
        hold(&g_ui.island_tilt.x, &g_ui.island_tilt_vel.x,
             margin - island_base.x, dw - margin - pair_w - island_base.x);
        hold(&g_ui.island_tilt.y, &g_ui.island_tilt_vel.y,
             margin - island_base.y, dh_ - margin - kIslandH - island_base.y);
    }

    const ImVec2 island_pos(island_base.x + g_ui.island_tilt.x,
                            island_base.y + g_ui.island_tilt.y);
    const ImVec2 island_size(kIslandW, kIslandH);

    g_ui.island_rect = ImVec4(island_pos.x, island_pos.y, kIslandW, kIslandH);

    const float dh = state->display_h > 0 ? (float)state->display_h : io.DisplaySize.y;
    const ImVec2 card_size(560.0f, 360.0f);
    ImVec2 card_pos(island_pos.x + kIslandW * 0.5f - card_size.x * 0.5f,
                    island_pos.y + kIslandH * 0.5f - card_size.y * 0.35f);
    if (card_pos.x < 16.0f) card_pos.x = 16.0f;
    if (card_pos.y < 16.0f) card_pos.y = 16.0f;
    if (card_pos.x + card_size.x > dw - 16.0f) card_pos.x = dw - 16.0f - card_size.x;
    if (card_pos.y + card_size.y > dh - 16.0f) card_pos.y = dh - 16.0f - card_size.y;

    g_ui.shell_rest_rect =
        state->stage == UiState::StageIsland
            ? g_ui.island_rect
            : (state->stage == UiState::StageCard
                   ? ImVec4(card_pos.x, card_pos.y, card_size.x, card_size.y)
                   : ImVec4(state->last_full_pos.x, state->last_full_pos.y,
                            state->last_full_size.x, state->last_full_size.y));

    auto lerp = [](ImVec2 a, ImVec2 b, float u) {
        return ImVec2(a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u);
    };

    if (state->display_w > 0 && state->display_h > 0) {
        const float keep = 120.0f;
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

    {
        float j = g_ui.expand_vel * 0.055f;
        if (j >  0.16f) j =  0.16f;
        if (j < -0.16f) j = -0.16f;
        const ImVec2 c(win_pos.x + win_size.x * 0.5f, win_pos.y + win_size.y * 0.5f);
        win_size.x *= (1.0f + j * 0.55f);
        win_size.y *= (1.0f - j * 0.85f);
        win_pos.x = c.x - win_size.x * 0.5f;
        win_pos.y = c.y - win_size.y * 0.5f;
    }

    g_ui.shell_rect = ImVec4(win_pos.x, win_pos.y, win_size.x, win_size.y);

    const bool show_chrome    = (lt > 0.75f);
    const bool overriding_pos = (lt < 0.999f);

    if (overriding_pos) {
        ImGui::SetNextWindowPos (win_pos);
        ImGui::SetNextWindowSize(win_size);
    } else {

        ImGui::SetNextWindowPos(state->last_full_pos,
                                g_ui.content_moving ? ImGuiCond_Always
                                                      : ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(state->last_full_size, ImGuiCond_Always);
        ImGui::SetNextWindowSizeConstraints(ImVec2(700, 560), ImVec2(FLT_MAX, FLT_MAX));
    }

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

    state->glass_count = 0;
    if (state->screen_texture_id && !state->exit_anim_active) {
        GlassRect base{};
        base.rounding = rounding;
        base.alpha    = 1.0f;
        base.tintA    = state->glass_clarity;
        base.lightX   = state->glass_light_x;
        base.lightY   = state->glass_light_y;

        const float minSide = win_size.x < win_size.y ? win_size.x : win_size.y;
        if (minSide < 200.0f) {
            base.edgeWidth = minSide * 0.30f;
            base.blur      = 5.0f;
        }

        {
            const float dt = ImGui::GetIO().DeltaTime;
            if (g_ui.glass_pos_valid) {
                g_ui.glass_nav_lag.x -= (win_pos.x - g_ui.glass_prev_pos.x) * kNavFollow;
                g_ui.glass_nav_lag.y -= (win_pos.y - g_ui.glass_prev_pos.y) * kNavFollow;
            }
            g_ui.glass_prev_pos  = win_pos;
            g_ui.glass_pos_valid = true;
            UpdateSpring(&g_ui.glass_nav_lag.x, &g_ui.glass_nav_lag_vel.x, 0.0f, dt);
            UpdateSpring(&g_ui.glass_nav_lag.y, &g_ui.glass_nav_lag_vel.y, 0.0f, dt);

            auto hold = [](float* v, float* vel, float lim) {
                if (*v < -lim) { *v = -lim; if (*vel < 0.0f) *vel = 0.0f; }
                if (*v >  lim) { *v =  lim; if (*vel > 0.0f) *vel = 0.0f; }
            };
            hold(&g_ui.glass_nav_lag.x, &g_ui.glass_nav_lag_vel.x, kNavLagMax);
            hold(&g_ui.glass_nav_lag.y, &g_ui.glass_nav_lag_vel.y, kNavLagMax);
        }

        const float split = (lt > 0.70f ? (lt - 0.70f) / 0.30f : 0.0f)
                          * (1.0f - mclose);
        GlassRect a = base, b = base, strand = base, title = base;
        bool parted = false;
        if (split > 0.01f) {

            const ImGuiStyle& stl = ImGui::GetStyle();
            const float divide  = win_pos.x + stl.WindowPadding.x + kSidebarW;
            const float title_h = ImGui::GetFontSize() + stl.FramePadding.y * 2.0f;
            const float gap     = kGlassGap * split;
            const float win_r   = win_pos.x + win_size.x;
            const float win_b   = win_pos.y + win_size.y;

            const ImVec2 lag(g_ui.glass_nav_lag.x * split, g_ui.glass_nav_lag.y * split);
            g_ui.glass_nav_offset = lag;

            // The title bar and the content are one rect, not two butted
            // together. They used to be a full-width band overlapping the
            // content by four pixels, and at the window's right edge the band's
            // bottom-right corner and the content's top-right corner rounded
            // away from each other into a visible waist — which is what made the
            // title read as a separate strip laid across the top.
            //
            // So the content pane simply starts at the top of the window, and
            // the title shape is only the cap over the nav column, reaching far
            // enough across the divide that the two share a straight top edge:
            // past 2x the corner radius the rounding of each is inside the
            // other, so there is nothing left for the smoothing to fill.
            constexpr float kTitleOverlap = 4.0f;
            constexpr float kTitleJoin    = 26.0f;

            b.x = divide + gap * 0.5f;
            b.y = win_pos.y;
            b.w = win_r - b.x;
            b.h = win_b - b.y;

            title.x = win_pos.x;
            title.y = win_pos.y;
            title.w = (b.x - win_pos.x) + kTitleJoin;
            title.h = title_h + kTitleOverlap;

            const float nav_y0 = win_pos.y + title_h + kTitleOverlap + gap;
            a.x = win_pos.x + lag.x;
            a.y = nav_y0 + lag.y;
            a.w = (divide - gap * 0.5f) - win_pos.x;
            a.h = win_b - nav_y0;

            const float nav_r = a.x + a.w;
            const float slot  = b.x - nav_r;
            const float u     = (slot - kBridgeHold) / (kBridgeSnap - kBridgeHold);
            const float uc    = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
            const float neck  = 1.0f - uc * uc * (3.0f - 2.0f * uc);

            const bool joined = g_ui.strand_joined ? neck > 0.06f : neck > 0.35f;
            if (joined != g_ui.strand_joined) {
                g_ui.strand_joined = joined;
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
            g_ui.glass_nav_offset = ImVec2(0, 0);
            GlassRect r = base;
            r.x = win_pos.x; r.y = win_pos.y; r.w = win_size.x; r.h = win_size.y;
            if (dot_t > 0.02f) {

                GlassRect dot = base;
                dot.w = dot.h = kDotD * dot_t;
                dot.x = win_pos.x + win_size.x + kDotGap
                        + g_ui.glass_nav_lag.x * dot_t;
                dot.y = win_pos.y + win_size.y * 0.5f - dot.h * 0.5f
                        + g_ui.glass_nav_lag.y * dot_t;
                r.merge = dot.merge = kDotMerge;
                g_ui.dot_center = ImVec2(dot.x + dot.w * 0.5f, dot.y + dot.h * 0.5f);
                g_ui.dot_radius = dot.w * 0.5f;
                state->glass_rects[state->glass_count++] = r;
                state->glass_rects[state->glass_count++] = dot;
            } else {
                g_ui.dot_radius = 0.0f;
                state->glass_rects[state->glass_count++] = r;
            }
        }
    }

    if (dialog::JoinedShell()) {
        for (int i = 0; i < state->glass_count; ++i) {
            GlassRect& gr = state->glass_rects[i];
            if (gr.group != 0 || gr.w < 2.0f || gr.h < 2.0f || gr.alpha <= 0.001f)
                continue;
            dialog::BlendMaterial(&gr);
            break;
        }
    }

    const bool l2d_hidden_chrome = l2d_active && lt < 0.999f;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,
                        (l2d_hidden_chrome || state->screen_texture_id) ? 0.0f : 1.0f);
    if (l2d_hidden_chrome) ImGui::SetNextWindowBgAlpha(lt);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoResize;
    if (!show_chrome || g_ui.resizing) {
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

        const float card_alpha = (lt <= 0.30f || lt >= 0.72f) ? 0.0f
                               : (lt < 0.46f ? (lt - 0.30f) / 0.16f
                                             : (lt > 0.62f ? (0.72f - lt) / 0.10f : 1.0f));
        if (card_alpha > 0.01f) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, card_alpha);
            DrawCardContent(state);
            ImGui::PopStyleVar();

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

            DrawSidebar(page, keep_running, state);
            state->nav_page = (int)page;
            DrawContent(state, page);

            ImGui::PopStyleVar();
        }
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
    if (pushed_glass_text) ImGui::PopStyleColor(pushed_glass_text);

    ripple::DrawAll();

    dialog::Draw(state);

    state->widget_count = chrome::Drain(state->widget_rects, kMaxWidgetGlass);

    if (state->exit_anim_active && g_ui.exit_anim_first_frame) {
        const float now     = (float)ImGui::GetTime();
        const float t01     = (now - g_ui.exit_anim_start) / 1.35f;
        const float clamped = t01 < 0.0f ? 0.0f : (t01 > 1.0f ? 1.0f : t01);
        dissolve::Step(dt, clamped,
                      (ImTextureID)(uintptr_t)state->scene_snapshot_id);
        g_ui.exit_anim_first_frame = false;
    }
}

}
