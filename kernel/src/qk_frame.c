/* SPDX-License-Identifier: MIT */
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>

#include "qk_app.h"
#include "qk_arena.h"
#include "qk_fpu.h"
#include "qk_frame.h"
#include "qk_input.h"
#include "qk_present.h"
#include "qk_raster.h"
#include "qk_ui.h"
#include "qk_utils.h"

#define QK_EVENT_BATCH   64
#define QK_SNAPSHOT_MAX  (4u << 20)

static struct {
    struct workqueue_struct *queue;
    struct delayed_work work;
    struct qk_window *window;
    void *arena;
    size_t arena_bytes;
    unsigned char *snapshot;
    u32 snapshot_bytes;
    bool running;
    bool ui_ready;
    unsigned int interval_ms, idle_ms;
    u64 last_frame_ns;
    u64 frames, skipped_frames, raster_errors, ui_errors, fpu_busy, settings_writes;
    bool exit_seen;
    u64 raster_ns_total, ui_ns_total;
    unsigned int idle_streak;
    struct qk_raster_stats last_stats;
    /* What each buffer still has ink on, so a frame clears only that. */
    struct qk_clip dirty[2];
} frame;

/* The draw data is the whole picture: identical bytes mean identical pixels,
 * so an unchanged UI costs no rasterising, no commit and no display traffic.
 */
static bool qk_unchanged(const struct qk_draw_data *data)
{
    const u32 vertices = data->vertex_count * (u32)sizeof(struct qk_vertex);
    const u32 indices = data->index_count * (u32)sizeof(u32);
    const u32 commands = data->command_count * (u32)sizeof(struct qk_command);
    const u32 total = vertices + indices + commands;
    bool same;

    if (!frame.snapshot || total > QK_SNAPSHOT_MAX)
        return false;
    same = total == frame.snapshot_bytes &&
           !memcmp(frame.snapshot, data->vertices, vertices) &&
           !memcmp(frame.snapshot + vertices, data->indices, indices) &&
           !memcmp(frame.snapshot + vertices + indices, data->commands, commands);
    if (!same) {
        memcpy(frame.snapshot, data->vertices, vertices);
        memcpy(frame.snapshot + vertices, data->indices, indices);
        memcpy(frame.snapshot + vertices + indices, data->commands, commands);
        frame.snapshot_bytes = total;
    }
    return same;
}

static int qk_render(const struct qk_draw_data *data)
{
    struct qk_surface surface = { 0 };
    const int back = qk_present_back_index();
    u32 pitch = 0;
    int ret;

    surface.pixels = qk_present_back_buffer(&pitch);
    if (!surface.pixels)
        return -ENODEV;
    surface.width = frame.window->width;
    surface.height = frame.window->height;
    surface.pitch = pitch;
    /* Transparent is what lets Android show through, so every pixel this
     * buffer still has ink on is cleared -- and only those: the rest of the
     * plane was never written and is already clear.
     */
    if (back >= 0 && back < 2)
        qk_surface_fill(&surface, &frame.dirty[back], 0);
    ret = qk_raster_draw(&surface, data, &frame.last_stats);
    if (ret)
        return ret;
    if (back >= 0 && back < 2)
        frame.dirty[back] = frame.last_stats.damage;
    return qk_present_swap();
}

