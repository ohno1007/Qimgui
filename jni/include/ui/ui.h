#pragma once

#include "imgui.h"

#include "core/glass.h"

#include <cstdint>

namespace aimgui {

struct UiState {
    const char* renderer_name = nullptr;

    bool permeate_record         = false;
    bool request_permeate_toggle = false;

    bool mirror_hides_window = true;

    int target_fps = 0;

    int nav_page = 0;

    bool haptics_enabled = true;

    int display_w = 0;
    int display_h = 0;

    enum Stage { StageIsland = 0, StageCard = 1, StageWindow = 2 };
    int   stage       = StageWindow;
    bool  collapsed   = false;
    float expand      = 1.0f;

    const char* island_text = nullptr;
    const char* island_icon = nullptr;
    const char* dot_text    = nullptr;
    const char* card_title  = nullptr;
    const char* card_icon   = nullptr;
    const char* card_body   = nullptr;

    ImVec2 ball_pos = ImVec2(-1.0f, -1.0f);
    float  ball_scale = 1.0f;

    ImVec2 last_full_pos  = ImVec2(60, 100);
    ImVec2 last_full_size = ImVec2(900, 620);

    float bloom_intensity = 0.0f;

    float glass_clarity = 0.11f;

    float  tilt_x = 0.0f;
    float  tilt_y = 0.0f;

    float glass_light_x = -0.6f;
    float glass_light_y = -0.8f;

    bool     screen_mirror         = false;
    bool     screen_mirror_running = false;
    uint64_t screen_mirror_frames  = 0;
    int      screen_mirror_w       = 0;
    int      screen_mirror_h       = 0;
    unsigned long long screen_texture_id = 0;

    GlassRect glass_rects[kMaxGlassRects];
    int       glass_count = 0;

    // Controls, refracting the sheet they sit on rather than the desktop. Their
    // pass runs after the panes above and samples the result, so the depth
    // composes: the control bends the window's glass, which bends the screen.
    GlassRect widget_rects[kMaxWidgetGlass];
    int       widget_count = 0;
    bool      widget_glass = true;
    // Set by the main loop from the renderer: false means the second pass will
    // not run, and controls keep painting their own edge.
    bool      widget_glass_ok = false;

    bool  exit_anim_active      = false;

    unsigned long long scene_snapshot_id = 0;
};

void DrawUi(UiState* state, bool* keep_running);

namespace ripple {

void TouchLastItem();
}

namespace chrome {

void Rect(const ImVec2& a, const ImVec2& b, float rounding,
          bool hovered, bool active);

void LastItem(float rounding = -1.0f);

void LastItemFrame(const char* label, float rounding = -1.0f);
}

// A list row that opens into a panel. The row's glass body is the panel's: one
// shape whose height and rounding travel from a capsule to a card, so the row
// is never replaced by something else, it becomes it. Under-damped, so it
// arrives slightly past the open height and settles back.
//
//     if (expander::Begin(u8"外观", ICON_FA_PALETTE)) {
//         ... body ...
//     }
//     expander::End();     // always, whatever Begin returned
namespace expander {
bool Begin(const char* label, const char* icon = nullptr);
void End();
bool IsOpen(const char* label);
}

namespace dialog {

enum Kind {
    KindConfirm = 0,
    KindLicense,
    KindCustom,
};

enum Result {
    ResultNone = 0,
    ResultOk,
    ResultCancel,
};

void Open(Kind kind, const char* title, const char* body = nullptr,
          const char* ok = nullptr, const char* cancel = nullptr);
void Close();

bool IsOpen();

Result Take();

const char* Input();

}

void ApplyGlassPalette();

}
