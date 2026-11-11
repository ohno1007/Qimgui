/* SPDX-License-Identifier: MIT */
/* Module entry point: parameters, the shared window state, the status node,
 * and the order in which the display, input and frame layers come up.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "qk_frame.h"
#include "qk_geometry.h"
#include "qk_input.h"
#include "qk_present.h"
#include "qk_utils.h"

static bool enabled = true;
static unsigned int card;
static unsigned int crtc_id;
static unsigned int plane_id;
/* Centred on a 1080x2400 panel; the window is re-clamped once the panel size
 * is known from the CRTC mode.
 */
static int pos_x = 220, pos_y = 560;
static int width = 640, height = 1280;
static bool grab = true;
static unsigned int arena_mb = 16;
static unsigned int interval_ms = 16;
static unsigned int idle_ms = 200;
static int touch_max_x, touch_max_y;
static bool swap_xy, invert_x, invert_y;

static struct qk_window window;
static struct proc_dir_entry *status_node;
static bool started;

static int qk_set_enabled(const char *value, const struct kernel_param *param)
{
    int ret = param_set_bool(value, param);

    if (!ret && started) {
        qk_present_set_enabled(enabled);
        qk_frame_kick();
    }
    return ret;
}

static const struct kernel_param_ops enabled_ops = {
    .set = qk_set_enabled,
    .get = param_get_bool,
};

static int qk_set_grab(const char *value, const struct kernel_param *param)
{
    int ret = param_set_bool(value, param);

    if (!ret)
        WRITE_ONCE(window.grab, grab ? 1 : 0);
    return ret;
}

static const struct kernel_param_ops grab_ops = {
    .set = qk_set_grab,
    .get = param_get_bool,
};

module_param_cb(enabled, &enabled_ops, &enabled, 0600);
MODULE_PARM_DESC(enabled, "Show the window; attaching pins the module until reboot");
module_param_cb(grab, &grab_ops, &grab, 0600);
MODULE_PARM_DESC(grab, "Keep gestures that start inside the window from Android");
module_param(card, uint, 0400);
MODULE_PARM_DESC(card, "DRM primary minor number, default 0");
module_param(crtc_id, uint, 0400);
MODULE_PARM_DESC(crtc_id, "CRTC object ID; 0 selects the active DSI CRTC");
module_param(plane_id, uint, 0400);
MODULE_PARM_DESC(plane_id, "Idle primary/overlay plane ID; 0 selects automatically");
module_param(pos_x, int, 0400);
module_param(pos_y, int, 0400);
module_param(width, int, 0400);
module_param(height, int, 0400);
MODULE_PARM_DESC(width, "Window width in pixels; the image buffers match it");
module_param(arena_mb, uint, 0400);
MODULE_PARM_DESC(arena_mb, "Megabytes reserved for the UI's own allocations");
module_param(interval_ms, uint, 0600);
MODULE_PARM_DESC(interval_ms, "Frame interval while the UI is changing");
module_param(idle_ms, uint, 0600);
MODULE_PARM_DESC(idle_ms, "Frame interval once the UI has settled");
module_param(touch_max_x, int, 0400);
module_param(touch_max_y, int, 0400);
MODULE_PARM_DESC(touch_max_x, "Digitiser X range; 0 means it reports panel pixels");
module_param(swap_xy, bool, 0400);
module_param(invert_x, bool, 0400);
module_param(invert_y, bool, 0400);

static int qk_status_show(struct seq_file *seq, void *unused)
{
    seq_printf(seq, "module=%s\n", KBUILD_MODNAME);
    seq_printf(seq, "grab=%u\ntouch_max=%dx%d\nswap_xy=%u\ninvert=%u,%u\n",
               READ_ONCE(window.grab), window.device_max_x, window.device_max_y,
               window.swap_xy, window.invert_x, window.invert_y);
    qk_present_status(seq);
    qk_frame_status(seq);
    qk_input_status(seq);
    seq_puts(seq, "unload_requires_reboot_after_attach=1\n");
    return 0;
}

static int qk_status_open(struct inode *inode, struct file *file)
{
    return single_open(file, qk_status_show, NULL);
}

static const struct proc_ops status_ops = {
    .proc_open = qk_status_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static int __init qk_init(void)
{
    struct qk_present_config config = { 0 };
    int ret;

    if (width < 64 || height < 64 || width > QK_MAX_DIMENSION ||
        height > QK_MAX_DIMENSION || pos_x < 0 || pos_y < 0 || card >= 64 ||
        arena_mb < 8 || arena_mb > 256)
        return -EINVAL;

    /* The panel size is provisional until a CRTC is chosen; it only has to be
     * large enough for the configured window until then.
     */
    window.panel_width = pos_x + width;
    window.panel_height = pos_y + height;
    window.device_max_x = touch_max_x;
    window.device_max_y = touch_max_y;
    window.swap_xy = swap_xy ? 1 : 0;
    window.invert_x = invert_x ? 1 : 0;
    window.invert_y = invert_y ? 1 : 0;
    window.grab = grab ? 1 : 0;
    ret = qk_window_place(&window, pos_x, pos_y, width, height);
    if (ret)
        return ret;

    config.card = card;
    config.crtc_id = crtc_id;
    config.plane_id = plane_id;
    config.x = window.x;
    config.y = window.y;
    config.width = window.width;
    config.height = window.height;
    ret = qk_present_init(&config);
    if (ret)
        return ret;
    qk_present_set_enabled(enabled);

    ret = qk_input_init(&window);
    if (ret)
        goto release_present;
    ret = qk_frame_start(&window, arena_mb, interval_ms, idle_ms);
    if (ret)
        goto stop_input;

    status_node = proc_create("qimgui", 0400, NULL, &status_ops);
    if (!status_node) {
        ret = -ENOMEM;
        goto stop_frame;
    }
    started = true;
    LOGI("Loaded: %dx%d window at %d,%d, %u MiB arena", window.width, window.height,
         window.x, window.y, arena_mb);
    return 0;

stop_frame:
    qk_frame_stop();
stop_input:
    qk_input_exit();
release_present:
    qk_present_release();
    return ret;
}

static void __exit qk_exit(void)
{
    started = false;
    proc_remove(status_node);
    qk_input_exit();
    qk_frame_stop();
    /* Unloading is only reachable before the display hook went in: once
     * attached the module holds a reference to itself until reboot.
     */
    if (!qk_present_attached())
        qk_present_release();
}

module_init(qk_init);
module_exit(qk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Qimgui: Material Design 3 interface rendered and presented from kernel space");
