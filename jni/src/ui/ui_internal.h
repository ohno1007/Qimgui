#pragma once

// Shared between the ui/*.cpp translation units. Nothing outside jni/src/ui
// includes this; the public surface is ui/ui.h.

#include "ui/ui.h"
#include "ui/main_ui.h"

#include "imgui.h"

#include <cmath>

namespace aimgui {

// ─── Shared layout ───────────────────────────────────────────────────────
// Only the numbers more than one file needs. Everything else stays local to
// the file that owns it.

constexpr float kSidebarW  = 230.0f;   // nav column, pane and child alike

// The Dynamic Island at rest. The shell interpolates down to this and the
// modal hangs off it, so both have to agree.
constexpr float kIslandW   = 280.0f;
constexpr float kIslandH   = 56.0f;
constexpr float kIslandTop = 28.0f;

// The one spring everything that follows the window uses, so it all moves with
// the same weight. Under-damped on purpose: critically damped, the window
// arrives and stops — correct and lifeless. The overshoot is what reads as
// elastic, and the squash taken from this velocity is the other half of it.
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

// ─── Shell state ─────────────────────────────────────────────────────────
// Everything the ui/ files pass between themselves and nobody outside has any
// business seeing. It lived in UiState, which is a public header main.cpp and
// the page content also include — so forty percent of that struct was plumbing
// for animations they cannot observe and must not touch.
//
// One instance, defined in ui.cpp. A global rather than a parameter because
// every file here already keeps its own state that way (the ripple list, the
// dialog, the press table), and threading one more pointer through would say
// nothing the include does not already say.
struct Shell {
    // The rest state actually being animated towards, which is UiState::stage
    // except while a modal is being re-issued.
    int    stage_shown = UiState::StageWindow;
    float  expand_vel  = 0.0f;   // also drives the squash-and-stretch

    // The shell as it stands, the island's capsule wherever it is, and where
    // the shell will settle for the stage that has been asked for. The modal is
    // drawn out of the capsule and pressed down by the rest rect — not by the
    // live one, which is briefly enormous mid-collapse.
    ImVec4 shell_rect      = ImVec4(0, 0, 0, 0);
    ImVec4 island_rect     = ImVec4(0, 0, 0, 0);
    ImVec4 shell_rest_rect = ImVec4(0, 0, 0, 0);

    // The modal's whole body, for the exit dissolve to seed particles over.
    // Zero size when none is up.
    ImVec4 modal_rect  = ImVec4(0, 0, 0, 0);
    // How far the shell has closed ranks for a modal sharing its body, 0..1:
    // one merged body gets four shapes and the modal is three of them, so the
    // shell has to come down to one — no parted column, no companion dot.
    float  modal_close = 0.0f;

    // Where the companion circle ended up, so its content can go on the
    // foreground list. radius 0 = gone.
    ImVec2 dot_center = ImVec2(0, 0);
    float  dot_radius = 0.0f;

    // Where the lean has carried the island, through a spring — the spring is
    // what gives it weight, since a value that merely follows the sensor reads
    // as a readout rather than as something being tipped around.
    ImVec2 island_tilt     = ImVec2(0, 0);
    ImVec2 island_tilt_vel = ImVec2(0, 0);

    // True on the frames a content drag owns the window position rather than
    // ImGui.
    bool   content_moving = false;

    // The resize grip: a drag previews a frame at the target size without
    // changing the live window, and on release the window springs to it.
    // Seeded from last_full_size on the first frame, so a size restored from
    // config is not sprung away from.
    bool   resizing               = false;
    bool   resize_valid           = false;
    ImVec2 resize_target_size     = ImVec2(900, 620);
    ImVec2 resize_anim_vel        = ImVec2(0, 0);
    ImVec2 resize_drag_start_mouse = ImVec2(0, 0);
    ImVec2 resize_drag_start_size  = ImVec2(900, 620);

    // How far the nav column is lagging behind the window, px, and the lag
    // actually applied this frame. The column is a separate body: drag the
    // window and it is left behind, stop and it springs back through. Pane and
    // widgets read the one offset or they drift apart mid-transition.
    ImVec2 glass_nav_lag     = ImVec2(0, 0);
    ImVec2 glass_nav_lag_vel = ImVec2(0, 0);
    ImVec2 glass_nav_offset  = ImVec2(0, 0);
    ImVec2 glass_prev_pos    = ImVec2(0, 0);
    bool   glass_pos_valid   = false;

    // Edge detection for the feedback pulses.
    int    haptic_last_stage = UiState::StageWindow;
    bool   strand_joined     = true;

    // The exit confirmation is up and its answer still wanted.
    bool   pending_exit = false;
    // Click-frame grace for the dissolve, and when it started.
    bool   exit_anim_first_frame = false;
    float  exit_anim_start       = 0.0f;
};
extern Shell g_ui;

// ─── theme.cpp ───────────────────────────────────────────────────────────
void ApplyStyleOnce();

// ─── sidebar.cpp ─────────────────────────────────────────────────────────
void DrawSidebar(Page& current, bool* keep_running, UiState* state);

// ─── content.cpp ─────────────────────────────────────────────────────────
// The three rest states' contents. Each is drawn by ui.cpp at the alpha its
// own stage has faded to.
void DrawContent(UiState* state, Page page);
void DrawCardContent(const UiState* state);
void DrawIslandContent(const UiState* state);
void DrawDotContent(const UiState* state, float alpha);

// ─── gestures.cpp ────────────────────────────────────────────────────────
// One drag in the content means either a scroll or a window move; the resize
// grip previews a size and springs to it on release.
void   ContentGesture(const char* id, UiState* state);
void   HandleResizeInput(UiState* state, const ImGuiIO& io);
void   DrawResizeGrip(const UiState* state);
ImVec2 GripMin(const UiState* state);
ImVec2 GripMax(const UiState* state);

// ─── ripple.cpp ──────────────────────────────────────────────────────────
// Recording is public (ui.h) because page content records its own; drawing is
// once a frame from here, after everything that could have recorded.
namespace ripple {
void DrawAll();
} // namespace ripple

// ─── dissolve.cpp ────────────────────────────────────────────────────────
// The exit animation: the UI comes apart into particles that sample the last
// scene snapshot, and the process stays up until they have played out.
namespace dissolve {
void Begin(const ImVec2& origin, const ImVec2& size, float tex_w, float tex_h);
void Step(float dt, float t01, ImTextureID snapshot_tex);
} // namespace dissolve

// ─── dialog.cpp ──────────────────────────────────────────────────────────
// The modal's internal side. Openness drives the material blend; JoinedShell
// and StageMismatch tell ui.cpp whether the shell is sharing the modal's body
// and whether it must hold still while that changes.
namespace dialog {
void  Draw(UiState* state);
bool  JoinedShell();
float Openness();
void  BlendMaterial(GlassRect* lead);
bool  StageMismatch(int stage);
} // namespace dialog

} // namespace aimgui
