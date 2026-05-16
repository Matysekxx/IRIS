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
}

#endif //SYSTEM_LIB_H
