/* SPDX-License-Identifier: MIT */
#include "qmd3_widgets.h"
#include "qmd3_assets.h"

#define QM_TEXT_CAPACITY 4096U

static int min_i(int a, int b) { return a < b ? a : b; }
static int max_i(int a, int b) { return a > b ? a : b; }
static int clamp_i(int value, int lo, int hi)
{
    return value < lo ? lo : (value > hi ? hi : value);
}
static qm_color faded(qm_color color, unsigned int alpha)
{
    return (color & 0x00ffffffU) |
           ((((color >> 24) * alpha + 127U) / 255U) << 24);
}
static bool valid_rect(struct qm_rect rect)
{
    return rect.x >= -32768 && rect.x <= 32767 &&
           rect.y >= -32768 && rect.y <= 32767 &&
           rect.w > 0 && rect.w <= 4096 && rect.h > 0 && rect.h <= 4096;
}
static bool begin_item(struct qm_context *ctx, qm_u32 id, struct qm_rect rect,
                       bool enabled, unsigned int *flags)
{
    if (!ctx || !id || !valid_rect(rect) || !qm_clip_push(ctx, rect))
        return false;
    *flags = qm_interact(ctx, id, rect, enabled);
    return true;
}
static void centered_text(struct qm_context *ctx, struct qm_rect rect,
                          const char *text, qm_color color)
{
    if (text)
        qm_text(ctx, rect.x + (rect.w - qm_text_width(ctx, text)) / 2,
                rect.y + (rect.h - qm_font_height(ctx)) / 2, text, color);
}
static void centered_icon(struct qm_context *ctx, struct qm_rect rect,
                          const char *token, int diameter, qm_color color)
{
    enum qm_icon icon;
    if (token && qm_icon_from_token(token, &icon)) {
        struct qm_rect box = {rect.x + (rect.w - diameter) / 2,
                              rect.y + (rect.h - diameter) / 2,
                              diameter, diameter};
        qm_draw_icon(ctx, icon, box, color);
    } else {
        centered_text(ctx, rect, token, color);
    }
}
static void state_layer(struct qm_context *ctx, struct qm_rect rect,
                        int radius, unsigned int flags, qm_color foreground,
                        bool enabled)
{
    if (!enabled)
        return;
    if (flags & QM_HELD)
        qm_round_rect(ctx, rect, radius, faded(foreground, 31));
    else if (flags & QM_HOVERED)
        qm_round_rect(ctx, rect, radius, faded(foreground, 20));
    if (flags & QM_FOCUSED)
        qm_stroke_rect(ctx, rect, radius, 2, ctx->theme.primary);
}
static void draw_check(struct qm_context *ctx, int x, int y, qm_color color)
{
    qm_line(ctx, x - 5, y, x - 1, y + 4, 2, color);
    qm_line(ctx, x - 1, y + 4, x + 6, y - 4, 2, color);
}
static void draw_close(struct qm_context *ctx, int x, int y, qm_color color)
{
    qm_line(ctx, x - 4, y - 4, x + 4, y + 4, 2, color);
    qm_line(ctx, x + 4, y - 4, x - 4, y + 4, 2, color);
}

bool qm_button(struct qm_context *ctx, qm_u32 id, struct qm_rect rect,
               const char *label, enum qm_button_style style, bool enabled)
{
    unsigned int flags;
    qm_color background, foreground;
    int radius = rect.h / 2;
    if (style < QM_BUTTON_FILLED || style > QM_BUTTON_ELEVATED ||
        !begin_item(ctx, id, rect, enabled, &flags))
        return false;
    background = 0;
    foreground = ctx->theme.primary;
    switch (style) {
    case QM_BUTTON_FILLED:
        background = ctx->theme.primary;
        foreground = ctx->theme.on_primary;
        break;
    case QM_BUTTON_TONAL:
        background = ctx->theme.secondary_container;
        foreground = ctx->theme.on_secondary_container;
        break;
    case QM_BUTTON_ELEVATED:
        background = ctx->theme.surface_container;
        break;
    default: break;
    }
    if (!enabled) {
        foreground = faded(ctx->theme.on_surface, 97);
        if (background)
            background = faded(ctx->theme.on_surface, 31);
    }
    if (background)
        qm_round_rect(ctx, rect, radius, background);
    if (style == QM_BUTTON_OUTLINED)
        qm_stroke_rect(ctx, rect, radius, 1,
                       enabled ? ctx->theme.outline : faded(ctx->theme.on_surface, 31));
    state_layer(ctx, rect, radius, flags, foreground, enabled);
    centered_text(ctx, rect, label, foreground);
    qm_clip_pop(ctx);
    return (flags & QM_ACTIVATED) != 0;
}

