// Accuracy and range checks for the kernel UI's libm subset. The kernel links
// no libm, so kernel/compat/qk_math.c must stand on its own; here the host's
// libm is the reference. Tolerances are relative unless stated otherwise.
#include "qk_math.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace {

int failures = 0;
long long checks = 0;

void Report(const char* name, double input, double other, double got, double want,
            double error, double tolerance) {
    if (failures++ < 20)
        std::printf("FAIL %s(%.17g, %.17g) = %.17g want %.17g error %.3g > %.3g\n",
                    name, input, other, got, want, error, tolerance);
}

// Relative error, falling back to absolute error near zero so a cancelling
// result like sin(pi) is not judged against a meaningless ratio.
void Close(const char* name, double got, double want, double tolerance,
           double input, double other = 0) {
    ++checks;
    if (std::isinf(want) || std::isinf(got)) {
        // An overflow must match exactly, sign included.
        if (got != want)
            Report(name, input, other, got, want, 1, tolerance);
        return;
    }
    const double scale = std::fabs(want);
    const double error = std::fabs(got - want) / (scale > 1e-8 ? scale : 1.0);
    if (!(error <= tolerance) || std::isnan(got) != std::isnan(want))
        Report(name, input, other, got, want, error, tolerance);
}

void Rounding() {
    const double values[] = {0.0, -0.0, 0.5, -0.5, 1.5, -1.5, 2.5, -2.5, 7.25, -7.25,
                             1e15 + 0.5, -1e15 - 0.5, 1e300, -1e300};
    for (double value : values) {
        Close("floor", qk_floor(value), std::floor(value), 0, value);
        Close("ceil", qk_ceil(value), std::ceil(value), 0, value);
        Close("trunc", qk_trunc(value), std::trunc(value), 0, value);
        Close("fabs", qk_fabs(value), std::fabs(value), 0, value);
        Close("round", qk_round(value), std::round(value), 0, value);
        Close("roundf", qk_roundf((float)value), std::round((float)value), 0, value);
        Close("sqrt", qk_sqrt(std::fabs(value)), std::sqrt(std::fabs(value)), 0, value);
    }
    // Halfway cases and the value just below .5 are where a naive
    // floor(x + 0.5) gets it wrong.
    for (double value : {0.49999999999999994, -0.49999999999999994, 0.5, 1.5,
                         2.5, 4503599627370495.5, 1e308, 0.0}) {
        Close("round", qk_round(value), std::round(value), 0, value);
        Close("round", qk_round(-value), std::round(-value), 0, -value);
    }
    for (int step = -100000; step <= 100000; step += 7) {
        const double value = step * 0.05;
        Close("round", qk_round(value), std::round(value), 0, value);
    }
    for (int exponent = -1080; exponent <= 1020; exponent += 7)
        Close("ldexp", qk_ldexp(1.3, exponent), std::ldexp(1.3, exponent), 1e-15, exponent);
    assert(qk_isnan(qk_fmod(1.0, 0.0)));
    assert(qk_isinf(1.0 / 0.0) && !qk_isfinite(1.0 / 0.0));
    assert(qk_isfinite(1e308) && !qk_isnan(1e308));
}

void Modulo() {
    const double numerators[] = {0.0, 1e-30, 0.5, 1.0, 1.5, 3.0, 7.0, 1e3, 1e6, 1e15,
                                 1e30, 1e100, -2.5, -1e9};
    const double denominators[] = {1e-20, 0.1, 0.25, 1.0, 2.0, 3.0, 6.28318530717958623,
                                   1e5, 1e20, -1.0, -0.75};
    for (double numerator : numerators)
        for (double denominator : denominators) {
            Close("fmod", qk_fmod(numerator, denominator),
                  std::fmod(numerator, denominator), 1e-15, numerator, denominator);
            Close("remainder", qk_remainder(numerator, denominator),
                  std::remainder(numerator, denominator), 1e-15, numerator, denominator);
        }
    // Ties go to the even quotient, which is where remainder differs from fmod.
    for (double numerator : {0.5, 1.5, 2.5, 3.5, -0.5, -1.5, -2.5, 7.5, 180.0, 540.0})
        for (double denominator : {1.0, 2.0, 5.0, 360.0})
            Close("remainder", qk_remainder(numerator, denominator),
                  std::remainder(numerator, denominator), 1e-15, numerator, denominator);
}

