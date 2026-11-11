#ifndef QMD3_LAYOUT_H
#define QMD3_LAYOUT_H

#include "qmd3.h"

/* Integer software-renderer composition. Rectangles are pixels; applications
 * own state. Arrays accept 1..32 globally unique nonzero IDs; invalid/repeated
 * IDs reject a group. Icons accept @alarm/@clock/... tokens or short text. */
struct qm_item {
    qm_u32 id;
    const char *label;
    const char *icon;
    int disabled;
    int badge; /* zero: none; negative: dot; positive: count */
};
enum qm_navigation_style { QM_NAV_RAIL, QM_NAV_BAR, QM_NAV_DRAWER };
enum qm_card_style { QM_CARD_FILLED, QM_CARD_OUTLINED, QM_CARD_ELEVATED };
enum qm_group_style { QM_GROUP_FILLED, QM_GROUP_TONAL, QM_GROUP_OUTLINED };
enum qm_sheet_side { QM_SHEET_BOTTOM, QM_SHEET_LEFT, QM_SHEET_RIGHT };
enum qm_split_action { QM_SPLIT_NONE, QM_SPLIT_PRIMARY, QM_SPLIT_MENU };
struct qm_stack {
    struct qm_rect bounds;
    int offset, gap, vertical;
};
/* An exhausted row/column returns an empty rectangle. */
void qm_stack_init(struct qm_stack *stack, struct qm_rect bounds,
                   int vertical, int gap);
struct qm_rect qm_stack_next(struct qm_stack *stack, int extent);
int qm_navigation(struct qm_context *ctx, struct qm_rect rect,
                  const struct qm_item *items, int count, int *selected,
                  enum qm_navigation_style style);
int qm_tabs(struct qm_context *ctx, struct qm_rect rect,
            const struct qm_item *items, int count, int *selected);
int qm_segmented(struct qm_context *ctx, struct qm_rect rect,
                 const struct qm_item *items, int count, int *selected);
int qm_multi_segmented(struct qm_context *ctx, struct qm_rect rect,
                       const struct qm_item *items, int count,
                       qm_u32 *selected, int allow_empty);
int qm_button_group(struct qm_context *ctx, struct qm_rect rect,
                    const struct qm_item *items, int count, int *selected,
                    enum qm_group_style style);
int qm_multi_button_group(struct qm_context *ctx, struct qm_rect rect,
                          const struct qm_item *items, int count,
                          qm_u32 *selected, int allow_empty,
                          enum qm_group_style style);
/* Returns action index or -1. Optional mask toggles independent actions;
 * bits outside the group are retained. Arrows move keyboard focus only. */
int qm_toolbar(struct qm_context *ctx, struct qm_rect rect,
               const struct qm_item *items, int count, qm_u32 *toggles,
               int vertical, int vibrant);
enum qm_split_action qm_split_button(struct qm_context *ctx,
    struct qm_rect rect, qm_u32 primary_id, qm_u32 menu_id,
    const char *label, int *expanded, int enabled);
/* Elevation uses tonal color. There is no shadow/bloom renderer. */
void qm_card_surface(struct qm_context *ctx, struct qm_rect rect,
                     enum qm_card_style style);
int qm_card(struct qm_context *ctx, qm_u32 id, struct qm_rect rect,
            const char *headline, const char *supporting,
            enum qm_card_style style, int enabled);
int qm_list_item(struct qm_context *ctx, qm_u32 id, struct qm_rect rect,
                 const char *headline, const char *supporting,
                 const char *trailing, int selected, int enabled);
void qm_divider(struct qm_context *ctx, struct qm_rect rect, int vertical);
void qm_badge(struct qm_context *ctx, int center_x, int center_y, int count);
/* Progress is 0..1000; negative is indeterminate. Caller-owned phase uses
 * 1/1024 turns. A constant phase supports reduced motion. No timers installed. */
void qm_linear_progress(struct qm_context *ctx, struct qm_rect rect,
                        int value_per_mille, unsigned phase, int wavy);
void qm_circular_progress(struct qm_context *ctx, struct qm_rect rect,
                          int value_per_mille, unsigned phase, int wavy);
/* Controlled panels. While open, draw background controls with input disabled,
 * then enable input for panel contents. No implicit popup stack is maintained.
 * Escape/outside press closes dismissible panels. content is the inner rect;
 * no clip-stack entry is retained by these functions. */
int qm_dialog_panel(struct qm_context *ctx, struct qm_rect viewport,
                    struct qm_rect rect, const char *title, int *open,
                    int dismissible, struct qm_rect *content);
int qm_sheet_panel(struct qm_context *ctx, struct qm_rect viewport,
                   enum qm_sheet_side side, int extent, int *open,
                   int dismissible, struct qm_rect *content);
/* Remaining milliseconds: zero persistent; hover/focus pauses. Returns action
 * activation. Action/dismiss IDs must be distinct and nonzero when present. */
int qm_snackbar(struct qm_context *ctx, struct qm_rect rect,
                const char *message, const char *action,
                qm_u32 action_id, qm_u32 dismiss_id, int *open,
                qm_u32 *remaining_ms, int dismissible);

#endif
