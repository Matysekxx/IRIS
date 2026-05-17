#ifndef STANDARD_LIBRARY_H
#define STANDARD_LIBRARY_H

#include "../core/NativeRegistry.h"
#include "Math.h"
#include "NativeList.h"
#include "NativeHashMap.h"
#include "NativeEnumMap.h"
#include "NativeHashSet.h"
#include "NativeLinkedList.h"
#include "NativeStreams.h"
#include "System.h"

namespace iris::std_lib {
    inline void initialize() {
        auto &registry = iris::core::NativeRegistry::getInstance();

        // IO functions (Low-level Streams)
        registry.registerFunction("Collections.NativeFileInputStream", [](iris::core::Value *args, int argCount) {
            if (argCount < 1 || !args[0].isString()) return iris::core::Value();
            return iris::core::Value(new NativeFileInputStream(args[0].str()));
        }, 1);

        registry.registerFunction("Collections.NativeFileOutputStream", [](iris::core::Value *args, int argCount) {
            if (argCount < 1 || !args[0].isString()) return iris::core::Value();
            bool append = (argCount >= 2 && args[1].isBool()) ? args[1].asBool() : false;
            return iris::core::Value(new NativeFileOutputStream(args[0].str(), append));
        }, 2);

        // Math functions
        registry.bind("Math.sin", iris_math_sin);
        registry.bind("Math.cos", iris_math_cos);
        registry.bind("Math.sqrt", iris_math_sqrt);
        registry.bind("Math.pow", iris_math_pow);
        registry.bind("Math.abs", iris_math_abs);

        // System functions
        registry.bind("System.time", iris_system_time);
        registry.registerFunction("System.hash", iris_system_hash, 1);

        // List & Map constructors (factory functions)
        registry.registerFunction("Collections.NativeList", [](iris::core::Value *args, int argCount) {
            return iris::core::Value(new NativeList());
        }, 0);

        registry.registerFunction("Collections.NativeHashMap", [](iris::core::Value *args, int argCount) {
            return iris::core::Value(new NativeHashMap());
        }, 0);

        registry.registerFunction("Collections.NativeHashSet", [](iris::core::Value *args, int argCount) {
            return iris::core::Value(new NativeHashSet());
        }, 0);

        registry.registerFunction("Collections.NativeLinkedList", [](iris::core::Value *args, int argCount) {
            return iris::core::Value(new NativeLinkedList());
        }, 0);

        registry.registerFunction("Collections.NativeEnumMap", [](iris::core::Value *args, int argCount) {
            if (argCount < 1 || !args[0].isInt()) return iris::core::Value();
            return iris::core::Value(new NativeEnumMap(args[0].asInt()));
        }, 1);
    }
}

#endif //STANDARD_LIBRARY_H
