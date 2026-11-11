/* SPDX-License-Identifier: MIT */
#include "qk_app.h"

#include "qk_file.h"
#include "qk_math.h"

#include "imgui.h"
#include "ui/md3.h"
#include "ui/md3_showcase.h"
#include "ui/main_ui.h"
#include "ui/ui.h"
#include "core/haptics.h"

#include <cstdio>
#include <cstring>
#include <new>

namespace {

struct AppState {
    aimgui::UiState ui{};
    bool keep_running = true;
    int mode = QK_APP_MAIN;
    const char *settings_path = nullptr;
    bool loaded = false;
    /* The values last written, to tell a real change from a redraw. */
    struct {
        bool valid = false;
        bool dark = false;
        bool haptics = true;
        float scale = 0.f;
        int page = 0;
        int component_page = 0;
        int stage = aimgui::UiState::StageWindow;
        float ball_scale = 1.f;
        float window_x = 0.f, window_y = 0.f, window_w = 0.f, window_h = 0.f;
    } saved;
};

AppState app;

/* Settings file: one key=value per line, ASCII, no nesting. The kernel writes
 * it with kernel_write and reads it back with kernel_read; it is deliberately
 * not the userspace build's file, because the two describe different features.
 */
constexpr unsigned long kSettingsLimit = 4096;

/* The kernel stack is a few pages for everything: these buffers live here
 * instead. Only the frame worker touches them, one call at a time.
 */
char settings_buffer[kSettingsLimit + 1];

bool Changed()
{
    const aimgui::UiState &ui = app.ui;

    return !app.saved.valid || app.saved.dark != ui.md3_dark ||
           app.saved.haptics != ui.haptics_enabled || app.saved.scale != ui.ui_scale ||
           app.saved.page != ui.nav_page || app.saved.component_page != ui.component_page ||
           app.saved.stage != ui.stage || app.saved.ball_scale != ui.ball_scale ||
           app.saved.window_x != ui.last_full_pos.x ||
           app.saved.window_y != ui.last_full_pos.y ||
           app.saved.window_w != ui.last_full_size.x ||
           app.saved.window_h != ui.last_full_size.y;
}

void Remember()
{
    const aimgui::UiState &ui = app.ui;

    app.saved.valid = true;
    app.saved.dark = ui.md3_dark;
    app.saved.haptics = ui.haptics_enabled;
    app.saved.scale = ui.ui_scale;
    app.saved.page = ui.nav_page;
    app.saved.component_page = ui.component_page;
    app.saved.stage = ui.stage;
    app.saved.ball_scale = ui.ball_scale;
    app.saved.window_x = ui.last_full_pos.x;
    app.saved.window_y = ui.last_full_pos.y;
    app.saved.window_w = ui.last_full_size.x;
    app.saved.window_h = ui.last_full_size.y;
}

/* Scans "key=value" out of one line. Values are read with the module's own
 * strtod: there is no libc here.
 */
void Apply(const char *key, const char *value)
{
    aimgui::UiState &ui = app.ui;
    const double number = qk_strtod(value, nullptr);

    if (!std::strcmp(key, "dark"))
        ui.md3_dark = number != 0;
    else if (!std::strcmp(key, "haptics"))
        ui.haptics_enabled = number != 0;
    else if (!std::strcmp(key, "scale"))
        ui.ui_scale = (float)number;
    else if (!std::strcmp(key, "page"))
        ui.nav_page = (int)number;
    else if (!std::strcmp(key, "component_page"))
        ui.component_page = (int)number;
    else if (!std::strcmp(key, "stage"))
        ui.stage = (int)number;
    else if (!std::strcmp(key, "ball_scale"))
        ui.ball_scale = (float)number;
    else if (!std::strcmp(key, "window_x"))
        ui.last_full_pos.x = (float)number;
    else if (!std::strcmp(key, "window_y"))
        ui.last_full_pos.y = (float)number;
    else if (!std::strcmp(key, "window_w"))
        ui.last_full_size.x = (float)number;
    else if (!std::strcmp(key, "window_h"))
        ui.last_full_size.y = (float)number;
}

void Load(const char *text)
{
    char *cursor;
    unsigned long length = 0;

    if (!text)
        return;
    while (length < kSettingsLimit && text[length])
        length++;
    if (!length)
        return;
    /* Parsing splits the text in place, so it works on this copy. */
    __builtin_memcpy(settings_buffer, text, length);
    settings_buffer[length] = 0;
    for (cursor = settings_buffer; *cursor;) {
        char *line = cursor, *separator;

        while (*cursor && *cursor != '\n')
            cursor++;
        if (*cursor)
            *cursor++ = 0;
        separator = std::strchr(line, '=');
        if (!separator)
            continue;
        *separator = 0;
        Apply(line, separator + 1);
    }
    /* A saved scale or stage outside the supported range is ignored rather
     * than clamped into something the user never chose.
     */
    if (!(app.ui.ui_scale >= 0.f && app.ui.ui_scale <= 3.f))
        app.ui.ui_scale = 0.f;
    if (app.ui.stage < aimgui::UiState::StageIsland ||
        app.ui.stage > aimgui::UiState::StageWindow)
        app.ui.stage = aimgui::UiState::StageWindow;
    if (app.ui.nav_page < 0 || app.ui.nav_page >= aimgui::kPagesCount)
        app.ui.nav_page = 0;
    Remember();
}

} // namespace

