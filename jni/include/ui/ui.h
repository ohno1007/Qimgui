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

    // Whether the live mirror is allowed to hide this window from captures.
    //
    // It has to hide it from *its own* capture or the mirror feeds the window
    // back into itself and the glass saturates. But SurfaceFlinger's
    // skipScreenshot is one flag, not one per capture: it cannot tell our
    // virtual display apart from the user pressing screenshot. So this is a
    // genuine either/or, and it is a setting rather than a hidden side effect
    // of turning the glass on — which is what made "anti-record is off, why
    // can't I screenshot the UI" so mystifying.
    bool mirror_hides_window = true;

    // Frame-rate cap. 0 = vsync (panel refresh).
    int target_fps = 0;

    // Which nav entry is showing. Lives here rather than as a static inside
    // DrawSidebar so it can be persisted with everything else.
    int nav_page = 0;

    // Vibration on taps and stage changes. Off is a legitimate preference, and
    // it is also what a device with no reachable vibrator ends up looking like.
    bool haptics_enabled = true;

    // Current visible display size in ImGui coordinates, so the Dynamic
    // Island can re-center itself on portrait↔landscape rotation.
    int display_w = 0;
    int display_h = 0;

    // ── Dynamic Island ───────────────────────────────────────────────
    // Three resting states rather than two: the pill, a compact card, and
    // the full window. One tap moves up a step, so the island can be opened
    // far enough to read at a glance without committing to the whole window.
    //
    // `stage` is the target; `expand` is the animated value chasing it
    // through a spring, at 0.0 / 0.5 / 1.0, and `expand_vel` is that
    // spring's velocity — also what drives the squash-and-stretch, since a
    // fast-moving spring is exactly when a jelly should deform.
    enum Stage { StageIsland = 0, StageCard = 1, StageWindow = 2 };
    int   stage       = StageWindow;
    bool  collapsed   = false;   // derived: stage == StageIsland
    float expand      = 1.0f;
    float expand_vel  = 0.0f;

    // What the three rest states show. Every one of these is optional; leave it
    // null and the built-in default is used. Icons come from ui/icons.h and are
    // just strings, so they concatenate with text: ICON_FA_BOLT "  就绪".
    //
    // These are borrowed pointers read during DrawUi, so whatever they point at
    // has to outlive the frame — a literal, or a buffer the caller keeps.
    const char* island_text = nullptr;   // null → live frame rate
    const char* island_icon = nullptr;   // drawn ahead of the text
    const char* dot_text    = nullptr;   // the companion circle; null → island_icon
    const char* card_title  = nullptr;   // null → "AImGui"
    const char* card_icon   = nullptr;
    const char* card_body   = nullptr;   // null → the built-in status block

    // Where the companion circle ended up this frame, published by DrawUi so
    // its content can be drawn on the foreground list — it sits outside the
    // ImGui window and so is not reachable through the layout. radius 0 = gone.
    ImVec2 dot_center = ImVec2(0, 0);
    float  dot_radius = 0.0f;


    // Live2D floating "ball": when collapsed the character is the visual and
    // can be dragged anywhere; this is its centre in screen px. Re-clamped to
    // the display each frame. (-1,-1) = uninitialised → placed on first use.
    ImVec2 ball_pos = ImVec2(-1.0f, -1.0f);
    // Model-size multiplier for the ball, adjustable from the UI.
    float  ball_scale = 1.0f;

    // Remembered full-window pos / size so the window springs back to
    // wherever the user last dragged it.
    ImVec2 last_full_pos  = ImVec2(60, 100);
    // True while a drag in the content is moving the window, during which
    // last_full_pos is authoritative rather than mirroring ImGui's own.
    bool   content_moving = false;
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
    float bloom_intensity = 0.0f;

    // How much of a wash sits over the refracted screen. 0 is bare glass, 1 is
    // an opaque panel; the useful range is the bottom third, which is why the
    // slider stops well short of the top.
    //
    // This carries the whole wash. ImGui used to paint a 5% sheet of its own on
    // top, but that fill is always a window-shaped rectangle and would bridge
    // the slot between parted panes — so the default here absorbs it.
    float glass_clarity = 0.11f;

    // Device lean from the accelerometer, roughly -1..1 per axis. Both zero
    // when the panel is flat, and when no sensor is reachable at all — so
    // everything reading these degrades to standing still rather than to a
    // special case.
    float  tilt_x = 0.0f;
    float  tilt_y = 0.0f;
    // Where the lean has carried the island, and the spring chasing it. The
    // spring is what gives the island weight: the accelerometer is already
    // low-passed, but a value that merely follows the lean reads as a readout
    // rather than as something being tipped around.
    ImVec2 island_tilt     = ImVec2(0, 0);
    ImVec2 island_tilt_vel = ImVec2(0, 0);

    // Key-light direction for the glass, in screen space. Steered by the
    // accelerometer so the rim highlight sweeps as the panel leans; stays at
    // the fixed up-and-left default wherever no sensor is reachable.
    float glass_light_x = -0.6f;
    float glass_light_y = -0.8f;

    // How far the nav column's free edges are lagging behind the window, px.
    // The column is a separate body: drag the window and it is left behind,
    // stop and it springs back through. Nothing about the merge itself is
    // animated — surface tension is a constant. It is the distance that moves,
    // and whether the two run together follows from that, which is the only
    // way it reads as proximity rather than as a scripted effect.
    ImVec2 glass_nav_lag     = ImVec2(0, 0);
    ImVec2 glass_nav_lag_vel = ImVec2(0, 0);
    // The lag actually applied this frame — faded in with the stage. Both the
    // pane and the widgets standing on it read this one value, or they would
    // drift apart during the island-to-window transition.
    ImVec2 glass_nav_offset  = ImVec2(0, 0);
    ImVec2 glass_prev_pos    = ImVec2(0, 0);
    bool   glass_pos_valid   = false;
    // Edge detection for the feedback pulses: the last rest state a pulse was
    // fired for, and whether the strand was joined when we last looked.
    int    haptic_last_stage = StageWindow;
    bool   strand_joined     = true;

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

    // Exit fragmentation animation: when the 退出 button is pressed, the
    // UI dissolves into drifting particles and the process keeps running until
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

// ─── Control chrome ──────────────────────────────────────────────────────
// The sheet announces a shape by its edge and by what it does to the light
// passing through, never by filling itself in. Controls drawn as flat coloured
// slabs speak the opposite grammar and read as stickers on the glass, so they
// are drawn here instead: a contact shadow to lift them, a wash barely strong
// enough to separate them, and a rim that is bright along the top where the
// key light falls and dim along the bottom — the same up-and-left key the
// pane's own shader uses.
//
// Everything is drawn *after* the widget, which works only because there is no
// heavy fill to cover its label. That is what keeps this from needing draw-list
// channel splitting at every call site.
namespace chrome {
// rounding < 0 means a capsule (half the height).
void Rect(const ImVec2& a, const ImVec2& b, float rounding,
          bool hovered, bool active);
// For widgets whose whole item rect is the frame — Button, ProgressBar.
void LastItem(float rounding = -1.0f);
// For widgets that put their label to the right and report an item rect
// covering both — Checkbox, Combo, SliderFloat. The step belongs to the frame.
void LastItemFrame(const char* label, float rounding = -1.0f);
} // namespace chrome

// Re-applies the glass palette over whatever base theme is loaded. Called at
// startup and again after anything that calls StyleColorsDark/Light/Classic,
// which would otherwise put ImGui's own slab colours back.
void ApplyGlassPalette();

} // namespace aimgui
