/* SPDX-License-Identifier: GPL-2.0-only */
#include "qmd3.h"
#include "qmd3_assets.h"
#include "qmd3_widgets.h"
#include "qmd3_layout.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WIDTH = 320, HEIGHT = 240, STRIDE_WORDS = WIDTH + 3, GUARD = 8 };
static const qm_u32 sentinel = 0xa5c37e19u;
static struct qm_context *ctx;
static qm_u32 *storage;
static struct qm_surface surface;

static struct qm_input pointer(int x, int y, bool down)
{
    struct qm_input input = {0};
    input.x = x;
    input.y = y;
    input.down = down;
    input.dt_ms = 16;
    return input;
}

static struct qm_input keyboard(enum qm_key key)
{
    struct qm_input input = pointer(-1, -1, false);
    input.key = key;
    return input;
}

static void reset(void)
{
    unsigned i;
    for (i = 0; i < GUARD * 2 + STRIDE_WORDS * HEIGHT; ++i)
        storage[i] = sentinel;
    qm_init(ctx, false);
}

static void begin(struct qm_input input)
{
    assert(qm_begin(ctx, &surface, &input, 0));
}

static qm_u32 pixel(int x, int y)
{
    assert(x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT);
    return surface.pixels[y * STRIDE_WORDS + x];
}

static void guards(void)
{
    unsigned i;
    int x, y;
    for (i = 0; i < GUARD; ++i) {
        assert(storage[i] == sentinel);
        assert(storage[GUARD + STRIDE_WORDS * HEIGHT + i] == sentinel);
    }
    for (y = 0; y < HEIGHT; ++y)
        for (x = WIDTH; x < STRIDE_WORDS; ++x)
            assert(surface.pixels[y * STRIDE_WORDS + x] == sentinel);
}

static unsigned painted(struct qm_rect r)
{
    unsigned count = 0;
    int x, y;
    for (y = r.y; y < r.y + r.h; ++y)
        for (x = r.x; x < r.x + r.w; ++x)
            if (pixel(x, y) != 0)
                ++count;
    return count;
}

static void test_surface_and_clipping(void)
{
    struct qm_surface bad;
    struct qm_input idle = pointer(-1, -1, false);
    unsigned i;
    reset();
    bad = surface;
    bad.width = QM_MAX_DIMENSION + 1;
    assert(!qm_begin(ctx, &bad, &idle, 0));
    bad = surface;
    bad.stride_bytes = WIDTH * 4 - 1;
    assert(!qm_begin(ctx, &bad, &idle, 0));
    bad = surface;
    bad.size_bytes = 1;
    assert(!qm_begin(ctx, &bad, &idle, 0));
    bad = surface;
    bad.pixels = NULL;
    assert(!qm_begin(ctx, &bad, &idle, 0));
    bad = surface;
    bad.height = -1;
    assert(!qm_begin(ctx, &bad, &idle, 0));
    for (i = 0; i < GUARD * 2 + STRIDE_WORDS * HEIGHT; ++i)
        assert(storage[i] == sentinel);

    reset();
    begin(idle);
    assert(pixel(0, 0) == 0 && pixel(WIDTH - 1, HEIGHT - 1) == 0);
    qm_fill(ctx, (struct qm_rect){1, 1, 4, 3}, 0x80ff0000u);
    assert(pixel(1, 1) == 0x80800000u);
    assert(pixel(4, 3) == 0x80800000u);
    assert(pixel(5, 3) == 0 && pixel(1, 4) == 0);
    qm_fill(ctx, (struct qm_rect){2, 2, 1, 1}, 0x800000ffu);
    assert(pixel(2, 2) == 0xc0400080u);
    assert(qm_clip_push(ctx, (struct qm_rect){10, 10, 12, 12}));
    assert(qm_clip_push(ctx, (struct qm_rect){15, 15, 12, 12}));
    qm_fill(ctx, (struct qm_rect){0, 0, WIDTH, HEIGHT}, 0xff00ff00u);
    assert(pixel(15, 15) == 0xff00ff00u);
    assert(pixel(21, 21) == 0xff00ff00u);
    assert(pixel(14, 15) == 0 && pixel(22, 21) == 0);
    qm_clip_pop(ctx);
    qm_clip_pop(ctx);
    qm_fill(ctx, (struct qm_rect){WIDTH - 1, HEIGHT - 1, 5, 5}, 0xff0000ffu);
    assert(pixel(WIDTH - 1, HEIGHT - 1) == 0xff0000ffu);
    guards();
    qm_end(ctx);

    reset();
    begin(idle);
    for (i = 1; i < QM_MAX_CLIPS; ++i)
        assert(qm_clip_push(ctx, (struct qm_rect){0, 0, WIDTH, HEIGHT}));
    assert(!qm_clip_push(ctx, (struct qm_rect){0, 0, WIDTH, HEIGHT}));
    assert(ctx->errors & QM_ERROR_CLIP);
    for (i = 1; i < QM_MAX_CLIPS; ++i)
        qm_clip_pop(ctx);
    qm_clip_pop(ctx);
    assert(ctx->errors & QM_ERROR_CLIP);
    qm_end(ctx);
}

