/* SPDX-License-Identifier: MIT */
#include "qk_geometry.h"

static s32 qk_clamp(s32 value, s32 low, s32 high)
{
    return value < low ? low : (value > high ? high : value);
}

int qk_window_place(struct qk_window *window, s32 x, s32 y, s32 width, s32 height)
{
    if (!window || window->panel_width <= 0 || window->panel_height <= 0)
        return -22;
    if (width <= 0 || height <= 0 || width > QK_MAX_DIMENSION ||
        height > QK_MAX_DIMENSION)
        return -22;
    if (width > window->panel_width || height > window->panel_height)
        return -22;
    window->width = width;
    window->height = height;
    window->x = qk_clamp(x, 0, window->panel_width - width);
    window->y = qk_clamp(y, 0, window->panel_height - height);
    return 0;
}

void qk_window_map(const struct qk_window *window, s32 device_x, s32 device_y,
                   s32 *panel_x, s32 *panel_y)
{
    s32 x = device_x, y = device_y, swap;

    if (window->swap_xy) {
        swap = x;
        x = y;
        y = swap;
    }
    /* A digitiser with its own resolution is scaled to the panel; one that
     * already reports pixels (max 0) is taken as is.
     */
    if (window->device_max_x > 0) {
        x = qk_clamp(x, 0, window->device_max_x);
        x = (s32)(((long long)x * (window->panel_width - 1)) / window->device_max_x);
    }
    if (window->device_max_y > 0) {
        y = qk_clamp(y, 0, window->device_max_y);
        y = (s32)(((long long)y * (window->panel_height - 1)) / window->device_max_y);
    }
    if (window->invert_x)
        x = window->panel_width - 1 - x;
    if (window->invert_y)
        y = window->panel_height - 1 - y;
    *panel_x = qk_clamp(x, 0, window->panel_width - 1);
    *panel_y = qk_clamp(y, 0, window->panel_height - 1);
}

int qk_window_contains(const struct qk_window *window, s32 panel_x, s32 panel_y)
{
    return panel_x >= window->x && panel_x < window->x + window->width &&
           panel_y >= window->y && panel_y < window->y + window->height;
}

void qk_window_local(const struct qk_window *window, s32 panel_x, s32 panel_y,
                     s32 *local_x, s32 *local_y)
{
    *local_x = qk_clamp(panel_x - window->x, 0, window->width - 1);
    *local_y = qk_clamp(panel_y - window->y, 0, window->height - 1);
}

void qk_slots_reset(struct qk_slots *slots)
{
    int slot;

    for (slot = 0; slot < QK_MAX_SLOTS; slot++) {
        slots->decision[slot] = QK_SLOT_FREE;
        slots->x[slot] = 0;
        slots->y[slot] = 0;
    }
    slots->primary = -1;
}

static int qk_slot_valid(int slot)
{
    return slot >= 0 && slot < QK_MAX_SLOTS;
}

int qk_slots_begin(struct qk_slots *slots, const struct qk_window *window, int slot,
                   s32 device_x, s32 device_y)
{
    s32 panel_x, panel_y;

    if (!qk_slot_valid(slot))
        return QK_SLOT_PASSED;
    qk_window_map(window, device_x, device_y, &panel_x, &panel_y);
    slots->x[slot] = panel_x;
    slots->y[slot] = panel_y;
    if (window->grab && qk_window_contains(window, panel_x, panel_y)) {
        slots->decision[slot] = QK_SLOT_CLAIMED;
        /* One pointer drives the UI: the first contact to arrive keeps it. */
        if (slots->primary < 0)
            slots->primary = slot;
    } else {
        slots->decision[slot] = QK_SLOT_PASSED;
    }
    return slots->decision[slot];
}

int qk_slots_move(struct qk_slots *slots, const struct qk_window *window, int slot,
                  s32 device_x, s32 device_y)
{
    s32 panel_x, panel_y;

    if (!qk_slot_valid(slot) || slots->decision[slot] == QK_SLOT_FREE)
        return QK_SLOT_FREE;
    qk_window_map(window, device_x, device_y, &panel_x, &panel_y);
    /* A claimed gesture keeps its pointer even once it leaves the window:
     * dragging a slider past the edge must not hand the finger to Android.
     */
    slots->x[slot] = panel_x;
    slots->y[slot] = panel_y;
    return slots->decision[slot];
}

int qk_slots_release(struct qk_slots *slots, int slot)
{
    int decision;

    if (!qk_slot_valid(slot))
        return QK_SLOT_FREE;
    decision = slots->decision[slot];
    slots->decision[slot] = QK_SLOT_FREE;
    if (slots->primary == slot) {
        int candidate;

        slots->primary = -1;
        for (candidate = 0; candidate < QK_MAX_SLOTS; candidate++)
            if (slots->decision[candidate] == QK_SLOT_CLAIMED) {
                slots->primary = candidate;
                break;
            }
    }
    return decision;
}

int qk_slots_position(const struct qk_slots *slots, int slot, s32 *panel_x, s32 *panel_y)
{
    if (!qk_slot_valid(slot) || slots->decision[slot] == QK_SLOT_FREE)
        return 0;
    *panel_x = slots->x[slot];
    *panel_y = slots->y[slot];
    return 1;
}
