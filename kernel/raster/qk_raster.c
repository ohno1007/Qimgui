/* SPDX-License-Identifier: MIT */
#include "qk_raster.h"

/* Magnitude budget, with dimensions capped at 4096: positions in 8.8 keep edge
 * functions below 2^43, and attributes below 2^17 keep their numerators below
 * 2^61 -- hence 12.4 texture coordinates rather than 8.8.
 * One 64-bit division per triangle produces the reciprocal of the area; the
 * per-pixel work is 64-bit adds and 128-bit multiplies, which arm64 does
 * inline. No 128-bit division: the kernel links no libgcc helper for it.
 */
#define QK_RECIPROCAL_BITS 62

struct qk_attribute {
    long long value;   /* Numerator at the current pixel. */
    long long dx, dy;  /* Numerator step per pixel. */
};

struct qk_edge {
    long long value, dx, dy;
    int top_left;      /* Include pixels exactly on this edge. */
};

static s32 qk_min(s32 a, s32 b) { return a < b ? a : b; }
static s32 qk_max(s32 a, s32 b) { return a > b ? a : b; }

static long long qk_scale(long long numerator, long long reciprocal)
{
    return (long long)(((__int128)numerator * reciprocal) >> QK_RECIPROCAL_BITS);
}

/* Rounded (value * factor) / 255 for 8-bit channels. */
static u32 qk_mul255(u32 value, u32 factor)
{
    u32 product = value * factor + 128;

    return (product + (product >> 8)) >> 8;
}

static void qk_damage(struct qk_raster_stats *stats, s32 x0, s32 y0, s32 x1, s32 y1)
{
    if (!stats || x0 >= x1 || y0 >= y1)
        return;
    if (stats->damage.x0 >= stats->damage.x1 || stats->damage.y0 >= stats->damage.y1) {
        stats->damage.x0 = x0;
        stats->damage.y0 = y0;
        stats->damage.x1 = x1;
        stats->damage.y1 = y1;
        return;
    }
    stats->damage.x0 = qk_min(stats->damage.x0, x0);
    stats->damage.y0 = qk_min(stats->damage.y0, y0);
    stats->damage.x1 = qk_max(stats->damage.x1, x1);
    stats->damage.y1 = qk_max(stats->damage.y1, y1);
}

static int qk_surface_valid(const struct qk_surface *surface)
{
    return surface && surface->pixels && surface->width > 0 && surface->height > 0 &&
           surface->width <= QK_MAX_DIMENSION && surface->height <= QK_MAX_DIMENSION &&
           !(surface->pitch & 3) && surface->pitch >= (u32)surface->width * 4;
}

static u32 *qk_row(const struct qk_surface *surface, s32 y)
{
    return (u32 *)((u8 *)surface->pixels + (unsigned long)y * surface->pitch);
}

void qk_surface_fill(const struct qk_surface *surface, const struct qk_clip *clip,
                     u32 argb)
{
    s32 x0 = 0, y0 = 0, x1, y1, x, y;

    if (!qk_surface_valid(surface))
        return;
    x1 = surface->width;
    y1 = surface->height;
    if (clip) {
        x0 = qk_max(x0, clip->x0);
        y0 = qk_max(y0, clip->y0);
        x1 = qk_min(x1, clip->x1);
        y1 = qk_min(y1, clip->y1);
    }
    for (y = y0; y < y1; y++) {
        u32 *row = qk_row(surface, y);

        for (x = x0; x < x1; x++)
            row[x] = argb;
    }
}

/* Premultiplied source over premultiplied destination. */
static void qk_blend(u32 *pixel, u32 red, u32 green, u32 blue, u32 alpha)
{
    u32 destination, inverse;

    if (!alpha)
        return;
    if (alpha == 255) {
        *pixel = (alpha << 24) | (qk_mul255(red, 255) << 16) |
                 (qk_mul255(green, 255) << 8) | qk_mul255(blue, 255);
        return;
    }
    destination = *pixel;
    inverse = 255 - alpha;
    *pixel = ((alpha + qk_mul255(destination >> 24, inverse)) << 24) |
             ((qk_mul255(red, alpha) + qk_mul255((destination >> 16) & 0xff, inverse)) << 16) |
             ((qk_mul255(green, alpha) + qk_mul255((destination >> 8) & 0xff, inverse)) << 8) |
             (qk_mul255(blue, alpha) + qk_mul255(destination & 0xff, inverse));
}

