#ifndef MANAGED_H
#define MANAGED_H

#include <cstdint>

namespace iris::core {
    /**
     * @brief Base class for heap-allocated reference-counted data.
     */
    struct Managed {
        uint32_t refCount = 0;
        Managed() = default;
        Managed(const Managed&) : refCount(0) {}
        Managed& operator=(const Managed&) { return *this; }
        virtual ~Managed() = default;
    };
}

#endif //MANAGED_H