void Trigonometry() {
    for (int step = -200000; step <= 200000; step += 7) {
        const double angle = step * 0.0013;
        Close("sin", qk_sin(angle), std::sin(angle), 2e-15, angle);
        Close("cos", qk_cos(angle), std::cos(angle), 2e-15, angle);
    }
    // Exact multiples of pi/2 are where argument reduction is worst.
    for (int quadrant = -40; quadrant <= 40; quadrant++) {
        const double angle = quadrant * (M_PI / 2);
        Close("sin", qk_sin(angle), std::sin(angle), 1e-14, angle);
        Close("cos", qk_cos(angle), std::cos(angle), 1e-14, angle);
    }
    for (double angle : {1e6, 1e7, 5e7, -1e7, 123456.789, 1e8}) {
        Close("sin", qk_sin(angle), std::sin(angle), 1e-10, angle);
        Close("cos", qk_cos(angle), std::cos(angle), 1e-10, angle);
    }
    for (int step = -1000; step <= 1000; step++) {
        const double angle = step * 0.0015;
        Close("tan", qk_tan(angle), std::tan(angle), 1e-14, angle);
    }
    assert(qk_isnan(qk_sin(qk_ldexp(1.0, 1024))));
}

void InverseTrigonometry() {
    for (int step = -100000; step <= 100000; step += 3) {
        const double value = step * 0.0007;
        Close("atan", qk_atan(value), std::atan(value), 2e-15, value);
    }
    for (double value : {1e-300, 1e-8, 1.0, 1e8, 1e300, -1e300}) {
        Close("atan", qk_atan(value), std::atan(value), 2e-15, value);
        Close("atan", qk_atan(-value), std::atan(-value), 2e-15, -value);
    }
    for (int step = -1000; step <= 1000; step++) {
        const double value = step * 0.001;
        Close("asin", qk_asin(value), std::asin(value), 1e-14, value);
        Close("acos", qk_acos(value), std::acos(value), 1e-14, value);
    }
    assert(qk_isnan(qk_asin(1.5)) && qk_isnan(qk_acos(-1.5)));
    const double quadrants[] = {0.0, -0.0, 1.0, -1.0, 3.5, -3.5, 1e8, -1e8};
    for (double y : quadrants)
        for (double x : quadrants)
            Close("atan2", qk_atan2(y, x), std::atan2(y, x), 1e-14, y, x);
    Close("atan2", qk_atan2(1.0, 0.0), std::atan2(1.0, 0.0), 0, 1.0, 0.0);
    Close("atan2", qk_atan2(0.0, -1.0), std::atan2(0.0, -1.0), 1e-15, 0.0, -1.0);
}

void Exponentials() {
    for (int step = -7000; step <= 7000; step += 3) {
        const double value = step * 0.1;
        Close("exp", qk_exp(value), std::exp(value), 2e-15, value);
    }
    assert(qk_isinf(qk_exp(1000.0)) && qk_exp(-1000.0) == 0.0);
    for (int step = 1; step <= 200000; step += 7) {
        const double value = step * 0.0013;
        Close("log", qk_log(value), std::log(value), 4e-15, value);
    }
    for (double value : {1e-308, 5e-324, 1e-30, 0.5, 1.0, 1.0000001, 2.0, 1e30, 1e300}) {
        Close("log", qk_log(value), std::log(value), 1e-13, value);
        Close("exp", qk_exp(qk_log(value)), value, 1e-13, value);
    }
    assert(qk_log(0.0) < -1e300 && qk_isnan(qk_log(-1.0)));
}