static void qk_run_frame(void)
{
    struct qk_ui_event events[QK_EVENT_BATCH];
    const struct qk_draw_data *data = NULL;
    u64 now = ktime_get_ns(), started;
    u32 count;
    int ret;

    count = qk_input_drain(events, QK_EVENT_BATCH);
    started = now;
    /* Floating point only inside this region, and nothing that sleeps. */
    ret = qk_fpu_begin();
    if (ret) {
        frame.fpu_busy++;
        return;
    }
    ret = qk_ui_frame(frame.last_frame_ns ? div_u64(now - frame.last_frame_ns, 1000)
                                          : 16667,
                      events, count, &data);
    qk_fpu_end();
    frame.last_frame_ns = now;
    frame.ui_ns_total += ktime_get_ns() - started;
    if (ret || !data) {
        frame.ui_errors++;
        LOGE("UI frame failed: %d; stopping the window", ret);
        qk_present_set_enabled(false);
        qk_present_update();
        return;
    }

    if (qk_unchanged(data) && !count) {
        frame.skipped_frames++;
        frame.idle_streak++;
        return;
    }
    frame.idle_streak = 0;
    /* Settings touch the filesystem, so they are written here, between
     * frames, and never from inside the floating-point region.
     */
    if (qk_app_save_settings() > 0)
        frame.settings_writes++;
    if (qk_ui_wants_exit() && !frame.exit_seen) {
        frame.exit_seen = true;
        LOGI("Window closed from the interface; set enabled=1 to show it again");
        qk_present_set_enabled(false);
        qk_present_update();
        return;
    }
    started = ktime_get_ns();
    ret = qk_render(data);
    frame.raster_ns_total += ktime_get_ns() - started;
    if (ret) {
        frame.raster_errors++;
        pr_warn_ratelimited("[" QK_TAG "] Frame not presented: %d\n", ret);
        return;
    }
    frame.frames++;
}

static void qk_worker(struct work_struct *work)
{
    unsigned int delay = frame.interval_ms;
    int ret;

    if (!READ_ONCE(frame.running))
        return;
    if (!qk_present_attached()) {
        ret = qk_present_bind();
        if (ret) {
            if (ret != -EAGAIN && ret != -EBUSY)
                pr_warn_ratelimited("[" QK_TAG "] Cannot attach the window: %d\n", ret);
            queue_delayed_work(frame.queue, &frame.work, msecs_to_jiffies(1000));
            return;
        }
        /* The panel size, and a window that asked for all of it, are known
         * only once a CRTC has been chosen.
         */
        qk_present_panel_size(&frame.window->panel_width, &frame.window->panel_height);
        {
            int x = 0, y = 0, width = 0, height = 0;

            qk_present_window_rect(&x, &y, &width, &height);
            if (width != frame.window->width || height != frame.window->height ||
                x != frame.window->x || y != frame.window->y) {
                qk_window_place(frame.window, x, y, width, height);
                if (qk_fpu_begin() == 0) {
                    qk_ui_resize(width, height);
                    qk_fpu_end();
                }
            }
            frame.dirty[0] = frame.dirty[1] = (struct qk_clip){ 0, 0, 0, 0 };
        }
    }
    if (frame.ui_ready)
        qk_run_frame();
    /* An idle UI costs one cheap frame every idle_ms instead of every frame. */
    if (frame.idle_streak > 30)
        delay = frame.idle_ms;
    queue_delayed_work(frame.queue, &frame.work, msecs_to_jiffies(delay));
}

