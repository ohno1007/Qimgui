/* SPDX-License-Identifier: MIT */
/* File access from the module: the device's fonts and the saved settings.
 *
 * Process context only, and never inside the floating-point region: these
 * calls sleep. The host build maps them onto stdio so the same code paths are
 * exercised by tests/run_kernel_checks.sh.
 */
#ifndef QK_FILE_H
#define QK_FILE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qk_types.h"

/* Size in bytes, or a negative errno. */
long qk_file_size(const char *path);
/* Reads at most size bytes. Returns the count read, or a negative errno. */
long qk_file_read(const char *path, void *buffer, unsigned long size);
/* Replaces the file's contents. Returns 0, or a negative errno. */
int qk_file_write(const char *path, const void *buffer, unsigned long size);
/* Reads a whole file into a buffer this function allocates with vmalloc (the
 * host build uses malloc). Caller frees it with qk_file_release.
 */
long qk_file_load(const char *path, void **buffer, unsigned long limit);
void qk_file_release(void *buffer);

#ifdef __cplusplus
}
#endif

#endif /* QK_FILE_H */
