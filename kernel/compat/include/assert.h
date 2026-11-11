/* SPDX-License-Identifier: MIT */
/* An assertion inside the kernel warns and keeps going: taking the machine
 * down over a UI invariant is never the right trade.
 */
#pragma once
#ifdef __KERNEL__
#include <linux/printk.h>
#define assert(condition) \
    do { if (!(condition)) \
        pr_warn_ratelimited("[qimgui] assertion failed: %s at %s:%d\n", \
                            #condition, __FILE__, __LINE__); } while (0)
#else
extern "C" void qk_assert_failed(const char *condition, const char *file, int line);
#define assert(condition) \
    do { if (!(condition)) qk_assert_failed(#condition, __FILE__, __LINE__); } while (0)
#endif
