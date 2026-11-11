/* SPDX-License-Identifier: MIT */
/* Display path: a double-buffered ARGB8888 image in MSM GEM memory, bound to
 * an idle physical plane on the active DSI CRTC, and kept there by a hook in
 * the device's atomic_check so Android's own transactions do not drop it.
 *
 * This mirrors the proven DRM overlay flow: CPU-written GEM memory, a DRM
 * framebuffer, plane and CRTC state fed into the original MSM/SDE driver,
 * which validates and commits it. Nothing here registers a SurfaceFlinger
 * layer, so the window is on the panel but not in Android's own composition.
 */
#ifndef QK_PRESENT_H
#define QK_PRESENT_H

#include <linux/types.h>

struct seq_file;

struct qk_present_config {
    unsigned int card;      /* DRM primary minor. */
    unsigned int crtc_id;   /* 0 selects the active DSI CRTC. */
    unsigned int plane_id;  /* 0 selects an idle primary/overlay plane. */
    int x, y;               /* Window origin on the panel. */
    int width, height;      /* Window size; the image buffers match it. */
};

int qk_present_init(const struct qk_present_config *config);
/* Acquires the device, picks CRTC and plane, allocates both buffers, installs
 * the atomic_check hook and commits the first frame. Pins the module.
 */
int qk_present_bind(void);
/* Re-applies the current state from the worker: recovers after a blank, a
 * mode change or a transaction that dropped the plane.
 */
int qk_present_update(void);
void qk_present_release(void);
bool qk_present_attached(void);
void qk_present_set_enabled(bool enabled);

/* CPU address of the buffer the UI may draw into, and its row stride. */
u32 *qk_present_back_buffer(u32 *pitch);
/* Publishes the back buffer and commits it. */
int qk_present_swap(void);
/* Panel size discovered from the CRTC mode, for placing the window. */
void qk_present_panel_size(int *width, int *height);
void qk_present_status(struct seq_file *seq);

#endif /* QK_PRESENT_H */
