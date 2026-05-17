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

    inline iris::core::Value iris_system_hash(iris::core::Value *args, int argCount) {
        if (argCount < 1) return iris::core::Value(0);
        const auto &v = args[0];
        int h = 0;
        if (v.isInt()) h = static_cast<int>(std::hash<int>{}(v.asInt));
        else if (v.isDouble()) h = static_cast<int>(std::hash<double>{}(v.asDouble));
        else if (v.isBool()) h = static_cast<int>(std::hash<bool>{}(v.asBool));
        else if (v.isString()) h = static_cast<int>(std::hash<std::string>{}(v.str()));
        else if (v.isHeap()) h = static_cast<int>(std::hash<void *>{}(v.asPtr));
        return iris::core::Value(h);
    }
}

#endif //SYSTEM_LIB_H
