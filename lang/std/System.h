#ifndef SYSTEM_LIB_H
#define SYSTEM_LIB_H

#include "../core/Native.h"
#include <chrono>

namespace iris::std_lib {
    inline double iris_system_time() {
        auto now = std::chrono::high_resolution_clock::now();
        auto duration = now.time_since_epoch();
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        return static_cast<double>(millis);
    }

    inline int iris_system_hash(iris::core::Value* args, int argCount) {
        if (argCount < 1) return 0;
        const auto& v = args[0];
        if (v.isInt()) return static_cast<int>(std::hash<int>{}(v.asInt));
        if (v.isDouble()) return static_cast<int>(std::hash<double>{}(v.asDouble));
        if (v.isBool()) return static_cast<int>(std::hash<bool>{}(v.asBool));
        if (v.isString()) return static_cast<int>(std::hash<std::string>{}(v.str()));
        if (v.isHeap()) return static_cast<int>(std::hash<void*>{}(v.asPtr));
        return 0;
    }
}

#endif //SYSTEM_LIB_H
