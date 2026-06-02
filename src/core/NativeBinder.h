#ifndef NATIVE_BINDER_H
#define NATIVE_BINDER_H

#include "Value.h"
#include "Native.h"
#include <tuple>
#include <type_traits>
#include <string>

namespace iris::core {
    /**
     * @brief Type traits for converting between IRIS Value and C++ types.
     */
    template<typename T>
    struct TypeConverter {
        static T fromValue(const Value &v) {
            static_assert(std::is_same_v<T, void>, "Unsupported type in NativeBinder");
            return T();
        }

        static Value toValue(T val) {
            return Value();
        }
    };

    template<>
    struct TypeConverter<int> {
        static int fromValue(const Value &v) { return v.isInt() ? v.asInt() : (int) toDouble(v); }
        static Value toValue(int val) { return Value(val); }
    };

    template<>
    struct TypeConverter<double> {
        static double fromValue(const Value &v) { return toDouble(v); }
        static Value toValue(double val) { return Value(val); }
    };

    template<>
    struct TypeConverter<bool> {
        static bool fromValue(const Value &v) { return v.isBool() ? v.asBool() : toDouble(v) != 0; }
        static Value toValue(bool val) { return Value(val); }
    };

    template<>
    struct TypeConverter<std::string> {
        static std::string fromValue(const Value &v) { return v.str(); }
        static Value toValue(const std::string &val) { return Value(new StringData(val)); }
    };

    template<>
    struct TypeConverter<Value> {
        static Value fromValue(const Value &v) { return v; }
        static Value toValue(Value val) { return val; }
    };

    // Specialization for void return type
    template<>
    struct TypeConverter<void> {
        static Value toValue() { return Value(); }
    };

    /**
     * @brief Helper to call a C++ function with arguments from Value array.
     */
    template<typename R, typename... Args, size_t... Is>
    R callFuncHelper(R (*func)(Args...), Value *args, std::index_sequence<Is...>) {
        return func(TypeConverter<std::decay_t<Args> >::fromValue(args[Is])...);
    }

    /**
     * @brief Automated binder for C++ functions.
     */
    template<typename R, typename... Args>
    NativeFn bindFunction(R (*func)(Args...)) {
        return [func](Value *args, int argCount) -> Value {
            constexpr size_t numArgs = sizeof...(Args);
            if constexpr (std::is_same_v<R, void>) {
                callFuncHelper(func, args, std::make_index_sequence<numArgs>{});
                return Value();
            } else {
                R result = callFuncHelper(func, args, std::make_index_sequence<numArgs>{});
                return TypeConverter<R>::toValue(result);
            }
        };
    }
}

#endif //NATIVE_BINDER_H
