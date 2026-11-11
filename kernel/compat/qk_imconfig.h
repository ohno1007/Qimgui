/* SPDX-License-Identifier: MIT */
/* ImGui configuration for the kernel build: no libc allocator, no libm, no
 * files. The host tests compile ImGui with this same header so the arena and
 * the math subset are exercised off-device too.
 */
#pragma once

#include "aimgui_imconfig.h"

/* The kernel has no FILE*, and settings/log files have no meaning here. */
#define IMGUI_DISABLE_FILE_FUNCTIONS
/* No wall clock: ImGui only uses it to stamp a session date. */
#define IMGUI_DISABLE_TIME_FUNCTIONS
/* Every allocation goes to the pre-reserved region: see qk_arena.h. */
#define IMGUI_DISABLE_DEFAULT_ALLOCATORS
/* Routed to kernel/compat/qk_math.c. */
#define IMGUI_DISABLE_DEFAULT_MATH_FUNCTIONS

#include "qk_math.h"

#define ImFabs(X)           qk_fabsf(X)
#define ImSqrt(X)           qk_sqrtf(X)
#define ImFmod(X, Y)        qk_fmodf((X), (Y))
#define ImCos(X)            qk_cosf(X)
#define ImSin(X)            qk_sinf(X)
#define ImAcos(X)           qk_acosf(X)
#define ImAtan2(Y, X)       qk_atan2f((Y), (X))
#define ImAtof(STR)         qk_strtod((STR), nullptr)
#define ImCeil(X)           qk_ceilf(X)

static inline float  ImPow(float x, float y)   { return qk_powf(x, y); }
static inline double ImPow(double x, double y) { return qk_pow(x, y); }
static inline float  ImLog(float x)            { return qk_logf(x); }
static inline double ImLog(double x)           { return qk_log(x); }
static inline int    ImAbs(int x)              { return x < 0 ? -x : x; }
static inline float  ImAbs(float x)            { return qk_fabsf(x); }
static inline double ImAbs(double x)           { return qk_fabs(x); }
static inline float  ImSign(float x)           { return x < 0.f ? -1.f : (x > 0.f ? 1.f : 0.f); }
static inline double ImSign(double x)          { return x < 0.0 ? -1.0 : (x > 0.0 ? 1.0 : 0.0); }
static inline float  ImRsqrt(float x)          { return 1.f / qk_sqrtf(x); }
static inline float  ImRsqrtPrecise(float x)   { return 1.f / qk_sqrtf(x); }
static inline double ImRsqrt(double x)         { return 1.0 / qk_sqrt(x); }
