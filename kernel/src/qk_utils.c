/* SPDX-License-Identifier: MIT */
#include <linux/errno.h>
#include <linux/kallsyms.h>
#include <linux/kprobes.h>
#include <linux/mutex.h>

#include "qk_utils.h"

static unsigned long (*lookup_name_fn)(const char *);
static DEFINE_MUTEX(lookup_lock);

/* kallsyms_lookup_name stopped being exported in 5.7; a kprobe on it still
 * reports its address. The probe exists only for that one lookup: no image
 * memory, no atomic state and no display work happens in a probe handler.
 */
static int qk_resolve_lookup(void)
{
    struct kprobe probe = { .symbol_name = "kallsyms_lookup_name" };
    int ret;

    ret = register_kprobe(&probe);
    if (ret) {
        LOGE("Cannot probe kallsyms_lookup_name: %d", ret);
        return ret;
    }
    lookup_name_fn = (unsigned long (*)(const char *))probe.addr;
    unregister_kprobe(&probe);
    return lookup_name_fn ? 0 : -ENOENT;
}

/* Reported by the assert() the C++ sources compile against: see
 * kernel/compat/include/assert.h.
 */
void qk_assert_failed(const char *condition, const char *file, int line)
{
    pr_warn_ratelimited("[" QK_TAG "] assertion failed: %s at %s:%d\n",
                        condition, file, line);
}

unsigned long NO_CFI qk_lookup_name(const char *name)
{
    unsigned long address = 0;

    if (!name)
        return 0;
    mutex_lock(&lookup_lock);
    if (!lookup_name_fn && qk_resolve_lookup()) {
        mutex_unlock(&lookup_lock);
        return 0;
    }
    address = lookup_name_fn(name);
    mutex_unlock(&lookup_lock);
    return address;
}
