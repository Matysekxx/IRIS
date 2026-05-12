#ifndef NATIVE_LINKED_LIST_H
#define NATIVE_LINKED_LIST_H

#include "../core/Native.h"
#include <list>
#include <string>

namespace iris::std_lib {
    /**
     * @brief A bidirectional linked list implemented in C++.
     * Optimized for O(1) insertions/deletions at both ends.
     */
    class NativeLinkedList : public iris::core::NativeObject {
        std::list<iris::core::Value> items;

    public:
        iris::core::Value callMethod(const std::string& name, iris::core::Value* args, int argCount) override {
            if (name == "add" || name == "addLast") {
                if (argCount < 1) return iris::core::Value();
                items.push_back(args[0]);
                return iris::core::Value();
            }
            if (name == "addFirst") {
                if (argCount < 1) return iris::core::Value();
                items.push_front(args[0]);
                return iris::core::Value();
            }
            if (name == "popFirst") {
                if (items.empty()) return iris::core::Value();
                iris::core::Value val = items.front();
                items.pop_front();
                return val;
            }
            if (name == "popLast") {
                if (items.empty()) return iris::core::Value();
                iris::core::Value val = items.back();
                items.pop_back();
                return val;
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
            std::string res = "LinkedList[";
            size_t i = 0;
            for (auto const& val : items) {
                res += iris::core::toString(val);
                if (++i < items.size()) res += ", ";
                if (i > 10) {
                    res += "...";
                    break;
                }
            }
            res += "]";
            return res;
        }
    };
}

#endif //NATIVE_LINKED_LIST_H