static void qk_sample(const struct qk_texture *texture, long long u, long long v,
                      u32 *red, u32 *green, u32 *blue, u32 *alpha)
{
    s32 x, y;
    const u8 *texel;

    if (!texture || texture->format == QK_TEXTURE_NONE)
        return;
    /* Nearest neighbour: ImGui maps glyphs texel-to-pixel, and MD3 draws its
     * shapes as geometry rather than as scaled images.
     */
    x = (s32)(u >> QK_UV_BITS);
    y = (s32)(v >> QK_UV_BITS);
    x = qk_max(0, qk_min(x, texture->width - 1));
    y = qk_max(0, qk_min(y, texture->height - 1));
    if (texture->format == QK_TEXTURE_ALPHA8) {
        texel = texture->pixels + (unsigned long)y * texture->pitch + x;
        *alpha = qk_mul255(*alpha, texel[0]);
        return;
    }
    texel = texture->pixels + (unsigned long)y * texture->pitch + (unsigned long)x * 4;
    *red = qk_mul255(*red, texel[0]);
    *green = qk_mul255(*green, texel[1]);
    *blue = qk_mul255(*blue, texel[2]);
    *alpha = qk_mul255(*alpha, texel[3]);
}

static int qk_texture_valid(const struct qk_texture *texture)
{
    if (texture->format == QK_TEXTURE_NONE)
        return 1;
    if (!texture->pixels || texture->width <= 0 || texture->height <= 0 ||
        texture->width > QK_MAX_DIMENSION || texture->height > QK_MAX_DIMENSION)
        return 0;
    return texture->pitch >= (u32)texture->width *
           (texture->format == QK_TEXTURE_RGBA32 ? 4u : 1u);
}

static void qk_setup_edge(struct qk_edge *edge, const struct qk_vertex *a,
                          const struct qk_vertex *b, long long px, long long py)
{
    const long long ax = b->x - a->x, ay = b->y - a->y;

    edge->value = ax * (py - a->y) - ay * (px - a->x);
    edge->dx = -ay * QK_SUBPIXEL;
    edge->dy = ax * QK_SUBPIXEL;
    /* Top-left fill rule, so neighbouring triangles do not blend a shared
     * edge twice. Pixels exactly on an edge are rare at 1/256 precision.
     */
    edge->top_left = ay > 0 || (ay == 0 && ax < 0);
}

static void qk_setup_attribute(struct qk_attribute *attribute, long long a0,
                               long long a1, long long a2, const struct qk_edge *edges)
{
    attribute->value = a0 * edges[0].value + a1 * edges[1].value + a2 * edges[2].value;
    attribute->dx = a0 * edges[0].dx + a1 * edges[1].dx + a2 * edges[2].dx;
    attribute->dy = a0 * edges[0].dy + a1 * edges[1].dy + a2 * edges[2].dy;
}

/* Integer ceiling and floor division with a positive divisor. */
static long long qk_ceil_div(long long numerator, long long denominator)
{
    return numerator >= 0 ? (numerator + denominator - 1) / denominator
                          : -((-numerator) / denominator);
}

static long long qk_floor_div(long long numerator, long long denominator)
{
    return numerator >= 0 ? numerator / denominator
                          : -(((-numerator) + denominator - 1) / denominator);
}

static void qk_triangle(const struct qk_surface *surface, const struct qk_vertex *v,
                        const struct qk_texture *texture, const struct qk_clip *clip,
                        struct qk_raster_stats *stats)
{
    struct qk_edge edges[3];
    struct qk_attribute channels[6] = { { 0, 0, 0 } };
    long long area, reciprocal, px, py;
    s32 x0, y0, x1, y1, y, minimum, maximum;
    unsigned int i, flat_color, textured, modulate = 0;
    u32 modulate_r = 255, modulate_g = 255, modulate_b = 255, modulate_a = 255;
    u32 flat_red = 0, flat_green = 0, flat_blue = 0, flat_alpha = 0, opaque_pixel = 0;
    u32 pixels = 0;

    minimum = (s32)qk_min(qk_min(v[0].x, v[1].x), v[2].x);
    maximum = (s32)qk_max(qk_max(v[0].x, v[1].x), v[2].x);
    x0 = qk_max(clip->x0, minimum >> QK_SUBPIXEL_BITS);
    x1 = qk_min(clip->x1, (maximum + QK_SUBPIXEL - 1) >> QK_SUBPIXEL_BITS);
    minimum = (s32)qk_min(qk_min(v[0].y, v[1].y), v[2].y);
    maximum = (s32)qk_max(qk_max(v[0].y, v[1].y), v[2].y);
    y0 = qk_max(clip->y0, minimum >> QK_SUBPIXEL_BITS);
    y1 = qk_min(clip->y1, (maximum + QK_SUBPIXEL - 1) >> QK_SUBPIXEL_BITS);
    x0 = qk_max(x0, 0);
    y0 = qk_max(y0, 0);
    x1 = qk_min(x1, surface->width);
    y1 = qk_min(y1, surface->height);
    if (x0 >= x1 || y0 >= y1) {
        if (stats)
            stats->skipped++;
        return;
    }

