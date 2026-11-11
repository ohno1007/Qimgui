/* SPDX-License-Identifier: MIT */
/* The kernel-side UI: one ImGui context, the MD3 component gallery, and the
 * translation from ImGui's draw lists into the integer draw data the software
 * rasteriser consumes.
 *
 * Everything behind this interface uses floating point, so qk_ui_frame() must
 * be called inside the region qk_fpu.h opens. qk_raster_draw() on the returned
 * data must be called outside it. Call qk_arena_init() first: ImGui allocates
 * from the arena, not from the kernel allocators.
 */
#ifndef QK_UI_H
#define QK_UI_H

#include "qk_raster.h"

#ifdef __cplusplus
extern "C" {
#endif

enum qk_event_type {
    QK_EVENT_POINTER_DOWN = 1,
    QK_EVENT_POINTER_MOVE,
    QK_EVENT_POINTER_UP,
    QK_EVENT_KEY,
    QK_EVENT_SCROLL,
};

struct qk_ui_event {
    u32 type;      /* enum qk_event_type */
    s32 x, y;      /* Window-local pixels, for pointer and scroll events. */
    u32 code;      /* Linux key code for QK_EVENT_KEY. */
    u32 down;      /* Key or button state. */
};

struct qk_ui_config {
    s32 width, height;      /* Window size in pixels. */
    s32 dark;               /* Start in the dark palette. */
    s32 scale_percent;      /* UI scale; 100 is 1x. 0 follows the display. */
    s32 mode;               /* enum qk_app_mode: main window or gallery. */
    const char *settings_path;  /* NULL keeps settings in memory only. */
    const char *settings_text;  /* The settings file, already read by the caller. */
    /* A font read from the device, merged over the built-in Latin subset so
     * the interface can show Chinese. The buffer must outlive the context.
     */
    const void *extra_font;
    s32 extra_font_size;
};

struct qk_ui_stats {
    u32 vertices, indices, commands, textures;
    u64 frames;
    u32 arena_peak;
};

int qk_ui_init(const struct qk_ui_config *config);
void qk_ui_shutdown(void);
int qk_ui_resize(s32 width, s32 height);
/* Feeds the events, runs one frame, and publishes the draw data. The data
 * stays valid until the next qk_ui_frame() or qk_ui_shutdown().
 */
int qk_ui_frame(u64 elapsed_us, const struct qk_ui_event *events, u32 count,
                const struct qk_draw_data **data);
void qk_ui_get_stats(struct qk_ui_stats *stats);
/* True once the user has closed the window from the UI itself. */
int qk_ui_wants_exit(void);

#ifdef __cplusplus
}
#endif

#endif /* QK_UI_H */