bool qm_icon_button(struct qm_context *ctx, qm_u32 id, struct qm_rect rect,
                    const char *icon, enum qm_icon_button_style style,
                    bool *selected, bool enabled)
{
    unsigned int flags;
    qm_color background, foreground;
    bool on;
    int radius = min_i(rect.w, rect.h) / 2;
    if (style < QM_ICON_STANDARD || style > QM_ICON_OUTLINED ||
        !begin_item(ctx, id, rect, enabled, &flags))
        return false;
    if ((flags & QM_ACTIVATED) && selected)
        *selected = !*selected;
    on = !selected || *selected;
    background = 0;
    foreground = on ? ctx->theme.primary : ctx->theme.on_surface_variant;
    if (style == QM_ICON_FILLED) {
        background = on ? ctx->theme.primary : ctx->theme.surface_container_high;
        foreground = on ? ctx->theme.on_primary : ctx->theme.primary;
    } else if (style == QM_ICON_TONAL) {
        background = on ? ctx->theme.secondary_container : ctx->theme.surface_container_high;
        foreground = on ? ctx->theme.on_secondary_container : ctx->theme.on_surface_variant;
    } else if (style == QM_ICON_OUTLINED && on && selected) {
        background = ctx->theme.inverse_surface;
        foreground = ctx->theme.inverse_on_surface;
    }
    if (!enabled) {
        foreground = faded(ctx->theme.on_surface, 97);
        if (background)
            background = faded(ctx->theme.on_surface, 31);
    }
    if (background)
        qm_round_rect(ctx, rect, radius, background);
    if (style == QM_ICON_OUTLINED)
        qm_stroke_rect(ctx, rect, radius, 1,
                       enabled ? ctx->theme.outline : faded(ctx->theme.on_surface, 31));
    state_layer(ctx, rect, radius, flags, foreground, enabled);
    centered_icon(ctx, rect, icon, 24, foreground);
    qm_clip_pop(ctx);
    return (flags & QM_ACTIVATED) != 0;
}

bool qm_fab(struct qm_context *ctx, qm_u32 id, struct qm_rect rect,
            const char *icon, const char *label, enum qm_fab_size size, bool enabled)
{
    unsigned int flags;
    qm_color background, foreground;
    int radius;
    if (size < QM_FAB_SMALL || size > QM_FAB_LARGE ||
        !begin_item(ctx, id, rect, enabled, &flags))
        return false;
    radius = size == QM_FAB_SMALL ? 12 : (size == QM_FAB_LARGE ? 28 : 16);
    background = enabled ? ctx->theme.primary_container : faded(ctx->theme.on_surface, 31);
    foreground = enabled ? ctx->theme.on_primary_container : faded(ctx->theme.on_surface, 97);
    qm_round_rect(ctx, rect, radius, background);
    state_layer(ctx, rect, radius, flags, foreground, enabled);
    if (label && *label) {
        struct qm_rect icon_rect = {rect.x, rect.y, min_i(rect.w, 48), rect.h};
        struct qm_rect text_rect = {rect.x + 48, rect.y, max_i(0, rect.w - 60), rect.h};
        centered_icon(ctx, icon_rect, icon, 24, foreground);
        centered_text(ctx, text_rect, label, foreground);
    } else {
        centered_icon(ctx, rect, icon, size == QM_FAB_LARGE ? 36 : 24, foreground);
    }
    qm_clip_pop(ctx);
    return (flags & QM_ACTIVATED) != 0;
}

