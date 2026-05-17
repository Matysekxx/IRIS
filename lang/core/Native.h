#ifndef NATIVE_H
#define NATIVE_H

#include "Managed.h"
#include <functional>
#include <string>
#include <vector>

namespace iris::core {
    struct Value; // Forward declaration

    /**
     * @brief Type for native functions called from IRIS.
     * args: Pointer to the first argument register.
     * argCount: Number of arguments passed.
     * return: Result of the function call.
     */
    using NativeFn = std::function<Value(Value *args, int argCount)>;

    /**
     * @brief A function object that represents a native C++ function.
     */
    struct NativeFunction : Managed {
        std::string name;
        NativeFn fn;
        int arity;

        NativeFunction(std::string name, NativeFn fn, int arity)
            : Managed(ManagedType::Native), name(std::move(name)), fn(std::move(fn)), arity(arity) {
        }
    };

    /**
     * @brief Base class for native objects exposed to IRIS.
     * Allows C++ objects to be treated as IRIS objects with methods.
     */
    struct NativeObject : Managed {
        NativeObject() : Managed(ManagedType::Native) {}
        virtual ~NativeObject() override = default;

        /**
         * @brief Called when a method is invoked on this native object.
         * @param name The name of the method.
         * @param args Pointer to the argument registers.
         * @param argCount Number of arguments.
         * @return The result of the method call.
         */
        virtual Value callMethod(const std::string &name, Value *args, int argCount);

        virtual std::string toString() const {
            return "[native object]";
        }
    };
}

#endif //NATIVE_H
