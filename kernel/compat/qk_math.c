/* SPDX-License-Identifier: MIT */
/* Freestanding float/double subset: no libm, no tables that need runtime
 * initialisation, no errno and no FP exception flags. Argument reduction uses
 * fused multiply-add, which is baseline on arm64; the host test build gets it
 * from the compiler. Accuracy targets and ranges are in tests/kernel_math_test.cpp.
 */
#include "qk_math.h"

union qk_double_bits {
    double value;
    unsigned long long bits;
};

static double qk_from_bits(unsigned long long bits)
{
    union qk_double_bits split;

    split.bits = bits;
    return split.value;
}

static unsigned long long qk_to_bits(double value)
{
    union qk_double_bits split;

    split.value = value;
    return split.bits;
}

#define QK_NAN  qk_from_bits(0x7ff8000000000000ULL)
#define QK_INF  qk_from_bits(0x7ff0000000000000ULL)
#define QK_PI       3.14159265358979311600
#define QK_PI_2     1.57079632679489655800
#define QK_TWO_PI   6.28318530717958623200
#define QK_SQRT2    1.41421356237309514547

/* pi/2 and ln2 as a sum of doubles: the reduction below needs more than the
 * 53 bits a single double carries.
 */
#define QK_PI_2_HI  1.57079632679489655800e+00
#define QK_PI_2_MID 6.12323399573676603587e-17
#define QK_PI_2_LO -1.49738490485916975402e-33
#define QK_LN2_HI   6.93147180559945286227e-01
#define QK_LN2_LO   2.31904681384629955831e-17

int qk_isnan(double x)
{
    unsigned long long bits = qk_to_bits(x);

    return ((bits >> 52) & 0x7ffULL) == 0x7ffULL && (bits & 0x000fffffffffffffULL);
}

int qk_isinf(double x)
{
    unsigned long long bits = qk_to_bits(x);

    return ((bits >> 52) & 0x7ffULL) == 0x7ffULL && !(bits & 0x000fffffffffffffULL);
}

int qk_isfinite(double x)
{
    return ((qk_to_bits(x) >> 52) & 0x7ffULL) != 0x7ffULL;
}

static int qk_exponent_of(double x)
{
    return (int)((qk_to_bits(x) >> 52) & 0x7ffULL) - 1023;
}

double qk_ldexp(double x, int exponent)
{
    /* Two steps keep the scale factor itself representable. */
    while (exponent > 1023) {
        x *= qk_from_bits((unsigned long long)(1023 + 1023) << 52);
        exponent -= 1023;
    }
    while (exponent < -1022) {
        x *= qk_from_bits((unsigned long long)(-1022 + 1023) << 52);
        exponent += 1022;
    }
    if (!qk_isfinite(x) || x == 0.0)
        return x;
    return x * qk_from_bits((unsigned long long)(exponent + 1023) << 52);
}

double qk_fmod(double x, double y)
{
    double ax = qk_fabs(x), ay = qk_fabs(y), remainder, scaled;
    int shift;

    if (qk_isnan(x) || qk_isnan(y) || qk_isinf(x) || ay == 0.0)
        return QK_NAN;
    if (qk_isinf(y) || ax < ay)
        return x;
    if (ax == ay)
        return qk_copysign(0.0, x);
    /* Subnormal operands are not worth an exact path in a UI. */
    if (qk_exponent_of(ax) <= -1023 || qk_exponent_of(ay) <= -1023)
        return qk_copysign(ax - qk_trunc(ax / ay) * ay, x);

    shift = qk_exponent_of(ax) - qk_exponent_of(ay);
    remainder = ax;
    scaled = qk_ldexp(ay, shift);
    /* Each subtraction is exact: operands stay within a factor of two. */
    for (; shift >= 0; shift--) {
        if (remainder >= scaled)
            remainder -= scaled;
        scaled *= 0.5;
    }
    return qk_copysign(remainder, x);
}

/* sin(z)/z and cos(z) on |z| <= pi/4. Truncated past the point where the next
 * term falls below one unit in the last place of the result.
 */