static void test_geometry_bounds(void)
{
    const struct qm_point concave[] = {
        {10, 10}, {50, 10}, {50, 50}, {30, 30}, {10, 50}
    };
    const struct qm_point unit_square[] = {
        {0, 0}, {4096, 0}, {4096, 4096}, {0, 4096}
    };
    struct qm_rect r;
    reset();
    begin(pointer(-1, -1, false));
    qm_round_rect(ctx, (struct qm_rect){10, 10, 40, 30}, 12, 0xffffffffu);
    assert(pixel(30, 25) == 0xffffffffu);
    assert(pixel(10, 10) == 0);
    qm_stroke_rect(ctx, (struct qm_rect){70, 10, 40, 30}, 8, 2, 0xff00ff00u);
    assert(pixel(90, 10) != 0);
    assert(pixel(90, 25) == 0);
    qm_circle(ctx, 140, 30, 10, 0xffff0000u);
    assert(pixel(140, 30) == 0xffff0000u);
    assert(pixel(125, 30) == 0);
    qm_line(ctx, 10, 70, 60, 70, 3, 0xff0000ffu);
    assert(pixel(30, 70) == 0xff0000ffu);
    assert(pixel(30, 75) == 0);
    qm_polygon_transformed(ctx, unit_square, 4,
                           (struct qm_rect){180, 10, 30, 30}, 4096, 0xffffffffu);
    assert(pixel(195, 25) == 0xffffffffu);
    assert(pixel(175, 25) == 0);
    qm_end(ctx);
    guards();

    reset();
    begin(pointer(-1, -1, false));
    qm_polygon(ctx, concave, 5, 0xffffffffu);
    assert(pixel(30, 20) != 0);
    assert(pixel(30, 42) == 0);
    qm_end(ctx);
    guards();

    reset();
    begin(pointer(-1, -1, false));
    qm_fill(ctx, (struct qm_rect){INT_MAX, INT_MIN, INT_MAX, INT_MAX}, 0xffffffffu);
    qm_round_rect(ctx, (struct qm_rect){INT_MIN, 0, INT_MAX, 10}, INT_MAX, 0xffffffffu);
    qm_stroke_rect(ctx, (struct qm_rect){INT_MAX, 0, INT_MAX, 10}, INT_MAX, INT_MAX,
                   0xffffffffu);
    qm_circle(ctx, INT_MAX, INT_MIN, INT_MAX, 0xffffffffu);
    qm_line(ctx, INT_MIN, INT_MIN, INT_MAX, INT_MAX, INT_MAX, 0xffffffffu);
    qm_polygon(ctx, NULL, 10, 0xffffffffu);
    qm_polygon_transformed(ctx, unit_square, 4, (struct qm_rect){0, 0, 10, 10}, 0,
                           0xffffffffu);
    assert(painted((struct qm_rect){0, 0, WIDTH, HEIGHT}) == 0);
    qm_end(ctx);
    guards();
    r = qm_intersection((struct qm_rect){INT_MAX - 2, 0, 10, 10},
                        (struct qm_rect){0, 0, 10, 10});
    assert(r.w <= 0 || r.h <= 0);
    assert(!qm_contains((struct qm_rect){INT_MAX - 2, 0, 10, 10}, 0, 0));
    assert(qm_lerp(INT_MIN, INT_MAX, 0) == INT_MIN);
    assert(qm_lerp(INT_MIN, INT_MAX, 65536) == INT_MAX);
    assert(qm_ease(0, 100) == 0);
    assert(qm_ease(100, 100) == 65536);
    assert(qm_ease(UINT_MAX, 100) == 65536);
}

