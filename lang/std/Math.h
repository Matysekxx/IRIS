#ifndef MATH_LIB_H
#define MATH_LIB_H

#include "../core/Native.h"
#include <cmath>

namespace iris::std_lib {
    inline iris::core::Value iris_math_sin(iris::core::Value* args, int argCount) {
        if (argCount < 1) return iris::core::Value(0.0);
        return iris::core::Value(std::sin(iris::core::toDouble(args[0])));
    }

    inline iris::core::Value iris_math_cos(iris::core::Value* args, int argCount) {
        if (argCount < 1) return iris::core::Value(0.0);
        return iris::core::Value(std::cos(iris::core::toDouble(args[0])));
    }

    inline iris::core::Value iris_math_sqrt(iris::core::Value* args, int argCount) {
        if (argCount < 1) return iris::core::Value(0.0);
        return iris::core::Value(std::sqrt(iris::core::toDouble(args[0])));
    }

    inline iris::core::Value iris_math_pow(iris::core::Value* args, int argCount) {
        if (argCount < 2) return iris::core::Value(0.0);
        return iris::core::Value(std::pow(iris::core::toDouble(args[0]), iris::core::toDouble(args[1])));
    }
    
    inline iris::core::Value iris_math_abs(iris::core::Value* args, int argCount) {
        if (argCount < 1) return iris::core::Value(0.0);
        if (args[0].isInt()) return iris::core::Value(std::abs(args[0].asInt));
        return iris::core::Value(std::abs(args[0].asDouble));
    }
}

#endif //MATH_LIB_H
