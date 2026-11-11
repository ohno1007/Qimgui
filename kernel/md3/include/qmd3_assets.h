/* SPDX-License-Identifier: Apache-2.0 */
#ifndef QMD3_ASSETS_H
#define QMD3_ASSETS_H

#include "qmd3.h"

/* Coverage is tightly packed, row-major, one byte per pixel. Bearings are
 * relative to the glyph's baseline. The caller owns custom font storage and
 * must keep it alive while attached to a context. Glyphs are sorted by codepoint.
 * The bundled Roboto font is 18 px, ASCII U+0020..U+007E; caller-supplied fonts
 * can extend coverage. Missing glyphs fall back to '?' if that glyph exists. */
struct qm_glyph {
    qm_u32 codepoint, offset;
    qm_u16 width, height;
    qm_s16 bearing_x, bearing_y;
    qm_u16 advance;
};

struct qm_font {
    const qm_u8 *coverage;
    qm_u32 coverage_size;
    const struct qm_glyph *glyphs;
    qm_u32 glyph_count;
    int line_height, ascent;
};

const struct qm_font *qm_default_font(void);
const struct qm_glyph *qm_font_glyph(const struct qm_font *font, qm_u32 codepoint);

#define QM_SHAPE_UNITS 4096
enum qm_shape {
    QM_SHAPE_CIRCLE, QM_SHAPE_SQUARE, QM_SHAPE_SLANTED, QM_SHAPE_ARCH,
    QM_SHAPE_FAN, QM_SHAPE_ARROW, QM_SHAPE_SEMI_CIRCLE, QM_SHAPE_OVAL,
    QM_SHAPE_PILL, QM_SHAPE_TRIANGLE, QM_SHAPE_DIAMOND, QM_SHAPE_CLAM_SHELL,
    QM_SHAPE_PENTAGON, QM_SHAPE_GEM, QM_SHAPE_SUNNY, QM_SHAPE_VERY_SUNNY,
    QM_SHAPE_COOKIE4_SIDED, QM_SHAPE_COOKIE6_SIDED, QM_SHAPE_COOKIE7_SIDED,
    QM_SHAPE_COOKIE9_SIDED, QM_SHAPE_COOKIE12_SIDED, QM_SHAPE_GHOSTISH,
    QM_SHAPE_CLOVER4_LEAF, QM_SHAPE_CLOVER8_LEAF, QM_SHAPE_BURST,
    QM_SHAPE_SOFT_BURST, QM_SHAPE_BOOM, QM_SHAPE_SOFT_BOOM, QM_SHAPE_FLOWER,
    QM_SHAPE_PUFFY, QM_SHAPE_PUFFY_DIAMOND, QM_SHAPE_PIXEL_CIRCLE,
    QM_SHAPE_PIXEL_TRIANGLE, QM_SHAPE_BUN, QM_SHAPE_HEART, QM_SHAPE_COUNT
};

/* Immutable closed contours with no duplicate terminal point, normalized to
 * 0..4096. These are flattened, quantized approximations of the 35 official
 * cubic presets. See generated/provenance.json for error bounds and sources. */
struct qm_shape_data {
    const char *name;
    const struct qm_point *points;
    unsigned int count;
};

const struct qm_shape_data *qm_get_shape(enum qm_shape shape);
/* Preserve the preset's square normalization, centered in bounds. */
void qm_draw_shape(struct qm_context *ctx, enum qm_shape shape,
                   struct qm_rect bounds, qm_color color);

enum qm_icon {
    QM_ICON_HOME, QM_ICON_WIDGETS, QM_ICON_SETTINGS, QM_ICON_SPEED, QM_ICON_INFO,
    QM_ICON_MENU, QM_ICON_CLOSE, QM_ICON_MINIMIZE, QM_ICON_SUN, QM_ICON_MOON,
    QM_ICON_EXPAND, QM_ICON_FORMAT_BOLD, QM_ICON_FORMAT_ITALIC,
    QM_ICON_FORMAT_UNDERLINED, QM_ICON_FORMAT_COLOR_TEXT, QM_ICON_FORMAT_COLOR_FILL,
    QM_ICON_PERSON_ADD, QM_ICON_SHARE, QM_ICON_ALARM, QM_ICON_CLOCK, QM_ICON_TIMER,
    QM_ICON_STOPWATCH, QM_ICON_PLAY, QM_ICON_PAUSE, QM_ICON_RESET, QM_ICON_PLUS,
    QM_ICON_CHECK, QM_ICON_CHEVRON_DOWN, QM_ICON_COUNT
};

/* Accepts the existing SDK's @token spelling, plus @check and @chevron_down.
 * Unknown/null tokens return false and leave *icon unchanged. */
bool qm_icon_from_token(const char *token, enum qm_icon *icon);
void qm_draw_icon(struct qm_context *ctx, enum qm_icon icon,
                  struct qm_rect bounds, qm_color color);

#endif
