/* SPDX-License-Identifier: MIT */
#include <linux/atomic.h>
#include <linux/dma-fence.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_blend.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem.h>
#include <drm/drm_modeset_lock.h>
#include <drm/drm_plane.h>
#include <drm/drm_property.h>

#include "qk_present.h"
#include "qk_symbols.h"
#include "qk_utils.h"

#define QK_BUFFERS 2

struct qk_buffer {
    struct drm_gem_object *object;  /* Borrowed from fb once fb_init succeeds. */
    struct drm_framebuffer *fb;
    u32 *pixels;
};

static struct {
    struct qk_present_config config;
    bool enabled;
    bool attached;
    struct drm_minor *minor;
    struct drm_device *dev;
    struct module *driver_owner;
    struct drm_crtc *crtc;
    struct drm_plane *plane;
    struct qk_buffer buffers[QK_BUFFERS];
    u32 pitch;
    int front;                      /* Buffer the plane is showing. */
    const struct drm_mode_config_funcs *original_funcs;
    struct drm_mode_config_funcs hooked_funcs;
    unsigned int selected_crtc, selected_plane;
    int panel_width, panel_height;
    int last_error, last_check_error;
    unsigned int last_candidate;
    int candidate_error;
    atomic64_t injected_checks, busy_checks, failed_checks, commits, swaps;
} present;

static struct drm_property *qk_plane_property(struct drm_plane *plane, const char *name)
{
    unsigned int i;

    for (i = 0; i < plane->properties.count; i++) {
        struct drm_property *prop = plane->properties.properties[i];

        if (!strcmp(prop->name, name))
            return prop;
    }
    return NULL;
}

static int qk_enum_value(struct drm_property *property, const char *name, u64 *value)
{
    struct drm_property_enum *entry;

    if (!property || !(property->flags & DRM_MODE_PROP_ENUM))
        return -EINVAL;
    list_for_each_entry(entry, &property->enum_list, head) {
        if (!strcmp(entry->name, name)) {
            *value = entry->value;
            return 0;
        }
    }
    return -ENOENT;
}

static int qk_set_plane_property(struct drm_atomic_state *state, const char *name,
                                 u64 value, bool required)
{
    struct drm_property *prop = qk_plane_property(present.plane, name);

    if (!prop)
        return required ? -EOPNOTSUPP : 0;
    return qk_set_property(state, &present.plane->base, prop, value);
}

static int qk_set_plane_enum(struct drm_atomic_state *state, const char *property,
                             const char *entry, bool required)
{
    struct drm_property *prop = qk_plane_property(present.plane, property);
    u64 value;
    int ret;

    if (!prop)
        return required ? -EOPNOTSUPP : 0;
    ret = qk_enum_value(prop, entry, &value);
    return ret ? ret : qk_set_property(state, &present.plane->base, prop, value);
}

static bool qk_supports_argb_linear(struct drm_plane *plane)
{
    unsigned int i;
    bool found = false;

    for (i = 0; i < plane->format_count; i++)
        if (plane->format_types[i] == DRM_FORMAT_ARGB8888)
            found = true;
    if (!found)
        return false;
    if (plane->funcs->format_mod_supported)
        return plane->funcs->format_mod_supported(plane, DRM_FORMAT_ARGB8888,
                                                  DRM_FORMAT_MOD_LINEAR);
    if (!plane->modifier_count)
        return true;
    for (i = 0; i < plane->modifier_count; i++)
        if (plane->modifiers[i] == DRM_FORMAT_MOD_LINEAR)
            return true;
    return false;
}

/* Idle PRIMARY planes are usable too: on SDE the unused physical SSPPs are
 * tagged PRIMARY and OVERLAY is not a capability bit. Routing and resource
 * validation stay with the original atomic_check. Modeset locks held.
 */
