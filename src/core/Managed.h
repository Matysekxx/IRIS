#ifndef MANAGED_H
#define MANAGED_H

#include <cstdint>

namespace iris::core {
    /**
     * @brief Types of managed objects for fast identification.
     */
    enum class ManagedType : uint8_t {
        String,
        Object,
        Array,
        Native
    };

    /**
     * @brief Base class for heap-allocated objects managed by the Garbage Collector.
     */
    struct Managed {
        Managed* next = nullptr; // Global list of all managed objects
        ManagedType type;
        bool marked = false;

        explicit Managed(ManagedType t, size_t allocSize = 32); // Implementation in Value.cpp to register with GC

        Managed(const Managed &) : type(ManagedType::Object) {}
        Managed &operator=(const Managed &) { return *this; }
        virtual ~Managed() = default;
    };
}

#endif //MANAGED_H
