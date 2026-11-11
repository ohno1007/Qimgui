/* SPDX-License-Identifier: MIT */
/* Integer software rasteriser for ImGui draw lists.
 *
 * Positions and interpolated attributes are 8.8 fixed point: 1/256 of a pixel
 * is finer than the one-pixel anti-aliasing fringes ImGui emits, and 1/256 of
 * a colour step is below what an 8-bit surface can show. Everything here is
 * integer arithmetic, so this runs with preemption enabled, outside the
 * floating-point region that produced the draw list (see docs/KERNEL_PORT.md).
 *
 * Output pixels are premultiplied ARGB8888, which is what the DRM plane is
 * configured for: blend_op=premultiplied.
 */
#ifndef QK_RASTER_H
#define QK_RASTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qk_types.h"

#define QK_SUBPIXEL_BITS 8
#define QK_SUBPIXEL      (1 << QK_SUBPIXEL_BITS)
/* Texture coordinates carry less precision than positions so that the
 * attribute numerators stay inside 64 bits: 1/16 of a texel still lands on the
 * right texel for nearest sampling.
 */
#define QK_UV_BITS       4
#define QK_UV_ONE        (1 << QK_UV_BITS)
/* Bounds that keep every edge-function product inside 64 bits. */
#define QK_MAX_DIMENSION 4096

enum qk_texture_format {
    QK_TEXTURE_NONE = 0,   /* Untextured: vertex colour only. */
    QK_TEXTURE_ALPHA8,     /* One byte of coverage per texel. */
    QK_TEXTURE_RGBA32,     /* Non-premultiplied R,G,B,A bytes per texel. */
};

struct qk_surface {
    u32 *pixels;
    s32 width, height;
    u32 pitch;             /* Bytes per row; must be a multiple of four. */
};

struct qk_texture {
    const u8 *pixels;
    s32 width, height;
    u32 pitch;
    int format;            /* enum qk_texture_format */
};

struct qk_vertex {
    s32 x, y;              /* 8.8 surface pixels. */
    s32 u, v;              /* 12.4 texels. */
    u32 color;             /* R | G<<8 | B<<16 | A<<24, not premultiplied. */
};

struct qk_clip {
    s32 x0, y0, x1, y1;    /* Half-open pixel rect. */
};

struct qk_command {
    u32 index_offset;
    u32 index_count;
    u32 texture;           /* Index into qk_draw_data.textures. */
    struct qk_clip clip;
};

struct qk_draw_data {
    const struct qk_vertex *vertices;
    u32 vertex_count;
    const u32 *indices;    /* Flattened: vertex offsets already folded in. */
    u32 index_count;
    const struct qk_command *commands;
    u32 command_count;
    const struct qk_texture *textures;
    u32 texture_count;
};

struct qk_raster_stats {
    u32 triangles;         /* Triangles accepted. */
    u32 skipped;           /* Degenerate, off-surface or fully clipped. */
    u32 pixels;            /* Pixels blended. */
    struct qk_clip damage; /* Union of touched pixels; empty rect if none. */
};

/* Fills a clipped rect with one premultiplied ARGB8888 value. */
void qk_surface_fill(const struct qk_surface *surface, const struct qk_clip *clip,
                     u32 argb);
/* Returns 0, or a negative errno for malformed inputs. Stats may be NULL. */
int qk_raster_draw(const struct qk_surface *surface, const struct qk_draw_data *data,
                   struct qk_raster_stats *stats);

#ifdef __cplusplus
}
#endif

#endif /* QK_RASTER_H */
