#ifndef STANDARD_LIBRARY_H
#define STANDARD_LIBRARY_H

#include "../core/NativeRegistry.h"
#include "Math.h"
#include "NativeList.h"
#include "NativeHashMap.h"
#include "NativeEnumMap.h"
#include "NativeHashSet.h"
#include "NativeLinkedList.h"

namespace iris::std_lib {
    inline void initialize() {
        auto& registry = iris::core::NativeRegistry::getInstance();

        // Math functions
        registry.registerFunction("sin", iris_math_sin, 1);
        registry.registerFunction("cos", iris_math_cos, 1);
        registry.registerFunction("tan", iris_math_tan, 1);
        registry.registerFunction("sqrt", iris_math_sqrt, 1);
        registry.registerFunction("pow", iris_math_pow, 2);
        registry.registerFunction("abs", iris_math_abs, 1);

        // List & Map constructors (factory functions)
        registry.registerFunction("NativeList", [](iris::core::Value* args, int argCount) {
            return iris::core::Value(new NativeList());
        }, 0);

        registry.registerFunction("NativeHashMap", [](iris::core::Value* args, int argCount) {
            return iris::core::Value(new NativeHashMap());
        }, 0);

        registry.registerFunction("NativeHashSet", [](iris::core::Value* args, int argCount) {
            return iris::core::Value(new NativeHashSet());
        }, 0);

        registry.registerFunction("NativeLinkedList", [](iris::core::Value* args, int argCount) {
            return iris::core::Value(new NativeLinkedList());
        }, 0);

        registry.registerFunction("NativeEnumMap", [](iris::core::Value* args, int argCount) {
            if (argCount < 1 || !args[0].isInt()) return iris::core::Value();
            return iris::core::Value(new NativeEnumMap(args[0].asInt));
        }, 1);
    }
}

#endif //STANDARD_LIBRARY_H