bool qm_checkbox(struct qm_context *ctx, qm_u32 id, struct qm_rect rect,
                 const char *label, bool *value, bool mixed, bool enabled)
{
    unsigned int flags;
    qm_color foreground, mark;
    struct qm_rect box, highlight;
    int x, y;
    if (!value || !begin_item(ctx, id, rect, enabled, &flags))
        return false;
    if (flags & QM_ACTIVATED)
        *value = mixed ? true : !*value;
    x = rect.x + 24;
    y = rect.y + rect.h / 2;
    foreground = (*value || mixed) ? ctx->theme.primary : ctx->theme.on_surface_variant;
    if (!enabled)
        foreground = faded(ctx->theme.on_surface, 97);
    mark = enabled ? ctx->theme.on_primary : ctx->theme.surface;
    box = (struct qm_rect){x - 9, y - 9, 18, 18};
    highlight = (struct qm_rect){x - 20, y - 20, 40, 40};
    state_layer(ctx, highlight, 20, flags, foreground, enabled);
    if (*value || mixed)
        qm_round_rect(ctx, box, 2, foreground);
    else
        qm_stroke_rect(ctx, box, 2, 2, foreground);
    if (mixed)
        qm_line(ctx, x - 5, y, x + 5, y, 2, mark);
    else if (*value)
        draw_check(ctx, x, y, mark);
    qm_text(ctx, rect.x + 52, y - qm_font_height(ctx) / 2, label,
            enabled ? ctx->theme.on_surface : faded(ctx->theme.on_surface, 97));
    qm_clip_pop(ctx);
    return (flags & QM_ACTIVATED) != 0;
}

bool qm_radio(struct qm_context *ctx, qm_u32 id, struct qm_rect rect,
              const char *label, int *value, int option, bool enabled)
{
    unsigned int flags;
    qm_color foreground;
    struct qm_rect circle, highlight;
    bool changed;
    int x, y;
    if (!value || !begin_item(ctx, id, rect, enabled, &flags))
        return false;
    changed = (flags & QM_ACTIVATED) && *value != option;
    if (flags & QM_ACTIVATED)
        *value = option;
    foreground = *value == option ? ctx->theme.primary : ctx->theme.on_surface_variant;
    if (!enabled)
        foreground = faded(ctx->theme.on_surface, 97);
    x = rect.x + 24;
    y = rect.y + rect.h / 2;
    circle = (struct qm_rect){x - 10, y - 10, 20, 20};
    highlight = (struct qm_rect){x - 20, y - 20, 40, 40};
    state_layer(ctx, highlight, 20, flags, foreground, enabled);
    qm_stroke_rect(ctx, circle, 10, 2, foreground);
    if (*value == option)
        qm_circle(ctx, x, y, 5, foreground);
    qm_text(ctx, rect.x + 52, y - qm_font_height(ctx) / 2, label,
            enabled ? ctx->theme.on_surface : faded(ctx->theme.on_surface, 97));
    qm_clip_pop(ctx);
    return changed;
}

bool qm_switch(struct qm_context *ctx, qm_u32 id, struct qm_rect rect,
               const char *label, bool *value, bool enabled)
{
    unsigned int flags;
    struct qm_rect track, highlight;
    qm_color background, thumb, outline;
    int y, x;
    if (!value || !begin_item(ctx, id, rect, enabled, &flags))
        return false;
    if (flags & QM_ACTIVATED)
        *value = !*value;
    y = rect.y + rect.h / 2;
    track = (struct qm_rect){rect.x + 2, y - 16, 52, 32};
    x = track.x + (*value ? 36 : 16);
    highlight = (struct qm_rect){x - 20, y - 20, 40, 40};
    background = *value ? ctx->theme.primary : ctx->theme.surface_container_high;
    thumb = *value ? ctx->theme.on_primary : ctx->theme.outline;
    outline = ctx->theme.outline;
    if (!enabled) {
        background = faded(ctx->theme.on_surface, 31);
        thumb = faded(ctx->theme.on_surface, 97);
        outline = faded(ctx->theme.on_surface, 31);
    }
    qm_round_rect(ctx, track, 16, background);
    if (!*value)
        qm_stroke_rect(ctx, track, 16, 2, outline);
    state_layer(ctx, highlight, 20, flags, ctx->theme.primary, enabled);
    qm_circle(ctx, x, y, *value ? 12 : 8, thumb);
    if (*value)
        draw_check(ctx, x, y, enabled ? ctx->theme.primary : ctx->theme.surface);
    qm_text(ctx, rect.x + 66, y - qm_font_height(ctx) / 2, label,
            enabled ? ctx->theme.on_surface : faded(ctx->theme.on_surface, 97));
    qm_clip_pop(ctx);
    return (flags & QM_ACTIVATED) != 0;
}

