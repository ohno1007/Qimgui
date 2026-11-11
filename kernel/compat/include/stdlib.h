/* SPDX-License-Identifier: MIT */
#pragma once
#include <stddef.h>
#include "qk_math.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Backed by the frame arena, not by any kernel allocator: see qk_arena.h. */
void *malloc(size_t bytes);
void *calloc(size_t count, size_t bytes);
void *realloc(void *pointer, size_t bytes);
void free(void *pointer);
void qsort(void *base, size_t count, size_t size,
           int (*compare)(const void *, const void *));
#ifdef __cplusplus
}
#endif
#define atof(text) qk_strtod((text), 0)
#define strtod(text, end) qk_strtod((text), (end))
static inline int abs(int value) { return value < 0 ? -value : value; }