static const char *qk_plane_rejection(struct drm_plane *plane)
{
    struct drm_property *prop;
    u64 blend;

    if (plane->type != DRM_PLANE_TYPE_OVERLAY && plane->type != DRM_PLANE_TYPE_PRIMARY)
        return "type";
    if (!plane->state)
        return "no_state";
    if (plane->state->fb || plane->state->crtc)
        return "occupied";
    /* SDE virtual/multirect planes share a physical SSPP: exclude them. */
    if (qk_plane_property(plane, "multirect_mode"))
        return "virtual";
    if (!qk_supports_argb_linear(plane))
        return "argb_linear";
    prop = qk_plane_property(plane, "alpha");
    if (!prop || !(prop->flags & DRM_MODE_PROP_RANGE) || prop->num_values < 2)
        return "alpha";
    prop = qk_plane_property(plane, "zpos");
    if (!prop || !(prop->flags & DRM_MODE_PROP_RANGE) || prop->num_values < 2)
        return "zpos";
    prop = qk_plane_property(plane, "blend_op");
    if (qk_enum_value(prop, "premultiplied", &blend))
        return "blend_op";
    return NULL;
}

/* Called with all modeset locks held. No driver-private structure offsets. */
static int qk_select_output(unsigned int first_plane)
{
    struct drm_crtc *crtc;
    struct drm_plane *plane;
    struct drm_connector *connector;
    struct drm_connector_list_iter iter;

    present.crtc = NULL;
    if (present.config.crtc_id) {
        drm_for_each_crtc(crtc, present.dev)
            if (crtc->base.id == present.config.crtc_id)
                present.crtc = crtc;
    } else {
        drm_connector_list_iter_begin(present.dev, &iter);
        drm_for_each_connector_iter(connector, &iter) {
            if (connector->connector_type == DRM_MODE_CONNECTOR_DSI &&
                connector->state && connector->state->crtc) {
                present.crtc = connector->state->crtc;
                break;
            }
        }
        drm_connector_list_iter_end(&iter);
    }
    if (!present.crtc || !present.crtc->state || !present.crtc->state->active ||
        !present.crtc->state->enable)
        return -EAGAIN;

    present.panel_width = present.crtc->state->mode.hdisplay;
    present.panel_height = present.crtc->state->mode.vdisplay;
    /* A window of zero size means the whole panel, which is only knowable
     * once a CRTC has been chosen.
     */
    if (!present.config.width) {
        present.config.x = 0;
        present.config.width = present.panel_width;
    }
    if (!present.config.height) {
        present.config.y = 0;
        present.config.height = present.panel_height;
    }
    if (present.config.x + present.config.width > present.panel_width ||
        present.config.y + present.config.height > present.panel_height)
        return -ERANGE;

    present.plane = NULL;
    drm_for_each_plane(plane, present.dev) {
        if (drm_plane_index(plane) < first_plane)
            continue;
        if (present.config.plane_id && plane->base.id != present.config.plane_id)
            continue;
        if (!(plane->possible_crtcs & drm_crtc_mask(present.crtc)) ||
            qk_plane_rejection(plane))
            continue;
        present.plane = plane;
        return 0;
    }
    return -EBUSY;
}

static void qk_free_buffers(void)
{
    int index;

    for (index = 0; index < QK_BUFFERS; index++) {
        struct qk_buffer *buffer = &present.buffers[index];

        if (buffer->pixels)
            qk_gem_put_vaddr(buffer->object);
        buffer->pixels = NULL;
        if (buffer->fb)
            drm_framebuffer_put(buffer->fb);
        else if (buffer->object)
            drm_gem_object_put(buffer->object);
        buffer->fb = NULL;
        buffer->object = NULL;
    }
}

void qk_present_release(void)
{
    qk_free_buffers();
    present.crtc = NULL;
    present.plane = NULL;
    if (present.driver_owner)
        module_put(present.driver_owner);
    present.driver_owner = NULL;
    if (present.minor)
        qk_minor_release(present.minor);
    present.minor = NULL;
    present.dev = NULL;
}

