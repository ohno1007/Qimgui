/* SPDX-License-Identifier: Apache-2.0 */
#include "qmd3_assets.h"

struct qm_icon_path {
    const struct qm_point *points;
    unsigned int count;
    bool closed, filled;
};

struct qm_icon_data {
    const char *token;
    unsigned int offset, count;
};

#include "../generated/shapes.inc"
#include "../generated/font.inc"
#include "../generated/icons.inc"

const struct qm_font *qm_default_font(void)
{
    return &qm_roboto18;
}

static const struct qm_glyph *find_glyph(const struct qm_font *font, qm_u32 codepoint)
{
    qm_u32 low = 0, high = font->glyph_count;

    while (low < high) {
        qm_u32 mid = low + (high - low) / 2;
        const struct qm_glyph *glyph = font->glyphs + mid;

        if (glyph->codepoint == codepoint)
            return glyph;
        if (glyph->codepoint < codepoint)
            low = mid + 1;
        else
            high = mid;
    }
    return 0;
}

const struct qm_glyph *qm_font_glyph(const struct qm_font *font, qm_u32 codepoint)
{
    const struct qm_glyph *glyph;

    if (!font || !font->glyphs || !font->glyph_count)
        return 0;
    glyph = find_glyph(font, codepoint);
    if (!glyph && codepoint != '?')
        glyph = find_glyph(font, '?');
    return glyph;
}

const struct qm_shape_data *qm_get_shape(enum qm_shape shape)
{
    if ((unsigned int)shape >= QM_SHAPE_COUNT)
        return 0;
    return &qm_shapes[shape];
}

/* Keep scaling arithmetic defined even for hostile/extreme rectangles; draw
 * entry points subsequently clip against the canvas. This layer has no device
 * pointers, allocations, FPU operations, or mutable process-global state. */
static bool normalized_bounds(struct qm_rect *bounds)
{
    int side, inset_x, inset_y;
    qm_s64 x, y;

    if (bounds->w <= 0 || bounds->h <= 0 ||
        bounds->w > 4096 || bounds->h > 4096 ||
        bounds->x < -32768 || bounds->x > 32767 ||
        bounds->y < -32768 || bounds->y > 32767)
        return false;
    side = bounds->w < bounds->h ? bounds->w : bounds->h;
    inset_x = (bounds->w - side) / 2;
    inset_y = (bounds->h - side) / 2;
    x = (qm_s64)bounds->x + inset_x;
    y = (qm_s64)bounds->y + inset_y;
    if (x < -32768 || x > 32767 || y < -32768 || y > 32767 ||
        x + side > 32767 || y + side > 32767)
        return false;
    bounds->x = (int)x;
    bounds->y = (int)y;
    bounds->w = bounds->h = side;
    return true;
}

void qm_draw_shape(struct qm_context *ctx, enum qm_shape shape,
                   struct qm_rect bounds, qm_color color)
{
    const struct qm_shape_data *data = qm_get_shape(shape);

    if (!ctx || !data || !normalized_bounds(&bounds))
        return;
    qm_polygon_transformed(ctx, data->points, data->count, bounds,
                           QM_SHAPE_UNITS, color);
}

static bool token_equal(const char *left, const char *right)
{
    unsigned int i;

    for (i = 0; i < 64; ++i) {
        if (left[i] != right[i])
            return false;
        if (!left[i])
            return true;
    }
    return false;
}

bool qm_icon_from_token(const char *token, enum qm_icon *icon)
{
    unsigned int i;

    if (!token || !icon || token[0] != '@')
        return false;
    for (i = 0; i < QM_ICON_COUNT; ++i) {
        if (token_equal(token, qm_icons[i].token)) {
            *icon = (enum qm_icon)i;
            return true;
        }
    }
    return false;
}

static struct qm_point icon_point(struct qm_point point, struct qm_rect bounds)
{
    struct qm_point scaled;

    scaled.x = bounds.x + (point.x * bounds.w + QM_SHAPE_UNITS / 2) / QM_SHAPE_UNITS;
    scaled.y = bounds.y + (point.y * bounds.h + QM_SHAPE_UNITS / 2) / QM_SHAPE_UNITS;
    return scaled;
}

void qm_draw_icon(struct qm_context *ctx, enum qm_icon icon,
                  struct qm_rect bounds, qm_color color)
{
    const struct qm_icon_data *data;
    unsigned int i;
    int width;

    if (!ctx || (unsigned int)icon >= QM_ICON_COUNT || !normalized_bounds(&bounds))
        return;
    data = &qm_icons[icon];
    width = (bounds.w + 6) / 12;
    if (width < 1)
        width = 1;
    for (i = 0; i < data->count; ++i) {
        const struct qm_icon_path *path = &qm_icon_paths[data->offset + i];
        unsigned int j;

        if (path->filled) {
            qm_polygon_transformed(ctx, path->points, path->count, bounds,
                                   QM_SHAPE_UNITS, color);
            continue;
        }
        for (j = 0; j + 1 < path->count; ++j) {
            struct qm_point a = icon_point(path->points[j], bounds);
            struct qm_point b = icon_point(path->points[j + 1], bounds);

            qm_line(ctx, a.x, a.y, b.x, b.y, width, color);
        }
        if (path->closed && path->count > 1) {
            struct qm_point a = icon_point(path->points[path->count - 1], bounds);
            struct qm_point b = icon_point(path->points[0], bounds);

            qm_line(ctx, a.x, a.y, b.x, b.y, width, color);
        }
    }
}
