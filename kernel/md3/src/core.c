/* SPDX-License-Identifier: GPL-2.0-only */
#include "qmd3.h"
#include "qmd3_assets.h"

#define QM_COORD_LIMIT 32767

static int valid_dimension(int v)
{
    return v > 0 && v <= QM_MAX_DIMENSION;
}

static int valid_rect(struct qm_rect r)
{
    return r.x >= -QM_COORD_LIMIT && r.x <= QM_COORD_LIMIT &&
           r.y >= -QM_COORD_LIMIT && r.y <= QM_COORD_LIMIT &&
           r.w >= 0 && r.w <= QM_COORD_LIMIT && r.h >= 0 && r.h <= QM_COORD_LIMIT &&
           (qm_s64)r.x + r.w <= QM_COORD_LIMIT &&
           (qm_s64)r.y + r.h <= QM_COORD_LIMIT;
}

static qm_color premultiply(qm_color c)
{
    unsigned a = c >> 24;
    unsigned r = ((c >> 16) & 255u) * a + 127u;
    unsigned g = ((c >> 8) & 255u) * a + 127u;
    unsigned b = (c & 255u) * a + 127u;
    return (a << 24) | ((r / 255u) << 16) | ((g / 255u) << 8) | (b / 255u);
}

static struct qm_rect surface_bounds(const struct qm_context *c)
{
    struct qm_rect r = {0, 0, 0, 0};
    if (c) {
        r.w = c->surface.width;
        r.h = c->surface.height;
    }
    return r;
}

static struct qm_rect clip_bounds(const struct qm_context *c)
{
    if (c && c->clip_depth)
        return c->clips[c->clip_depth - 1];
    return surface_bounds(c);
}

bool qm_contains(struct qm_rect r, int x, int y)
{
    return valid_rect(r) && (qm_s64)x >= r.x && (qm_s64)y >= r.y &&
           (qm_s64)x < (qm_s64)r.x + r.w &&
           (qm_s64)y < (qm_s64)r.y + r.h;
}

struct qm_rect qm_intersection(struct qm_rect a, struct qm_rect b)
{
    qm_s64 left = a.x > b.x ? a.x : b.x;
    qm_s64 top = a.y > b.y ? a.y : b.y;
    qm_s64 right = ((qm_s64)a.x + a.w) < ((qm_s64)b.x + b.w) ?
                   (qm_s64)a.x + a.w : (qm_s64)b.x + b.w;
    qm_s64 bottom = ((qm_s64)a.y + a.h) < ((qm_s64)b.y + b.h) ?
                    (qm_s64)a.y + a.h : (qm_s64)b.y + b.h;
    struct qm_rect out = {(int)left, (int)top,
                          right > left ? (int)(right - left) : 0,
                          bottom > top ? (int)(bottom - top) : 0};
    return out;
}

static void set_light_theme(struct qm_theme *t)
{
    *t = (struct qm_theme){
        0xff6750a4u, 0xffffffffu, 0xffeaddffu, 0xff21005du,
        0xffe8def8u, 0xff1d192bu, 0xfffffbfeu, 0xfff3edf7u,
        0xffece6f0u, 0xff1d1b20u, 0xff49454fu, 0xff79747eu,
        0xffc9c5d0u, 0xffb3261eu, 0xffffffffu, 0xff322f35u, 0xfff5eff7u, false
    };
}

static void set_dark_theme(struct qm_theme *t)
{
    *t = (struct qm_theme){
        0xffd0bcffu, 0xff381e72u, 0xff4f378bu, 0xffeaddffu,
        0xff4a4458u, 0xffe8def8u, 0xff141218u, 0xff211f26u,
        0xff2b2930u, 0xffe6e0e9u, 0xffcac4d0u, 0xff938f99u,
        0xff49454fu, 0xfff2b8b5u, 0xff601410u, 0xffe6e0e9u, 0xff322f35u, true
    };
}