void Powers() {
    const double bases[] = {0.5, 1.0, 1.5, 2.0, 10.0, 1e5, 0.001, -2.0, -0.5};
    const double exponents[] = {0.0, 1.0, 2.0, 3.0, -1.0, -2.0, 7.0, 0.5, 1.0 / 3,
                                2.2, -3.7, 10.5};
    for (double base : bases)
        for (double exponent : exponents) {
            const double want = std::pow(base, exponent);
            const double got = qk_pow(base, exponent);
            if (std::isnan(want)) {
                ++checks;
                if (!qk_isnan(got))
                    Report("pow", base, exponent, got, want, 1, 0);
                continue;
            }
            Close("pow", got, want, 2e-14, base, exponent);
        }
    Close("pow", qk_pow(0.0, 2.0), 0.0, 0, 0.0, 2.0);
    assert(qk_isinf(qk_pow(0.0, -2.5)));
    Close("pow", qk_pow(123.456, 0.0), 1.0, 0, 123.456, 0.0);
    // Integral exponents go through binary exponentiation: check the bit walk.
    for (int exponent = -30; exponent <= 30; exponent++)
        Close("pow", qk_pow(1.7, exponent), std::pow(1.7, exponent), 1e-13, 1.7, exponent);
    const double sides[] = {0.0, 1e-200, 3.0, 4.0, 1e200, -5.0};
    for (double x : sides)
        for (double y : sides)
            Close("hypot", qk_hypot(x, y), std::hypot(x, y), 1e-15, x, y);
    assert(qk_isinf(qk_hypot(1.0 / 0.0, 1.0)));
}

void Floats() {
    for (int step = -20000; step <= 20000; step += 3) {
        const float value = step * 0.0017f;
        Close("sinf", qk_sinf(value), std::sin((double)value), 1e-6, value);
        Close("cosf", qk_cosf(value), std::cos((double)value), 1e-6, value);
        Close("atanf", qk_atanf(value), std::atan((double)value), 1e-6, value);
    }
    for (int step = 1; step <= 20000; step += 3) {
        const float value = step * 0.0017f;
        Close("logf", qk_logf(value), std::log((double)value), 1e-6, value);
        Close("sqrtf", qk_sqrtf(value), std::sqrt((double)value), 1e-6, value);
        Close("expf", qk_expf(value), std::exp((double)value), 1e-6, value);
        Close("powf", qk_powf(value, 1.7f), std::pow((double)value, 1.7), 1e-6, value);
        Close("hypotf", qk_hypotf(value, 3.f), std::hypot((double)value, 3.0), 1e-6, value);
        Close("fmodf", qk_fmodf(value, 2.f), std::fmod((double)value, 2.0), 1e-6, value);
    }
}

void Parsing() {
    struct Case { const char* text; double value; int consumed; };
    const Case cases[] = {
        {"0", 0.0, 1}, {"1", 1.0, 1}, {"-1", -1.0, 2}, {"+2.5", 2.5, 4},
        {"  3.75xyz", 3.75, 6}, {".5", 0.5, 2}, {"5.", 5.0, 2},
        {"1e3", 1000.0, 3}, {"1E-3", 0.001, 4}, {"1.5e+2", 150.0, 6},
        {"2.5e", 2.5, 3}, {"2.5e+", 2.5, 3}, {"1234567890123456789", 1.234567890123456789e18, 19},
        {"0.000001", 1e-6, 8}, {"6.02214076e23", 6.02214076e23, 13},
        {"abc", 0.0, 0}, {"", 0.0, 0}, {"-.25e1", -2.5, 6},
    };
    for (const Case& test : cases) {
        char* end = nullptr;
        const double got = qk_strtod(test.text, &end);
        ++checks;
        if (std::fabs(got - test.value) > 1e-15 * std::fabs(test.value) + 1e-300 ||
            end - test.text != test.consumed) {
            std::printf("FAIL strtod(\"%s\") = %.17g consumed %d want %.17g consumed %d\n",
                        test.text, got, (int)(end - test.text), test.value, test.consumed);
            ++failures;
        }
    }
}

} // namespace

int main() {
    Rounding();
    Modulo();
    Trigonometry();
    InverseTrigonometry();
    Exponentials();
    Powers();
    Floats();
    Parsing();
    std::printf("kernel math: %lld checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
