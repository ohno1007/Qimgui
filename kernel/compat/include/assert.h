/* SPDX-License-Identifier: MIT */
/* An assertion inside the kernel warns and keeps going: taking the machine
 * down over a UI invariant is never the right trade. The report goes through
 * a C function, because <linux/printk.h> is not valid C++.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
void qk_assert_failed(const char *condition, const char *file, int line);
#ifdef __cplusplus
}
#endif

#define assert(condition) \
    do { if (!(condition)) qk_assert_failed(#condition, __FILE__, __LINE__); } while (0)
