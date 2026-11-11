/* SPDX-License-Identifier: MIT */
/* Floating point in kernel context.
 *
 * arm64 builds the kernel with -mgeneral-regs-only and does not preserve the
 * FP/SIMD registers across kernel code by default: using them requires
 * kernel_neon_begin(), which also disables preemption and softirqs. So the
 * region opened here must not sleep -- hence the arena allocator -- and must
 * stay short, because nothing else runs on this CPU meanwhile.
 *
 * Android's arm64 kernels spell those two as an open/close macro pair that
 * keeps the saved register state on the caller's stack, so they have to appear
 * in the same block. These macros therefore come in pairs too, rather than the
 * begin/end functions a single kernel version would allow:
 *
 *     if (!qk_fpu_available()) { ... }
 *     QK_FPU_BEGIN();
 *     result = something_using_floats();
 *     QK_FPU_END();
 *
 * Only the UI frame runs inside. Rasterising, the DRM commit and the input
 * path are integer code and stay outside.
 */
#ifndef QK_FPU_H
#define QK_FPU_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <asm/neon.h>
#include <asm/simd.h>

#define qk_fpu_available()  may_use_simd()
#define QK_FPU_BEGIN()      kernel_neon_begin()
#define QK_FPU_END()        kernel_neon_end()
#else
#define qk_fpu_available()  1
#define QK_FPU_BEGIN()      do {
#define QK_FPU_END()        } while (0)
#endif

#endif /* QK_FPU_H */
