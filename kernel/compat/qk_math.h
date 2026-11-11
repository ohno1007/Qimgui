/* SPDX-License-Identifier: MIT */
/* Floating-point subset for the kernel UI. The kernel links no libm, so the
 * ImGui/MD3 sources get these through compat/include/{math.h,cmath}.
 *
 * Every function here uses hardware float/double arithmetic. On arm64 that is
 * only legal inside a kernel_neon_begin()/kernel_neon_end() region, which also
 * disables preemption: see qk_fpu.h and docs/KERNEL_PORT.md. The host test
 * build calls the same code directly and compares it against libm.
 */
#ifndef QK_MATH_H
#define QK_MATH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Exact, single-instruction on arm64 and x86-64 alike. */
#define qk_fabs(x)      __builtin_fabs(x)
#define qk_fabsf(x)     __builtin_fabsf(x)
#define qk_sqrt(x)      __builtin_sqrt(x)
#define qk_sqrtf(x)     __builtin_sqrtf(x)
#define qk_floor(x)     __builtin_floor(x)
#define qk_floorf(x)    __builtin_floorf(x)
#define qk_ceil(x)      __builtin_ceil(x)
#define qk_ceilf(x)     __builtin_ceilf(x)
#define qk_trunc(x)     __builtin_trunc(x)
#define qk_truncf(x)    __builtin_truncf(x)
/* Round half away from zero. arm64 has FRINTA for this, but x86 does not, and
 * a call to libm's round() is exactly what this file exists to avoid; floor
 * and copysign are single instructions on both.
 */
static inline double qk_round(double x)
{
    double magnitude = __builtin_fabs(x), rounded;

    if (!(magnitude < 4503599627370496.0)) /* >= 2^52, or NaN: already whole. */
        return x;
    /* Nearest, ties to even, then push the ties away from zero. Adding 0.5 and
     * flooring instead would round 0.49999999999999994 up to 1.
     */
    rounded = __builtin_rint(magnitude);
    if (magnitude - rounded == 0.5)
        rounded += 1.0;
    return __builtin_copysign(rounded, x);
}

static inline float qk_roundf(float x)
{
    return (float)qk_round((double)x);
}
#define qk_copysign(x, y)  __builtin_copysign((x), (y))
#define qk_copysignf(x, y) __builtin_copysignf((x), (y))

/* Transcendentals and the operations with no arm64 instruction. Accuracy is
 * verified by tests/kernel_math_test.cpp: see its tolerances for each group.
 */
double qk_fmod(double x, double y);
double qk_sin(double x);
double qk_cos(double x);
double qk_tan(double x);
double qk_atan(double x);
double qk_atan2(double y, double x);
double qk_asin(double x);
double qk_acos(double x);
double qk_exp(double x);
double qk_log(double x);
double qk_pow(double x, double y);
double qk_hypot(double x, double y);
double qk_remainder(double x, double y);
double qk_ldexp(double x, int exponent);
double qk_strtod(const char *text, char **end);

int qk_isfinite(double x);
int qk_isnan(double x);
int qk_isinf(double x);

float qk_fmodf(float x, float y);
float qk_sinf(float x);
float qk_cosf(float x);
float qk_tanf(float x);
float qk_atanf(float x);
float qk_atan2f(float y, float x);
float qk_asinf(float x);
float qk_acosf(float x);
float qk_expf(float x);
float qk_logf(float x);
float qk_powf(float x, float y);
float qk_hypotf(float x, float y);
float qk_remainderf(float x, float y);

#ifdef __cplusplus
}
#endif

#endif /* QK_MATH_H */
