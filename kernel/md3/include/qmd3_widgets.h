/* SPDX-License-Identifier: MIT */
#ifndef QMD3_WIDGETS_H
#define QMD3_WIDGETS_H

#include "qmd3.h"

enum qm_button_style {
    QM_BUTTON_FILLED, QM_BUTTON_TONAL, QM_BUTTON_OUTLINED,
    QM_BUTTON_TEXT, QM_BUTTON_ELEVATED
};
enum qm_icon_button_style {
    QM_ICON_STANDARD, QM_ICON_FILLED, QM_ICON_TONAL, QM_ICON_OUTLINED
};
enum qm_fab_size { QM_FAB_SMALL, QM_FAB_MEDIUM, QM_FAB_LARGE };
enum qm_chip_style { QM_CHIP_ASSIST, QM_CHIP_FILTER, QM_CHIP_INPUT, QM_CHIP_SUGGESTION };
enum qm_text_field_style { QM_TEXT_FILLED, QM_TEXT_OUTLINED };

/* All positions/sizes are integer pixels. Rectangles bound both drawing and
 * interaction. IDs are caller-owned, nonzero and stable across frames. State
 * remains with the caller; none of these controls allocates memory. Disabled
 * controls never change caller data or take focus. */
bool qm_button(struct qm_context *ctx, qm_u32 id, struct qm_rect rect,
               const char *label, enum qm_button_style style, bool enabled);
bool qm_icon_button(struct qm_context *ctx, qm_u32 id, struct qm_rect rect,
                    const char *icon, enum qm_icon_button_style style,
                    bool *selected, bool enabled);
bool qm_fab(struct qm_context *ctx, qm_u32 id, struct qm_rect rect,
            const char *icon, const char *label, enum qm_fab_size size, bool enabled);
bool qm_checkbox(struct qm_context *ctx, qm_u32 id, struct qm_rect rect,
                 const char *label, bool *value, bool mixed, bool enabled);
bool qm_radio(struct qm_context *ctx, qm_u32 id, struct qm_rect rect,
              const char *label, int *value, int option, bool enabled);
bool qm_switch(struct qm_context *ctx, qm_u32 id, struct qm_rect rect,
               const char *label, bool *value, bool enabled);

/* remove_id is required only for a removable input chip and must be distinct
 * from every other live ID (including id). A removed chip is no longer drawn
 * until the caller resets *removed. Deletion uses its separate trailing button
 * or the Delete key while the chip has focus. */
bool qm_chip(struct qm_context *ctx, qm_u32 id, qm_u32 remove_id,
             struct qm_rect rect, const char *label, enum qm_chip_style style,
             bool *selected, bool *removed, bool enabled);

/* min/max may span the entire signed-int domain. steps=0 gives integer steps;
 * 1..1024 divides the domain into equal intervals, rounded to nearest integer.
 * Invalid domains and step counts are rejected. Merely drawing/focusing a
 * slider does not clamp or snap caller data. Values change only on interaction.
 * Home/End select endpoints; arrows move one tick (one integer for steps=0). */
bool qm_slider(struct qm_context *ctx, qm_u32 id, struct qm_rect rect,
               const char *label, int *value, int minimum, int maximum,
               unsigned int steps, bool enabled);
struct qm_range_state { int active_thumb; }; /* initialize to 0 */
bool qm_range_slider(struct qm_context *ctx, qm_u32 id, struct qm_rect rect,
                     const char *label, int *lower, int *upper,
                     int minimum, int maximum, unsigned int steps,
                     struct qm_range_state *state, bool enabled);

/* UTF-8 byte offset at a codepoint boundary. Initialize both fields to zero.
 * scroll_px is maintained by the field so long lines retain a visible caret. */
struct qm_text_state { unsigned int caret; int scroll_px; };
/* capacity includes the terminating NUL and must be 1..4096. Malformed UTF-8,
 * missing terminators and invalid caret positions are handled without reading
 * beyond capacity. The single-line field supports insertion, arrows, Home/End,
 * Backspace/Delete and pointer positioning. Enter/Escape leave editing focus.
 * Incoming text is supplied by the caller through ctx->input.text; this is
 * independent of any OS keyboard/IME service. No selection/clipboard is owned
 * by this layer. Returns true only when buffer contents change. */
bool qm_text_field(struct qm_context *ctx, qm_u32 id, struct qm_rect rect,
                   const char *label, char *buffer, unsigned int capacity,
                   struct qm_text_state *state, enum qm_text_field_style style,
                   const char *supporting, bool error, bool enabled);

#endif