static double qk_sin_core(double z)
{
    double w = z * z;

    return z * (1.0 - w * (1.0 / 6 - w * (1.0 / 120 - w * (1.0 / 5040 -
           w * (1.0 / 362880 - w * (1.0 / 39916800 - w * (1.0 / 6227020800.0 -
           w * (1.0 / 1307674368000.0 - w * (1.0 / 355687428096000.0)))))))));
}

static double qk_cos_core(double z)
{
    double w = z * z;

    return 1.0 - w * (1.0 / 2 - w * (1.0 / 24 - w * (1.0 / 720 - w * (1.0 / 40320 -
           w * (1.0 / 3628800 - w * (1.0 / 479001600 - w * (1.0 / 87178291200.0 -
           w * (1.0 / 20922789888000.0))))))));
}

/* Reduce x to r = x - n*pi/2 with |r| <= pi/4 and report n mod 4. */
static int qk_reduce_quadrant(double x, double *reduced)
{
    double n, product, error;
    long long quadrant;

    if (qk_fabs(x) > 1.0e8) {
        /* Beyond this the double approximation of 2*pi dominates the result. */
        x = qk_fmod(x, QK_TWO_PI);
    }
    n = qk_round(x * (2.0 / QK_PI));
    quadrant = (long long)n;
    product = n * QK_PI_2_HI;
    error = __builtin_fma(n, QK_PI_2_HI, -product);
    x = (x - product) - error;
    product = n * QK_PI_2_MID;
    error = __builtin_fma(n, QK_PI_2_MID, -product);
    x = (x - product) - error;
    *reduced = x - n * QK_PI_2_LO;
    return (int)(quadrant & 3);
}

double qk_sin(double x)
{
    double r;
    int quadrant;

    if (!qk_isfinite(x))
        return QK_NAN;
    quadrant = qk_reduce_quadrant(x, &r);
    switch (quadrant) {
    case 0:  return qk_sin_core(r);
    case 1:  return qk_cos_core(r);
    case 2:  return -qk_sin_core(r);
    default: return -qk_cos_core(r);
    }
}

double qk_cos(double x)
{
    double r;
    int quadrant;

    if (!qk_isfinite(x))
        return QK_NAN;
    quadrant = qk_reduce_quadrant(x, &r);
    switch (quadrant) {
    case 0:  return qk_cos_core(r);
    case 1:  return -qk_sin_core(r);
    case 2:  return -qk_cos_core(r);
    default: return qk_sin_core(r);
    }
}

double qk_tan(double x)
{
    double sine = qk_sin(x), cosine = qk_cos(x);

    if (cosine == 0.0)
        return qk_copysign(QK_INF, sine);
    return sine / cosine;
}

/* atan(t) for t >= 0. Three half-angle steps bring the argument below 0.1,
 * where the plain series converges well past double precision and needs no
 * table of atan constants.
 */
static double qk_atan_core(double t)
{
    double w, series;
    int step;

    for (step = 0; step < 3; step++)
        t = t / (1.0 + qk_sqrt(1.0 + t * t));
    w = t * t;
    series = t * (1.0 - w * (1.0 / 3 - w * (1.0 / 5 - w * (1.0 / 7 - w * (1.0 / 9 -
             w * (1.0 / 11 - w * (1.0 / 13 - w * (1.0 / 15 - w * (1.0 / 17)))))))));
    return 8.0 * series;
}

double qk_atan(double x)
{
    double magnitude = qk_fabs(x), result;

    if (qk_isnan(x))
        return QK_NAN;
    if (qk_isinf(x))
        return qk_copysign(QK_PI_2, x);
    result = magnitude > 1.0 ? QK_PI_2 - qk_atan_core(1.0 / magnitude)
                             : qk_atan_core(magnitude);
    return qk_copysign(result, x);
}

double qk_atan2(double y, double x)
{
    double angle;

    if (qk_isnan(x) || qk_isnan(y))
        return QK_NAN;
    if (x == 0.0 && y == 0.0) {
        /* Match libm: the sign of a zero x still picks the half plane. */
        return qk_to_bits(x) >> 63 ? qk_copysign(QK_PI, y) : qk_copysign(0.0, y);
    }
    if (qk_isinf(x) && qk_isinf(y))
        return qk_copysign(qk_to_bits(x) >> 63 ? 3.0 * QK_PI / 4.0 : QK_PI / 4.0, y);
    if (x == 0.0)
        return qk_copysign(QK_PI_2, y);
    if (qk_isinf(y))
        return qk_copysign(QK_PI_2, y);
    if (qk_isinf(x))
        return qk_to_bits(x) >> 63 ? qk_copysign(QK_PI, y) : qk_copysign(0.0, y);
    angle = qk_atan(qk_fabs(y / x));
    if (qk_to_bits(x) >> 63)
        angle = QK_PI - angle;
    return qk_copysign(angle, y);
}

