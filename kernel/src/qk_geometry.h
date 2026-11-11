/* SPDX-License-Identifier: MIT */
/* Window placement on the panel, touch coordinate mapping, and the per-contact
 * decision of who owns a gesture.
 *
 * This is the part of the input path that has no kernel dependencies, so it is
 * tested on the host. The plumbing around it -- the input handler, the filter
 * and the replay of contacts that belong to Android -- lives in qk_input.c.
 */
#ifndef QK_GEOMETRY_H
#define QK_GEOMETRY_H

#include "qk_raster.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The MT protocol allows more, but a UI needs one pointer and a little slack. */
#define QK_MAX_SLOTS 10

enum qk_slot_decision {
    QK_SLOT_FREE = 0,   /* No contact in this slot. */
    QK_SLOT_CLAIMED,    /* Started inside the window: the UI owns the gesture. */
    QK_SLOT_PASSED,     /* Started outside: Android owns it, start to finish. */
};

struct qk_window {
    s32 panel_width, panel_height;
    s32 x, y, width, height;        /* Window rect in panel pixels. */
    s32 device_max_x, device_max_y; /* Touch device range; 0 means pixels already. */
    u8 swap_xy, invert_x, invert_y; /* Panel-to-digitiser orientation. */
    u8 grab;                        /* Take gestures that start inside the window. */
};

struct qk_slots {
    u8 decision[QK_MAX_SLOTS];
    s32 x[QK_MAX_SLOTS], y[QK_MAX_SLOTS]; /* Last panel position. */
    s32 primary;                          /* Slot the UI pointer follows, or -1. */
};

/* Places the window, clamping it inside the panel. Returns 0, or -EINVAL. */
int qk_window_place(struct qk_window *window, s32 x, s32 y, s32 width, s32 height);
/* Digitiser coordinates to panel pixels, honouring range and orientation. */
void qk_window_map(const struct qk_window *window, s32 device_x, s32 device_y,
                   s32 *panel_x, s32 *panel_y);
int qk_window_contains(const struct qk_window *window, s32 panel_x, s32 panel_y);
/* Panel pixels to window-local UI coordinates, clamped to the window. */
void qk_window_local(const struct qk_window *window, s32 panel_x, s32 panel_y,
                     s32 *local_x, s32 *local_y);

void qk_slots_reset(struct qk_slots *slots);
/* A contact appears. Returns QK_SLOT_CLAIMED or QK_SLOT_PASSED; the decision
 * holds for the whole gesture, even if it later leaves the window.
 */
int qk_slots_begin(struct qk_slots *slots, const struct qk_window *window, int slot,
                   s32 device_x, s32 device_y);
/* A contact moves. Returns its standing decision, or QK_SLOT_FREE if unknown. */
int qk_slots_move(struct qk_slots *slots, const struct qk_window *window, int slot,
                  s32 device_x, s32 device_y);
/* A contact lifts. Returns the decision that was in force. */
int qk_slots_release(struct qk_slots *slots, int slot);
/* Panel position of the slot the UI follows; 0 if there is none. */
int qk_slots_position(const struct qk_slots *slots, int slot, s32 *panel_x, s32 *panel_y);

#ifdef __cplusplus
}
#endif

#endif /* QK_GEOMETRY_H */
