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