static int qk_create_buffer(struct qk_buffer *buffer)
{
    struct drm_mode_fb_cmd2 cmd = { 0 };
    struct drm_gem_object *objects[4] = { NULL };
    int ret;

    buffer->object = qk_gem_new(present.dev,
                                PAGE_ALIGN(present.pitch * present.config.height));
    if (IS_ERR(buffer->object)) {
        ret = PTR_ERR(buffer->object);
        buffer->object = NULL;
        return ret;
    }
    buffer->pixels = qk_gem_get_vaddr(buffer->object);
    if (IS_ERR_OR_NULL(buffer->pixels)) {
        ret = buffer->pixels ? PTR_ERR(buffer->pixels) : -ENOMEM;
        buffer->pixels = NULL;
        return ret;
    }
    memset(buffer->pixels, 0, present.pitch * present.config.height);
    cmd.width = present.config.width;
    cmd.height = present.config.height;
    cmd.pixel_format = DRM_FORMAT_ARGB8888;
    cmd.pitches[0] = present.pitch;
    objects[0] = buffer->object;
    buffer->fb = qk_fb_init(present.dev, &cmd, objects);
    if (IS_ERR(buffer->fb)) {
        ret = PTR_ERR(buffer->fb);
        buffer->fb = NULL;
        return ret;
    }
    return 0;
}

static int qk_create_buffers(void)
{
    int index, ret;

    /* 256-byte rows keep the scanout fetch aligned, as the reference flow does. */
    present.pitch = ALIGN((u32)present.config.width * 4, 256);
    for (index = 0; index < QK_BUFFERS; index++) {
        if (present.buffers[index].fb)
            continue;
        ret = qk_create_buffer(&present.buffers[index]);
        if (ret)
            return ret;
    }
    return 0;
}

u32 *qk_present_back_buffer(u32 *pitch)
{
    const int back = READ_ONCE(present.front) ^ 1;

    if (pitch)
        *pitch = present.pitch;
    return present.buffers[back].pixels;
}

int qk_present_back_index(void)
{
    return READ_ONCE(present.front) ^ 1;
}

void qk_present_window_rect(int *x, int *y, int *width, int *height)
{
    if (x)
        *x = present.config.x;
    if (y)
        *y = present.config.y;
    if (width)
        *width = present.config.width;
    if (height)
        *height = present.config.height;
}