double qk_asin(double x)
{
    if (qk_fabs(x) > 1.0)
        return QK_NAN;
    return qk_atan2(x, qk_sqrt((1.0 - x) * (1.0 + x)));
}

double qk_acos(double x)
{
    if (qk_fabs(x) > 1.0)
        return QK_NAN;
    return qk_atan2(qk_sqrt((1.0 - x) * (1.0 + x)), x);
}

double qk_exp(double x)
{
    double n, product, error, r, series;
    int exponent;

    if (qk_isnan(x))
        return QK_NAN;
    if (x > 710.0)
        return QK_INF;
    if (x < -745.0)
        return 0.0;
    n = qk_round(x * (1.0 / QK_LN2_HI));
    exponent = (int)n;
    product = n * QK_LN2_HI;
    error = __builtin_fma(n, QK_LN2_HI, -product);
    r = ((x - product) - error) - n * QK_LN2_LO;
    series = 1.0 + r * (1.0 + r * (1.0 / 2 + r * (1.0 / 6 + r * (1.0 / 24 +
             r * (1.0 / 120 + r * (1.0 / 720 + r * (1.0 / 5040 + r * (1.0 / 40320 +
             r * (1.0 / 362880 + r * (1.0 / 3628800 + r * (1.0 / 39916800 +
             r * (1.0 / 479001600))))))))))));
    return qk_ldexp(series, exponent);
}

double qk_log(double x)
{
    double mantissa, s, w, series;
    int exponent;

    if (qk_isnan(x) || x < 0.0)
        return QK_NAN;
    if (x == 0.0)
        return -QK_INF;
    if (qk_isinf(x))
        return QK_INF;
    exponent = qk_exponent_of(x);
    if (exponent <= -1023) {
        /* Normalise a subnormal before splitting off its exponent. */
        x = qk_ldexp(x, 60);
        exponent = qk_exponent_of(x) - 60;
        mantissa = qk_ldexp(x, -qk_exponent_of(x));
    } else {
        mantissa = qk_ldexp(x, -exponent);
    }
    if (mantissa > QK_SQRT2) {
        mantissa *= 0.5;
        exponent += 1;
    }
    /* log(m) = 2*atanh((m-1)/(m+1)); |s| <= 0.1716 for m in [sqrt(.5), sqrt(2)]. */
    s = (mantissa - 1.0) / (mantissa + 1.0);
    w = s * s;
    series = s * (1.0 + w * (1.0 / 3 + w * (1.0 / 5 + w * (1.0 / 7 + w * (1.0 / 9 +
             w * (1.0 / 11 + w * (1.0 / 13 + w * (1.0 / 15 + w * (1.0 / 17 +
             w * (1.0 / 19 + w * (1.0 / 21)))))))))));
    return (double)exponent * QK_LN2_HI + ((double)exponent * QK_LN2_LO + 2.0 * series);
}

double qk_pow(double x, double y)
{
    double result, base;
    long long whole;
    int negate_exponent;

    if (y == 0.0 || x == 1.0)
        return 1.0;
    if (qk_isnan(x) || qk_isnan(y))
        return QK_NAN;
    /* An integral exponent keeps negative bases usable and stays exact for the
     * small powers the UI asks for.
     */
    if (y == qk_trunc(y) && qk_fabs(y) <= 1024.0) {
        whole = (long long)qk_fabs(y);
        negate_exponent = y < 0.0;
        result = 1.0;
        base = x;
        while (whole) {
            if (whole & 1)
                result *= base;
            base *= base;
            whole >>= 1;
        }
        return negate_exponent ? 1.0 / result : result;
    }
    if (x < 0.0)
        return QK_NAN;
    if (x == 0.0)
        return y < 0.0 ? QK_INF : 0.0;
    return qk_exp(y * qk_log(x));
}

