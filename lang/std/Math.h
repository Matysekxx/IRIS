#ifndef MATH_LIB_H
#define MATH_LIB_H

#include "../core/Native.h"
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
}

#endif //MATH_LIB_H