static unsigned interaction(struct qm_input input, bool enabled)
{
    unsigned result;
    begin(input);
    result = qm_interact(ctx, 1, (struct qm_rect){10, 10, 80, 40}, enabled);
    qm_end(ctx);
    return result;
}

static void test_interaction(void)
{
    unsigned flags;
    reset();
    flags = interaction(pointer(20, 20, true), true);
    assert((flags & QM_HELD) && !(flags & QM_ACTIVATED));
    assert(interaction(pointer(20, 20, false), true) & QM_ACTIVATED);
    assert(!(interaction(pointer(20, 20, false), true) & QM_ACTIVATED));

    reset();
    interaction(pointer(20, 20, true), true);
    interaction(pointer(250, 200, true), true);
    assert(!(interaction(pointer(250, 200, false), true) & QM_ACTIVATED));
    assert(!(interaction(pointer(20, 20, false), true) & QM_ACTIVATED));
    assert(ctx->active_id == 0);

    reset();
    interaction(pointer(20, 20, true), true);
    assert(!(interaction(pointer(20, 20, true), false) & (QM_HELD | QM_ACTIVATED)));
    assert(!(interaction(pointer(20, 20, false), true) & QM_ACTIVATED));

    reset();
    begin(pointer(20, 20, true));
    assert(qm_set_input_enabled(ctx, false));
    assert(!qm_interact(ctx, 1, (struct qm_rect){10, 10, 80, 40}, true));
    assert(!qm_set_input_enabled(ctx, true));
    assert(qm_interact(ctx, 2, (struct qm_rect){10, 10, 80, 40}, true) & QM_HELD);
    qm_end(ctx);
    begin(pointer(20, 20, false));
    assert(qm_interact(ctx, 2, (struct qm_rect){10, 10, 80, 40}, true) & QM_ACTIVATED);
    qm_end(ctx);

    reset();
    begin(pointer(20, 20, true));
    assert(qm_clip_push(ctx, (struct qm_rect){100, 100, 20, 20}));
    assert(!qm_interact(ctx, 1, (struct qm_rect){10, 10, 80, 40}, true));
    qm_clip_pop(ctx);
    qm_end(ctx);

    reset();
    begin(pointer(-1, -1, false));
    qm_interact(ctx, 1, (struct qm_rect){10, 10, 80, 40}, true);
    qm_interact(ctx, 1, (struct qm_rect){100, 10, 80, 40}, true);
    assert(ctx->errors & QM_ERROR_DUPLICATE_ID);
    qm_end(ctx);
}

static void register_focus_widgets(void)
{
    qm_interact(ctx, 11, (struct qm_rect){0, 0, 80, 30}, true);
    qm_interact(ctx, 12, (struct qm_rect){0, 40, 80, 30}, false);
    qm_interact(ctx, 13, (struct qm_rect){0, 80, 80, 30}, true);
}

static void test_focus(void)
{
    struct qm_input input;
    reset();
    begin(pointer(-1, -1, false));
    register_focus_widgets();
    qm_end(ctx);
    begin(keyboard(QM_KEY_TAB));
    register_focus_widgets();
    assert(ctx->focused_id == 11);
    qm_end(ctx);
    begin(keyboard(QM_KEY_TAB));
    register_focus_widgets();
    assert(ctx->focused_id == 13);
    qm_end(ctx);
    input = keyboard(QM_KEY_TAB);
    input.shift = true;
    begin(input);
    register_focus_widgets();
    assert(ctx->focused_id == 11);
    qm_end(ctx);
}

