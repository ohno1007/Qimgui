/* SPDX-License-Identifier: MIT */
/* Floating point in kernel context.
 *
 * arm64 builds the kernel with -mgeneral-regs-only and does not preserve the
 * FP/SIMD registers across kernel code by default: using them requires
 * kernel_neon_begin(), which also disables preemption and softirqs. So the
 * region opened here must not sleep -- hence the arena allocator -- and must
 * stay short, because nothing else runs on this CPU meanwhile.
 *
 * Only the UI frame runs inside it. Rasterising, the DRM commit and the input
 * path are integer code and stay outside.
 */
#ifndef QK_FPU_H
#define QK_FPU_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <asm/neon.h>
#include <asm/simd.h>

static inline int qk_fpu_begin(void)
{
    if (!may_use_simd())
        return -EBUSY;
    kernel_neon_begin();
    return 0;
}

static inline void qk_fpu_end(void)
{
    kernel_neon_end();
}
#else
static inline int qk_fpu_begin(void) { return 0; }
static inline void qk_fpu_end(void) { }
#endif

#endif /* QK_FPU_H */
