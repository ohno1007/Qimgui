/* SPDX-License-Identifier: MIT */
#ifndef QK_UTILS_H
#define QK_UTILS_H

#include <linux/printk.h>

#define QK_TAG "qimgui"
#define LOGI(fmt, ...) pr_info("[" QK_TAG "] " fmt "\n", ##__VA_ARGS__)
#define LOGW(fmt, ...) pr_warn("[" QK_TAG "] " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...) pr_err("[" QK_TAG "] " fmt "\n", ##__VA_ARGS__)

/* Calls through a resolved address skip the compiler's control-flow integrity
 * check, which cannot know a target it never saw.
 */
/* Only clang implements the kernel's CFI, and only clang understands this
 * attribute; GCC warns about the unknown value and checks nothing anyway.
 */
#if defined(__clang__) && defined(__has_attribute)
#if __has_attribute(__no_sanitize__)
#define NO_CFI __attribute__((__no_sanitize__("cfi")))
#endif
#endif
#ifndef NO_CFI
#define NO_CFI
#endif

/* Resolves a kernel symbol by name. The first call installs a temporary kprobe
 * to find kallsyms_lookup_name, unregisters it, and caches the address.
 */
unsigned long qk_lookup_name(const char *name);

#endif /* QK_UTILS_H */