bool qm_chip(struct qm_context *ctx, qm_u32 id, qm_u32 remove_id,
             struct qm_rect rect, const char *label, enum qm_chip_style style,
             bool *selected, bool *removed, bool enabled)
{
    unsigned int flags, remove_flags = 0;
    bool removable, chosen, changed;
    qm_color background, foreground;
    struct qm_rect body, removal;
    if (!ctx || !id || !valid_rect(rect) || style < QM_CHIP_ASSIST ||
        style > QM_CHIP_SUGGESTION || (removed && *removed))
        return false;
    removable = style == QM_CHIP_INPUT && removed;
    if (removable && (!remove_id || remove_id == id || rect.w < 48))
        return false;
    if (!qm_clip_push(ctx, rect))
        return false;
    body = rect;
    removal = (struct qm_rect){rect.x + rect.w - 28, rect.y, 28, rect.h};
    if (removable) {
        body.w -= 28;
        remove_flags = qm_interact(ctx, remove_id, removal, enabled);
    }
    flags = qm_interact(ctx, id, body, enabled);
    changed = (flags & QM_ACTIVATED) != 0;
    if (changed && selected && (style == QM_CHIP_FILTER || style == QM_CHIP_INPUT))
        *selected = !*selected;
    if (removable && enabled && ((remove_flags & QM_ACTIVATED) ||
        ((flags & QM_FOCUSED) && ctx->input.key == QM_KEY_DELETE))) {
        *removed = true;
        changed = true;
    }
    chosen = selected && *selected;
    background = chosen ? ctx->theme.secondary_container : 0;
    foreground = chosen ? ctx->theme.on_secondary_container : ctx->theme.on_surface_variant;
    if (!enabled) {
        foreground = faded(ctx->theme.on_surface, 97);
        if (background)
            background = faded(ctx->theme.on_surface, 31);
    }
    if (background)
        qm_round_rect(ctx, rect, 8, background);
    else
        qm_stroke_rect(ctx, rect, 8, 1,
                       enabled ? ctx->theme.outline : faded(ctx->theme.on_surface, 31));
    state_layer(ctx, rect, 8, flags | remove_flags, foreground, enabled);
    if (style == QM_CHIP_FILTER && chosen) {
        draw_check(ctx, body.x + 16, body.y + body.h / 2, foreground);
        body.x += 24;
        body.w = max_i(0, body.w - 24);
    }
    if (body.w > 0 && qm_clip_push(ctx, body)) {
        centered_text(ctx, body, label, foreground);
        qm_clip_pop(ctx);
    }
    if (removable)
        draw_close(ctx, removal.x + removal.w / 2, removal.y + removal.h / 2, foreground);
    qm_clip_pop(ctx);
    return changed;
}