double qk_hypot(double x, double y)
{
    double larger = qk_fabs(x), smaller = qk_fabs(y), ratio;

    if (qk_isinf(x) || qk_isinf(y))
        return QK_INF;
    if (larger < smaller) {
        ratio = larger;
        larger = smaller;
        smaller = ratio;
    }
    if (larger == 0.0)
        return 0.0;
    ratio = smaller / larger;
    return larger * qk_sqrt(1.0 + ratio * ratio);
}

/* IEEE remainder: like fmod, but the result is the one nearest zero, and a
 * tie goes to the even quotient.
 */
double qk_remainder(double x, double y)
{
    double magnitude, half, rest, quotient;

    if (qk_isnan(x) || qk_isnan(y) || qk_isinf(x) || y == 0.0)
        return QK_NAN;
    if (qk_isinf(y))
        return x;
    rest = qk_fmod(x, y);
    magnitude = qk_fabs(y);
    half = magnitude * 0.5;
    if (rest > half)
        rest -= magnitude;
    else if (rest < -half)
        rest += magnitude;
    if (qk_fabs(rest) == half) {
        quotient = (x - rest) / y;
        if (qk_fmod(quotient, 2.0) != 0.0)
            rest -= qk_copysign(magnitude, rest);
    }
    return rest;
}

double qk_strtod(const char *text, char **end)
{
    const char *cursor = text;
    double mantissa = 0.0, scale;
    int negative = 0, digits = 0, fraction = 0, exponent = 0, exponent_sign = 1;
    int exponent_digits = 0, explicit_exponent = 0;

    if (!text) {
        if (end)
            *end = (char *)text;
        return 0.0;
    }
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r' ||
           *cursor == '\v' || *cursor == '\f')
        cursor++;
    if (*cursor == '+' || *cursor == '-')
        negative = *cursor++ == '-';
    for (; (*cursor >= '0' && *cursor <= '9') || (*cursor == '.' && !fraction); cursor++) {
        if (*cursor == '.') {
            fraction = 1;
            continue;
        }
        /* Past 17 significant digits the extra ones cannot change the result. */
        if (digits < 17) {
            mantissa = mantissa * 10.0 + (*cursor - '0');
            digits++;
            if (fraction)
                exponent--;
        } else if (!fraction) {
            exponent++;
        }
    }
    if (!digits) {
        if (end)
            *end = (char *)text;
        return 0.0;
    }
    if (*cursor == 'e' || *cursor == 'E') {
        const char *mark = cursor++;

        if (*cursor == '+' || *cursor == '-')
            exponent_sign = *cursor++ == '-' ? -1 : 1;
        for (; *cursor >= '0' && *cursor <= '9'; cursor++) {
            if (exponent_digits < 6) {
                explicit_exponent = explicit_exponent * 10 + (*cursor - '0');
                exponent_digits++;
            }
        }
        if (exponent_digits)
            exponent += exponent_sign * explicit_exponent;
        else
            cursor = mark;
    }
    if (end)
        *end = (char *)cursor;
    scale = qk_pow(10.0, (double)(exponent < 0 ? -exponent : exponent));
    mantissa = exponent < 0 ? mantissa / scale : mantissa * scale;
    return negative ? -mantissa : mantissa;
}

float qk_fmodf(float x, float y)   { return (float)qk_fmod(x, y); }
float qk_sinf(float x)             { return (float)qk_sin(x); }
float qk_cosf(float x)             { return (float)qk_cos(x); }
float qk_tanf(float x)             { return (float)qk_tan(x); }
float qk_atanf(float x)            { return (float)qk_atan(x); }
float qk_atan2f(float y, float x)  { return (float)qk_atan2(y, x); }
float qk_asinf(float x)            { return (float)qk_asin(x); }
float qk_acosf(float x)            { return (float)qk_acos(x); }
float qk_expf(float x)             { return (float)qk_exp(x); }
float qk_logf(float x)             { return (float)qk_log(x); }
float qk_powf(float x, float y)    { return (float)qk_pow(x, y); }
float qk_hypotf(float x, float y)  { return (float)qk_hypot(x, y); }
float qk_remainderf(float x, float y) { return (float)qk_remainder(x, y); }
