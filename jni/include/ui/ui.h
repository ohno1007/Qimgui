#pragma once

#include "imgui.h"   // ImVec2

#include "core/glass.h"

#include <cstdint>

namespace aimgui {

// State shared between the main loop and the UI layer. The main loop owns the
// struct and hands a pointer to DrawUi() each frame; the UI sets request_*
// flags, main consumes and clears them next frame.
struct UiState {
    const char* renderer_name = nullptr;

    // Anti-recording: the surface carries skipScreenshot, so it does not appear
    // in captures or casts.
    bool permeate_record         = false;
    bool request_permeate_toggle = false;

    // Whether the live mirror may hide this window from captures. It has to hide
    // it from *its own* capture or the mirror feeds the window back into itself
    // and the glass saturates — but skipScreenshot is one flag, not one per
    // capture, so this is a genuine either/or and belongs to the user rather
    // than being a hidden side effect of turning the glass on.
    bool mirror_hides_window = true;

    // Frame-rate cap. 0 = vsync (panel refresh).
    int target_fps = 0;

    // Which nav entry is showing. Here rather than a static in DrawSidebar so it
    // is persisted with everything else.
    int nav_page = 0;

    // Vibration on taps and stage changes. Off is a legitimate preference, and
    // also what a device with no reachable vibrator looks like.
    bool haptics_enabled = true;

    // Visible display size in ImGui coordinates, so the island can re-centre
    // itself on rotation.
    int display_w = 0;
    int display_h = 0;

    // ── Dynamic Island ───────────────────────────────────────────────
    // Three rest states, not two: the pill, a compact card, and the full
    // window. One tap moves up a step, so the island can be opened far enough
    // to read at a glance without committing to the whole window.
    //
    // `stage` is the target; `expand` is the spring chasing it at 0.0 / 0.5 /
    // 1.0.
    enum Stage { StageIsland = 0, StageCard = 1, StageWindow = 2 };
    int   stage       = StageWindow;
    bool  collapsed   = false;   // derived: stage == StageIsland
    float expand      = 1.0f;

    // What the three rest states show. All optional — leave one null for the
    // built-in default. Icons are plain strings, so they concatenate with text:
    // ICON_FA_BOLT "  就绪". Borrowed pointers read during DrawUi, so whatever
    // they point at has to outlive the frame.
    const char* island_text = nullptr;   // null → live frame rate
    const char* island_icon = nullptr;   // drawn ahead of the text
    const char* dot_text    = nullptr;   // the companion circle; null → island_icon
    const char* card_title  = nullptr;   // null → "AImGui"
    const char* card_icon   = nullptr;
    const char* card_body   = nullptr;   // null → the built-in status block

    // Live2D floating "ball": when collapsed the character is the visual and can
    // be dragged anywhere. Centre in screen px, re-clamped each frame.
    // (-1,-1) = uninitialised, placed on first use.
    ImVec2 ball_pos = ImVec2(-1.0f, -1.0f);
    float  ball_scale = 1.0f;

    // Remembered full-window pos and size, so the window springs back to
    // wherever it was last dragged.
    ImVec2 last_full_pos  = ImVec2(60, 100);
    ImVec2 last_full_size = ImVec2(900, 620);

    // Post-process bloom intensity, applied at composite. 0 = off.
    float bloom_intensity = 0.0f;

    // How much wash sits over the refracted screen. 0 is bare glass, 1 an opaque
    // panel; the useful range is the bottom third, which is why the slider stops
    // well short of the top. This carries the *whole* wash — ImGui paints none,
    // because any fill of its own would be a window-shaped rectangle and would
    // bridge the slot between parted panes.
    float glass_clarity = 0.11f;

    // Device lean, roughly -1..1 per axis. Both zero when the panel is flat and
    // when no sensor is reachable, so everything reading them degrades to
    // standing still rather than to a special case.
    float  tilt_x = 0.0f;
    float  tilt_y = 0.0f;

    // Key-light direction for the glass, in screen space. Steered by the
    // accelerometer, fixed up-and-left where no sensor is reachable.
    float glass_light_x = -0.6f;
    float glass_light_y = -0.8f;