static int slider_x(struct qm_rect rect, int value, int minimum, int maximum)
{
    qm_s64 span = (qm_s64)maximum - minimum;
    qm_s64 position = (qm_s64)clamp_i(value, minimum, maximum) - minimum;
    return rect.x + 12 + (int)((position * (rect.w - 24) + span / 2) / span);
}
static int snapped_value(qm_s64 position, qm_s64 span, unsigned int steps,
                         int minimum)
{
    qm_s64 tick;
    if (position < 0)
        position = 0;
    if (position > span)
        position = span;
    if (steps) {
        tick = (position * steps + span / 2) / span;
        position = (tick * span + steps / 2) / steps;
    }
    return (int)((qm_s64)minimum + position);
}
static int pointer_value(struct qm_context *ctx, struct qm_rect rect,
                          int minimum, int maximum, unsigned int steps)
{
    qm_s64 span = (qm_s64)maximum - minimum;
    qm_s64 delta = (qm_s64)ctx->input.x - rect.x - 12;
    int width = rect.w - 24;
    if (delta < 0)
        delta = 0;
    if (delta > width)
        delta = width;
    return snapped_value((delta * span + width / 2) / width, span, steps, minimum);
}
static bool keyboard_slider(struct qm_context *ctx, int current,
                             int minimum, int maximum, unsigned int steps,
                             int *next)
{
    qm_s64 span = (qm_s64)maximum - minimum;
    qm_s64 offset = (qm_s64)clamp_i(current, minimum, maximum) - minimum;
    qm_s64 tick;
    int direction;
    switch (ctx->input.key) {
    case QM_KEY_HOME: *next = minimum; return true;
    case QM_KEY_END: *next = maximum; return true;
    case QM_KEY_LEFT: case QM_KEY_DOWN: direction = -1; break;
    case QM_KEY_RIGHT: case QM_KEY_UP: direction = 1; break;
    default: return false;
    }
    if (steps) {
        tick = (offset * steps + span / 2) / span + direction;
        if (tick < 0)
            tick = 0;
        if (tick > steps)
            tick = steps;
        offset = (tick * span + steps / 2) / steps;
    } else {
        offset += direction;
        if (offset < 0)
            offset = 0;
        if (offset > span)
            offset = span;
    }
    *next = (int)((qm_s64)minimum + offset);
    return true;
}
static int slider_y(struct qm_context *ctx, struct qm_rect rect, const char *label)
{
    if (label && *label && rect.h >= qm_font_height(ctx) + 28)
        return rect.y + qm_font_height(ctx) + (rect.h - qm_font_height(ctx)) / 2;
    return rect.y + rect.h / 2;
}
static void slider_label(struct qm_context *ctx, struct qm_rect rect,
                          const char *label, bool enabled)
{
    if (label && *label && rect.h >= qm_font_height(ctx) + 28)
        qm_text(ctx, rect.x, rect.y, label,
                enabled ? ctx->theme.on_surface : faded(ctx->theme.on_surface, 97));
}
static void slider_focus(struct qm_context *ctx, struct qm_rect rect,
                          unsigned int flags, bool enabled)
{
    if (enabled && (flags & QM_FOCUSED))
        qm_stroke_rect(ctx, rect, 8, 2, ctx->theme.primary);
}

bool qm_slider(struct qm_context *ctx, qm_u32 id, struct qm_rect rect,
               const char *label, int *value, int minimum, int maximum,
               unsigned int steps, bool enabled)
{
    unsigned int flags;
    int next, x, y;
    bool changed = false;
    qm_color active, inactive;
    if (!value || minimum >= maximum || steps > 1024 || rect.w < 25 ||
        !begin_item(ctx, id, rect, enabled, &flags))
        return false;
    next = *value;
    if (enabled && (flags & QM_HELD))
        next = pointer_value(ctx, rect, minimum, maximum, steps);
    else if (enabled && (flags & QM_FOCUSED))
        keyboard_slider(ctx, *value, minimum, maximum, steps, &next);
    if (next != *value) {
        *value = next;
        changed = true;
    }
    x = slider_x(rect, *value, minimum, maximum);
    y = slider_y(ctx, rect, label);
    active = enabled ? ctx->theme.primary : faded(ctx->theme.on_surface, 97);
    inactive = enabled ? ctx->theme.secondary_container : faded(ctx->theme.on_surface, 31);
    slider_label(ctx, rect, label, enabled);
    qm_line(ctx, rect.x + 12, y, rect.x + rect.w - 12, y, 6, inactive);
    qm_line(ctx, rect.x + 12, y, x, y, 6, active);
    if (steps && steps <= 64) {
        unsigned int i;
        for (i = 0; i <= steps; ++i) {
            int tick_x = rect.x + 12 + (int)((qm_s64)i * (rect.w - 24) / steps);
            qm_circle(ctx, tick_x, y, 1, x >= tick_x ? ctx->theme.on_primary : active);
        }
    }
    qm_round_rect(ctx, (struct qm_rect){x - 3, y - 13, 6, 26}, 3, active);
    slider_focus(ctx, rect, flags, enabled);
    qm_clip_pop(ctx);
    return changed;
}

