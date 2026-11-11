/* SPDX-License-Identifier: MIT */
/* Touch and key input straight from the kernel input subsystem: there is no
 * userspace process to read /dev/input any more.
 *
 * A gesture that starts inside the window belongs to the UI and is withheld
 * from Android for its whole life; everything else is replayed to the input
 * core untouched. Keys are observed only, never withheld.
 */
#ifndef QK_INPUT_H
#define QK_INPUT_H

#include "qk_geometry.h"
#include "qk_ui.h"

struct seq_file;

/* The window is shared with the display side; the filter reads it in atomic
 * context, so it is updated only between frames.
 */
int qk_input_init(struct qk_window *window);
void qk_input_exit(void);
/* Moves up to max events into the caller's array. Worker context. */
u32 qk_input_drain(struct qk_ui_event *events, u32 max);
void qk_input_status(struct seq_file *seq);

#endif /* QK_INPUT_H */