    // Live screen mirror: SurfaceFlinger composites the screen into buffers we
    // own, which is what makes a refracting backdrop possible. The frame counter
    // is a liveness signal — if it stops rising, frames stopped arriving.
    bool     screen_mirror         = false;
    bool     screen_mirror_running = false;
    uint64_t screen_mirror_frames  = 0;
    int      screen_mirror_w       = 0;
    int      screen_mirror_h       = 0;
    unsigned long long screen_texture_id = 0;   // newest frame, 0 = unavailable

    // Panes to refract this frame, rebuilt by DrawUi and consumed by the main
    // loop right after — it is the main loop that talks to the renderer.
    GlassRect glass_rects[kMaxGlassRects];
    int       glass_count = 0;

    // The exit confirmation is up and its answer still wanted. Here rather than
    // a static so the sidebar does not own dialog state that outlives its frame.

    // Exit animation: the UI dissolves into drifting particles and the process
    // keeps running until they have played out (~1.2 s). DrawUi owns these.
    bool  exit_anim_active      = false;

    // Last frame's scene snapshot, from IRenderer::GetSceneSnapshotID(), so the
    // dissolve particles can sample the real UI as a texture.
    unsigned long long scene_snapshot_id = 0;
};

void DrawUi(UiState* state, bool* keep_running);

namespace ripple {
// Records a ripple at the last drawn item if it was just activated. Call right
// after any clickable widget that should ripple.
void TouchLastItem();
} // namespace ripple

// ─── Control chrome ──────────────────────────────────────────────────────
// The sheet announces a shape by its edge and by what it does to the light
// through it, never by filling itself in. Controls drawn as flat coloured slabs
// speak the opposite grammar and read as stickers, so they are drawn here
// instead: a contact shadow to lift them, a wash barely strong enough to
// separate them, and a rim bright along the top and dim along the bottom, on
// the same up-and-left key the pane's own shader uses.
//
// Drawn *after* the widget, which works only because there is no heavy fill to
// cover its label — and is what keeps this from needing channel splitting at
// every call site.
namespace chrome {
// rounding < 0 means a capsule (half the height).
void Rect(const ImVec2& a, const ImVec2& b, float rounding,
          bool hovered, bool active);
// For widgets whose whole item rect is the frame — Button, ProgressBar.
void LastItem(float rounding = -1.0f);
// For widgets that put their label to the right and report an item rect
// covering both — Checkbox, Combo, SliderFloat. The frame is what gets the step.
void LastItemFrame(const char* label, float rounding = -1.0f);
} // namespace chrome

// ─── Modal dialogs ───────────────────────────────────────────────────────
// Three bodies of the same liquid glass — a capsule for the text and two
// smaller ones for the answers — drawn out of the Dynamic Island's capsule and
// hanging below it, which is where they live whatever the shell is doing.
//
// Whether they are *part of* the shell depends on what the shell is. Beside the
// island or the card they share its body: one merged field, so they neck and
// let go by distance, and a card growing downwards presses them out of its way.
// Over a full window they cannot — a window contains the island's spot, and a
// smooth union swallows a shape that lies inside another one, so merging there
// would delete the modal rather than join it. There it is a thinner sheet of
// its own, which sharing costs it: one group is one pass and one material.
//
// Modal in name only, deliberately: it claims the three bodies it draws and
// leaves the rest of the app live. One at a time; opening while one is up
// replaces it.
namespace dialog {

enum Kind {
    KindConfirm = 0,   // a question and two answers
    KindLicense,       // a field for a licence key, with paste rather than OK
    KindCustom,        // whatever the caller labels the two buttons
};

enum Result {
    ResultNone = 0,
    ResultOk,
    ResultCancel,
};

// `body` may be null for KindLicense, where the field takes its place; `ok` and
// `cancel` may be null for anything but KindCustom, which is the point of it.
// All four are copied, so none has to outlive the call.
void Open(Kind kind, const char* title, const char* body = nullptr,
          const char* ok = nullptr, const char* cancel = nullptr);
void Close();

// True while it is up or still animating. Not a reason to gate your own input —
// it reaches over the window rather than taking it away.
bool IsOpen();

// The answer, once, on the frame it is given. Reading it clears it.
Result Take();

// What was typed, for KindLicense. Valid until the next Open.
const char* Input();

} // namespace dialog

// Re-applies the glass palette over whatever base theme is loaded. Called at
// startup, and again after anything that calls StyleColorsDark/Light/Classic.
void ApplyGlassPalette();

} // namespace aimgui
