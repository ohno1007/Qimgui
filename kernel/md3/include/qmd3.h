/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef QMD3_H
#define QMD3_H
#ifdef __KERNEL__
#include <linux/types.h>
typedef u8 qm_u8;
typedef u16 qm_u16;
typedef u32 qm_u32;
typedef u64 qm_u64;
typedef s16 qm_s16;
typedef s32 qm_s32;
typedef s64 qm_s64;
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint8_t qm_u8;
typedef uint16_t qm_u16;
typedef uint32_t qm_u32;
typedef uint64_t qm_u64;
typedef int16_t qm_s16;
typedef int32_t qm_s32;
typedef int64_t qm_s64;
#endif

#define QM_MAX_DIMENSION 4096
#define QM_MAX_WIDGETS 128
#define QM_MAX_CLIPS 8
#define QM_MAX_POLYGON_POINTS 512
#define QM_MAX_TEXT_BYTES 4096
typedef qm_u32 qm_color; /* Straight alpha, numeric 0xAARRGGBB. */
struct qm_point { int x, y; };
struct qm_rect { int x, y, w, h; };
struct qm_font;

/* Caller-owned CPU pixels: top row first, premultiplied ARGB8888.
 * Each pixel is a native u32, 0xAARRGGBB (BGRA bytes on little endian).
 * Stride is bytes. Caller synchronizes rendering/presentation. This library
 * never acquires a display, input device, or an existing driver's resources.
 */
struct qm_surface {
    qm_u32 *pixels;
    int width, height;
    unsigned stride_bytes;
    qm_u64 size_bytes;
};
struct qm_theme {
    qm_color primary, on_primary, primary_container, on_primary_container;
    qm_color secondary_container, on_secondary_container;
    qm_color surface, surface_container, surface_container_high;
    qm_color on_surface, on_surface_variant, outline, outline_variant;
    qm_color error, on_error, inverse_surface, inverse_on_surface;
    bool dark;
};
enum qm_key {
    QM_KEY_NONE, QM_KEY_TAB, QM_KEY_ENTER, QM_KEY_SPACE,
    QM_KEY_LEFT, QM_KEY_RIGHT, QM_KEY_UP, QM_KEY_DOWN,
    QM_KEY_HOME, QM_KEY_END, QM_KEY_BACKSPACE, QM_KEY_DELETE, QM_KEY_ESCAPE
};
struct qm_input {
    int x, y;
    bool down, pressed, released, shift;
    enum qm_key key;
    char text[32]; /* One NUL-terminated UTF-8 insertion, at most 31 bytes. */
    unsigned dt_ms;
};
enum qm_interaction {
    QM_HOVERED = 1, QM_HELD = 2, QM_ACTIVATED = 4, QM_FOCUSED = 8
};
enum qm_error {
    QM_ERROR_NONE = 0, QM_ERROR_SURFACE = 1, QM_ERROR_CLIP = 2,
    QM_ERROR_WIDGET_LIMIT = 4, QM_ERROR_DUPLICATE_ID = 8,
    QM_ERROR_ARGUMENT = 16, QM_ERROR_FRAME = 32
};

/* One context per UI, kept off the kernel stack. No heap allocation,
 * floating point, global state, or threads. Caller serializes all calls.
 */
struct qm_context {
    struct qm_surface surface;
    struct qm_theme theme;
    const struct qm_font *font;
    struct qm_input input;
    struct qm_rect clips[QM_MAX_CLIPS];
    unsigned clip_depth, errors;
    qm_u32 active_id, focused_id;
    qm_u32 ids[2][QM_MAX_WIDGETS];
    qm_u32 focus_ids[2][QM_MAX_WIDGETS];
    unsigned id_count[2], focus_count[2], id_bank;
    qm_u64 time_ms;
    bool previous_down, in_frame, press_claimed, key_claimed, input_enabled;
};
void qm_init(struct qm_context *ctx, bool dark);
void qm_set_theme(struct qm_context *ctx, bool dark);
/* Validate the surface before writes. Derive pressed/released from down,
 * ignore caller edge fields, clamp dt to 1000ms. IDs are unique and nonzero.
 */
bool qm_begin(struct qm_context *ctx, const struct qm_surface *surface,
              const struct qm_input *input, qm_color clear);
void qm_end(struct qm_context *ctx);
bool qm_set_input_enabled(struct qm_context *ctx, bool enabled);
unsigned qm_interact(struct qm_context *ctx, qm_u32 id,
                     struct qm_rect bounds, bool enabled);
bool qm_contains(struct qm_rect bounds, int x, int y);
struct qm_rect qm_intersection(struct qm_rect a, struct qm_rect b);
bool qm_clip_push(struct qm_context *ctx, struct qm_rect bounds);
void qm_clip_pop(struct qm_context *ctx);
/* Geometry outside +/-32767 is rejected. Writes always honor the clip. */
void qm_fill(struct qm_context *ctx, struct qm_rect bounds, qm_color color);
void qm_round_rect(struct qm_context *ctx, struct qm_rect bounds, int radius,
                   qm_color color);
void qm_stroke_rect(struct qm_context *ctx, struct qm_rect bounds, int radius,
                    int width, qm_color color);
void qm_line(struct qm_context *ctx, int x0, int y0, int x1, int y1,
             int width, qm_color color);
void qm_circle(struct qm_context *ctx, int cx, int cy, int radius, qm_color color);
void qm_polygon(struct qm_context *ctx, const struct qm_point *points,
                unsigned count, qm_color color);
/* Transform [0,units] points into bounds without a temporary point buffer. */
void qm_polygon_transformed(struct qm_context *ctx, const struct qm_point *points,
                            unsigned count, struct qm_rect bounds, int units,
                            qm_color color);
int qm_font_height(const struct qm_context *ctx);
int qm_text_width(const struct qm_context *ctx, const char *text);
void qm_text(struct qm_context *ctx, int x, int y, const char *text, qm_color color);
/* Integer easing: elapsed/duration milliseconds -> 0..65536 inclusive. */
unsigned qm_ease(unsigned elapsed_ms, unsigned duration_ms);
int qm_lerp(int from, int to, unsigned amount_q16);
#endif
