/* SPDX-License-Identifier: GPL-2.0-only */
#include "qmd3.h"
#include "qmd3_assets.h"

#define QM_COORD_LIMIT 32767
#define QM_Q16_MIN (-2147483647 - 1)

static int valid_coord(int v) { return v >= -QM_COORD_LIMIT && v <= QM_COORD_LIMIT; }
static int valid_rect(struct qm_rect r) {
    if (!valid_coord(r.x) || !valid_coord(r.y) || r.w < 0 || r.h < 0) return 0;
    if (r.w > QM_COORD_LIMIT || r.h > QM_COORD_LIMIT) return 0;
    if (r.x > QM_COORD_LIMIT - r.w || r.y > QM_COORD_LIMIT - r.h) return 0;
    return 1;
}
static struct qm_rect surface_rect(const struct qm_context *c) {
    struct qm_rect r = { 0, 0, c ? c->surface.width : 0, c ? c->surface.height : 0 };
    return r;
}
static struct qm_rect current_clip(const struct qm_context *c) {
    if (!c || !c->clip_depth) return surface_rect(c);
    return c->clips[c->clip_depth - 1];
}
static int clip_rect(const struct qm_context *c, struct qm_rect in, struct qm_rect *out) {
    struct qm_rect a, b;
    if (!c || !out || !valid_rect(in) || !c->in_frame || !c->surface.pixels) return 0;
    a = current_clip(c);
    b = surface_rect(c);
    if (!valid_rect(a)) return 0;
    if (a.x < b.x) a.x = b.x;
    if (a.y < b.y) a.y = b.y;
    if (a.x + a.w > b.x + b.w) a.w = b.x + b.w - a.x;
    if (a.y + a.h > b.y + b.h) a.h = b.y + b.h - a.y;
    if (in.x < a.x) { in.w -= a.x - in.x; in.x = a.x; }
    if (in.y < a.y) { in.h -= a.y - in.y; in.y = a.y; }
    if (in.x + in.w > a.x + a.w) in.w = a.x + a.w - in.x;
    if (in.y + in.h > a.y + a.h) in.h = a.y + a.h - in.y;
    if (in.w <= 0 || in.h <= 0) return 0;
    *out = in;
    return 1;
}
static qm_u32 blend_pixel(qm_u32 dst, qm_color color) {
    unsigned sa = (color >> 24) & 255u;
    unsigned sr = ((color >> 16) & 255u) * sa;
    unsigned sg = ((color >> 8) & 255u) * sa;
    unsigned sb = (color & 255u) * sa;
    unsigned da = (dst >> 24) & 255u;
    unsigned dr = (dst >> 16) & 255u;
    unsigned dg = (dst >> 8) & 255u;
    unsigned db = dst & 255u;
    unsigned inv = 255u - sa;
    unsigned oa = sa + (da * inv + 127u) / 255u;
    unsigned orr = (sr + dr * inv + 127u) / 255u;
    unsigned og = (sg + dg * inv + 127u) / 255u;
    unsigned ob = (sb + db * inv + 127u) / 255u;
    if (orr > 255u) orr = 255u;
    if (og > 255u) og = 255u;
    if (ob > 255u) ob = 255u;
    return (oa << 24) | (orr << 16) | (og << 8) | ob;
}
static void put_pixel(struct qm_context *c, int x, int y, qm_color color) {
    qm_u8 *row;
    if (!c || !c->in_frame || !c->surface.pixels || x < 0 || y < 0 ||
        x >= c->surface.width || y >= c->surface.height) return;
    if (!qm_contains(current_clip(c), x, y)) return;
    row = (qm_u8 *)c->surface.pixels + (qm_u64)y * c->surface.stride_bytes;
    ((qm_u32 *)row)[x] = blend_pixel(((qm_u32 *)row)[x], color);
}
static int inside_round(int x, int y, struct qm_rect r, int rad) {
    int dx = 0, dy = 0;
    qm_s64 d;
    if (x < r.x || y < r.y || x >= r.x + r.w || y >= r.y + r.h) return 0;
    if (rad <= 0) return 1;
    if (rad > r.w / 2) rad = r.w / 2;
    if (rad > r.h / 2) rad = r.h / 2;
    if (x < r.x + rad) dx = r.x + rad - x;
    else if (x >= r.x + r.w - rad) dx = x - (r.x + r.w - rad - 1);
    if (y < r.y + rad) dy = r.y + rad - y;
    else if (y >= r.y + r.h - rad) dy = y - (r.y + r.h - rad - 1);
    if (!dx || !dy) return 1;
    d = (qm_s64)dx * dx + (qm_s64)dy * dy;
    return d <= (qm_s64)rad * rad;
}
static void fill_pixel_loop(struct qm_context *c, struct qm_rect r, qm_color color,
                            int radius, int stroke, int inner_radius) {
    int y, x;
    struct qm_rect q;
    if (!clip_rect(c, r, &q)) return;
    for (y = q.y; y < q.y + q.h; ++y) {
        for (x = q.x; x < q.x + q.w; ++x) {
            if (!inside_round(x, y, r, radius)) continue;
            if (stroke && inside_round(x, y,
                    (struct qm_rect){r.x + stroke, r.y + stroke,
                     r.w - 2 * stroke, r.h - 2 * stroke}, inner_radius)) continue;
            put_pixel(c, x, y, color);
        }
    }
}
void qm_fill(struct qm_context *c, struct qm_rect r, qm_color color) {
    struct qm_rect q; int y, x;
    if (!clip_rect(c, r, &q)) return;
    for (y = q.y; y < q.y + q.h; ++y)
        for (x = q.x; x < q.x + q.w; ++x) put_pixel(c, x, y, color);
}
void qm_round_rect(struct qm_context *c, struct qm_rect r, int radius, qm_color color) {
    if (!valid_rect(r) || radius < 0) { if (c) c->errors |= QM_ERROR_ARGUMENT; return; }
    fill_pixel_loop(c, r, color, radius, 0, 0);
}
void qm_stroke_rect(struct qm_context *c, struct qm_rect r, int radius, int width, qm_color color) {
    int inner;
    if (!valid_rect(r) || radius < 0 || width <= 0 || width > 32767) {
        if (c) c->errors |= QM_ERROR_ARGUMENT;
        return;
    }
    if (width > r.w / 2 || width > r.h / 2) { qm_round_rect(c, r, radius, color); return; }
    inner = radius > width ? radius - width : 0;
    fill_pixel_loop(c, r, color, radius, width, inner);
}
void qm_line(struct qm_context *c, int x0, int y0, int x1, int y1, int width, qm_color color) {
    int dx, sx, dy, sy, err, e2, rad, i, j, px, py;
    if (!c || !valid_coord(x0) || !valid_coord(y0) || !valid_coord(x1) || !valid_coord(y1) ||
        width <= 0 || width > QM_COORD_LIMIT) {
        if (c) c->errors |= QM_ERROR_ARGUMENT;
        return;
    }
    rad = (width - 1) / 2; dx = x1 > x0 ? x1 - x0 : x0 - x1; sx = x0 < x1 ? 1 : -1;
    dy = y1 > y0 ? y0 - y1 : y1 - y0; sy = y0 < y1 ? 1 : -1; err = dx + dy;
    for (;;) {
        for (j = -rad; j <= rad; ++j) for (i = -rad; i <= rad; ++i) {
            px = x0 + i; py = y0 + j; put_pixel(c, px, py, color);
        }
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err; if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
void qm_circle(struct qm_context *c, int cx, int cy, int radius, qm_color color) {
    int y, x;
    qm_s64 rr;
    if (!c || !valid_coord(cx) || !valid_coord(cy) || radius < 0 || radius > 32767) {
        if (c) c->errors |= QM_ERROR_ARGUMENT;
        return;
    }
    rr = (qm_s64)radius * radius;
    for (y = cy - radius; y <= cy + radius; ++y) for (x = cx - radius; x <= cx + radius; ++x) {
        qm_s64 dx = x - cx, dy = y - cy;
        if (dx * dx + dy * dy <= rr) put_pixel(c, x, y, color);
    }
}
static struct qm_point poly_point(const struct qm_point *p, unsigned i,
                                  struct qm_rect b, int units, int transformed) {
    struct qm_point q = p[i];
    if (transformed) {
        if (units <= 0) return (struct qm_point){0, 0};
        q.x = b.x + (int)(((qm_s64)q.x * b.w) / units);
        q.y = b.y + (int)(((qm_s64)q.y * b.h) / units);
    }
    return q;
}
static void polygon_fill(struct qm_context *c, const struct qm_point *p, unsigned n,
                         struct qm_rect bounds, int units, int transformed, qm_color color) {
    int y, miny, maxy;
    if (!c || !p || n < 3 || n > QM_MAX_POLYGON_POINTS) { if (c) c->errors |= QM_ERROR_ARGUMENT; return; }
    miny = 32767; maxy = -32767;
    for (unsigned i = 0; i < n; ++i) {
        struct qm_point q = poly_point(p, i, bounds, units, transformed);
        if (!valid_coord(q.x) || !valid_coord(q.y)) { c->errors |= QM_ERROR_ARGUMENT; return; }
        if (q.y < miny) miny = q.y;
        if (q.y > maxy) maxy = q.y;
    }
    for (y = miny; y <= maxy; ++y) {
        int left = 0, have_left = 0, guard;
        qm_s32 last = QM_Q16_MIN;
        for (guard = 0; guard < (int)n; ++guard) {
            qm_s32 next = 2147483647;
            for (unsigned i = 0; i < n; ++i) {
                struct qm_point a = poly_point(p, i, bounds, units, transformed);
                struct qm_point b = poly_point(p, (i + 1u) % n, bounds, units, transformed);
                int lo = a.y < b.y ? a.y : b.y, hi = a.y > b.y ? a.y : b.y;
                qm_s64 num; qm_s32 xq;
                if (a.y == b.y || y < lo || y >= hi) continue;
                num = ((qm_s64)a.x * (b.y - a.y) +
                       (qm_s64)(y - a.y) * (b.x - a.x)) * 65536;
                xq = (qm_s32)(num / (b.y - a.y));
                if (xq > last && xq < next) next = xq;
            }
            if (next == 2147483647) break;
            last = next;
            if (!have_left) { left = next; have_left = 1; }
            else {
                int x0 = (left + 65535) >> 16, x1 = next >> 16;
                if (x1 >= x0) { struct qm_rect q = {x0, y, x1 - x0 + 1, 1}; qm_fill(c, q, color); }
                have_left = 0;
            }
        }
    }
}
void qm_polygon(struct qm_context *c, const struct qm_point *p, unsigned n, qm_color color) {
    polygon_fill(c, p, n, (struct qm_rect){0, 0, 0, 0}, 0, 0, color);
}
void qm_polygon_transformed(struct qm_context *c, const struct qm_point *p, unsigned n,
                            struct qm_rect bounds, int units, qm_color color) {
    if (!valid_rect(bounds) || units <= 0 || units > 32767) {
        if (c) c->errors |= QM_ERROR_ARGUMENT;
        return;
    }
    polygon_fill(c, p, n, bounds, units, 1, color);
}
int qm_font_height(const struct qm_context *c) {
    const struct qm_font *f = c ? c->font : 0;
    if (!f) f = qm_default_font();
    return f ? f->line_height : 0;
}
static qm_u32 decode_utf8(const char **sp) {
    const qm_u8 *s = (const qm_u8 *)*sp; qm_u32 cp = *s++;
    if (!cp) return 0;
    if (cp < 0x80) { *sp = (const char *)s; return cp; }
    if ((cp & 0xe0) == 0xc0 && (s[0] & 0xc0) == 0x80) { cp = ((cp & 31) << 6) | (s[0] & 63); *sp = (const char *)(s + 1); return cp; }
    if ((cp & 0xf0) == 0xe0 && (s[0] & 0xc0) == 0x80 && (s[1] & 0xc0) == 0x80) { cp = ((cp & 15) << 12) | ((s[0] & 63) << 6) | (s[1] & 63); *sp = (const char *)(s + 2); return cp; }
    if ((cp & 0xf8) == 0xf0 && (s[0] & 0xc0) == 0x80 && (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80) { cp = ((cp & 7) << 18) | ((s[0] & 63) << 12) | ((s[1] & 63) << 6) | (s[2] & 63); *sp = (const char *)(s + 3); return cp; }
    *sp = (const char *)s; return '?';
}
int qm_text_width(const struct qm_context *c, const char *text) {
    const struct qm_font *f; int out = 0; unsigned guard = 0;
    if (!text) return 0;
    f = c && c->font ? c->font : qm_default_font();
    if (!f) return 0;
    while (*text && guard++ < QM_MAX_TEXT_BYTES) {
        qm_u32 cp = decode_utf8(&text); const struct qm_glyph *g = qm_font_glyph(f, cp);
        if (!g) g = qm_font_glyph(f, '?');
        if (g) out += g->advance;
    }
    return out;
}
void qm_text(struct qm_context *c, int x, int y, const char *text, qm_color color) {
    const struct qm_font *f; unsigned guard = 0;
    if (!c || !text) { if (c) c->errors |= QM_ERROR_ARGUMENT; return; }
    f = c->font ? c->font : qm_default_font(); if (!f) return;
    /* Public text coordinates are top-left, matching widget rectangles. The
     * bundled glyph bearings are baseline-relative, so convert once here. */
    y += f->ascent;
    while (*text && guard++ < QM_MAX_TEXT_BYTES) {
        qm_u32 cp = decode_utf8(&text); const struct qm_glyph *g = qm_font_glyph(f, cp);
        if (!g) g = qm_font_glyph(f, '?');
        if (!g) continue;
        for (unsigned gy = 0; gy < g->height; ++gy) for (unsigned gx = 0; gx < g->width; ++gx) {
            qm_u64 coverage_offset = (qm_u64)g->offset + (qm_u64)gy * g->width + gx;
            unsigned cov = coverage_offset < f->coverage_size ? f->coverage[coverage_offset] : 0;
            if (cov) { qm_color cc = (color & 0x00ffffffu) | (((color >> 24) * cov / 255u) << 24); put_pixel(c, x + g->bearing_x + (int)gx, y + g->bearing_y + (int)gy, cc); }
        }
        x += g->advance;
    }
}