void qm_init(struct qm_context *ctx, bool dark)
{
    if (!ctx)
        return;
    *ctx = (struct qm_context){0};
    ctx->font = qm_default_font();
    ctx->input_enabled = true;
    qm_set_theme(ctx, dark);
}

void qm_set_theme(struct qm_context *ctx, bool dark)
{
    if (!ctx)
        return;
    if (dark)
        set_dark_theme(&ctx->theme);
    else
        set_light_theme(&ctx->theme);
}

static int valid_surface(const struct qm_surface *s)
{
    qm_u64 required;
    if (!s || !s->pixels || !valid_dimension(s->width) || !valid_dimension(s->height))
        return 0;
    if (s->stride_bytes < (unsigned)s->width * sizeof(qm_u32) ||
        s->stride_bytes % sizeof(qm_u32))
        return 0;
    required = (qm_u64)s->stride_bytes * (qm_u64)s->height;
    return required <= s->size_bytes;
}

static void clear_surface(struct qm_context *ctx, qm_color color)
{
    int y;
    qm_u32 pixel = premultiply(color);
    for (y = 0; y < ctx->surface.height; ++y) {
        qm_u32 *row = (qm_u32 *)((qm_u8 *)ctx->surface.pixels +
                                 (qm_u64)y * ctx->surface.stride_bytes);
        int x;
        for (x = 0; x < ctx->surface.width; ++x)
            row[x] = pixel;
    }
}

bool qm_begin(struct qm_context *ctx, const struct qm_surface *surface,
              const struct qm_input *input, qm_color clear)
{
    if (!ctx || ctx->in_frame) {
        if (ctx)
            ctx->errors |= QM_ERROR_FRAME;
        return false;
    }
    if (!valid_surface(surface) || !input) {
        ctx->errors |= QM_ERROR_SURFACE;
        return false;
    }
    ctx->surface = *surface;
    ctx->input = *input;
    ctx->input.pressed = ctx->input.down && !ctx->previous_down;
    ctx->input.released = !ctx->input.down && ctx->previous_down;
    if (ctx->input.dt_ms > 1000)
        ctx->input.dt_ms = 1000;
    ctx->time_ms += ctx->input.dt_ms;
    ctx->clip_depth = 1;
    ctx->clips[0] = surface_bounds(ctx);
    ctx->id_bank ^= 1u;
    ctx->id_count[ctx->id_bank] = 0;
    ctx->focus_count[ctx->id_bank] = 0;
    ctx->press_claimed = false;
    ctx->key_claimed = false;
    if (ctx->input.key == QM_KEY_TAB && ctx->focus_count[ctx->id_bank ^ 1u]) {
        unsigned n = ctx->focus_count[ctx->id_bank ^ 1u], i;
        for (i = 0; i < n; ++i)
            if (ctx->focus_ids[ctx->id_bank ^ 1u][i] == ctx->focused_id) break;
        if (i == n) i = ctx->input.shift ? n - 1 : 0;
        else i = ctx->input.shift ? (i ? i - 1 : n - 1) : (i + 1) % n;
        ctx->focused_id = ctx->focus_ids[ctx->id_bank ^ 1u][i];
        ctx->input.key = QM_KEY_NONE;
    }
    ctx->in_frame = true;
    clear_surface(ctx, clear);
    return true;
}

void qm_end(struct qm_context *ctx)
{
    if (!ctx || !ctx->in_frame)
        return;
    if (!ctx->input.down)
        ctx->active_id = 0;
    ctx->previous_down = ctx->input.down;
    ctx->in_frame = false;
    ctx->clip_depth = 0;
}

bool qm_set_input_enabled(struct qm_context *ctx, bool enabled)
{
    bool old;
    if (!ctx)
        return false;
    old = ctx->input_enabled;
    ctx->input_enabled = enabled;
    if (!enabled) {
        ctx->active_id = 0;
    }
    return old;
}