static void test_selection_widgets(void)
{
    bool checked = false, switched = false;
    int selected = 1;
    reset();
    begin(pointer(20, 20, true));
    assert(!qm_checkbox(ctx, 1, (struct qm_rect){10, 10, 180, 40}, "Check", &checked,
                        false, true));
    qm_end(ctx);
    begin(pointer(20, 20, false));
    assert(qm_checkbox(ctx, 1, (struct qm_rect){10, 10, 180, 40}, "Check", &checked,
                       false, true));
    assert(checked);
    qm_end(ctx);
    ctx->focused_id = 2;
    begin(keyboard(QM_KEY_SPACE));
    assert(qm_switch(ctx, 2, (struct qm_rect){10, 10, 200, 40}, "Switch", &switched,
                     true));
    assert(switched);
    qm_end(ctx);
    ctx->focused_id = 3;
    begin(keyboard(QM_KEY_ENTER));
    assert(!qm_radio(ctx, 3, (struct qm_rect){10, 10, 180, 40}, "Radio", &selected,
                     2, false));
    assert(selected == 1);
    qm_end(ctx);
    ctx->focused_id = 3;
    begin(keyboard(QM_KEY_ENTER));
    assert(qm_radio(ctx, 3, (struct qm_rect){10, 10, 180, 40}, "Radio", &selected,
                    2, true));
    assert(selected == 2);
    qm_end(ctx);
    guards();
}

static void test_sliders(void)
{
    int value = 73, lo = -10, hi = 10;
    struct qm_range_state range = {0};
    struct qm_rect r = {10, 10, 240, 40};
    reset();
    begin(pointer(-1, -1, false));
    assert(!qm_slider(ctx, 1, r, "Slider", &value, 0, 100, 4, true));
    assert(value == 73); /* Showing a control must not quantize caller state. */
    qm_end(ctx);
    ctx->focused_id = 1;
    begin(keyboard(QM_KEY_END));
    assert(qm_slider(ctx, 1, r, "Slider", &value, INT_MIN, INT_MAX, 0, true));
    assert(value == INT_MAX);
    qm_end(ctx);
    ctx->focused_id = 1;
    begin(keyboard(QM_KEY_HOME));
    assert(qm_slider(ctx, 1, r, "Slider", &value, INT_MIN, INT_MAX, 0, true));
    assert(value == INT_MIN);
    qm_end(ctx);
    ctx->focused_id = 1;
    begin(keyboard(QM_KEY_RIGHT));
    assert(qm_slider(ctx, 1, r, "Slider", &value, INT_MIN, INT_MAX, 0, true));
    assert(value == INT_MIN + 1);
    qm_end(ctx);
    ctx->focused_id = 1;
    begin(keyboard(QM_KEY_END));
    assert(!qm_slider(ctx, 1, r, "Slider", &value, INT_MIN, INT_MAX, 0, false));
    assert(value == INT_MIN + 1);
    qm_end(ctx);

    ctx->focused_id = 2;
    begin(keyboard(QM_KEY_END));
    qm_range_slider(ctx, 2, r, "Range", &lo, &hi, -100, 100, 0, &range, true);
    assert(lo <= hi && lo >= -100 && hi <= 100);
    qm_end(ctx);
    begin(pointer(-1, -1, false));
    assert(!qm_slider(ctx, 3, r, "Invalid", &value, 2, 1, 0, true));
    assert(!qm_slider(ctx, 4, r, "Invalid", &value, 0, 100, 1025, true));
    qm_end(ctx);
    guards();
}

static bool text_frame(struct qm_input input, char *buffer, unsigned capacity,
                       struct qm_text_state *state, bool enabled)
{
    bool changed;
    begin(input);
    changed = qm_text_field(ctx, 30, (struct qm_rect){10, 10, 240, 56}, "Name",
                            buffer, capacity, state, QM_TEXT_OUTLINED, NULL,
                            false, enabled);
    qm_end(ctx);
    return changed;
}

static void test_text_editing(void)
{
    struct qm_text_state state = {0};
    struct qm_input input;
    char buffer[16] = "A";
    char tight[4] = "AB";
    char unterminated[3] = {'x', 'y', 'z'};
    reset();
    ctx->focused_id = 30;
    state.caret = 1;
    input = keyboard(QM_KEY_NONE);
    memcpy(input.text, "\xc3\xa9", 3);
    assert(text_frame(input, buffer, sizeof(buffer), &state, true));
    assert(!strcmp(buffer, "A\xc3\xa9") && state.caret == 3);
    ctx->focused_id = 30;
    assert(text_frame(keyboard(QM_KEY_BACKSPACE), buffer, sizeof(buffer), &state, true));
    assert(!strcmp(buffer, "A") && state.caret == 1);
    ctx->focused_id = 30;
    assert(!text_frame(keyboard(QM_KEY_HOME), buffer, sizeof(buffer), &state, true));
    assert(state.caret == 0);
    ctx->focused_id = 30;
    assert(text_frame(keyboard(QM_KEY_DELETE), buffer, sizeof(buffer), &state, true));
    assert(!strcmp(buffer, ""));

    state.caret = 2;
    ctx->focused_id = 30;
    assert(!text_frame(input, tight, sizeof(tight), &state, true));
    assert(!strcmp(tight, "AB")); /* Never insert half a UTF-8 codepoint. */
    ctx->focused_id = 30;
    state.caret = 0;
    assert(!text_frame(input, unterminated, sizeof(unterminated), &state, true));
    assert(!memcmp(unterminated, "xyz", sizeof(unterminated)));
    ctx->focused_id = 30;
    assert(!text_frame(input, buffer, sizeof(buffer), &state, false));
    assert(!strcmp(buffer, ""));
    guards();
}

