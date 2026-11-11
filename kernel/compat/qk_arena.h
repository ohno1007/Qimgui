/* SPDX-License-Identifier: MIT */
/* Allocator for everything ImGui and MD3 allocate while a frame is running.
 *
 * A frame runs with preemption disabled (floating point needs kernel_neon_begin
 * on arm64), so it must not call kmalloc/vmalloc. The module reserves one
 * region at init and this allocator hands it out: first fit, split on demand,
 * coalescing both neighbours on free.
 *
 * One region, one user. The frame worker is the only caller; readers of the
 * statistics see a snapshot that may be a frame old.
 *
 * ImGui never checks whether an allocation succeeded, so a NULL return would
 * become a NULL dereference inside the kernel. The region therefore keeps a
 * reserve block: once the main region is full, allocations that fit are served
 * from it and qk_arena_exhausted() reports true. Those allocations overlap each
 * other, so the frame's output is then meaningless -- but the writes stay
 * inside the region, and the caller abandons the frame and tears the UI down
 * instead of taking the machine with it.
 */
#ifndef QK_ARENA_H
#define QK_ARENA_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__KERNEL__)
#include <linux/types.h>
#else
#include <stddef.h>
#include <stdint.h>
typedef uint32_t u32;
typedef uint64_t u64;
#endif

struct qk_arena_stats {
    u32 capacity;     /* Payload bytes the region can hold, minus headers. */
    u32 in_use;       /* Payload bytes currently handed out. */
    u32 peak;         /* Highest in_use since init. */
    u32 blocks;       /* Live allocations. */
    u32 largest_free; /* Payload bytes of the biggest free block. */
    u32 reserve;      /* Payload bytes of the reserve block. */
    u32 overflows;    /* Allocations served from the reserve. */
    u64 allocations;  /* Successful qk_arena_alloc calls. */
    u64 failures;     /* Calls that found no block. */
};

/* Smallest whole region (payload plus reserve plus headers) the kernel UI is
 * allowed to run on. Sized from the peak the
 * MD3 gallery reaches on a panel-sized window (see tests/kernel_ui_test.cpp,
 * which fails if the headroom disappears), with room for the font atlas and
 * several times the frame's own buffers.
 */
#define QK_ARENA_MINIMUM (16u << 20)
/* Upper bound on the reserve block: any single allocation ImGui makes has to
 * fit, the font atlas included.
 */
#define QK_ARENA_RESERVE_MAX (2u << 20)

/* memory must be 16-byte aligned and stay valid until the next init. */
int qk_arena_init(void *memory, size_t bytes);
void *qk_arena_alloc(size_t bytes);
void qk_arena_free(void *pointer);
void qk_arena_get_stats(struct qk_arena_stats *stats);
/* True once an allocation has been served from the reserve block. */
int qk_arena_exhausted(void);
/* Walks the whole region: used by the host tests and the diagnostic node. */
int qk_arena_check(void);

#ifdef __cplusplus
}
#endif

#endif /* QK_ARENA_H */