bool qm_range_slider(struct qm_context *ctx, qm_u32 id, struct qm_rect rect,
                     const char *label, int *lower, int *upper,
                     int minimum, int maximum, unsigned int steps,
                     struct qm_range_state *state, bool enabled)
{
    unsigned int flags;
    int next, low, high, x0, x1, y;
    bool changed = false;
    qm_color active, inactive;
    if (!lower || !upper || !state || lower == upper || minimum >= maximum ||
        steps > 1024 || rect.w < 25 ||
        !begin_item(ctx, id, rect, enabled, &flags))
        return false;
    low = clamp_i(*lower, minimum, maximum);
    high = clamp_i(*upper, minimum, maximum);
    if (low > high)
        low = high;
    x0 = slider_x(rect, low, minimum, maximum);
    x1 = slider_x(rect, high, minimum, maximum);
    if (enabled && (flags & QM_HELD) && ctx->input.pressed) {
        qm_s64 distance0 = (qm_s64)ctx->input.x - x0;
        qm_s64 distance1 = (qm_s64)ctx->input.x - x1;
        if (distance0 < 0) distance0 = -distance0;
        if (distance1 < 0) distance1 = -distance1;
        state->active_thumb = distance0 <= distance1 ? 0 : 1;
    }
    if (enabled && (flags & QM_FOCUSED) && ctx->input.key == QM_KEY_SPACE)
        state->active_thumb = state->active_thumb == 1 ? 0 : 1;
    if (enabled && (flags & QM_HELD)) {
        next = pointer_value(ctx, rect, minimum, maximum, steps);
        if (state->active_thumb == 1)
            high = max_i(next, low);
        else
            low = min_i(next, high);
        changed = *lower != low || *upper != high;
    } else if (enabled && (flags & QM_FOCUSED)) {
        if (state->active_thumb == 1 &&
            keyboard_slider(ctx, high, minimum, maximum, steps, &next)) {
            high = max_i(next, low);
            changed = *lower != low || *upper != high;
        } else if (state->active_thumb != 1 &&
                   keyboard_slider(ctx, low, minimum, maximum, steps, &next)) {
            low = min_i(next, high);
            changed = *lower != low || *upper != high;
        }
    }
    if (changed) {
        *lower = low;
        *upper = high;
    }
    x0 = slider_x(rect, low, minimum, maximum);
    x1 = slider_x(rect, high, minimum, maximum);
    y = slider_y(ctx, rect, label);
    active = enabled ? ctx->theme.primary : faded(ctx->theme.on_surface, 97);
    inactive = enabled ? ctx->theme.secondary_container : faded(ctx->theme.on_surface, 31);
    slider_label(ctx, rect, label, enabled);
    qm_line(ctx, rect.x + 12, y, rect.x + rect.w - 12, y, 6, inactive);
    qm_line(ctx, x0, y, x1, y, 6, active);
    qm_round_rect(ctx, (struct qm_rect){x0 - 3, y - 13, 6, 26}, 3, active);
    qm_round_rect(ctx, (struct qm_rect){x1 - 3, y - 13, 6, 26}, 3, active);
    if (enabled && (flags & QM_FOCUSED))
        qm_circle(ctx, state->active_thumb == 1 ? x1 : x0, y, 2, ctx->theme.on_primary);
    slider_focus(ctx, rect, flags, enabled);
    qm_clip_pop(ctx);
    return changed;
}