static void test_assets_and_shapes(void)
{
    enum qm_shape shape;
    const struct qm_font *font;
    const struct qm_glyph *glyph;
    struct qm_context local;
    struct qm_surface local_surface;
    qm_u32 *local_pixels;
    enum qm_icon icon;
    unsigned i, j;

    font = qm_default_font();
    assert(font && font->glyph_count > 0 && font->line_height > 0);
    glyph = qm_font_glyph(font, (qm_u32)'A');
    assert(glyph && glyph->width && glyph->height && glyph->advance);
    assert(glyph->offset + (qm_u32)glyph->width * glyph->height <= font->coverage_size);
    assert(qm_font_glyph(font, 0x10ffffu) != NULL); /* '?' fallback is explicit. */
    assert(qm_icon_from_token("@clock", &icon) && icon == QM_ICON_CLOCK);
    assert(qm_icon_from_token("@chevron_down", &icon) && icon == QM_ICON_CHEVRON_DOWN);
    assert(qm_icon_from_token("@check", &icon) && icon == QM_ICON_CHECK);
    assert(!qm_icon_from_token("@does_not_exist", &icon));

    local_pixels = calloc(64 * 64, sizeof(*local_pixels));
    assert(local_pixels);
    local_surface = (struct qm_surface){local_pixels, 64, 64, 64 * sizeof(*local_pixels),
                                        64 * 64 * sizeof(*local_pixels)};
    qm_init(&local, false);
    assert(qm_begin(&local, &local_surface, &(struct qm_input){0}, 0));
    for (shape = 0; shape < QM_SHAPE_COUNT; ++shape) {
        const struct qm_shape_data *data = qm_get_shape(shape);
        assert(data && data->name && data->points && data->count > 2);
        assert(data->count <= QM_MAX_POLYGON_POINTS);
        for (i = 0; i < data->count; ++i) {
            assert(data->points[i].x >= 0 && data->points[i].x <= QM_SHAPE_UNITS);
            assert(data->points[i].y >= 0 && data->points[i].y <= QM_SHAPE_UNITS);
        }
        qm_draw_shape(&local, shape, (struct qm_rect){2, 2, 60, 60}, 0xffffffffu);
        for (j = i = 0; i < 64 * 64; ++i)
            j += local_pixels[i] != 0;
        assert(j > 0);
        memset(local_pixels, 0, 64 * 64 * sizeof(*local_pixels));
    }
    qm_draw_icon(&local, QM_ICON_CLOCK, (struct qm_rect){10, 10, 32, 32}, 0xffffffffu);
    assert(local_pixels[20 * 64 + 26] != 0 || local_pixels[26 * 64 + 20] != 0);
    qm_end(&local);
    free(local_pixels);
}