static int register_id(struct qm_context *ctx, qm_u32 id)
{
    unsigned i;
    qm_u32 *ids;
    if (!id || !ctx)
        return 0;
    ids = ctx->ids[ctx->id_bank];
    for (i = 0; i < ctx->id_count[ctx->id_bank]; ++i) {
        if (ids[i] == id) {
            ctx->errors |= QM_ERROR_DUPLICATE_ID;
            return 0;
        }
    }
    if (ctx->id_count[ctx->id_bank] >= QM_MAX_WIDGETS) {
        ctx->errors |= QM_ERROR_WIDGET_LIMIT;
        return 0;
    }
    ids[ctx->id_count[ctx->id_bank]++] = id;
    return 1;
}

unsigned qm_interact(struct qm_context *ctx, qm_u32 id,
                     struct qm_rect bounds, bool enabled)
{
    unsigned flags = 0;
    bool hover;
    if (!ctx || !ctx->in_frame || !id || !valid_rect(bounds)) {
        if (ctx)
            ctx->errors |= QM_ERROR_ARGUMENT;
        return 0;
    }
    if (!register_id(ctx, id))
        return 0;
    if (enabled && ctx->focus_count[ctx->id_bank] < QM_MAX_WIDGETS)
        ctx->focus_ids[ctx->id_bank][ctx->focus_count[ctx->id_bank]++] = id;
    hover = ctx->input_enabled && enabled && qm_contains(clip_bounds(ctx),
                                                         ctx->input.x, ctx->input.y) &&
            qm_contains(bounds, ctx->input.x, ctx->input.y);
    if (hover)
        flags |= QM_HOVERED;
    if (!enabled || !ctx->input_enabled) {
        if (ctx->active_id == id)
            ctx->active_id = 0;
        return flags;
    }
    if (ctx->input.pressed && hover && !ctx->press_claimed) {
        ctx->active_id = id;
        ctx->focused_id = id;
        ctx->press_claimed = true;
    }
    if (ctx->active_id == id && ctx->input.down)
        flags |= QM_HELD;
    if (ctx->active_id == id && ctx->input.released) {
        if (hover)
            flags |= QM_ACTIVATED;
        ctx->active_id = 0;
    }
    if (ctx->focused_id == id)
        flags |= QM_FOCUSED;
    if (ctx->focused_id == id && !ctx->key_claimed &&
        (ctx->input.key == QM_KEY_ENTER || ctx->input.key == QM_KEY_SPACE)) {
        flags |= QM_ACTIVATED;
        ctx->key_claimed = true;
    }
    return flags;
}

bool qm_clip_push(struct qm_context *ctx, struct qm_rect bounds)
{
    struct qm_rect next;
    if (!ctx || !ctx->in_frame || !valid_rect(bounds) ||
        ctx->clip_depth >= QM_MAX_CLIPS) {
        if (ctx)
            ctx->errors |= QM_ERROR_CLIP;
        return false;
    }
    next = qm_intersection(clip_bounds(ctx), bounds);
    if (!valid_rect(next)) {
        ctx->errors |= QM_ERROR_CLIP;
        return false;
    }
    ctx->clips[ctx->clip_depth++] = next;
    return true;
}

void qm_clip_pop(struct qm_context *ctx)
{
    if (!ctx || !ctx->in_frame || ctx->clip_depth <= 1) {
        if (ctx)
            ctx->errors |= QM_ERROR_CLIP;
        return;
    }
    --ctx->clip_depth;
}

unsigned qm_ease(unsigned elapsed_ms, unsigned duration_ms)
{
    qm_u64 t;
    if (!duration_ms || elapsed_ms >= duration_ms)
        return 65536u;
    t = ((qm_u64)elapsed_ms << 16) / duration_ms;
    /* Smoothstep with a 16.16 fixed point intermediate. */
    return (unsigned)((t * t * (196608u - (t << 1))) >> 32);
}

int qm_lerp(int from, int to, unsigned amount_q16)
{
    qm_s64 delta = (qm_s64)to - from;
    if (amount_q16 >= 65536u)
        return to;
    return (int)((qm_s64)from + (delta * amount_q16) / 65536);
}
