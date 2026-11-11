/* SPDX-License-Identifier: MIT */
/* Formatting only; the kernel has vsnprintf, snprintf, sscanf and no files. */
#pragma once
#include <stdarg.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int vsnprintf(char *buffer, size_t size, const char *format, va_list arguments);
int snprintf(char *buffer, size_t size, const char *format, ...);
int sscanf(const char *text, const char *format, ...);
#ifdef __cplusplus
}
#endif