int qk_frame_start(struct qk_window *window, const struct qk_frame_config *setup)
{
    struct qk_ui_config config = { 0 };
    int ret;

    if (!window || window->width <= 0 || window->height <= 0 || !setup)
        return -EINVAL;
    frame.window = window;
    frame.interval_ms = setup->interval_ms ? setup->interval_ms : 16;
    frame.idle_ms = setup->idle_ms ? setup->idle_ms : 200;
    frame.arena_bytes = (size_t)setup->arena_mb << 20;
    frame.dirty[0] = frame.dirty[1] = (struct qk_clip){ 0, 0, 0, 0 };
    if (frame.arena_bytes < QK_ARENA_MINIMUM)
        frame.arena_bytes = QK_ARENA_MINIMUM;

    /* vmalloc: the UI's region is large and never needs to be contiguous. */
    frame.arena = vmalloc(frame.arena_bytes);
    if (!frame.arena)
        return -ENOMEM;
    ret = qk_arena_init(frame.arena, frame.arena_bytes);
    if (ret)
        goto free_arena;
    frame.snapshot = vmalloc(QK_SNAPSHOT_MAX);
    if (!frame.snapshot) {
        ret = -ENOMEM;
        goto free_arena;
    }

    config.width = window->width;
    config.height = window->height;
    config.dark = setup->dark;
    config.scale_percent = setup->scale_percent;
    config.mode = setup->mode;
    config.settings_path = setup->settings_path;
    config.extra_font = setup->font;
    config.extra_font_size = setup->font_size;
    /* The UI allocates its context and font atlas here, in a context that may
     * sleep and may use floating point only inside the guard.
     */
    ret = qk_fpu_begin();
    if (ret)
        goto free_snapshot;
    ret = qk_ui_init(&config);
    qk_fpu_end();
    if (ret)
        goto free_snapshot;
    frame.ui_ready = true;

    frame.queue = alloc_ordered_workqueue(QK_TAG, WQ_HIGHPRI);
    if (!frame.queue) {
        ret = -ENOMEM;
        goto stop_ui;
    }
    INIT_DELAYED_WORK(&frame.work, qk_worker);
    WRITE_ONCE(frame.running, true);
    queue_delayed_work(frame.queue, &frame.work, msecs_to_jiffies(1000));
    return 0;

stop_ui:
    if (qk_fpu_begin() == 0) {
        qk_ui_shutdown();
        qk_fpu_end();
    }
    frame.ui_ready = false;
free_snapshot:
    vfree(frame.snapshot);
    frame.snapshot = NULL;
free_arena:
    vfree(frame.arena);
    frame.arena = NULL;
    return ret;
}

void qk_frame_stop(void)
{
    WRITE_ONCE(frame.running, false);
    if (frame.queue) {
        cancel_delayed_work_sync(&frame.work);
        destroy_workqueue(frame.queue);
        frame.queue = NULL;
    }
    if (frame.ui_ready && qk_fpu_begin() == 0) {
        qk_ui_shutdown();
        qk_fpu_end();
    }
    frame.ui_ready = false;
    vfree(frame.snapshot);
    frame.snapshot = NULL;
    /* The arena stays mapped while the display hook can still run. */
    if (!qk_present_attached()) {
        vfree(frame.arena);
        frame.arena = NULL;
    }
}

void qk_frame_kick(void)
{
    if (READ_ONCE(frame.running) && frame.queue) {
        frame.idle_streak = 0;
        mod_delayed_work(frame.queue, &frame.work, 0);
    }
}

void qk_frame_status(struct seq_file *seq)
{
    struct qk_arena_stats arena;
    struct qk_ui_stats ui;

    qk_arena_get_stats(&arena);
    qk_ui_get_stats(&ui);
    seq_printf(seq, "frames=%llu\nskipped_frames=%llu\nui_errors=%llu\nraster_errors=%llu\n",
               frame.frames, frame.skipped_frames, frame.ui_errors, frame.raster_errors);
    seq_printf(seq, "fpu_unavailable=%llu\ninterval_ms=%u\nidle_ms=%u\nidle_streak=%u\n",
               frame.fpu_busy, frame.interval_ms, frame.idle_ms, frame.idle_streak);
    seq_printf(seq, "settings_writes=%llu\nclosed_by_user=%u\n",
               frame.settings_writes, frame.exit_seen);
    if (frame.frames) {
        seq_printf(seq, "ui_us_avg=%llu\nraster_us_avg=%llu\n",
                   div_u64(frame.ui_ns_total / 1000, frame.frames + frame.skipped_frames),
                   div_u64(frame.raster_ns_total / 1000, frame.frames));
    }
    seq_printf(seq, "triangles=%u\npixels=%u\nvertices=%u\ncommands=%u\ntextures=%u\n",
               frame.last_stats.triangles, frame.last_stats.pixels, ui.vertices,
               ui.commands, ui.textures);
    seq_printf(seq, "arena_kb=%u\narena_peak_kb=%u\narena_overflows=%u\narena_exhausted=%d\n",
               arena.capacity / 1024, arena.peak / 1024, arena.overflows,
               qk_arena_exhausted());
}
