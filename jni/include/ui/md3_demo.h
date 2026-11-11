#pragma once
#include "imgui.h"

namespace aimgui::md3demo {

// All state belongs to this demo instance, independent of the glass application.
struct State {
    bool visible = true;
    bool dark = false;
    int mode = 0;          // 0: component gallery, 1: interactive examples.
    int page = 0;          // Gallery categories, 0..7 (Expressive is last).
    int sample_page = 0;   // 0: actions, 1: settings, 2: form.
    float scale = 0;       // Zero follows screen size; positive values are manual.
    ImVec4 window_rect{};  // Current x, y, width, height; zero when minimized.

    int sample_clicks = 0;
    bool sample_enabled = true;
    bool sample_checked = false;
    bool sample_notifications = true;
    float sample_value = 40;
    char sample_name[96] = "";
    char saved_name[96] = "";
    bool sample_error = false;
    bool sample_saved = false;
    bool sample_dialog = false;
    bool snackbar_open = false;
    float snackbar_seconds = 0;

    // Window geometry is retained while minimized; Draw clamps after rotation.
    ImVec2 position{}, size{};
    bool positioned = false;
    int content_key = -1;
};

void ResetSamples(State& state);
// Call between ImGui::NewFrame and Render. display_size is the visible screen,
// even when the native drawing surface is a larger square used for rotation.
void Draw(State& state, bool* running, ImVec2 display_size);

} // namespace aimgui::md3demo
