#pragma once

#include "ui/ui.h"
#include "ui/main_ui.h"

#include "imgui.h"

#include <cmath>

namespace aimgui {

constexpr float kSidebarW  = 230.0f;

constexpr float kIslandW   = 280.0f;
constexpr float kIslandH   = 56.0f;
constexpr float kIslandTop = 28.0f;

inline void UpdateSpring(float* pos, float* vel, float target, float dt) {
    constexpr float kOmega = 7.2f;
    constexpr float kZeta  = 0.58f;
    const float diff  = target - *pos;
    const float accel = kOmega * kOmega * diff - 2.0f * kZeta * kOmega * (*vel);
    *vel += accel * dt;
    *pos += (*vel) * dt;
    if (std::abs(diff) < 0.001f && std::abs(*vel) < 0.005f) {
        *pos = target;
        *vel = 0.0f;
    }
}

struct Shell {

    int    stage_shown = UiState::StageWindow;
    float  expand_vel  = 0.0f;

    ImVec4 shell_rect      = ImVec4(0, 0, 0, 0);
    ImVec4 island_rect     = ImVec4(0, 0, 0, 0);
    ImVec4 shell_rest_rect = ImVec4(0, 0, 0, 0);

    ImVec4 modal_rect  = ImVec4(0, 0, 0, 0);

    float  modal_close = 0.0f;

    ImVec2 dot_center = ImVec2(0, 0);
    float  dot_radius = 0.0f;

    ImVec2 island_tilt     = ImVec2(0, 0);
    ImVec2 island_tilt_vel = ImVec2(0, 0);

    bool   content_moving = false;

    bool   resizing               = false;
    bool   resize_valid           = false;
    ImVec2 resize_target_size     = ImVec2(900, 620);
    ImVec2 resize_anim_vel        = ImVec2(0, 0);
    ImVec2 resize_drag_start_mouse = ImVec2(0, 0);
    ImVec2 resize_drag_start_size  = ImVec2(900, 620);

    ImVec2 glass_nav_lag     = ImVec2(0, 0);
    ImVec2 glass_nav_lag_vel = ImVec2(0, 0);
    ImVec2 glass_nav_offset  = ImVec2(0, 0);
    ImVec2 glass_prev_pos    = ImVec2(0, 0);
    bool   glass_pos_valid   = false;

    int    haptic_last_stage = UiState::StageWindow;
    bool   strand_joined     = true;

    bool   pending_exit = false;

    bool   exit_anim_first_frame = false;
    float  exit_anim_start       = 0.0f;
};
extern Shell g_ui;

void ApplyStyleOnce();

void DrawSidebar(Page& current, bool* keep_running, UiState* state);

void DrawContent(UiState* state, Page page);
void DrawCardContent(const UiState* state);
void DrawIslandContent(const UiState* state);
void DrawDotContent(const UiState* state, float alpha);

void   ContentGesture(const char* id, UiState* state);
void   HandleResizeInput(UiState* state, const ImGuiIO& io);
void   DrawResizeGrip(const UiState* state);
ImVec2 GripMin(const UiState* state);
ImVec2 GripMax(const UiState* state);

namespace ripple {
void DrawAll();
}

namespace dissolve {
void Begin(const ImVec2& origin, const ImVec2& size, float tex_w, float tex_h);
void Step(float dt, float t01, ImTextureID snapshot_tex);
}

namespace dialog {
void  Draw(UiState* state);
bool  JoinedShell();
float Openness();
void  BlendMaterial(GlassRect* lead);
bool  StageMismatch(int stage);
}

}