/* Process context only; DRM's acquire_ctx propagates -EDEADLK to the caller. */
static int qk_apply(struct drm_atomic_state *state, bool *changed)
{
    struct drm_framebuffer *fb = present.buffers[READ_ONCE(present.front)].fb;
    struct drm_crtc_state *cs;
    struct drm_plane_state *ps;
    struct drm_property *prop;
    static const char * const reset_properties[] = {
        "input_fence", "src_config", "color_fill", "h_decimate", "v_decimate",
        "scaler_v1", "scaler_v2", "excl_rect_v1", "inverse_pma",
    };
    unsigned int i;
    bool show;
    int ret;

    *changed = false;
    cs = drm_atomic_get_crtc_state(state, present.crtc);
    if (IS_ERR(cs))
        return PTR_ERR(cs);
    ps = drm_atomic_get_plane_state(state, present.plane);
    if (IS_ERR(ps))
        return PTR_ERR(ps);

    /* Android keeps ownership of its own buffers, even on our chosen plane. */
    if (ps->fb && ps->fb != present.buffers[0].fb && ps->fb != present.buffers[1].fb) {
        atomic64_inc(&present.busy_checks);
        return 0;
    }
    show = READ_ONCE(present.enabled) && cs->enable && cs->active &&
           present.config.x + present.config.width <= cs->mode.hdisplay &&
           present.config.y + present.config.height <= cs->mode.vdisplay;
    if (!show) {
        if (!ps->fb)
            return 0;
        drm_atomic_set_fb_for_plane(ps, NULL);
        ret = drm_atomic_set_crtc_for_plane(ps, NULL);
        if (ret)
            return ret;
        cs->planes_changed = true;
        *changed = true;
        return 0;
    }

    ret = drm_atomic_set_crtc_for_plane(ps, present.crtc);
    if (ret)
        return ret;
    drm_atomic_set_fb_for_plane(ps, fb);
    ps->crtc_x = present.config.x;
    ps->crtc_y = present.config.y;
    ps->crtc_w = present.config.width;
    ps->crtc_h = present.config.height;
    ps->src_x = 0;
    ps->src_y = 0;
    ps->src_w = (u32)present.config.width << 16;
    ps->src_h = (u32)present.config.height << 16;
    ps->rotation = DRM_MODE_ROTATE_0;
    ps->alpha = DRM_BLEND_ALPHA_OPAQUE;
    ps->pixel_blend_mode = DRM_MODE_BLEND_PREMULTI;
    /* SDE keeps its own atomic storage for these: setting only the generic
     * plane_state fields would leave the hardware mixer unconfigured.
     */
    prop = qk_plane_property(present.plane, "zpos");
    ps->zpos = prop->values[1];
    ret = qk_set_property(state, &present.plane->base, prop, prop->values[1]);
    if (ret)
        return ret;
    prop = qk_plane_property(present.plane, "alpha");
    ret = qk_set_property(state, &present.plane->base, prop, prop->values[1]);
    if (ret)
        return ret;
    ret = qk_set_plane_enum(state, "blend_op", "premultiplied", true);
    if (ret)
        return ret;
    ret = qk_set_plane_enum(state, "fb_translation_mode", "non_sec", false);
    if (ret)
        return ret;
    for (i = 0; i < ARRAY_SIZE(reset_properties); i++) {
        ret = qk_set_plane_property(state, reset_properties[i], 0, false);
        if (ret)
            return ret;
    }
    /* The pixels were written and ordered before the swap: no producer fence. */
    dma_fence_put(ps->fence);
    ps->fence = NULL;
    cs->planes_changed = true;
    *changed = true;
    return 0;
}

static int qk_hooked_atomic_check(struct drm_device *dev, struct drm_atomic_state *state)
{
    bool changed = false;
    int ret;

    /* Cursor-only and unrelated display transactions stay on their own path. */
    if (!state->async_update && !state->legacy_cursor_update &&
        (drm_atomic_get_new_crtc_state(state, present.crtc) ||
         drm_atomic_get_new_plane_state(state, present.plane))) {
        ret = qk_apply(state, &changed);
        if (ret)
            goto failed;
    }
    ret = present.original_funcs->atomic_check(dev, state);
    if (ret)
        goto failed;
    if (changed)
        atomic64_inc(&present.injected_checks);
    return 0;

failed:
    if (ret != -EDEADLK) {
        WRITE_ONCE(present.last_error, ret);
        WRITE_ONCE(present.last_check_error, ret);
        atomic64_inc(&present.failed_checks);
        /* Do not retry an invalid overlay on every Android frame. This
         * transaction fails normally; later ones run without the window.
         */
        if (changed) {
            WRITE_ONCE(present.enabled, false);
            pr_err_ratelimited("[" QK_TAG "] Window disabled after atomic error %d\n",
                               ret);
        }
    }
    return ret;
}

static int qk_commit_locked(struct drm_modeset_acquire_ctx *acquire, bool *committed)
{
    struct drm_atomic_state *state;
    bool changed;
    int ret;

    state = drm_atomic_state_alloc(present.dev);
    if (!state)
        return -ENOMEM;
    state->acquire_ctx = acquire;
    ret = qk_apply(state, &changed);
    if (!ret && changed) {
        ret = drm_atomic_commit(state);
        if (!ret) {
            atomic64_inc(&present.commits);
            if (committed)
                *committed = true;
        }
    }
    drm_atomic_state_put(state);
    return ret;
}

