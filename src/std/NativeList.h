#ifndef NATIVE_LIST_H
#define NATIVE_LIST_H

#include "../core/Native.h"
#include "../core/Value.h"
#include <vector>

namespace iris::std_lib {
    /**
     * @brief A high-performance dynamic array (List) implemented in C++.
     * This will be exposed to IRIS as the 'List' class in 'std.collections'.
     */
    class NativeList : public iris::core::NativeObject {
        std::vector<iris::core::Value> items;

    public:
        iris::core::Value callMethod(const std::string &name, iris::core::Value *args, int argCount) override {
            if (name == "add") {
                if (argCount < 1) return iris::core::Value(false);
                items.push_back(args[0]);
                return iris::core::Value(true);
            }
            if (name == "get") {
                if (argCount < 1 || !args[0].isInt()) return iris::core::Value();
                int idx = args[0].asInt();
                if (idx < 0 || idx >= static_cast<int>(items.size())) return iris::core::Value();
                return items[idx];
            }
            if (name == "size") {
                return iris::core::Value(static_cast<int>(items.size()));
            }
            if (name == "clear") {
                items.clear();
                return iris::core::Value();
            }
            if (name == "pop") {
                if (items.empty()) return iris::core::Value();
                iris::core::Value val = items.back();
                items.pop_back();
                return val;
            }
            return iris::core::NativeObject::callMethod(name, args, argCount);
        }

        std::string toString() const override {
            std::string res = "List[";
            for (size_t i = 0; i < items.size(); ++i) {
                res += iris::core::toString(items[i]);
                if (i < items.size() - 1) res += ", ";
            }
            res += "]";
            return res;
        }
    };
}

#endif //NATIVE_LIST_H
