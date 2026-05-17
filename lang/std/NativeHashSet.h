#ifndef NATIVE_HASH_SET_H
#define NATIVE_HASH_SET_H

#include "../core/Native.h"
#include "../core/Value.h"
#include <unordered_set>
#include <string>

namespace iris::std_lib {
    /**
     * @brief A high-performance hash set implemented in C++.
     * Optimized for O(1) membership checks.
     */
    class NativeHashSet : public iris::core::NativeObject {
        struct ValueHasher {
            size_t operator()(const iris::core::Value &v) const {
                if (v.isInt()) return std::hash<int>{}(v.asInt());
                if (v.isDouble()) return std::hash<double>{}(v.asDouble());
                if (v.isBool()) return std::hash<bool>{}(v.asBool());
                if (v.isString()) return std::hash<std::string>{}(v.str());
                if (v.isHeap()) return std::hash<void *>{}(v.asPtr());
                return 0;
            }
        };

        std::unordered_set<iris::core::Value, ValueHasher> items;

    public:
        iris::core::Value callMethod(const std::string &name, iris::core::Value *args, int argCount) override {
            if (name == "add") {
                if (argCount < 1) return iris::core::Value(false);
                return iris::core::Value(items.insert(args[0]).second);
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
            std::string res = "Set{";
            size_t i = 0;
            for (auto const &val: items) {
                res += iris::core::toString(val);
                if (++i < items.size()) res += ", ";
                if (i > 10) {
                    res += "...";
                    break;
                }
            }
            res += "}";
            return res;
        }
    };
}

#endif //NATIVE_HASH_SET_H
