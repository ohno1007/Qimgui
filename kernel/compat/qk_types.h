/* SPDX-License-Identifier: MIT */
/* Fixed-width types for headers shared between the module's C and C++ halves.
 *
 * The kernel's own headers are C only -- <linux/stddef.h> declares an enum
 * with members named false and true, which a C++ compiler rejects outright --
 * so a header that both halves include cannot pull <linux/types.h> in. The
 * spellings below match the kernel's on every architecture this module
 * targets, and the host build gets them from <stdint.h>.
 */
#ifndef QK_TYPES_H
#define QK_TYPES_H

#if defined(__KERNEL__) && !defined(__cplusplus)
#include <linux/types.h>
#elif defined(__KERNEL__)
#include <stddef.h>
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int s32;
typedef unsigned long long u64;
#else
#include <stddef.h>
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
#endif

#endif /* QK_TYPES_H */