/* Bounded, strict UTF-8 decoding. No calls into C library or kernel locale. */
static unsigned int utf8_length(const char *text, unsigned int remaining)
{
    const unsigned char *p = (const unsigned char *)text;
    unsigned int n, i;
    if (!remaining || !p[0]) return 0;
    if (p[0] < 0x80) return 1;
    if (p[0] >= 0xc2 && p[0] <= 0xdf) n = 2;
    else if (p[0] >= 0xe0 && p[0] <= 0xef) n = 3;
    else if (p[0] >= 0xf0 && p[0] <= 0xf4) n = 4;
    else return 0;
    if (n > remaining) return 0;
    for (i = 1; i < n; ++i)
        if ((p[i] & 0xc0) != 0x80) return 0;
    if ((p[0] == 0xe0 && p[1] < 0xa0) ||
        (p[0] == 0xed && p[1] >= 0xa0) ||
        (p[0] == 0xf0 && p[1] < 0x90) ||
        (p[0] == 0xf4 && p[1] >= 0x90)) return 0;
    return n;
}
static bool buffer_length(const char *text, unsigned int capacity, unsigned int *length)
{
    unsigned int cursor = 0, bytes;
    while (cursor < capacity) {
        if (!text[cursor]) {
            *length = cursor;
            return true;
        }
        bytes = utf8_length(text + cursor, capacity - cursor);
        if (!bytes || (bytes == 1 && (unsigned char)text[cursor] < 0x20))
            return false;
        cursor += bytes;
    }
    return false;
}
static unsigned int boundary_before(const char *text, unsigned int length,
                                     unsigned int position)
{
    unsigned int cursor = 0, next;
    if (position >= length)
        return length;
    while (cursor < position) {
        next = cursor + utf8_length(text + cursor, length - cursor);
        if (next > position || next <= cursor)
            break;
        cursor = next;
    }
    return cursor;
}
static unsigned int previous_boundary(const char *text, unsigned int position)
{
    if (!position) return 0;
    --position;
    while (position && ((unsigned char)text[position] & 0xc0) == 0x80)
        --position;
    return position;
}
static int prefix_width(struct qm_context *ctx, const char *text, unsigned int length)
{
    unsigned int cursor = 0, i, bytes;
    int width = 0;
    char character[5];
    while (cursor < length) {
        bytes = utf8_length(text + cursor, length - cursor);
        if (!bytes) break;
        for (i = 0; i < bytes; ++i)
            character[i] = text[cursor + i];
        character[bytes] = 0;
        width += qm_text_width(ctx, character);
        if (width > 1048576)
            return 1048576;
        cursor += bytes;
    }
    return width;
}
static unsigned int pointer_caret(struct qm_context *ctx, const char *text,
                                   unsigned int length, qm_s64 position)
{
    unsigned int cursor = 0, next;
    int current_width = 0, next_width;
    if (position <= 0) return 0;
    while (cursor < length) {
        next = cursor + utf8_length(text + cursor, length - cursor);
        next_width = current_width + prefix_width(ctx, text + cursor, next - cursor);
        if (position <= current_width + (next_width - current_width) / 2)
            break;
        current_width = next_width;
        cursor = next;
    }
    return cursor;
}
static void erase_bytes(char *buffer, unsigned int length,
                         unsigned int first, unsigned int last)
{
    unsigned int i;
    for (i = first; i + last - first <= length; ++i)
        buffer[i] = buffer[i + last - first];
}