    /* Vertex i is weighted by the edge opposite to it. */
    px = (long long)x0 * QK_SUBPIXEL + QK_SUBPIXEL / 2;
    py = (long long)y0 * QK_SUBPIXEL + QK_SUBPIXEL / 2;
    qk_setup_edge(&edges[0], &v[1], &v[2], px, py);
    qk_setup_edge(&edges[1], &v[2], &v[0], px, py);
    qk_setup_edge(&edges[2], &v[0], &v[1], px, py);
    area = edges[0].value + edges[1].value + edges[2].value;
    if (!area) {
        if (stats)
            stats->skipped++;
        return;
    }
    if (area < 0) {
        /* Normalise the winding so "inside" is one sign test per edge. */
        for (i = 0; i < 3; i++) {
            edges[i].value = -edges[i].value;
            edges[i].dx = -edges[i].dx;
            edges[i].dy = -edges[i].dy;
            edges[i].top_left = !edges[i].top_left;
        }
        area = -area;
    }
    reciprocal = ((long long)1 << QK_RECIPROCAL_BITS) / area;

    flat_color = v[0].color == v[1].color && v[1].color == v[2].color;
    textured = texture && texture->format != QK_TEXTURE_NONE;
    /* ImGui gives every shape vertex the atlas white pixel. One texel for the
     * whole triangle means one fetch here instead of one per pixel, and an
     * opaque white texel means none at all.
     */
    if (textured && v[0].u == v[1].u && v[1].u == v[2].u &&
        v[0].v == v[1].v && v[1].v == v[2].v) {
        qk_sample(texture, v[0].u, v[0].v, &modulate_r, &modulate_g, &modulate_b,
                  &modulate_a);
        textured = 0;
        modulate = !(modulate_r == 255 && modulate_g == 255 && modulate_b == 255 &&
                     modulate_a == 255);
    }
    if (flat_color) {
        flat_red = v[0].color & 0xff;
        flat_green = (v[0].color >> 8) & 0xff;
        flat_blue = (v[0].color >> 16) & 0xff;
        flat_alpha = (v[0].color >> 24) & 0xff;
        if (modulate) {
            flat_red = qk_mul255(flat_red, modulate_r);
            flat_green = qk_mul255(flat_green, modulate_g);
            flat_blue = qk_mul255(flat_blue, modulate_b);
            flat_alpha = qk_mul255(flat_alpha, modulate_a);
            modulate = 0;
        }
        opaque_pixel = (flat_alpha << 24) | (flat_red << 16) | (flat_green << 8) |
                       flat_blue;
    } else {
        for (i = 0; i < 4; i++) {
            const unsigned int shift = i * 8;

            qk_setup_attribute(&channels[i],
                               (long long)((v[0].color >> shift) & 0xff) * QK_SUBPIXEL,
                               (long long)((v[1].color >> shift) & 0xff) * QK_SUBPIXEL,
                               (long long)((v[2].color >> shift) & 0xff) * QK_SUBPIXEL,
                               edges);
        }
    }
    if (textured) {
        qk_setup_attribute(&channels[4], v[0].u, v[1].u, v[2].u, edges);
        qk_setup_attribute(&channels[5], v[0].v, v[1].v, v[2].v, edges);
    }

