#pragma once

#include "imgui.h"   // ImVec2

#include "core/glass.h"

#include <cstdint>

namespace aimgui {

// State shared between main loop and the UI layer. The main loop owns
// the struct, hands a pointer to DrawUi() each frame; UI sets *request_*
// flags, main consumes / clears them next frame.
struct UiState {
    const char* renderer_name = nullptr;

    // Anti-recording: surface created with the skipScreenshot flag so
    // it doesn't show up in screen captures / casts.
    bool permeate_record         = false;
    bool request_permeate_toggle = false;

    // Frame-rate cap. 0 = vsync (panel refresh).
    int target_fps = 0;

    // Current visible display size in ImGui coordinates, so the Dynamic
    // Island can re-center itself on portrait↔landscape rotation.
    int display_w = 0;
    int display_h = 0;

    // ── Dynamic Island ───────────────────────────────────────────────
    // `collapsed` is the *target* state (true = pill at top, false =
    // full window). `expand` is the animated value lerping toward it
    // through a spring; `expand_vel` is the spring's velocity.
    bool  collapsed   = false;
    float expand      = 1.0f;
    float expand_vel  = 0.0f;

    // Live2D floating "ball": when collapsed the character is the visual and
    // can be dragged anywhere; this is its centre in screen px. Re-clamped to
    // the display each frame. (-1,-1) = uninitialised → placed on first use.
    ImVec2 ball_pos = ImVec2(-1.0f, -1.0f);
    // Model-size multiplier for the ball, adjustable from the UI.
    float  ball_scale = 1.0f;

    // Remembered full-window pos / size so the window springs back to
    // wherever the user last dragged it.
    ImVec2 last_full_pos  = ImVec2(60, 100);
    ImVec2 last_full_size = ImVec2(900, 620);

    // Bottom-right resize handle: a drag previews a thick rounded frame at
    // the target size without changing the live window; on release the
    // window springs from its current size to that target.
    bool   resizing               = false;
    ImVec2 resize_target_size     = ImVec2(900, 620);
    ImVec2 resize_anim_vel        = ImVec2(0, 0);
    ImVec2 resize_drag_start_mouse = ImVec2(0, 0);
    ImVec2 resize_drag_start_size  = ImVec2(900, 620);

    // Post-process bloom intensity, applied at composite. 0 = bloom off.
    float bloom_intensity = 0.75f;

    // Live screen mirror: SurfaceFlinger composites the screen into buffers
    // we own, giving sampleable pixels for a refracting backdrop. The frame
    // counter is a liveness signal — if it stops rising, frames stopped
    // arriving.
    bool     screen_mirror         = false;
    bool     screen_mirror_running = false;
    uint64_t screen_mirror_frames  = 0;
    int      screen_mirror_w       = 0;
    int      screen_mirror_h       = 0;
    // ImTextureID for the newest mirrored frame, 0 when unavailable. Sampling
    // this is what makes a refracting backdrop possible at all.
    unsigned long long screen_texture_id = 0;
    // Panes to refract this frame, rebuilt by DrawUi and consumed by the main
    // loop right after. Held here rather than passed around because the main
    // loop is what talks to the renderer.
    GlassRect glass_rects[kMaxGlassRects];
    int       glass_count = 0;

    // Frosted-glass backdrop. SurfaceFlinger blurs what it composites behind
    // the window, so this costs nothing per frame — but it needs Android 12+
    // and a compositor built with blur support. `supported` is refreshed by
    // DrawUi each frame; when false the toggle has no effect and the UI says
    // so, because there is no cheap way to fake it below Android 12.
    bool  backdrop_blur           = false;
    float backdrop_blur_radius    = 40.0f;
    bool  backdrop_blur_supported = false;

    // Exit fragmentation animation: when the 退出 button is pressed, the
    // UI shatters into falling chips and the process keeps running until
    // the animation has played out (~1.2 s). DrawUi owns these.
    bool  exit_anim_active      = false;
    bool  exit_anim_first_frame = false; // click-frame grace; cleared at end of DrawUi
    float exit_anim_start       = 0.0f;

    // Opaque ImTextureID-compatible handle to last frame's scene snapshot,
    // updated by main loop from IRenderer::GetSceneSnapshotID(). Lets the
    // dissolve particles sample the real UI as a texture.
    unsigned long long scene_snapshot_id = 0;
};

void DrawUi(UiState* state, bool* keep_running);

namespace ripple {
// Records an MD3 ripple at the last drawn item if it was just activated.
// Call right after any clickable widget (Selectable / Button / Combo /
// Checkbox / CollapsingHeader / ...) that you want to ripple.
void TouchLastItem();
} // namespace ripple

} // namespace aimgui
