#ifndef STANDARD_LIBRARY_H
#define STANDARD_LIBRARY_H

#include "../core/NativeRegistry.h"
#include "Math.h"
#include "NativeList.h"
#include "NativeMap.h"

namespace iris::std_lib {
    inline void initialize() {
        auto& registry = iris::core::NativeRegistry::getInstance();

        // Math functions
        registry.registerFunction("sin", iris_math_sin, 1);
        registry.registerFunction("cos", iris_math_cos, 1);
        registry.registerFunction("sqrt", iris_math_sqrt, 1);
        registry.registerFunction("pow", iris_math_pow, 2);
        registry.registerFunction("abs", iris_math_abs, 1);

        // List & Map constructors (factory functions)
        registry.registerFunction("NativeList", [](iris::core::Value* args, int argCount) {
            return iris::core::Value(new NativeList());
        }, 0);

        registry.registerFunction("NativeMap", [](iris::core::Value* args, int argCount) {
            return iris::core::Value(new NativeMap());
        }, 0);
    }
}

#endif //STANDARD_LIBRARY_H
