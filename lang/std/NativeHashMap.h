#ifndef NATIVE_HASH_MAP_H
#define NATIVE_HASH_MAP_H

#include "../core/Native.h"
#include <unordered_map>
#include <string>

namespace iris::std_lib {
    /**
     * @brief A high-performance hash map implemented in C++.
     * Exposed as 'HashMap' in the standard library.
     */
    class NativeHashMap : public iris::core::NativeObject {

        // We use iris::core::Value as both key and value.
        // Note: For Value to be a key, we need a custom hasher.
        struct ValueHasher {
            size_t operator()(const iris::core::Value& v) const {
                if (v.isInt()) return std::hash<int>{}(v.asInt);
                if (v.isDouble()) return std::hash<double>{}(v.asDouble);
                if (v.isBool()) return std::hash<bool>{}(v.asBool);
                if (v.isString()) return std::hash<std::string>{}(v.str());
                if (v.isHeap()) return std::hash<void*>{}(v.asPtr);
                return 0;
            }
        };

        std::unordered_map<iris::core::Value, iris::core::Value, ValueHasher> items;

    public:
        iris::core::Value callMethod(const std::string& name, iris::core::Value* args, int argCount) override {
            if (name == "put" || name == "set") {
                if (argCount < 2) return iris::core::Value();
                items[args[0]] = args[1];
                return iris::core::Value();
            }
            if (name == "get") {
                if (argCount < 1) return iris::core::Value();
                auto it = items.find(args[0]);
                if (it != items.end()) return it->second;
                return iris::core::Value(); // Returns null if not found
            }
            if (name == "has" || name == "contains") {
                if (argCount < 1) return iris::core::Value(false);
                return iris::core::Value(items.find(args[0]) != items.end());
            }
            if (name == "remove") {
                if (argCount < 1) return iris::core::Value(false);
                return iris::core::Value(items.erase(args[0]) > 0);
            }
            if (name == "size") {
                return iris::core::Value(static_cast<int>(items.size()));
            }
            if (name == "clear") {
                items.clear();
                return iris::core::Value();
            }
            return iris::core::NativeObject::callMethod(name, args, argCount);
        }

        std::string toString() const override {
            std::string res = "Map{";
            size_t i = 0;
            for (auto const& [key, val] : items) {
                res += iris::core::toString(key) + ": " + iris::core::toString(val);
                if (++i < items.size()) res += ", ";
                if (i > 10) { // Limit string representation for large maps
                    res += "...";
                    break;
                }
            }
            res += "}";
            return res;
        }
    };
}

#endif //NATIVE_HASH_MAP_H
