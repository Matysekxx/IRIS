#ifndef MATH_LIB_H
#define MATH_LIB_H

#include "core/Native.h"
#include <cmath>

namespace iris::std_lib {
    inline double iris_math_sin(double v) {
        return std::sin(v);
    }

    inline double iris_math_cos(double v) {
        return std::cos(v);
    }

    inline double iris_math_tan(double v) {
        return std::tan(v);
    }

    inline double iris_math_sqrt(double v) {
        return std::sqrt(v);
    }

    inline double iris_math_pow(double base, double exp) {
        return std::pow(base, exp);
    }

    inline double iris_math_abs(double v) {
        return std::abs(v);
    }

    inline double iris_math_floor(double v) {
        return std::floor(v);
    }

    inline double iris_math_ceil(double v) {
        return std::ceil(v);
    }

    inline double iris_math_round(double v) {
        return std::round(v);
    }

    inline double iris_math_log(double v) {
        return std::log(v);
    }

    inline double iris_math_log10(double v) {
        return std::log10(v);
    }

    inline double iris_math_exp(double v) {
        return std::exp(v);
    }

    inline double iris_math_min(double a, double b) {
        return std::fmin(a, b);
    }

    inline double iris_math_max(double a, double b) {
        return std::fmax(a, b);
    }

    inline double iris_math_atan2(double y, double x) {
        return std::atan2(y, x);
    }

    inline double iris_math_asin(double v) {
        return std::asin(v);
    }

    inline double iris_math_acos(double v) {
        return std::acos(v);
    }

    inline double iris_math_atan(double v) {
        return std::atan(v);
    }

    inline double iris_math_sinh(double v) {
        return std::sinh(v);
    }

    inline double iris_math_cosh(double v) {
        return std::cosh(v);
    }

    inline double iris_math_tanh(double v) {
        return std::tanh(v);
    }

    inline double iris_math_degrees(double v) {
        return v * 180.0 / 3.14159265358979323846;
    }

    inline double iris_math_radians(double v) {
        return v * 3.14159265358979323846 / 180.0;
    }

    inline double iris_math_cbrt(double v) {
        return std::cbrt(v);
    }

    inline double iris_math_hypot(double x, double y) {
        return std::hypot(x, y);
    }

    inline double iris_math_log2(double v) {
        return std::log2(v);
    }

    inline double iris_math_trunc(double v) {
        return std::trunc(v);
    }

    inline double iris_math_sign(double v) {
        return (v > 0.0) ? 1.0 : (v < 0.0) ? -1.0 : 0.0;
    }

    inline double iris_math_fmod(double a, double b) {
        return std::fmod(a, b);
    }

    inline double iris_math_erf(double v) {
        return std::erf(v);
    }
}

#endif //MATH_LIB_H
