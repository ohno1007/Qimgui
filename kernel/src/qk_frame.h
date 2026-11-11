/* SPDX-License-Identifier: MIT */
/* The frame loop: drain input, run the UI with floating point enabled, raster
 * the result with integers, and publish it through the DRM plane.
 */
#ifndef QK_FRAME_H
#define QK_FRAME_H

#include "qk_geometry.h"

struct seq_file;

struct qk_frame_config {
    unsigned int arena_mb;
    unsigned int interval_ms, idle_ms;
    int mode;                   /* enum qk_app_mode */
    int dark;
    int scale_percent;
    const char *settings_path;  /* NULL keeps settings in memory only. */
    const void *font;           /* A device font merged over the built-in one. */
    int font_size;
};

int qk_frame_start(struct qk_window *window, const struct qk_frame_config *config);
void qk_frame_stop(void);
/* Runs the worker now: called when input arrives or a parameter changes. */
void qk_frame_kick(void);
void qk_frame_status(struct seq_file *seq);

#endif /* QK_FRAME_H */
