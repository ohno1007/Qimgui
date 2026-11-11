/* SPDX-License-Identifier: MIT */
#include <linux/errno.h>
#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_atomic.h>

#include "qk_symbols.h"
#include "qk_utils.h"

/* Qualcomm downstream 5.15 ABI, as shipped in this device's msm_drm.ko. An
 * upstream MSM prototype or a newer vendor ABI does not substitute: the
 * address resolving does not make the signature right.
 */
static struct drm_minor *(*minor_acquire_fn)(unsigned int);
static int (*atomic_check_fn)(struct drm_atomic_state *);
static void (*minor_release_fn)(struct drm_minor *);
static struct drm_gem_object *(*gem_new_fn)(struct drm_device *, u32, u32);
static void *(*gem_get_vaddr_fn)(struct drm_gem_object *);
static void (*gem_put_vaddr_fn)(struct drm_gem_object *);
static struct drm_framebuffer *(*fb_init_fn)(struct drm_device *,
        const struct drm_mode_fb_cmd2 *, struct drm_gem_object **);
static int (*set_property_fn)(struct drm_atomic_state *, struct drm_file *,
        struct drm_mode_object *, struct drm_property *, u64);

int qk_symbols_init(void)
{
#define RESOLVE(member, symbol) do { \
    member = (typeof(member))qk_lookup_name(symbol); \
    if (!member) { \
        LOGE("Required display symbol unavailable: %s", symbol); \
        return -ENOENT; \
    } \
} while (0)
    RESOLVE(minor_acquire_fn, "drm_minor_acquire");
    RESOLVE(atomic_check_fn, "drm_atomic_check_only");
    RESOLVE(minor_release_fn, "drm_minor_release");
    RESOLVE(gem_new_fn, "msm_gem_new");
    RESOLVE(gem_get_vaddr_fn, "msm_gem_get_vaddr");
    RESOLVE(gem_put_vaddr_fn, "msm_gem_put_vaddr");
    RESOLVE(fb_init_fn, "msm_framebuffer_init");
    RESOLVE(set_property_fn, "drm_atomic_set_property");
#undef RESOLVE
    return 0;
}

int NO_CFI qk_atomic_check(struct drm_atomic_state *state)
{
    return atomic_check_fn(state);
}

struct drm_minor *NO_CFI qk_minor_acquire(unsigned int minor)
{
    return minor_acquire_fn(minor);
}

void NO_CFI qk_minor_release(struct drm_minor *minor)
{
    minor_release_fn(minor);
}

struct drm_gem_object *NO_CFI qk_gem_new(struct drm_device *dev, u32 size)
{
    /* MSM_BO_SCANOUT | MSM_BO_WC, downstream msm_drm UAPI constants. The UI
     * writes these pages with the CPU and the display engine scans them out.
     */
    return gem_new_fn(dev, size, 0x00000001U | 0x00020000U);
}

void *NO_CFI qk_gem_get_vaddr(struct drm_gem_object *object)
{
    return gem_get_vaddr_fn(object);
}

void NO_CFI qk_gem_put_vaddr(struct drm_gem_object *object)
{
    gem_put_vaddr_fn(object);
}

struct drm_framebuffer *NO_CFI qk_fb_init(struct drm_device *dev,
        const struct drm_mode_fb_cmd2 *cmd, struct drm_gem_object **objects)
{
    /* On success this consumes the caller's GEM object references. */
    return fb_init_fn(dev, cmd, objects);
}

int NO_CFI qk_set_property(struct drm_atomic_state *state,
        struct drm_mode_object *object, struct drm_property *property, u64 value)
{
    /* Scalar and enum properties only: no FB_ID, CRTC_ID or user pointers. */
    return set_property_fn(state, NULL, object, property, value);
}