    for (y = y0; y < y1; y++) {
        u32 *row = qk_row(surface, y);
        long long span_start = x0, span_end = x1;
        s32 x;

        /* Solve the three half-plane inequalities instead of testing every
         * pixel: the row's covered span follows directly from the edges.
         */
        for (i = 0; i < 3; i++) {
            const long long threshold = edges[i].top_left ? 0 : 1;

            if (!edges[i].dx) {
                if (edges[i].value < threshold) {
                    span_end = span_start;
                    break;
                }
                continue;
            }
            if (edges[i].dx > 0) {
                const long long step =
                    qk_ceil_div(threshold - edges[i].value, edges[i].dx);

                if (x0 + step > span_start)
                    span_start = x0 + step;
            } else {
                const long long step =
                    qk_floor_div(edges[i].value - threshold, -edges[i].dx);

                if (x0 + step + 1 < span_end)
                    span_end = x0 + step + 1;
            }
        }
        if (span_start < x0)
            span_start = x0;
        if (span_end > x1)
            span_end = x1;

        if (span_start < span_end) {
            const s32 from = (s32)span_start, to = (s32)span_end;

            if (flat_color && !textured && flat_alpha == 255) {
                /* Opaque flat fill: the MD3 surfaces and containers. */
                for (x = from; x < to; x++)
                    row[x] = opaque_pixel;
            } else if (flat_color && !textured) {
                for (x = from; x < to; x++)
                    qk_blend(&row[x], flat_red, flat_green, flat_blue, flat_alpha);
            } else {
                const s32 offset = from - x0;
                struct qk_attribute values[6];

                for (i = 0; i < 6; i++) {
                    values[i] = channels[i];
                    values[i].value += channels[i].dx * offset;
                }
                for (x = from; x < to; x++) {
                    u32 red, green, blue, alpha;

                    if (flat_color) {
                        red = flat_red;
                        green = flat_green;
                        blue = flat_blue;
                        alpha = flat_alpha;
                    } else {
                        red = (u32)(qk_scale(values[0].value, reciprocal) >> QK_SUBPIXEL_BITS);
                        green = (u32)(qk_scale(values[1].value, reciprocal) >> QK_SUBPIXEL_BITS);
                        blue = (u32)(qk_scale(values[2].value, reciprocal) >> QK_SUBPIXEL_BITS);
                        alpha = (u32)(qk_scale(values[3].value, reciprocal) >> QK_SUBPIXEL_BITS);
                        red = red > 255 ? 255 : red;
                        green = green > 255 ? 255 : green;
                        blue = blue > 255 ? 255 : blue;
                        alpha = alpha > 255 ? 255 : alpha;
                    }
                    if (textured)
                        qk_sample(texture, qk_scale(values[4].value, reciprocal),
                                  qk_scale(values[5].value, reciprocal),
                                  &red, &green, &blue, &alpha);
                    else if (modulate) {
                        red = qk_mul255(red, modulate_r);
                        green = qk_mul255(green, modulate_g);
                        blue = qk_mul255(blue, modulate_b);
                        alpha = qk_mul255(alpha, modulate_a);
                    }
                    qk_blend(&row[x], red, green, blue, alpha);
                    for (i = 0; i < 6; i++)
                        values[i].value += values[i].dx;
                }
            }
            pixels += (u32)(span_end - span_start);
        }

        for (i = 0; i < 3; i++)
            edges[i].value += edges[i].dy;
        for (i = 0; i < 6; i++)
            channels[i].value += channels[i].dy;
    }
    if (stats) {
        stats->triangles++;
        stats->pixels += pixels;
        if (pixels)
            qk_damage(stats, x0, y0, x1, y1);
    }
}

int qk_raster_draw(const struct qk_surface *surface, const struct qk_draw_data *data,
                   struct qk_raster_stats *stats)
{
    u32 command, index, i;

    if (!qk_surface_valid(surface) || !data)
        return -22; /* -EINVAL */
    if ((data->vertex_count && !data->vertices) ||
        (data->index_count && !data->indices) ||
        (data->command_count && !data->commands) ||
        (data->texture_count && !data->textures))
        return -22;
    for (i = 0; i < data->texture_count; i++)
        if (!qk_texture_valid(&data->textures[i]))
            return -22;
    for (i = 0; i < data->vertex_count; i++) {
        const struct qk_vertex *vertex = &data->vertices[i];
        const s32 position = QK_MAX_DIMENSION * QK_SUBPIXEL;
        const s32 texel = QK_MAX_DIMENSION * QK_UV_ONE;

        /* Keep every later product inside the budget above. */
        if (vertex->x < -position || vertex->x > position ||
            vertex->y < -position || vertex->y > position ||
            vertex->u < -texel || vertex->u > texel ||
            vertex->v < -texel || vertex->v > texel)
            return -22;
    }
    if (stats) {
        stats->triangles = 0;
        stats->skipped = 0;
        stats->pixels = 0;
        stats->damage.x0 = stats->damage.y0 = 0;
        stats->damage.x1 = stats->damage.y1 = 0;
    }

    for (command = 0; command < data->command_count; command++) {
        const struct qk_command *draw = &data->commands[command];
        const struct qk_texture *texture = NULL;
        struct qk_clip clip;

        if (draw->index_count % 3 ||
            (u64)draw->index_offset + draw->index_count > data->index_count)
            return -22;
        if (draw->texture) {
            if (draw->texture >= data->texture_count)
                return -22;
            texture = &data->textures[draw->texture];
        }
        clip.x0 = qk_max(0, draw->clip.x0);
        clip.y0 = qk_max(0, draw->clip.y0);
        clip.x1 = qk_min(surface->width, draw->clip.x1);
        clip.y1 = qk_min(surface->height, draw->clip.y1);
        if (clip.x0 >= clip.x1 || clip.y0 >= clip.y1)
            continue;
        for (index = 0; index < draw->index_count; index += 3) {
            const u32 *triangle = &data->indices[draw->index_offset + index];
            struct qk_vertex vertices[3];

            if (triangle[0] >= data->vertex_count || triangle[1] >= data->vertex_count ||
                triangle[2] >= data->vertex_count)
                return -22;
            vertices[0] = data->vertices[triangle[0]];
            vertices[1] = data->vertices[triangle[1]];
            vertices[2] = data->vertices[triangle[2]];
            qk_triangle(surface, vertices, texture, &clip, stats);
        }
    }
    return 0;
}
