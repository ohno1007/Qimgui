/* SPDX-License-Identifier: MIT */
/* The MSM/SDE and DRM entry points this module needs that the device's kernel
 * does not export. Resolved by name, called through exact prototypes.
 */
#ifndef QK_SYMBOLS_H
#define QK_SYMBOLS_H

#include <linux/types.h>

struct drm_minor;
struct drm_device;
struct drm_gem_object;
struct drm_framebuffer;
struct drm_mode_fb_cmd2;
struct drm_atomic_state;
struct drm_mode_object;
struct drm_property;

int qk_symbols_init(void);
int qk_atomic_check(struct drm_atomic_state *state);
struct drm_minor *qk_minor_acquire(unsigned int minor);
void qk_minor_release(struct drm_minor *minor);
struct drm_gem_object *qk_gem_new(struct drm_device *dev, u32 size);
void *qk_gem_get_vaddr(struct drm_gem_object *object);
void qk_gem_put_vaddr(struct drm_gem_object *object);
struct drm_framebuffer *qk_fb_init(struct drm_device *dev,
                                   const struct drm_mode_fb_cmd2 *cmd,
                                   struct drm_gem_object **objects);

#endif /* QK_SYMBOLS_H */