/* The Android build buzzes the vibrator through evdev or sysfs from userspace.
 * Nothing here does: the module would have to drive a force-feedback device
 * from inside an input filter, which is not worth the risk for a tap.
 */
namespace aimgui {
namespace haptic {

void Install(Haptics *, const bool *) {}
void Tap() {}
void Step() {}
void Snap() {}
void Heavy() {}

} // namespace haptic
} // namespace aimgui

extern "C" void qk_app_init(int mode, int dark, int scale_percent,
                            const char *settings_path, const char *settings_text)
{
    /* Rebuilt in place: UiState carries a few kilobytes of rects, and
     * assigning a temporary would put all of it on the kernel's stack.
     */
    app.~AppState();
    new (&app) AppState();
    app.mode = mode == QK_APP_GALLERY ? QK_APP_GALLERY : QK_APP_MAIN;
    app.settings_path = settings_path;
    app.ui.md3_dark = dark != 0;
    app.ui.ui_scale = scale_percent > 0 ? (float)scale_percent / 100.f : 0.f;
    app.ui.renderer_name = "kernel software rasteriser";
    /* Everything the Android host provided and the module does not: the UI
     * already knows how to draw itself without them.
     */
    app.ui.glass_available = false;
    app.ui.glass_failed = false;
    app.ui.widget_glass = false;
    app.ui.widget_glass_ok = false;
    app.ui.screen_mirror = false;
    app.ui.screen_mirror_running = false;
    app.ui.block_touch = false;
    app.ui.block_touch_ok = false;
    app.ui.permeate_record = false;
    Load(settings_text);
    app.loaded = true;
}

extern "C" void qk_app_draw(void)
{
    ImGuiIO &io = ImGui::GetIO();

    app.ui.display_w = (int)io.DisplaySize.x;
    app.ui.display_h = (int)io.DisplaySize.y;
    if (app.mode == QK_APP_GALLERY) {
        aimgui::md3::SetTheme(app.ui.md3_dark,
                              app.ui.ui_scale > 0 ? app.ui.ui_scale : 1.f);
        aimgui::md3::PushTheme();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("qimgui-gallery", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
        aimgui::md3::Showcase(-1);
        ImGui::End();
        aimgui::md3::PopTheme();
        return;
    }
    /* The main window: shell, navigation, five pages, dialogs and the
     * collapse stages, exactly as the userspace build draws them.
     */
    aimgui::DrawUi(&app.ui, &app.keep_running);
}

extern "C" int qk_app_wants_exit(void)
{
    return app.keep_running ? 0 : 1;
}

extern "C" int qk_app_save_settings(void)
{
    int length;

    if (!app.settings_path || !app.loaded || !Changed())
        return 0;
    length = std::snprintf(settings_buffer, kSettingsLimit,
                           "dark=%d\nhaptics=%d\nscale=%.3f\npage=%d\n"
                           "component_page=%d\nstage=%d\nball_scale=%.3f\n"
                           "window_x=%.1f\nwindow_y=%.1f\nwindow_w=%.1f\nwindow_h=%.1f\n",
                           app.ui.md3_dark ? 1 : 0, app.ui.haptics_enabled ? 1 : 0,
                           (double)app.ui.ui_scale, app.ui.nav_page,
                           app.ui.component_page, app.ui.stage,
                           (double)app.ui.ball_scale, (double)app.ui.last_full_pos.x,
                           (double)app.ui.last_full_pos.y,
                           (double)app.ui.last_full_size.x,
                           (double)app.ui.last_full_size.y);
    if (length <= 0 || (unsigned long)length >= kSettingsLimit)
        return -22;
    const int ret = qk_file_write(app.settings_path, settings_buffer,
                                  (unsigned long)length);
    if (ret)
        return ret;
    Remember();
    return 1;
}
