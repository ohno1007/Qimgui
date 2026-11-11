/* SPDX-License-Identifier: MIT */
/* The libm names stb_truetype and ImGui reach for, mapped onto qk_math.
 * Functions rather than macros, so <cmath> can re-export them into namespace
 * std and C++ callers can still write std::sin.
 */
#pragma once
#include "qk_math.h"

#define QK_MAP1(name, call, type) \
    static inline type name(type x) { return call(x); }
#define QK_MAP2(name, call, type) \
    static inline type name(type x, type y) { return call(x, y); }

QK_MAP1(fabs, qk_fabs, double)     QK_MAP1(fabsf, qk_fabsf, float)
QK_MAP1(sqrt, qk_sqrt, double)     QK_MAP1(sqrtf, qk_sqrtf, float)
QK_MAP1(floor, qk_floor, double)   QK_MAP1(floorf, qk_floorf, float)
QK_MAP1(ceil, qk_ceil, double)     QK_MAP1(ceilf, qk_ceilf, float)
QK_MAP1(trunc, qk_trunc, double)   QK_MAP1(truncf, qk_truncf, float)
QK_MAP1(round, qk_round, double)   QK_MAP1(roundf, qk_roundf, float)
QK_MAP1(sin, qk_sin, double)       QK_MAP1(sinf, qk_sinf, float)
QK_MAP1(cos, qk_cos, double)       QK_MAP1(cosf, qk_cosf, float)
QK_MAP1(tan, qk_tan, double)       QK_MAP1(tanf, qk_tanf, float)
QK_MAP1(asin, qk_asin, double)     QK_MAP1(asinf, qk_asinf, float)
QK_MAP1(acos, qk_acos, double)     QK_MAP1(acosf, qk_acosf, float)
QK_MAP1(atan, qk_atan, double)     QK_MAP1(atanf, qk_atanf, float)
QK_MAP1(exp, qk_exp, double)       QK_MAP1(expf, qk_expf, float)
QK_MAP1(log, qk_log, double)       QK_MAP1(logf, qk_logf, float)
QK_MAP2(fmod, qk_fmod, double)     QK_MAP2(fmodf, qk_fmodf, float)
QK_MAP2(pow, qk_pow, double)       QK_MAP2(powf, qk_powf, float)
QK_MAP2(hypot, qk_hypot, double)   QK_MAP2(hypotf, qk_hypotf, float)
QK_MAP2(atan2, qk_atan2, double)   QK_MAP2(atan2f, qk_atan2f, float)
QK_MAP2(remainder, qk_remainder, double)
QK_MAP2(remainderf, qk_remainderf, float)
QK_MAP2(copysign, qk_copysign, double)
QK_MAP2(copysignf, qk_copysignf, float)

#undef QK_MAP1
#undef QK_MAP2

static inline long lround(double x) { return (long)qk_round(x); }
static inline long lroundf(float x) { return (long)qk_roundf(x); }
static inline long long llround(double x) { return (long long)qk_round(x); }

static inline int qk_isfinite_f(float x) { return qk_isfinite((double)x); }
static inline int qk_isnan_f(float x) { return qk_isnan((double)x); }