int qk_present_init(const struct qk_present_config *config)
{
    if (!config || config->width < 0 || config->height < 0)
        return -EINVAL;
    present.config = *config;
    present.front = 0;
    present.enabled = true;
    atomic64_set(&present.injected_checks, 0);
    atomic64_set(&present.busy_checks, 0);
    atomic64_set(&present.failed_checks, 0);
    atomic64_set(&present.commits, 0);
    atomic64_set(&present.swaps, 0);
    return qk_symbols_init();
}

/* modeset_lock_all_ctx handles lock ordering, plane selection included. Buffer
 * creation and the shadow table install happen once, here, in the worker.
 */
int qk_present_bind(void)
{
    struct drm_modeset_acquire_ctx acquire;
    struct drm_atomic_state *state = NULL;
    unsigned int first_plane = 0;
    bool changed;
    int ret;

    present.minor = qk_minor_acquire(present.config.card);
    if (IS_ERR(present.minor)) {
        ret = PTR_ERR(present.minor);
        present.minor = NULL;
        return ret;
    }
    present.dev = present.minor->dev;
    if (strcmp(present.dev->driver->name, "msm") &&
        strcmp(present.dev->driver->name, "msm_drm")) {
        ret = -ENODEV;
        goto release;
    }
    present.driver_owner = present.dev->driver->fops->owner;
    if (!present.driver_owner && present.dev->dev->driver)
        present.driver_owner = present.dev->dev->driver->owner;
    if (present.driver_owner && !try_module_get(present.driver_owner)) {
        present.driver_owner = NULL;
        ret = -ENODEV;
        goto release;
    }
    /* Re-resolve the vendor addresses while holding its module reference. */
    ret = qk_symbols_init();
    if (ret)
        goto release;
    present.original_funcs = READ_ONCE(present.dev->mode_config.funcs);
    if (!present.original_funcs || !present.original_funcs->atomic_check ||
        !present.original_funcs->atomic_commit) {
        ret = -EOPNOTSUPP;
        goto release;
    }

    drm_modeset_acquire_init(&acquire, 0);
retry:
    ret = drm_modeset_lock_all_ctx(present.dev, &acquire);
    if (ret)
        goto unlock;
next_plane:
    ret = qk_select_output(first_plane);
    if (ret)
        goto unlock;
    WRITE_ONCE(present.last_candidate, present.plane->base.id);
    ret = qk_create_buffers();
    if (ret)
        goto unlock;
    state = drm_atomic_state_alloc(present.dev);
    if (!state) {
        ret = -ENOMEM;
        goto unlock;
    }
    state->acquire_ctx = &acquire;
    ret = qk_apply(state, &changed);
    if (!ret && (!changed || !READ_ONCE(present.enabled)))
        ret = -ECANCELED;
    if (!ret)
        ret = qk_atomic_check(state);
    WRITE_ONCE(present.candidate_error, ret);
    if (ret && ret != -EDEADLK && ret != -ECANCELED && !present.config.plane_id) {
        pr_debug("[" QK_TAG "] Plane %u rejected the window: %d\n",
                 present.plane->base.id, ret);
        first_plane = drm_plane_index(present.plane) + 1;
        drm_atomic_state_put(state);
        state = NULL;
        goto next_plane;
    }
    if (ret)
        goto unlock;

    present.hooked_funcs = *present.original_funcs;
    present.hooked_funcs.atomic_check = qk_hooked_atomic_check;
    /* A mode_config callback has no owner and no unregister grace period, so
     * the code and the table are pinned before publication. They stay valid
     * until reboot, including with the window hidden. Never force an rmmod.
     */
    if (!try_module_get(THIS_MODULE)) {
        ret = -ENODEV;
        goto unlock;
    }
    if (cmpxchg(&present.dev->mode_config.funcs, present.original_funcs,
                &present.hooked_funcs) != present.original_funcs) {
        module_put(THIS_MODULE);
        ret = -EBUSY;
        goto unlock;
    }
    WRITE_ONCE(present.selected_crtc, present.crtc->base.id);
    WRITE_ONCE(present.selected_plane, present.plane->base.id);
    smp_store_release(&present.attached, true);
    LOGI("Window attached: CRTC %u plane %u %dx%d at %d,%d; module pinned until reboot",
         present.selected_crtc, present.selected_plane, present.config.width,
         present.config.height, present.config.x, present.config.y);
    ret = drm_atomic_commit(state);
    if (!ret)
        atomic64_inc(&present.commits);

unlock:
    if (state) {
        drm_atomic_state_put(state);
        state = NULL;
    }
    if (ret == -EDEADLK && !READ_ONCE(present.attached)) {
        ret = drm_modeset_backoff(&acquire);
        if (!ret)
            goto retry;
    }
    drm_modeset_drop_locks(&acquire);
    drm_modeset_acquire_fini(&acquire);
release:
    if (!READ_ONCE(present.attached))
        qk_present_release();
    return ret;
}