static void test_layout_composition(void)
{
    const struct qm_item items[] = {
        {101, "Alarm", "@alarm", 0, 0},
        {102, "Clock", "@clock", 1, 2},
        {103, "Timer", "@timer", 0, -1},
    };
    int selected = 0;
    int expanded = 0;
    qm_u32 selected_mask = 0;
    qm_u32 remaining = 4000;
    int open = 1;
    struct qm_rect content;
    struct qm_stack stack;
    struct qm_rect next;
    int result;

    reset();
    begin(pointer(-1, -1, false));
    qm_stack_init(&stack, (struct qm_rect){0, 0, 120, 200}, 1, 4);
    next = qm_stack_next(&stack, 20);
    assert(next.w == 120 && next.h == 20 && next.y == 0);
    next = qm_stack_next(&stack, 20);
    assert(next.y == 24);
    result = qm_navigation(ctx, (struct qm_rect){0, 0, 120, 200}, items, 3,
                            &selected, QM_NAV_RAIL);
    assert(result >= -1 && selected >= 0 && selected < 3 && selected != 1);
    assert(qm_tabs(ctx, (struct qm_rect){130, 0, 180, 44}, items, 3, &selected) >= -1);
    assert(qm_segmented(ctx, (struct qm_rect){130, 50, 180, 44}, items, 3, &selected) >= -1);
    assert(qm_multi_segmented(ctx, (struct qm_rect){130, 100, 180, 44}, items, 3,
                              &selected_mask, 1) >= -1);
    assert(qm_button_group(ctx, (struct qm_rect){130, 150, 180, 44}, items, 3,
                           &selected, QM_GROUP_TONAL) >= -1);
    assert(qm_toolbar(ctx, (struct qm_rect){130, 200, 180, 36}, items, 3,
                      &selected_mask, 0, 0) >= -1);
    assert(qm_split_button(ctx, (struct qm_rect){130, 210, 180, 28}, 401, 402,
                           "Share", &expanded, 1) == QM_SPLIT_NONE);
    qm_card_surface(ctx, (struct qm_rect){10, 200, 100, 30}, QM_CARD_FILLED);
    assert(qm_card(ctx, 501, (struct qm_rect){10, 200, 100, 30}, "Card", "Body",
                   QM_CARD_OUTLINED, 1) >= -1);
    assert(qm_list_item(ctx, 502, (struct qm_rect){10, 230, 100, 10}, "List", NULL,
                        NULL, 0, 1) >= -1);
    qm_divider(ctx, (struct qm_rect){10, 239, 100, 1}, 0);
    qm_badge(ctx, 50, 205, 4);
    qm_linear_progress(ctx, (struct qm_rect){0, 0, 100, 4}, 500, 0, 0);
    qm_circular_progress(ctx, (struct qm_rect){60, 60, 24, 24}, -1, 10, 0);
    assert(qm_dialog_panel(ctx, (struct qm_rect){0, 0, WIDTH, HEIGHT},
                           (struct qm_rect){50, 50, 180, 100}, "Dialog", &open,
                           1, &content));
    assert(open && content.w > 0 && content.h > 0);
    assert(qm_sheet_panel(ctx, (struct qm_rect){0, 0, WIDTH, HEIGHT},
                          QM_SHEET_RIGHT, 100, &open, 1, &content));
    assert(content.x >= 0 && content.x + content.w <= WIDTH);
    assert(qm_snackbar(ctx, (struct qm_rect){0, HEIGHT - 40, WIDTH, 40}, "Saved",
                       "Undo", 701, 702, &open, &remaining, 1) >= 0);
    qm_end(ctx);
    guards();

    reset();
    begin(pointer(-1, -1, false));
    assert(qm_navigation(ctx, (struct qm_rect){0, 0, 100, 100}, items, 0,
                         &selected, QM_NAV_RAIL) <= 0);
    assert(qm_tabs(ctx, (struct qm_rect){0, 0, 100, 40}, items, 33, &selected) <= 0);
    assert(qm_multi_segmented(ctx, (struct qm_rect){0, 0, 100, 40}, items, 3,
                              &selected_mask, 0) <= 0);
    qm_end(ctx);
    guards();
}

int main(void)
{
    ctx = calloc(1, sizeof(*ctx));
    storage = malloc((GUARD * 2 + STRIDE_WORDS * HEIGHT) * sizeof(*storage));
    assert(ctx && storage);
    surface = (struct qm_surface){storage + GUARD, WIDTH, HEIGHT,
                                  STRIDE_WORDS * sizeof(qm_u32),
                                  STRIDE_WORDS * HEIGHT * sizeof(qm_u32)};
    test_surface_and_clipping();
    test_geometry_bounds();
    test_interaction();
    test_focus();
    test_selection_widgets();
    test_sliders();
    test_text_editing();
    test_assets_and_shapes();
    test_layout_composition();
    free(storage);
    free(ctx);
    puts("PASS: kernel MD3 portable raster, clipping, input, controls, UTF-8 and bounds");
    return 0;
}
