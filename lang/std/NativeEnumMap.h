#ifndef NATIVE_ENUM_MAP_H
#define NATIVE_ENUM_MAP_H

#include "../core/Native.h"
#include "../core/Value.h"
#include <vector>

namespace iris::std_lib {
    /**
     * @brief A highly optimized map for Enum keys.
     * Internally uses a fixed-size array based on the number of enum values.
     */
    class NativeEnumMap : public iris::core::NativeObject {
        std::vector<iris::core::Value> values;

    public:
        explicit NativeEnumMap(int enumSize) : values(enumSize) {
        }

        iris::core::Value callMethod(const std::string &name, iris::core::Value *args, int argCount) override {
            if (name == "put" || name == "set") {
                if (argCount < 2 || !args[0].isInt()) return iris::core::Value();
                int ordinal = args[0].asInt(); // For IRIS, enums ARE ints
                if (ordinal >= 0 && ordinal < static_cast<int>(values.size())) {
                    values[ordinal] = args[1];
                }
                return iris::core::Value();
            }
            if (name == "get") {
                if (argCount < 1 || !args[0].isInt()) return iris::core::Value();
                int ordinal = args[0].asInt();
                if (ordinal >= 0 && ordinal < static_cast<int>(values.size())) {
                    return values[ordinal];
                }
                return iris::core::Value();
            }
            if (name == "size") {
                return iris::core::Value(static_cast<int>(values.size()));
            }
            return iris::core::NativeObject::callMethod(name, args, argCount);
        }

        std::string toString() const override {
            return "EnumMap[" + std::to_string(values.size()) + "]";
        }
    };
}

#endif //NATIVE_ENUM_MAP_H
