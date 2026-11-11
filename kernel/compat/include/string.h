/* SPDX-License-Identifier: MIT */
/* Declarations only: the kernel provides every one of these. */
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void *memcpy(void *to, const void *from, size_t bytes);
void *memmove(void *to, const void *from, size_t bytes);
void *memset(void *area, int value, size_t bytes);
int memcmp(const void *left, const void *right, size_t bytes);
void *memchr(const void *area, int value, size_t bytes);
size_t strlen(const char *text);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t count);
char *strcpy(char *to, const char *from);
char *strncpy(char *to, const char *from, size_t count);
char *strchr(const char *text, int value);
char *strrchr(const char *text, int value);
char *strstr(const char *haystack, const char *needle);
#ifdef __cplusplus
}
#endif
