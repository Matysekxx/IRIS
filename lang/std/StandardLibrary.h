#ifndef STANDARD_LIBRARY_H
#define STANDARD_LIBRARY_H

#include "../core/NativeRegistry.h"
#include "Math.h"
#include "NativeList.h"
#include "NativeHashMap.h"
#include "NativeEnumMap.h"
#include "NativeHashSet.h"
#include "NativeLinkedList.h"
#include "System.h"

namespace iris::std_lib {
    inline void initialize() {
        auto& registry = iris::core::NativeRegistry::getInstance();

        // Math functions
        registry.registerFunction("Math.sin", iris_math_sin, 1);
        registry.registerFunction("Math.cos", iris_math_cos, 1);
        registry.registerFunction("Math.sqrt", iris_math_sqrt, 1);
        registry.registerFunction("Math.pow", iris_math_pow, 2);
        registry.registerFunction("Math.abs", iris_math_abs, 1);

        // System functions
        registry.registerFunction("System.time", iris_system_time, 0);

        // List & Map constructors (factory functions)
        registry.registerFunction("Collections.NativeList", [](iris::core::Value* args, int argCount) {
            return iris::core::Value(new NativeList());
        }, 0);

        registry.registerFunction("Collections.NativeHashMap", [](iris::core::Value* args, int argCount) {
            return iris::core::Value(new NativeHashMap());
        }, 0);

        registry.registerFunction("Collections.NativeHashSet", [](iris::core::Value* args, int argCount) {
            return iris::core::Value(new NativeHashSet());
        }, 0);

        registry.registerFunction("Collections.NativeLinkedList", [](iris::core::Value* args, int argCount) {
            return iris::core::Value(new NativeLinkedList());
        }, 0);

        registry.registerFunction("Collections.NativeEnumMap", [](iris::core::Value* args, int argCount) {
            if (argCount < 1 || !args[0].isInt()) return iris::core::Value();
            return iris::core::Value(new NativeEnumMap(args[0].asInt));
        }, 1);
    }
}

#endif //STANDARD_LIBRARY_H
