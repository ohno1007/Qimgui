// SPDX-License-Identifier: GPL-2.0-only
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include "qmd3.h"
#include "qmd3_layout.h"

#define QMD3_NAME "qimgui_md3"
#define QMD3_IOC_MAGIC 'Q'
struct qmd3_input_ioctl { s32 x, y; u8 down; u8 key; u16 reserved; };
#define QMD3_IOC_INPUT _IOW(QMD3_IOC_MAGIC, 1, struct qmd3_input_ioctl)
#define QMD3_IOC_RENDER _IO(QMD3_IOC_MAGIC, 2)
#define QMD3_IOC_INFO _IOR(QMD3_IOC_MAGIC, 3, struct qmd3_info)
struct qmd3_info { u32 width, height, stride; u64 bytes; };

static unsigned int width = 640;
static unsigned int height = 480;
module_param(width, uint, 0444);
module_param(height, uint, 0444);
MODULE_PARM_DESC(width, "CPU canvas width (1..4096)");
MODULE_PARM_DESC(height, "CPU canvas height (1..4096)");

static struct qm_context ui;
static struct qm_surface surface;
static struct qm_input input;
static qm_u32 *pixels;
static size_t pixel_bytes;
static DEFINE_MUTEX(ui_lock);
static int selected;

static void draw_frame(void)
{
    static const struct qm_item nav[] = {
        { 1, "Alarm", "@alarm", 0, 0 },
        { 2, "Clock", "@clock", 0, 0 },
        { 3, "Timer", "@timer", 0, 0 },
        { 4, "Stopwatch", "@stopwatch", 0, 0 },
    };
    static const struct qm_item tabs[] = {
        { 10, "Overview", 0, 0, 0 },
        { 11, "Controls", 0, 0, 0 },
        { 12, "Shapes", 0, 0, 0 },
    };
    struct qm_rect rail, content, card;
    qm_u32 dark = ui.theme.dark ? 0xff141218U : 0xfffef7ffU;
    rail = (struct qm_rect){ 0, 0, (int)(width > 160 ? 160 : width / 3), (int)height };
    content = (struct qm_rect){ rail.w, 0, (int)width - rail.w, (int)height };
    qm_begin(&ui, &surface, &input, dark);
    qm_navigation(&ui, rail, nav, ARRAY_SIZE(nav), &selected, QM_NAV_RAIL);
    qm_tabs(&ui, (struct qm_rect){ content.x + 20, 20, content.w - 40, 48 },
            tabs, ARRAY_SIZE(tabs), &selected);
    card = (struct qm_rect){ content.x + 20, 90, content.w - 40, 130 };
    qm_card(&ui, 20, card, "Material 3", "Kernel rendered component library",
            QM_CARD_FILLED, 1);
    qm_linear_progress(&ui, (struct qm_rect){ content.x + 40, 250, content.w - 80, 8 },
                       700, 0, 0);
    qm_circular_progress(&ui, (struct qm_rect){ content.x + 40, 290, 80, 80 }, 700, 0, 0);
    qm_end(&ui);
}

static int qmd3_open(struct inode *inode, struct file *file)
{
    return 0;
}
static ssize_t qmd3_read(struct file *file, char __user *buf, size_t len, loff_t *pos)
{
    ssize_t ret;
    if (*pos >= pixel_bytes) return 0;
    mutex_lock(&ui_lock);
    ret = simple_read_from_buffer(buf, len, pos, pixels, pixel_bytes);
    mutex_unlock(&ui_lock);
    return ret;
}
static long qmd3_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct qmd3_input_ioctl event;
    struct qmd3_info info;
    int ret = 0;
    mutex_lock(&ui_lock);
    switch (cmd) {
    case QMD3_IOC_INPUT:
        if (copy_from_user(&event, (void __user *)arg, sizeof(event))) { ret = -EFAULT; break; }
        input.x = event.x; input.y = event.y; input.down = !!event.down;
        input.key = event.key <= QM_KEY_ESCAPE ? (enum qm_key)event.key : QM_KEY_NONE;
        input.dt_ms = 16; break;
    case QMD3_IOC_RENDER:
        draw_frame(); break;
    case QMD3_IOC_INFO:
        info.width = width; info.height = height; info.stride = surface.stride_bytes; info.bytes = pixel_bytes;
        if (copy_to_user((void __user *)arg, &info, sizeof(info))) ret = -EFAULT;
        break;
    default: ret = -ENOTTY; break;
    }
    mutex_unlock(&ui_lock);
    return ret;
}
static const struct file_operations qmd3_fops = {
    .owner = THIS_MODULE, .open = qmd3_open, .read = qmd3_read, .unlocked_ioctl = qmd3_ioctl,
    .llseek = default_llseek,
};
static struct miscdevice qmd3_misc = { .minor = MISC_DYNAMIC_MINOR, .name = QMD3_NAME, .fops = &qmd3_fops, .mode = 0600 };

static int __init qmd3_init_module(void)
{
    if (!width || width > QM_MAX_DIMENSION || !height || height > QM_MAX_DIMENSION) return -EINVAL;
    pixel_bytes = (size_t)width * height * sizeof(qm_u32);
    pixels = vzalloc(pixel_bytes);
    if (!pixels) return -ENOMEM;
    surface = (struct qm_surface){ pixels, (int)width, (int)height, width * sizeof(qm_u32), pixel_bytes };
    qm_init(&ui, false);
    draw_frame();
    if (misc_register(&qmd3_misc)) { vfree(pixels); pixels = NULL; return -EINVAL; }
    pr_info("qimgui_md3: %ux%u CPU MD3 canvas ready at /dev/%s\n", width, height, QMD3_NAME);
    return 0;
}
static void __exit qmd3_exit_module(void)
{
    misc_deregister(&qmd3_misc);
    vfree(pixels); pixels = NULL;
}
module_init(qmd3_init_module);
module_exit(qmd3_exit_module);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Qimgui MD3 integer kernel canvas and controls");
MODULE_AUTHOR("Qimgui contributors");