bool qm_text_field(struct qm_context *ctx, qm_u32 id, struct qm_rect rect,
                   const char *label, char *buffer, unsigned int capacity,
                   struct qm_text_state *state, enum qm_text_field_style style,
                   const char *supporting, bool error, bool enabled)
{
    unsigned int flags, length, caret, bytes, i, incoming = 0;
    bool changed = false, focused;
    int scroll, caret_width, text_y, inner_width;
    struct qm_rect content, field;
    qm_color foreground, border;
    if (!buffer || !state || !capacity || capacity > QM_TEXT_CAPACITY ||
        style < QM_TEXT_FILLED || style > QM_TEXT_OUTLINED ||
        !buffer_length(buffer, capacity, &length) ||
        !begin_item(ctx, id, rect, enabled, &flags))
        return false;
    focused = enabled && (flags & QM_FOCUSED);
    caret = boundary_before(buffer, length, state->caret);
    scroll = clamp_i(state->scroll_px, 0, 1048576);
    inner_width = max_i(0, rect.w - 24);
    if (focused && ctx->input.pressed)
        caret = pointer_caret(ctx, buffer, length,
                               (qm_s64)ctx->input.x - rect.x - 12 + scroll);
    if (focused) {
        switch (ctx->input.key) {
        case QM_KEY_LEFT: caret = previous_boundary(buffer, caret); break;
        case QM_KEY_RIGHT:
            if (caret < length) caret += utf8_length(buffer + caret, length - caret);
            break;
        case QM_KEY_HOME: caret = 0; break;
        case QM_KEY_END: caret = length; break;
        case QM_KEY_BACKSPACE:
            if (caret) {
                unsigned int start = previous_boundary(buffer, caret);
                erase_bytes(buffer, length, start, caret);
                length -= caret - start;
                caret = start;
                changed = true;
            }
            break;
        case QM_KEY_DELETE:
            if (caret < length) {
                bytes = utf8_length(buffer + caret, length - caret);
                erase_bytes(buffer, length, caret, caret + bytes);
                length -= bytes;
                changed = true;
            }
            break;
        case QM_KEY_ENTER: case QM_KEY_ESCAPE:
            ctx->focused_id = 0;
            focused = false;
            break;
        default: break;
        }
        while (focused && incoming < sizeof(ctx->input.text) && ctx->input.text[incoming]) {
            bytes = utf8_length(ctx->input.text + incoming,
                                (unsigned int)sizeof(ctx->input.text) - incoming);
            if (!bytes) break;
            if (bytes == 1 && (unsigned char)ctx->input.text[incoming] < 0x20) {
                incoming += bytes;
                continue;
            }
            if (length + bytes >= capacity) break;
            for (i = length + 1; i > caret; --i)
                buffer[i + bytes - 1] = buffer[i - 1];
            for (i = 0; i < bytes; ++i)
                buffer[caret + i] = ctx->input.text[incoming + i];
            length += bytes;
            caret += bytes;
            incoming += bytes;
            changed = true;
        }
    }
    caret_width = prefix_width(ctx, buffer, caret);
    if (focused) {
        if (caret_width < scroll)
            scroll = caret_width;
        if (caret_width - scroll > max_i(0, inner_width - 2))
            scroll = caret_width - max_i(0, inner_width - 2);
        state->caret = caret;
        state->scroll_px = scroll;
    }
    field = rect;
    if (supporting && *supporting && rect.h > 2 * qm_font_height(ctx) + 30)
        field.h -= qm_font_height(ctx) + 8;
    foreground = enabled ? ctx->theme.on_surface : faded(ctx->theme.on_surface, 97);
    border = error ? ctx->theme.error : (focused ? ctx->theme.primary : ctx->theme.outline);
    if (!enabled) border = faded(ctx->theme.on_surface, 31);
    if (style == QM_TEXT_FILLED) {
        qm_round_rect(ctx, field, 4, enabled ? ctx->theme.surface_container_high :
                      faded(ctx->theme.on_surface, 10));
        qm_line(ctx, field.x, field.y + field.h - 1,
                field.x + field.w - 1, field.y + field.h - 1,
                focused ? 2 : 1, border);
    } else {
        qm_stroke_rect(ctx, field, 4, focused ? 2 : 1, border);
    }
    text_y = field.y + (field.h - qm_font_height(ctx)) / 2;
    if (label && *label && field.h >= 2 * qm_font_height(ctx) + 10) {
        qm_text(ctx, field.x + 12, field.y + 4, label,
                enabled ? border : foreground);
        text_y = field.y + field.h - qm_font_height(ctx) - 6;
    }
    content = (struct qm_rect){field.x + 12, text_y, inner_width, qm_font_height(ctx)};
    if (content.w > 0 && qm_clip_push(ctx, content)) {
        qm_text(ctx, content.x - scroll, text_y, buffer, foreground);
        if (focused)
            qm_line(ctx, content.x + caret_width - scroll, text_y,
                    content.x + caret_width - scroll, text_y + qm_font_height(ctx) - 1,
                    1, border);
        qm_clip_pop(ctx);
    }
    if (field.h != rect.h)
        qm_text(ctx, rect.x + 12, rect.y + field.h + 4, supporting,
                enabled ? (error ? ctx->theme.error : ctx->theme.on_surface_variant) : foreground);
    qm_clip_pop(ctx);
    return changed;
}
