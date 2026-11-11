/* SPDX-License-Identifier: MIT */
/* Module entry point: parameters, the shared window state, the status node,
 * and the order in which the display, input and frame layers come up.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "qk_app.h"
#include "qk_file.h"
#include "qk_frame.h"
#include "qk_geometry.h"
#include "qk_input.h"
#include "qk_present.h"
#include "qk_utils.h"

static bool enabled = true;
static unsigned int card;
static unsigned int crtc_id;
static unsigned int plane_id;
/* Zero means the whole panel: the main window draws its own floating shell
 * and leaves the rest transparent, so Android shows through around it.
 */
static int pos_x, pos_y;
static int width, height;
static bool grab = true;
static unsigned int arena_mb = 24;
static unsigned int interval_ms = 16;
static unsigned int idle_ms = 200;
static int touch_max_x, touch_max_y;
static bool swap_xy, invert_x, invert_y;
static bool gallery;
static bool dark = true;
static unsigned int scale_percent;
static char settings_path[128] = "/data/local/tmp/qimgui.conf";
static char font_path[128];
static unsigned int font_mb = 32;

static void *device_font;
static long device_font_size;

/* TrueType outlines only: the glyph rasteriser cannot read CFF, so an .otf
 * from the device is skipped rather than drawn as blanks. Paths are tried in
 * order; font_path= overrides the list.
 */
static const char * const kFontCandidates[] = {
    "/data/local/tmp/qimgui.ttf",
    "/system/fonts/NotoSansCJK-Regular.ttc",
    "/system/fonts/NotoSansCJKsc-Regular.ttc",
    "/system/fonts/DroidSansFallbackFull.ttf",
    "/system/fonts/DroidSansFallback.ttf",
    "/system/fonts/MiSans-Regular.ttf",
    "/system/fonts/HarmonyOS_Sans_SC_Regular.ttf",
    "/system/fonts/OPlusSans3-Regular.ttf",
    "/system/fonts/VivoSansCN-Regular.ttf",
    "/system/fonts/HONOR Sans CN-Regular.ttf",
    "/system/fonts/NotoSansSC-Regular.ttf",
};

/* The built-in font is a Latin subset; the interface is mostly Chinese, so a
 * device font is loaded here, before the frame worker starts, because reading
 * a file sleeps and the frame may not.
 */
static void qk_load_font(void)
{
    const unsigned long limit = (unsigned long)font_mb << 20;
    unsigned int i;
    long size;

    if (font_path[0]) {
        size = qk_file_load(font_path, &device_font, limit);
        if (size > 0) {
            device_font_size = size;
            LOGI("Font loaded: %s (%ld KiB)", font_path, size / 1024);
        } else {
            LOGW("Cannot load font %s: %ld", font_path, size);
        }
        return;
    }
    for (i = 0; i < ARRAY_SIZE(kFontCandidates); i++) {
        size = qk_file_load(kFontCandidates[i], &device_font, limit);
        if (size > 0) {
            device_font_size = size;
            LOGI("Font loaded: %s (%ld KiB)", kFontCandidates[i], size / 1024);
            return;
        }
    }
    LOGW("No device font found; the interface stays in the built-in Latin subset");
}

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
MODULE_PARM_DESC(width, "Plane width in pixels; 0 covers the whole panel");
MODULE_PARM_DESC(height, "Plane height in pixels; 0 covers the whole panel");
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
module_param(gallery, bool, 0400);
MODULE_PARM_DESC(gallery, "Show the component gallery instead of the main window");
module_param(dark, bool, 0400);
MODULE_PARM_DESC(dark, "Start in the dark palette; the saved settings win if present");
module_param(scale_percent, uint, 0400);
MODULE_PARM_DESC(scale_percent, "UI scale; 0 follows the display density");
module_param_string(settings_path, settings_path, sizeof(settings_path), 0400);
MODULE_PARM_DESC(settings_path, "Where settings are saved; empty keeps them in memory");
module_param_string(font_path, font_path, sizeof(font_path), 0400);
MODULE_PARM_DESC(font_path, "TrueType font for Chinese text; empty searches /system/fonts");
module_param(font_mb, uint, 0400);
MODULE_PARM_DESC(font_mb, "Largest device font to load, in megabytes");
module_param(invert_x, bool, 0400);
module_param(invert_y, bool, 0400);

static int qk_status_show(struct seq_file *seq, void *unused)
{
    seq_printf(seq, "module=%s\n", KBUILD_MODNAME);
    seq_printf(seq, "grab=%u\ntouch_max=%dx%d\nswap_xy=%u\ninvert=%u,%u\n",
               READ_ONCE(window.grab), window.device_max_x, window.device_max_y,
               window.swap_xy, window.invert_x, window.invert_y);
    seq_printf(seq, "mode=%s\nfont_bytes=%ld\nsettings_path=%s\n",
               gallery ? "gallery" : "main", device_font_size,
               settings_path[0] ? settings_path : "(none)");
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
    struct qk_frame_config setup = { 0 };
    int ret;

    if (width < 0 || height < 0 || width > QK_MAX_DIMENSION ||
        height > QK_MAX_DIMENSION || (width && width < 64) ||
        (height && height < 64) || pos_x < 0 || pos_y < 0 || card >= 64 ||
        arena_mb < 16 || arena_mb > 256)
        return -EINVAL;

    /* Provisional until a CRTC is chosen. A window of zero size takes the
     * whole panel, whose size is not known yet, so a placeholder stands in
     * and qk_frame re-places the window once the display answers.
     */
    window.panel_width = width ? pos_x + width : QK_MAX_DIMENSION;
    window.panel_height = height ? pos_y + height : QK_MAX_DIMENSION;
    window.device_max_x = touch_max_x;
    window.device_max_y = touch_max_y;
    window.swap_xy = swap_xy ? 1 : 0;
    window.invert_x = invert_x ? 1 : 0;
    window.invert_y = invert_y ? 1 : 0;
    window.grab = grab ? 1 : 0;
    ret = qk_window_place(&window, pos_x, pos_y, width ? width : QK_MAX_DIMENSION,
                          height ? height : QK_MAX_DIMENSION);
    if (ret)
        return ret;

    config.card = card;
    config.crtc_id = crtc_id;
    config.plane_id = plane_id;
    config.x = width ? window.x : 0;
    config.y = height ? window.y : 0;
    config.width = width;
    config.height = height;
    ret = qk_present_init(&config);
    if (ret)
        return ret;
    qk_present_set_enabled(enabled);

    ret = qk_input_init(&window);
    if (ret)
        goto release_present;
    qk_file_init();
    qk_load_font();
    setup.arena_mb = arena_mb;
    setup.interval_ms = interval_ms;
    setup.idle_ms = idle_ms;
    setup.mode = gallery ? QK_APP_GALLERY : QK_APP_MAIN;
    setup.dark = dark ? 1 : 0;
    setup.scale_percent = (int)scale_percent;
    setup.settings_path = settings_path[0] ? settings_path : NULL;
    setup.font = device_font;
    setup.font_size = (int)device_font_size;
    ret = qk_frame_start(&window, &setup);
    if (ret)
        goto release_font;

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
release_font:
    qk_file_release(device_font);
    device_font = NULL;
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
    if (!qk_present_attached()) {
        qk_present_release();
        qk_file_release(device_font);
        device_font = NULL;
    }
}

module_init(qk_init);
module_exit(qk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Qimgui: Material Design 3 interface rendered and presented from kernel space");