static int qk_present_commit(void)
{
    struct drm_modeset_acquire_ctx acquire;
    int ret;

    if (!present.dev)
        return -ENODEV;
    drm_modeset_acquire_init(&acquire, 0);
retry:
    ret = qk_commit_locked(&acquire, NULL);
    if (ret == -EDEADLK) {
        ret = drm_modeset_backoff(&acquire);
        if (!ret)
            goto retry;
    }
    drm_modeset_drop_locks(&acquire);
    drm_modeset_acquire_fini(&acquire);
    return ret;
}

int qk_present_update(void)
{
    return qk_present_commit();
}

int qk_present_swap(void)
{
    int ret;

    if (!smp_load_acquire(&present.attached))
        return -ENODEV;
    /* The UI wrote these pages with the CPU through a write-combining
     * mapping: order those stores before the display engine may fetch them.
     */
    wmb();
    WRITE_ONCE(present.front, READ_ONCE(present.front) ^ 1);
    ret = qk_present_commit();
    if (ret)
        WRITE_ONCE(present.front, READ_ONCE(present.front) ^ 1);
    else
        atomic64_inc(&present.swaps);
    return ret;
}

bool qk_present_attached(void)
{
    return smp_load_acquire(&present.attached);
}

void qk_present_set_enabled(bool enabled)
{
    WRITE_ONCE(present.enabled, enabled);
}

void qk_present_panel_size(int *width, int *height)
{
    if (width)
        *width = present.panel_width;
    if (height)
        *height = present.panel_height;
}

void qk_present_status(struct seq_file *seq)
{
    seq_printf(seq, "attached=%u\nenabled=%u\ncard=%u\ncrtc_id=%u\nplane_id=%u\n",
               smp_load_acquire(&present.attached), READ_ONCE(present.enabled),
               present.config.card, READ_ONCE(present.selected_crtc),
               READ_ONCE(present.selected_plane));
    seq_printf(seq, "window=%dx%d+%d+%d\npanel=%dx%d\npitch=%u\nfront=%d\n",
               present.config.width, present.config.height, present.config.x,
               present.config.y, present.panel_width, present.panel_height,
               present.pitch, READ_ONCE(present.front));
    seq_printf(seq, "last_error=%d\nlast_check_error=%d\nlast_candidate=%u\ncandidate_error=%d\n",
               READ_ONCE(present.last_error), READ_ONCE(present.last_check_error),
               READ_ONCE(present.last_candidate), READ_ONCE(present.candidate_error));
    seq_printf(seq, "injected_checks=%lld\nbusy_checks=%lld\nfailed_checks=%lld\n",
               atomic64_read(&present.injected_checks),
               atomic64_read(&present.busy_checks),
               atomic64_read(&present.failed_checks));
    seq_printf(seq, "commits=%lld\nswaps=%lld\n", atomic64_read(&present.commits),
               atomic64_read(&present.swaps));
}
